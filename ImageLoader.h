#pragma once

#include <string>
#include <windowsx.h>

#include <Wincodec.h>

namespace DIVE
{

	class ImageLoader
	{
	public:
		ImageLoader();
		~ImageLoader();

		IWICBitmapSource* Load(const wchar_t* szFileName);
		IWICBitmapSource* LoadThumbnail( unsigned int width, unsigned int height, const wchar_t* szFileName);

	private:
		CComPtr<IWICImagingFactory> m_pWICFactory;
	};

}