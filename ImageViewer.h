#pragma once

#include <string>
#include <windowsx.h>
#include <memory>
#include <thread>
#include <vector>
#include <deque>
#include <mutex>
#include <d3d11_1.h>
#include <DirectXMath.h>

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
		void Show(IWICBitmapSource* pImage);
		ID2D1Bitmap* LoadD2DBitmap(const wchar_t* wszFileName);
		ID3D11ShaderResourceView* TextureFromWICBitmap(IWICBitmapSource* pWICBitmap);
		void Capture(size_t width, size_t height);
		void Render();

		void OnLBDown(HWND hWnd, LPARAM lParam);
		void OnLBUp();
		void OnMouseMove(HWND hWnd, LPARAM lParam);
		void OnMouseWheel(HWND hWnd, WPARAM wParam, LPARAM lParam);

		void PrevImage();
		void NextImage();
		void UpdateCacheForward();
		void UpdateCacheBackward();
		void RemoveCache(int index);
		struct ThumbnailInfo
		{
			ThumbnailInfo(const std::wstring& strFileName_)
				: strFileName( strFileName_), pBitmap(nullptr), pBitmapThumbnail( nullptr), fAlpha(0), ulBytes(0) {}

			std::wstring strFileName;
			CComPtr<IWICBitmapSource> pBitmap;
			CComPtr<ID2D1Bitmap> pBitmapThumbnail;
			float fAlpha;
			unsigned long ulBytes;
		};
		void SetFiles(std::vector <ThumbnailInfo>&& vecBitmaps);
		

	private:
		bool m_bLBDown = false;
		POINT m_ptDown;

		SIZE m_szClient;
		SIZE m_szImage;
		D2D1_RECT_F m_rcView;
		float m_fScale;
		float m_fScaleFrom;
		float m_fScaleTo;
		float m_fWheelU;
		float m_fWheelV;
		HWND m_hWnd;

		CComPtr<ID2D1Factory> m_pDirect2dFactory;
		CComPtr<ID2D1RenderTarget> m_pRenderTarget;
		CComPtr<ID2D1SolidColorBrush> m_pWhiteBrush;
		CComPtr<ID2D1SolidColorBrush> m_pBlackBrush;
		CComPtr<ID2D1Bitmap> m_pBackground;
		ID2D1Bitmap* m_pImage;
		std::wstring m_wstrPath;

		std::unique_ptr<ImageLoader> m_loader;
		
		std::thread* m_pthread_scan;
		std::thread* m_pthread_thumbnail;

		std::thread m_thread_load;
		std::condition_variable m_condition_load;

		std::mutex m_mutex;
		std::wstring m_wstrFileName;
		int m_nIndex;
		int m_nPreviewIndex;
		int m_nIndexOffset;
		int m_nCacheStart;
		int m_nCacheEnd;
		bool m_bEndThreads;
		std::vector <ThumbnailInfo> m_vecBitmaps;
		std::deque <int> m_deqCachesToLoad;

		int m_nThumbWidth;
		int m_nThumbHeight;
		int m_nThumbSpacing;

		bool m_bShowThumbs;

		D3D_DRIVER_TYPE				m_driverType;
		D3D_FEATURE_LEVEL			m_featureLevel;
		ID3D11Device*				m_pd3dDevice;
		ID3D11Device1*				m_pd3dDevice1;
		ID3D11DeviceContext*		m_pImmediateContext;
		ID3D11DeviceContext1*		m_pImmediateContext1;
		IDXGISwapChain*				m_pSwapChain;
		IDXGISwapChain1*			m_pSwapChain1;
		ID3D11RenderTargetView*		m_pRenderTargetView;
		ID3D11Texture2D*            m_pDepthStencil;
		ID3D11DepthStencilView*     m_pDepthStencilView;
		ID3D11VertexShader*         m_pVertexShader;
		ID3D11PixelShader*          m_pPixelShader;
		ID3D11InputLayout*          m_pVertexLayout;
		ID3D11Buffer*               m_pVertexBuffer;
		ID3D11Buffer*               m_pIndexBuffer;
		ID3D11Buffer*               m_pCBNeverChanges;
		ID3D11Buffer*               m_pCBChangeOnResize;
		ID3D11Buffer*               m_pCBChangesEveryFrame;
		ID3D11ShaderResourceView*	m_pTextureRV;
		ID3D11SamplerState*         m_pSamplerLinear;
		DirectX::XMMATRIX           m_World;
		DirectX::XMMATRIX           m_View;
		DirectX::XMMATRIX           m_Projection;
	};
}