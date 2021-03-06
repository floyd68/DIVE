#include "stdafx.h"
#include "ImageLoader.h"
#include "tga.h"
#include <dwrite.h>
#include <d2d1helper.h>
#include <d2d1effects.h>
#include <string>
#include <algorithm>

namespace DIVE
{

	ImageLoader::ImageLoader()
	{
		HRESULT hr;

		hr = CoCreateInstance(CLSID_WICImagingFactory, NULL,
			CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
			(LPVOID*)&m_pWICFactory);
		
	}
	ImageLoader::~ImageLoader()
	{
	}

	std::string UTF16toMBCS(const wchar_t* wszChar)
	{
		char szTemp[256];

		WideCharToMultiByte(CP_ACP, 0, wszChar, -1, szTemp, 256, nullptr, nullptr);

		return std::string( szTemp );
	}

	static HRESULT TGA2BitmapSource(TGA* pTGA, TGAData* pTGAData, IWICImagingFactory *factory, IWICBitmapSource **bitmapSource)
	{
		HRESULT result = S_OK;

		IWICBitmap *bitmap = NULL;
		IWICBitmapLock *bitmapLock = NULL;

		WICPixelFormatGUID pixelFormat = { 0 };
		UINT srcStride = 0;
		UINT destStride = 0;
		UINT cbBufferSize = 0;
		BYTE *data = NULL;

		if ((NULL == pTGA) || (NULL == pTGAData) || (NULL == factory) || (NULL == bitmapSource))
		{
			result = E_INVALIDARG;
		}

		if (pTGA->hdr.depth == 32)
		{
			pixelFormat = GUID_WICPixelFormat32bppBGRA;
			srcStride = pTGA->hdr.width * 4;
		}
		else if (pTGA->hdr.depth == 24)
		{
			pixelFormat = GUID_WICPixelFormat24bppBGR;
			srcStride = pTGA->hdr.width * 3;
		}
		else
		{
			MessageBox(NULL, L"Pixel Format Error", L"FIC", MB_OK);
			result = E_INVALIDARG;
		}

		// Create the bitmap
		if (SUCCEEDED(result))
		{
			result = factory->CreateBitmap(pTGA->hdr.width, pTGA->hdr.height, pixelFormat, WICBitmapCacheOnLoad, &bitmap);
		}

		// Set the resolution
		/*
		if ( SUCCEEDED( result ) )
		{
		result = bitmap->SetResolution( 96, 96 );
		}
		*/

		// Lock it so that we can store the data
		if (SUCCEEDED(result))
		{
			WICRect rct;
			rct.X = 0;
			rct.Y = 0;
			rct.Width = pTGA->hdr.width;
			rct.Height = pTGA->hdr.height;

			result = bitmap->Lock(&rct, WICBitmapLockWrite, &bitmapLock);
		}

		if (SUCCEEDED(result))
		{
			result = bitmapLock->GetDataPointer(&cbBufferSize, &data);
		}

		if (SUCCEEDED(result))
		{
			result = bitmapLock->GetStride(&destStride);
		}

		// Read the data from the stream
		if (SUCCEEDED(result))
		{
			// We must read one scanline at a time because the input stride
			// may not equal the output stride
			for (UINT y = 0; y < pTGA->hdr.height; y++)
			{
				memcpy(data, &pTGAData->img_data[y * srcStride], srcStride);
				data += destStride;
			}
		}

		// Close the lock
		if (bitmapLock && bitmap)
		{
			if (bitmapLock)
			{
				bitmapLock->Release();
				bitmapLock = NULL;
			}
		}

		// Finish
		if (SUCCEEDED(result))
		{
			result = bitmap->QueryInterface(IID_IWICBitmapSource, (void**)bitmapSource);
			if (SUCCEEDED(result))
			{
				bitmap->Release();
			}
		}
		else
		{
			if (bitmap)
			{
				bitmap->Release();
			}
			*bitmapSource = NULL;
		}

		return result;
	}

	IWICBitmapSource* ImageLoader::Load(const wchar_t* wszFileName)
	{
		wchar_t wszTemp[256];

		wcscpy(wszTemp, wszFileName);
		wcslwr(wszTemp);
		

		HRESULT hr = S_OK;

		IWICBitmapSource* pWICBitmap;

		if (wcsstr(wszTemp, L".tga"))
		{
			TGA* pTGA = TGAOpen((char *)UTF16toMBCS(wszFileName).c_str(), "rb");
			TGAData tga_data;
			tga_data.flags = TGA_IMAGE_DATA | TGA_FLIP_VERTICAL;

			TGAReadImage(pTGA, &tga_data);
			hr = TGA2BitmapSource(pTGA, &tga_data, m_pWICFactory, &pWICBitmap);

			free(tga_data.img_data);
			TGAClose(pTGA);
		}
		else
		{
			CComPtr<IWICBitmapDecoder> pDecoder = nullptr;
			CComPtr<IWICBitmapFrameDecode> pIDecoderFrame = nullptr;

			hr = m_pWICFactory->CreateDecoderFromFilename(
				wszFileName,
				NULL,
				GENERIC_READ,
				WICDecodeMetadataCacheOnLoad,
				&pDecoder
			);
			if (SUCCEEDED(hr))
				hr = pDecoder->GetFrame(0, &pIDecoderFrame);

			if (SUCCEEDED(hr))
				hr = pIDecoderFrame->QueryInterface(IID_IWICBitmapSource, (void **)&pWICBitmap);
		}
		WICPixelFormatGUID guidPixelFormat;
		hr = pWICBitmap->GetPixelFormat(&guidPixelFormat);
		if (guidPixelFormat != GUID_WICPixelFormat32bppPBGRA)
		{
			CComPtr<IWICFormatConverter> pConverter = NULL;

			hr = m_pWICFactory->CreateFormatConverter(&pConverter);
			if (SUCCEEDED(hr))
				hr = pConverter->Initialize(
					pWICBitmap,
					GUID_WICPixelFormat32bppPBGRA,
					WICBitmapDitherTypeNone,
					NULL,
					0.f,
					WICBitmapPaletteTypeMedianCut
				);
			if (SUCCEEDED(hr))
				hr = pConverter->QueryInterface(IID_IWICBitmapSource, (void **)&pWICBitmap);
		}

		return pWICBitmap;
	}
	IWICBitmapSource* ImageLoader::LoadThumbnail(unsigned int width, unsigned int height, const wchar_t* szFileName)
	{
		CComPtr<IWICBitmapSource> pWICBitmap = Load(szFileName);

		if (pWICBitmap)
		{
			HRESULT hr;

			IWICBitmapScaler* pWICScaler;

			hr = m_pWICFactory->CreateBitmapScaler(&pWICScaler);
			if (SUCCEEDED(hr))
				hr = pWICScaler->Initialize(pWICBitmap, width, height, WICBitmapInterpolationModeNearestNeighbor);
			if (SUCCEEDED(hr))
				return pWICScaler;
			else
			{
				pWICScaler->Release();
				return nullptr;
			}
		}
		else
			return nullptr;
	}
}