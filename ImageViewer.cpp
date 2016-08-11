#include "stdafx.h"
#include "ImageViewer.h"
#include "ImageLoader.h"
#include "tga.h"
#include <dwrite.h>
#include <wincodec.h>
#include <d2d1helper.h>
#include <d2d1effects.h>
#include <string>
#include <algorithm>

namespace DIVE
{
	ImageViewer::ImageViewer()
		: m_nScale(1000)
		, m_pDirect2dFactory(nullptr)
		, m_pRenderTarget(nullptr)
		, m_pBackground(nullptr)
		, m_pImage(nullptr)
		, m_loader( std::make_unique<ImageLoader>() )
		, m_pthread_scan(nullptr)
		, m_pthread_load(nullptr)
		, m_hWnd( NULL )
		, m_nIndex( -1 )
	{
		HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pDirect2dFactory);
	}

	ImageViewer::~ImageViewer()
	{
		if (m_pDirect2dFactory)
			m_pDirect2dFactory->Release();
	}

	void ImageViewer::Destroy()
	{
		if (m_pRenderTarget)
			m_pRenderTarget->Release();
		if (m_pBackground)
			m_pBackground->Release();
		if (m_pImage)
			m_pImage->Release();
	}
	bool ImageViewer::Initialize(HWND hWnd)
	{
		HRESULT hr = S_OK;
		m_hWnd = hWnd;
		RECT rc;
		GetClientRect(hWnd, &rc);

		D2D1_SIZE_U size = D2D1::SizeU(
			rc.right - rc.left,
			rc.bottom - rc.top
		);

		// Create a Direct2D render target.
		hr = m_pDirect2dFactory->CreateHwndRenderTarget(
			D2D1::RenderTargetProperties(),
			D2D1::HwndRenderTargetProperties(hWnd, size),
			&m_pRenderTarget
		);

		return true;
	}

	inline bool ends_with(std::wstring const & value, std::wstring const & ending)
	{
		if (ending.size() > value.size()) return false;
		return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
	}

	void ImageViewer::Draw(HWND hWnd)
	{
		RECT rcClient;
		::GetClientRect(hWnd, &rcClient);

		m_pRenderTarget->BeginDraw();
		m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

		m_pRenderTarget->DrawBitmap(
			m_pBackground,
			D2D1::RectF(
				rcClient.left,
				rcClient.top,
				rcClient.right,
				rcClient.bottom),
			1.0,
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
		);
		if (m_pImage)
		{
			m_pRenderTarget->DrawBitmap(
				m_pImage, m_rcView, 1.0,
				D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
			);
		}

		int nX = 0;
		int nY = rcClient.bottom - 260;
		for (auto& bmp : m_vecBitmaps)
		{
			if (bmp.fAlpha > 0.0f && bmp.pBitmap)
			{
				m_pRenderTarget->DrawBitmap(
					bmp.pBitmap,
					D2D1::RectF( nX, nY, nX + 60, nY + 40 ),
					bmp.fAlpha,
					D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
				);
			}
			nX += 64;
			if (nX + 60 > rcClient.right)
			{
				nX = 0;
				nY += 44;
			}
		}

		m_pRenderTarget->EndDraw();
		ValidateRect(hWnd, nullptr);
	}

	void ImageViewer::SetFiles(std::vector <ThumbnailInfo>&& vecBitmaps)
	{
		m_vecBitmaps = std::move(vecBitmaps);

		int nIndex = 0;
		for (auto& bmp : m_vecBitmaps)
		{
			if (bmp.strFileName == m_wstrFileName)
			{
				m_nIndex = nIndex;
				bmp.fAlpha = 1.0f;
				bmp.pBitmap = m_pImage;
				PostMessage(m_hWnd, WM_PAINT, 0, 0);
				break;
			}
			++nIndex;
		}
		if (!m_pthread_load)
		{
			m_pthread_load = new std::thread(
				[this]()
				{
					for (auto& bmp : m_vecBitmaps)
					{
						if (bmp.pBitmap == nullptr)
						{
							HRESULT hr = S_OK;

							IWICBitmapSource* pWICBitmap = m_loader->Load(bmp.strFileName.c_str());

							if (SUCCEEDED(hr))
								hr = m_pRenderTarget->CreateBitmapFromWicBitmap(
									pWICBitmap,
									NULL,
									&bmp.pBitmap
								);
							if (SUCCEEDED(hr))
							{
								bmp.fAlpha = 1.0f;
								PostMessage(m_hWnd, WM_PAINT, 0, 0);
							}
							pWICBitmap->Release();
						}

					}

				}
			);

		}
	}

	void ImageViewer::PrevImage()
	{
		if (m_nIndex > 0)
		{
			m_nIndex--;
			m_pImage = m_vecBitmaps[m_nIndex].pBitmap;
			m_szImage = SIZE{ (long)m_pImage->GetSize().width, (long)m_pImage->GetSize().height };

			if (m_szClient.cx < m_szImage.cx || m_szClient.cy < m_szImage.cy)
			{
				m_nScale = std::min(
					m_szClient.cx * 1000 / m_szImage.cx,
					m_szClient.cy * 1000 / m_szImage.cy);
			}
			else if (m_szImage.cx < m_szClient.cx / 2 || m_szImage.cy < m_szClient.cy / 2)
			{
				m_nScale = std::min(
					m_szClient.cx * 1000 / 2 / m_szImage.cx,
					m_szClient.cy * 1000 * 3 / 4 / m_szImage.cy);
			}

			m_rcView = D2D1::RectF(
				(m_szClient.cx - m_szImage.cx * m_nScale / 1000.0f) / 2,
				(m_szClient.cy - m_szImage.cy * m_nScale / 1000.0f) / 2,
				(m_szClient.cx + m_szImage.cx * m_nScale / 1000.0f) / 2,
				(m_szClient.cy + m_szImage.cy * m_nScale / 1000.0f) / 2);
		}
		SendMessage(m_hWnd, WM_PAINT, 0, 0);

	}

	void ImageViewer::NextImage()
	{
		if (m_nIndex < m_vecBitmaps.size() - 1 && m_nIndex >= 0)
		{
			m_nIndex++;
			m_pImage = m_vecBitmaps[m_nIndex].pBitmap;
			m_szImage = SIZE{ (long)m_pImage->GetSize().width, (long)m_pImage->GetSize().height };

			if (m_szClient.cx < m_szImage.cx || m_szClient.cy < m_szImage.cy)
			{
				m_nScale = std::min(
					m_szClient.cx * 1000 / m_szImage.cx,
					m_szClient.cy * 1000 / m_szImage.cy);
			}
			else if (m_szImage.cx < m_szClient.cx / 2 || m_szImage.cy < m_szClient.cy / 2)
			{
				m_nScale = std::min(
					m_szClient.cx * 1000 / 2 / m_szImage.cx,
					m_szClient.cy * 1000 * 3 / 4 / m_szImage.cy);
			}

			m_rcView = D2D1::RectF(
				(m_szClient.cx - m_szImage.cx * m_nScale / 1000.0f) / 2,
				(m_szClient.cy - m_szImage.cy * m_nScale / 1000.0f) / 2,
				(m_szClient.cx + m_szImage.cx * m_nScale / 1000.0f) / 2,
				(m_szClient.cy + m_szImage.cy * m_nScale / 1000.0f) / 2);
		}
		SendMessage(m_hWnd, WM_PAINT, 0, 0);

	}

	bool ImageViewer::Load(const wchar_t* wszFileName)
	{
		wchar_t wszTemp[256];

		wcscpy(wszTemp, wszFileName);
		wcslwr(wszTemp);


		wchar_t wszDrive[256];
		wchar_t wszPath[256];
		wchar_t wszFile[256];
		wchar_t wszExt[256];

		_wsplitpath(wszFileName, wszDrive, wszPath, wszFile, wszExt);

		m_wstrPath = wszDrive;
		m_wstrPath += wszPath;
		m_wstrFileName = wszFileName;

		
		m_nScale = 1000;

		HRESULT hr = S_OK;

		IWICBitmapSource* pWICBitmap = m_loader->Load( wszFileName );

		if (SUCCEEDED(hr))
			hr = m_pRenderTarget->CreateBitmapFromWicBitmap(
				pWICBitmap,
				NULL,
				&m_pImage
			);
		pWICBitmap->Release();

		if (!m_pthread_scan)
		{
			m_pthread_scan = new std::thread(
				[this](const std::wstring& strPath)
				{
					std::wstring strFiles = strPath + L"\\*.*";
					WIN32_FIND_DATA findData;
					std::vector <ThumbnailInfo> vecFiles;

					auto hFind = FindFirstFile(strFiles.c_str(), &findData);
					if (hFind)
					{
						bool bFind = true;
						while (bFind)
						{
							if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY)
							{
								wchar_t wszTemp[256];

								wcscpy(wszTemp, findData.cFileName );
								wcslwr(wszTemp);

								if (ends_with( wszTemp, L".tga" ) ||
									ends_with( wszTemp, L".bmp" ) ||
									ends_with( wszTemp, L".dds" ) ||
									ends_with( wszTemp, L".png" ) ||
									ends_with( wszTemp, L".tif" ) ||
									ends_with( wszTemp, L".jpg") ||
									ends_with( wszTemp, L".ico" ))
									vecFiles.push_back({ strPath + findData.cFileName });
							}
							bFind = FindNextFile(hFind, &findData);
						}
						this->SetFiles(std::move(vecFiles));
					}
				},
				m_wstrPath
			);
		}

		m_szImage = SIZE{ (long)m_pImage->GetSize().width, (long)m_pImage->GetSize().height };

		if (m_szClient.cx < m_szImage.cx || m_szClient.cy < m_szImage.cy)
		{
			m_nScale = std::min(
				m_szClient.cx * 1000 / m_szImage.cx,
				m_szClient.cy * 1000 / m_szImage.cy);
		}
		else if (m_szImage.cx < m_szClient.cx / 2 || m_szImage.cy < m_szClient.cy / 2)
		{
			m_nScale = std::min(
				m_szClient.cx * 1000 / 2 / m_szImage.cx,
				m_szClient.cy * 1000 * 3 / 4 / m_szImage.cy);
		}

		m_rcView = D2D1::RectF(
			(m_szClient.cx - m_szImage.cx * m_nScale / 1000.0f) / 2,
			(m_szClient.cy - m_szImage.cy * m_nScale / 1000.0f) / 2,
			(m_szClient.cx + m_szImage.cx * m_nScale / 1000.0f) / 2,
			(m_szClient.cy + m_szImage.cy * m_nScale / 1000.0f) / 2);

		m_pthread_scan->join();
		delete m_pthread_scan;

		return true;
	}

	void ImageViewer::OnLBDown(HWND hWnd, LPARAM lParam)
	{
		::SetCapture(hWnd);
		m_bLBDown = true;
		m_ptDown.x = GET_X_LPARAM(lParam);
		m_ptDown.y = GET_Y_LPARAM(lParam);

		::ClientToScreen(hWnd, &m_ptDown);

		SetCursor(LoadCursor(NULL, IDC_HAND));
	}
	void ImageViewer::OnLBUp()
	{
		::SetCursor(LoadCursor(NULL, IDC_ARROW));
		::ReleaseCapture();
		m_bLBDown = false;
	}
	void ImageViewer::OnMouseMove(HWND hWnd, LPARAM lParam)
	{
		if (m_bLBDown)
		{
			POINT ptMove{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

			::ClientToScreen(hWnd, &ptMove);

			auto move_x = ptMove.x - m_ptDown.x;
			auto move_y = ptMove.y - m_ptDown.y;

			m_rcView.left += move_x;
			m_rcView.right += move_x;
			m_rcView.top += move_y;
			m_rcView.bottom += move_y;

			Draw(hWnd);

			m_ptDown = ptMove;
		}
	}

	void ImageViewer::OnMouseWheel(HWND hWnd, WPARAM wParam, LPARAM lParam)
	{
		POINT ptWheel{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		auto fwKeys = GET_KEYSTATE_WPARAM(wParam);
		auto zDelta = GET_WHEEL_DELTA_WPARAM(wParam);

		auto wheel = zDelta / WHEEL_DELTA;

		if (fwKeys & MK_SHIFT)
		{
			m_nScale += wheel * 10;
		}
		else
		{
			auto scale = m_nScale * 150.0f / 1000.0f;
			if (scale < 15.0f)
				scale = 15.0f;

			m_nScale += scale * wheel;
		}

		auto old_w = m_rcView.right - m_rcView.left;
		auto old_h = m_rcView.bottom - m_rcView.top;

		auto new_w = m_nScale * m_szImage.cx / 1000.0f;
		auto new_h = m_nScale * m_szImage.cy / 1000.0f;

		float wp = 0.5f;
		float hp = 0.5f;

		if (ptWheel.x >= m_rcView.left && ptWheel.x < m_rcView.right  &&
			ptWheel.y >= m_rcView.top && ptWheel.y < m_rcView.bottom)
		{
			wp = (ptWheel.x - m_rcView.left) / old_w;
			hp = (ptWheel.y - m_rcView.top) / old_h;
		}
		m_rcView.left -= (new_w - old_w) * wp;
		m_rcView.right = m_rcView.left + new_w;
		m_rcView.top -= (new_h - old_h) * hp;
		m_rcView.bottom = m_rcView.top + new_h;

		InvalidateRect(hWnd, nullptr, false);
		UpdateWindow(hWnd);
	}

	void ImageViewer::Capture(size_t width, size_t height)
	{
		HWND hDesktopWnd = GetDesktopWindow();
		HDC hDesktopDC = GetDC(hDesktopWnd);
		HDC hCaptureDC = CreateCompatibleDC(hDesktopDC);

		BITMAPINFO bmi;

		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = -height;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = 0;

		void* pBackground;
		auto hBackground = CreateDIBSection(hDesktopDC, &bmi, DIB_RGB_COLORS, &pBackground, NULL, 0);
		auto hOld = SelectObject(hCaptureDC, hBackground);
		BitBlt(hCaptureDC, 0, 0, width, height, hDesktopDC, 0, 0, SRCCOPY | CAPTUREBLT);
		ReleaseDC(hDesktopWnd, hDesktopDC);
		SelectObject(hCaptureDC, hOld);
		DeleteDC(hCaptureDC);

		unsigned int bright;
		size_t count = width * height;
		unsigned int* pPixels = (unsigned int *)pBackground;

		BYTE r, g, b;
		float fr, fg, fb, aver;
		for (int i = 0; i < count; ++i)
		{
			r = ((*pPixels >> 16) & 0xff);
			g = ((*pPixels >> 8) & 0xff);
			b = ((*pPixels >> 0) & 0xff);
			aver = (r * 0.25 + g * 0.7 + b * 0.15);

			fr = (r + aver * 2) / 4.0f;
			if (fr > 255.0f)
				fr = 255;
			fg = (g + aver * 2) / 4.0f;
			if (fg > 255.0f)
				fg = 255;
			fb = (b + aver * 2) / 4.0f;
			if (fb > 255.0f)
				fb = 255;
			r = (BYTE)fr;
			g = (BYTE)fg;
			b = (BYTE)fb;
			bright = (r << 16) | (g << 8) | b;
			*pPixels = bright;
			++pPixels;
		}

		D2D1_BITMAP_PROPERTIES bp;
		bp.dpiX = 72.0f;
		bp.dpiY = 72.0f;
		bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
		bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;

		D2D1_SIZE_U sz{ width,height };
		HRESULT hr = m_pRenderTarget->CreateBitmap(sz, pBackground, width * 4, &bp, &m_pBackground);
		DeleteObject(hBackground);

		m_szClient = SIZE{ (long)width, (long)height };
	}
}