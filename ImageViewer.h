#pragma once

#include <string>
#include <windowsx.h>
#include <d2d1_1.h>
#include <memory>
#include <thread>
#include <vector>
#include <mutex>

namespace DIVE
{
	class ImageLoader;

	class ImageViewer
	{
	public:
		ImageViewer();
		~ImageViewer();

		bool Initialize(HWND hWnd);
		void Destroy();

		void Draw(HWND hWnd);
		bool Load(const wchar_t* szFileName);
		void Capture(size_t width, size_t height);

		void OnLBDown(HWND hWnd, LPARAM lParam);
		void OnLBUp();
		void OnMouseMove(HWND hWnd, LPARAM lParam);
		void OnMouseWheel(HWND hWnd, WPARAM wParam, LPARAM lParam);

		void PrevImage();
		void NextImage();

		struct ThumbnailInfo
		{
			ThumbnailInfo(const std::wstring& strFileName_)
				: strFileName( strFileName_), pBitmap(nullptr), fAlpha(0) {}

			std::wstring strFileName;
			ID2D1Bitmap* pBitmap;
			float fAlpha;
		};
		void SetFiles(std::vector <ThumbnailInfo>&& vecBitmaps);
		

	private:
		bool m_bLBDown = false;
		POINT m_ptDown;

		SIZE m_szClient;
		SIZE m_szImage;
		D2D1_RECT_F m_rcView;
		int m_nScale;
		HWND m_hWnd;

		ID2D1Factory* m_pDirect2dFactory;
		ID2D1HwndRenderTarget* m_pRenderTarget;
		ID2D1SolidColorBrush* m_pLightSlateGrayBrush;
		ID2D1SolidColorBrush* m_pCornflowerBlueBrush;
		ID2D1Bitmap* m_pBackground;
		ID2D1Bitmap* m_pImage;
		std::wstring m_wstrPath;

		std::unique_ptr<ImageLoader> m_loader;
		std::thread* m_pthread_scan;
		std::thread* m_pthread_load;

		std::mutex m_mutex;
		std::wstring m_wstrFileName;
		int m_nIndex;
		std::vector <ThumbnailInfo> m_vecBitmaps;
	};
}