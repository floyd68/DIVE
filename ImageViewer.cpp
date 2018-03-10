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

#include <directxcolors.h>
#include <d3dcompiler.h>
#include <chrono>
#include <ratio>


namespace DIVE
{
	struct SimpleVertex
	{
		DirectX::XMFLOAT3 Pos;
		DirectX::XMFLOAT2 Tex;
	};
	struct CBNeverChanges
	{
		DirectX::XMMATRIX mView;
	};

	struct CBChangeOnResize
	{
		DirectX::XMMATRIX mProjection;
	};

	struct CBChangesEveryFrame
	{
		DirectX::XMMATRIX mWorld;
	};

	HRESULT CompileShaderFromFile(WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut)
	{
		HRESULT hr = S_OK;

		DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
		// Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
		// Setting this flag improves the shader debugging experience, but still allows 
		// the shaders to be optimized and to run exactly the way they will run in 
		// the release configuration of this program.
		dwShaderFlags |= D3DCOMPILE_DEBUG;

		// Disable optimizations to further improve shader debugging
		dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		ID3DBlob* pErrorBlob = nullptr;
		hr = D3DCompileFromFile(szFileName, nullptr, nullptr, szEntryPoint, szShaderModel,
			dwShaderFlags, 0, ppBlobOut, &pErrorBlob);
		if (FAILED(hr))
		{
			if (pErrorBlob)
			{
				OutputDebugStringA(reinterpret_cast<const char*>(pErrorBlob->GetBufferPointer()));
				pErrorBlob->Release();
			}
			return hr;
		}
		if (pErrorBlob) pErrorBlob->Release();

		return S_OK;
	}

	ImageViewer::ImageViewer()
		: m_fScale(1.0f)
		, m_fScaleFrom(1.0f)
		, m_fScaleTo(1.0f)
		, m_pDirect2dFactory(nullptr)
		, m_pRenderTarget(nullptr)
		, m_pBackground(nullptr)
		, m_pImage(nullptr)
		, m_loader( std::make_unique<ImageLoader>() )
		, m_pthread_scan(nullptr)
		, m_pthread_thumbnail(nullptr)
		, m_hWnd( NULL )
		, m_nIndex( -1 )
		, m_nPreviewIndex( -1 )
		, m_nCacheStart( -1 )
		, m_nCacheEnd( -1 )
		, m_bEndThreads( false )
		, m_bShowThumbs( true )
		, m_nThumbWidth( 120 )
		, m_nThumbHeight( 90 )
		, m_nThumbSpacing( 4 )
		, m_driverType( D3D_DRIVER_TYPE_NULL )
		, m_featureLevel( D3D_FEATURE_LEVEL_11_0 )
		, m_pd3dDevice( nullptr )
		, m_pd3dDevice1( nullptr )
		, m_pImmediateContext( nullptr )
		, m_pImmediateContext1( nullptr )
		, m_pSwapChain( nullptr )
		, m_pSwapChain1( nullptr )
		, m_pRenderTargetView( nullptr )
		, m_pDepthStencil( nullptr )
		, m_pDepthStencilView( nullptr )
		, m_pVertexShader( nullptr )
		, m_pPixelShader( nullptr )
		, m_pVertexLayout( nullptr )
		, m_pVertexBuffer( nullptr )
		, m_pIndexBuffer( nullptr )
		, m_pCBNeverChanges( nullptr )
		, m_pCBChangeOnResize( nullptr )
		, m_pCBChangesEveryFrame( nullptr )
		, m_pTextureRV( nullptr )
		, m_pSamplerLinear( nullptr )
	{
		HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &m_pDirect2dFactory);

		m_thread_load = std::thread([this]()
		{
			int c;
			while (true)
			{
				{
					std::unique_lock<std::mutex> lock(m_mutex);

					m_condition_load.wait(lock,
						[this]() { return m_bEndThreads || !m_deqCachesToLoad.empty(); }
					);
				}
				OutputDebugString(L"Load Awaken\n");
				while (true)
				{
					c = -1;
					{
						std::lock_guard<std::mutex> lock(m_mutex);

						if (m_bEndThreads)
							return;

						if (m_deqCachesToLoad.empty())
							break;

						c = m_deqCachesToLoad.front();
						m_deqCachesToLoad.pop_front();
					}
					if (c != -1 && m_vecBitmaps[c].pBitmap == nullptr)
						m_vecBitmaps[c].pBitmap = m_loader->Load( m_vecBitmaps[c].strFileName.c_str() );
				}
			}
		});
	}


	ImageViewer::~ImageViewer()
	{
		m_bEndThreads = true;

		m_condition_load.notify_all();

		m_thread_load.join();

		if (m_pthread_scan)
		{
			m_pthread_scan->join();
			delete m_pthread_scan;
		}
		if (m_pthread_thumbnail)
		{
			m_pthread_thumbnail->join();
			delete m_pthread_thumbnail;
		}
	}

	void ImageViewer::Destroy()
	{
		if (m_pImage)
			m_pImage->Release();
		if (m_pImmediateContext) m_pImmediateContext->ClearState();

		if (m_pTextureRV) m_pTextureRV->Release();
		if (m_pRenderTargetView) m_pRenderTargetView->Release();
		if (m_pSwapChain1) m_pSwapChain1->Release();
		if (m_pSwapChain) m_pSwapChain->Release();
		if (m_pImmediateContext1) m_pImmediateContext1->Release();
		if (m_pImmediateContext) m_pImmediateContext->Release();
		if (m_pd3dDevice1) m_pd3dDevice1->Release();
		if (m_pd3dDevice) m_pd3dDevice->Release();
	}
	bool ImageViewer::Initialize(HWND hWnd)
	{
		HRESULT hr = S_OK;
		m_hWnd = hWnd;
		RECT rc;
		GetClientRect(hWnd, &rc);


		auto width = rc.right - rc.left;
		auto height = rc.bottom - rc.top;

		UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		D3D_DRIVER_TYPE driverTypes[] =
		{
			D3D_DRIVER_TYPE_HARDWARE,
			D3D_DRIVER_TYPE_WARP,
			D3D_DRIVER_TYPE_REFERENCE,
		};
		UINT numDriverTypes = ARRAYSIZE(driverTypes);

		D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
		};
		UINT numFeatureLevels = ARRAYSIZE(featureLevels);

		for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++)
		{
			m_driverType = driverTypes[driverTypeIndex];
			hr = D3D11CreateDevice(nullptr, m_driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
				D3D11_SDK_VERSION, &m_pd3dDevice, &m_featureLevel, &m_pImmediateContext);

			if (hr == E_INVALIDARG)
			{
				// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
				hr = D3D11CreateDevice(nullptr, m_driverType, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1,
					D3D11_SDK_VERSION, &m_pd3dDevice, &m_featureLevel, &m_pImmediateContext);
			}

			if (SUCCEEDED(hr))
				break;
		}
		if (FAILED(hr))
			return false;

		// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
		IDXGIFactory1* dxgiFactory = nullptr;
		{
			IDXGIDevice* dxgiDevice = nullptr;
			hr = m_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
			if (SUCCEEDED(hr))
			{
				IDXGIAdapter* adapter = nullptr;
				hr = dxgiDevice->GetAdapter(&adapter);
				if (SUCCEEDED(hr))
				{
					hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory));
					adapter->Release();
				}
				dxgiDevice->Release();
			}
		}
		if (FAILED(hr))
			return false;

		// Create swap chain
		IDXGIFactory2* dxgiFactory2 = nullptr;
		hr = dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2));
		if (dxgiFactory2)
		{
			// DirectX 11.1 or later
			hr = m_pd3dDevice->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&m_pd3dDevice1));
			if (SUCCEEDED(hr))
			{
				(void)m_pImmediateContext->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&m_pImmediateContext1));
			}

			DXGI_SWAP_CHAIN_DESC1 sd;
			ZeroMemory(&sd, sizeof(sd));
			sd.Width = width;
			sd.Height = height;
			sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			sd.SampleDesc.Count = 1;
			sd.SampleDesc.Quality = 0;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.BufferCount = 1;

			hr = dxgiFactory2->CreateSwapChainForHwnd(m_pd3dDevice, m_hWnd, &sd, nullptr, nullptr, &m_pSwapChain1);
			if (SUCCEEDED(hr))
			{
				hr = m_pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&m_pSwapChain));
			}

			dxgiFactory2->Release();
		}
		else
		{
			// DirectX 11.0 systems
			DXGI_SWAP_CHAIN_DESC sd;
			ZeroMemory(&sd, sizeof(sd));
			sd.BufferCount = 1;
			sd.BufferDesc.Width = width;
			sd.BufferDesc.Height = height;
			sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			sd.BufferDesc.RefreshRate.Numerator = 60;
			sd.BufferDesc.RefreshRate.Denominator = 1;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.OutputWindow = m_hWnd;
			sd.SampleDesc.Count = 1;
			sd.SampleDesc.Quality = 0;
			sd.Windowed = TRUE;

			hr = dxgiFactory->CreateSwapChain(m_pd3dDevice, &sd, &m_pSwapChain);
		}

		// Note this tutorial doesn't handle full-screen swapchains so we block the ALT+ENTER shortcut
		dxgiFactory->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_ALT_ENTER);

		dxgiFactory->Release();

		if (FAILED(hr))
			return hr;

		// Create a render target view
		ID3D11Texture2D* pBackBuffer = nullptr;
		hr = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
		if (FAILED(hr))
			return hr;

		hr = m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView);
		
		pBackBuffer->Release();
		if (FAILED(hr))
			return hr;

		m_pImmediateContext->OMSetRenderTargets(1, &m_pRenderTargetView, nullptr);

		// Create depth stencil texture
		D3D11_TEXTURE2D_DESC descDepth;
		ZeroMemory(&descDepth, sizeof(descDepth));
		descDepth.Width = width;
		descDepth.Height = height;
		descDepth.MipLevels = 1;
		descDepth.ArraySize = 1;
		descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		descDepth.SampleDesc.Count = 1;
		descDepth.SampleDesc.Quality = 0;
		descDepth.Usage = D3D11_USAGE_DEFAULT;
		descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		descDepth.CPUAccessFlags = 0;
		descDepth.MiscFlags = 0;
		hr = m_pd3dDevice->CreateTexture2D(&descDepth, nullptr, &m_pDepthStencil);
		if (FAILED(hr))
			return hr;

		// Create the depth stencil view
		D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;
		ZeroMemory(&descDSV, sizeof(descDSV));
		descDSV.Format = descDepth.Format;
		descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		descDSV.Texture2D.MipSlice = 0;
		hr = m_pd3dDevice->CreateDepthStencilView(m_pDepthStencil, &descDSV, &m_pDepthStencilView);
		if (FAILED(hr))
			return hr;

		m_pImmediateContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);

		// Setup the viewport
		D3D11_VIEWPORT vp;
		vp.Width = (FLOAT)width;
		vp.Height = (FLOAT)height;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		m_pImmediateContext->RSSetViewports(1, &vp);

		// Compile the vertex shader
		ID3DBlob* pVSBlob = nullptr;
		hr = CompileShaderFromFile(L"DIVE.fx", "VS", "vs_4_0", &pVSBlob);
		if (FAILED(hr))
		{
			MessageBox(nullptr,
				L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK);
			return hr;
		}

		// Create the vertex shader
		hr = m_pd3dDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pVertexShader);
		if (FAILED(hr))
		{
			pVSBlob->Release();
			return hr;
		}

		// Define the input layout
		D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		UINT numElements = ARRAYSIZE(layout);

		// Create the input layout
		hr = m_pd3dDevice->CreateInputLayout(layout, numElements, pVSBlob->GetBufferPointer(),
			pVSBlob->GetBufferSize(), &m_pVertexLayout);
		pVSBlob->Release();
		if (FAILED(hr))
			return hr;

		// Set the input layout
		m_pImmediateContext->IASetInputLayout(m_pVertexLayout);

		// Compile the pixel shader
		ID3DBlob* pPSBlob = nullptr;
		hr = CompileShaderFromFile(L"DIVE.fx", "PS", "ps_4_0", &pPSBlob);
		if (FAILED(hr))
		{
			MessageBox(nullptr,
				L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK);
			return hr;
		}

		// Create the pixel shader
		hr = m_pd3dDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pPixelShader);
		pPSBlob->Release();
		if (FAILED(hr))
			return hr;

		// Create vertex buffer
		SimpleVertex vertices[] =
		{
			{ DirectX::XMFLOAT3(-1.0f, 1.0f, -1.0f), DirectX::XMFLOAT2(1.0f, 0.0f) },
			{ DirectX::XMFLOAT3(1.0f, 1.0f, -1.0f), DirectX::XMFLOAT2(0.0f, 0.0f) },
			{ DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f), DirectX::XMFLOAT2(0.0f, 1.0f) },
			{ DirectX::XMFLOAT3(-1.0f, 1.0f, 1.0f), DirectX::XMFLOAT2(1.0f, 1.0f) },

			{ DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f), DirectX::XMFLOAT2(0.0f, 0.0f) },
			{ DirectX::XMFLOAT3(1.0f, -1.0f, -1.0f), DirectX::XMFLOAT2(1.0f, 0.0f) },
			{ DirectX::XMFLOAT3(1.0f, -1.0f, 1.0f), DirectX::XMFLOAT2(1.0f, 1.0f) },
			{ DirectX::XMFLOAT3(-1.0f, -1.0f, 1.0f), DirectX::XMFLOAT2(0.0f, 1.0f) },

			{ DirectX::XMFLOAT3(-1.0f, -1.0f, 1.0f), DirectX::XMFLOAT2(0.0f, 1.0f) },
			{ DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f), DirectX::XMFLOAT2(1.0f, 1.0f) },
			{ DirectX::XMFLOAT3(-1.0f, 1.0f, -1.0f), DirectX::XMFLOAT2(1.0f, 0.0f) },
			{ DirectX::XMFLOAT3(-1.0f, 1.0f, 1.0f), DirectX::XMFLOAT2(0.0f, 0.0f) },

			{ DirectX::XMFLOAT3(1.0f, -1.0f, 1.0f), DirectX::XMFLOAT2(1.0f, 1.0f) },
			{ DirectX::XMFLOAT3(1.0f, -1.0f, -1.0f), DirectX::XMFLOAT2(0.0f, 1.0f) },
			{ DirectX::XMFLOAT3(1.0f, 1.0f, -1.0f), DirectX::XMFLOAT2(0.0f, 0.0f) },
			{ DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f), DirectX::XMFLOAT2(1.0f, 0.0f) },

			{ DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f), DirectX::XMFLOAT2(0.0f, 1.0f) },
			{ DirectX::XMFLOAT3(1.0f, -1.0f, -1.0f), DirectX::XMFLOAT2(1.0f, 1.0f) },
			{ DirectX::XMFLOAT3(1.0f, 1.0f, -1.0f), DirectX::XMFLOAT2(1.0f, 0.0f) },
			{ DirectX::XMFLOAT3(-1.0f, 1.0f, -1.0f), DirectX::XMFLOAT2(0.0f, 0.0f) },

			{ DirectX::XMFLOAT3(-1.0f, -1.0f, 1.0f), DirectX::XMFLOAT2(1.0f, 1.0f) },
			{ DirectX::XMFLOAT3(1.0f, -1.0f, 1.0f), DirectX::XMFLOAT2(0.0f, 1.0f) },
			{ DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f), DirectX::XMFLOAT2(0.0f, 0.0f) },
			{ DirectX::XMFLOAT3(-1.0f, 1.0f, 1.0f), DirectX::XMFLOAT2(1.0f, 0.0f) },
		};

		D3D11_BUFFER_DESC bd;
		ZeroMemory(&bd, sizeof(bd));
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(SimpleVertex) * 24;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = 0;
		D3D11_SUBRESOURCE_DATA InitData;
		ZeroMemory(&InitData, sizeof(InitData));
		InitData.pSysMem = vertices;
		hr = m_pd3dDevice->CreateBuffer(&bd, &InitData, &m_pVertexBuffer);
		if (FAILED(hr))
			return hr;

		// Set vertex buffer
		UINT stride = sizeof(SimpleVertex);
		UINT offset = 0;
		m_pImmediateContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer, &stride, &offset);

		// Create index buffer
		// Create vertex buffer
		WORD indices[] =
		{
			3,1,0,
			2,1,3,

			6,4,5,
			7,4,6,

			11,9,8,
			10,9,11,

			14,12,13,
			15,12,14,

			19,17,16,
			18,17,19,

			22,20,21,
			23,20,22
		};

		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(WORD) * 36;
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bd.CPUAccessFlags = 0;
		InitData.pSysMem = indices;
		hr = m_pd3dDevice->CreateBuffer(&bd, &InitData, &m_pIndexBuffer);
		if (FAILED(hr))
			return hr;

		// Set index buffer
		m_pImmediateContext->IASetIndexBuffer(m_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);

		// Set primitive topology
		m_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Create the constant buffers
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(CBNeverChanges);
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = 0;
		hr = m_pd3dDevice->CreateBuffer(&bd, nullptr, &m_pCBNeverChanges);
		if (FAILED(hr))
			return hr;

		bd.ByteWidth = sizeof(CBChangeOnResize);
		hr = m_pd3dDevice->CreateBuffer(&bd, nullptr, &m_pCBChangeOnResize);
		if (FAILED(hr))
			return hr;

		bd.ByteWidth = sizeof(CBChangesEveryFrame);
		hr = m_pd3dDevice->CreateBuffer(&bd, nullptr, &m_pCBChangesEveryFrame);
		if (FAILED(hr))
			return hr;



		// Create the sample state
		D3D11_SAMPLER_DESC sampDesc;
		ZeroMemory(&sampDesc, sizeof(sampDesc));
		sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampDesc.MinLOD = 0;
		sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
		sampDesc.MaxAnisotropy = 16;
		hr = m_pd3dDevice->CreateSamplerState(&sampDesc, &m_pSamplerLinear);
		if (FAILED(hr))
			return hr;

		// Initialize the world matrices
		m_World = DirectX::XMMatrixIdentity();

		// Initialize the view matrix
		DirectX::XMVECTOR Eye = DirectX::XMVectorSet(0.0f, 3.0f, -6.0f, 0.0f);
		DirectX::XMVECTOR At = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
		DirectX::XMVECTOR Up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		m_View = DirectX::XMMatrixLookAtLH(Eye, At, Up);

		CBNeverChanges cbNeverChanges;
		cbNeverChanges.mView = XMMatrixTranspose(m_View);
		m_pImmediateContext->UpdateSubresource(m_pCBNeverChanges, 0, nullptr, &cbNeverChanges, 0, 0);

		// Initialize the projection matrix
		m_Projection = DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV4, width / (FLOAT)height, 0.01f, 100.0f);

		CBChangeOnResize cbChangesOnResize;
		cbChangesOnResize.mProjection = XMMatrixTranspose(m_Projection);
		m_pImmediateContext->UpdateSubresource(m_pCBChangeOnResize, 0, nullptr, &cbChangesOnResize, 0, 0);

		IDXGISurface* pSurface = nullptr;
		pBackBuffer->QueryInterface(&pSurface);

		D2D1_SIZE_U size = D2D1::SizeU(width, height);

		FLOAT dpiX;
		FLOAT dpiY;
		m_pDirect2dFactory->GetDesktopDpi(&dpiX, &dpiY);

		auto rtp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
			dpiX,
			dpiY
		);
		// Create a Direct2D render target.
		hr = m_pDirect2dFactory->CreateDxgiSurfaceRenderTarget(pSurface,
			&rtp,
			&m_pRenderTarget
		);
		pSurface->Release();
		

		hr = m_pRenderTarget->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF::White, 1.0f),
			&m_pWhiteBrush
		);
		hr = m_pRenderTarget->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF::Black, 1.0f),
			&m_pBlackBrush
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
		if (m_bShowThumbs)
		{
			int nCenterPos = (rcClient.right - rcClient.left - m_nThumbWidth) / 2;
			int nThumbsBeforeCenter =  (nCenterPos + m_nThumbWidth - 1) / (m_nThumbWidth + m_nThumbSpacing);
			int nStartIndex = m_nIndex - nThumbsBeforeCenter;
			if (nStartIndex < 0)
				nStartIndex = 0;
			int nEndIndex = nStartIndex + (rcClient.right - rcClient.left + m_nThumbWidth - 1) / (m_nThumbWidth + m_nThumbSpacing);

			int nX = nCenterPos - (m_nIndex - nStartIndex) * (m_nThumbWidth + m_nThumbSpacing);
			int nY = rcClient.bottom - m_nThumbHeight;

			if (m_nPreviewIndex >= nStartIndex && m_nPreviewIndex <= nEndIndex)
				nX -= m_nThumbWidth;

			while (nX < rcClient.right && nStartIndex < m_vecBitmaps.size())
			{
				auto& bmp = m_vecBitmaps[nStartIndex];

				if (m_bShowThumbs && m_nPreviewIndex == nStartIndex && bmp.pBitmap)
				{
					m_pRenderTarget->DrawBitmap(
						bmp.pBitmapThumbnail,
						D2D1::RectF(nX, nY - m_nThumbHeight * 2, nX + m_nThumbWidth * 3, nY + m_nThumbHeight),
						bmp.fAlpha,
						D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
					);
					nX += m_nThumbWidth * 2;
				}
				else if (bmp.fAlpha > 0.0f && bmp.pBitmapThumbnail)
				{
					m_pRenderTarget->DrawBitmap(
						bmp.pBitmapThumbnail,
						D2D1::RectF(nX, nY, nX + m_nThumbWidth, nY + m_nThumbHeight),
						bmp.fAlpha,
						D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
					);
					if (nStartIndex == m_nIndex)
						m_pRenderTarget->DrawRectangle(
							D2D1::RectF(nX - 2, nY - 2, nX + m_nThumbWidth + 2, nY + m_nThumbHeight + 2),
							m_pWhiteBrush, 4);

				}
				else
				{
					m_pRenderTarget->DrawRectangle(
						D2D1::RectF(nX - 2, nY - 2, nX + m_nThumbWidth + 2, nY + m_nThumbHeight + 2),
						m_pBlackBrush, 2.0f);
				}
				nX += m_nThumbWidth + m_nThumbSpacing;
				++nStartIndex;
			}
		}

		m_pRenderTarget->EndDraw();
	}

	void ImageViewer::RemoveCache(int index)
	{
		OutputDebugString(L"RemoveCache\n");
		if (index > 0 && index < m_vecBitmaps.size())
		{
			{
				std::unique_lock<std::mutex> lock(m_mutex);

				auto it = std::find(m_deqCachesToLoad.begin(), m_deqCachesToLoad.end(), index);
				if (it != m_deqCachesToLoad.end())
					m_deqCachesToLoad.erase(it);
			}
			if (m_vecBitmaps[index].pBitmap)
				m_vecBitmaps[index].pBitmap = nullptr;
		}
	}

	ID3D11ShaderResourceView* ImageViewer::TextureFromWICBitmap(IWICBitmapSource* pWICBitmap)
	{
		HRESULT hr;

		UINT width, height;
		hr = pWICBitmap->GetSize(&width, &height);
		if (FAILED(hr))
			return nullptr;

		size_t rowPitch = (width * 32 + 7) / 8;
		size_t imageSize = rowPitch * height;

		std::unique_ptr<uint8_t[]> temp(new uint8_t[imageSize]);

		hr = pWICBitmap->CopyPixels(0, static_cast<UINT>(rowPitch), static_cast<UINT>(imageSize), temp.get());
		if (FAILED(hr))
			return nullptr;

		// Create texture
		D3D11_TEXTURE2D_DESC desc;
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 0;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		CComPtr<ID3D11Texture2D> tex = nullptr;
		ID3D11ShaderResourceView* pTexRV = nullptr;

		hr = m_pd3dDevice->CreateTexture2D(&desc, nullptr, &tex);
		if (SUCCEEDED(hr) && tex != 0)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
			memset(&SRVDesc, 0, sizeof(SRVDesc));
			SRVDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			SRVDesc.Texture2D.MipLevels = -1;

			hr = m_pd3dDevice->CreateShaderResourceView(tex, &SRVDesc, &pTexRV);
			if (FAILED(hr))
				return nullptr;

			m_pImmediateContext->UpdateSubresource(tex, 0, nullptr, temp.get(), static_cast<UINT>(rowPitch), static_cast<UINT>(imageSize));
			m_pImmediateContext->GenerateMips(pTexRV);
			return pTexRV;
		}
		return nullptr;
	}

	void ImageViewer::UpdateCacheForward()
	{
		OutputDebugString(L"Cache Forward\n");
		int nCacheStart = m_nIndex - 1;
		int nCacheEnd = m_nIndex + 10;

		if (nCacheEnd >= m_vecBitmaps.size())
		{
			nCacheStart -= nCacheEnd - m_vecBitmaps.size() + 1;
			nCacheEnd = m_vecBitmaps.size() - 1;
		}
		if (nCacheStart < 0)
			nCacheStart = 0;
		
		for (int i = m_nCacheStart; i < nCacheStart; ++i)
			RemoveCache(i);
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			for (int i = m_nCacheEnd + 1; i <= nCacheEnd; ++i)
				m_deqCachesToLoad.push_back(i);
		}
		m_condition_load.notify_one();

		m_nCacheStart = nCacheStart;
		m_nCacheEnd = nCacheEnd;
	}

	void ImageViewer::UpdateCacheBackward()
	{
		OutputDebugString(L"Cache Backward\n");
		int nCacheStart = m_nIndex - 10;
		int nCacheEnd = m_nIndex + 1;

		if (nCacheStart < 0)
		{
			nCacheEnd += -nCacheStart;
			nCacheStart = 0;
		}
		if (nCacheEnd >= m_vecBitmaps.size())
			nCacheEnd = m_vecBitmaps.size() - 1;
		for (int i = nCacheEnd + 1; i <= m_nCacheEnd; ++i)
			RemoveCache(i);
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			for (int i = nCacheStart; i < m_nCacheStart; ++i)
				m_deqCachesToLoad.push_back(i);
		}
		m_condition_load.notify_one();

		m_nCacheStart = nCacheStart;
		m_nCacheEnd = nCacheEnd;

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
				m_nCacheStart = nIndex;
				m_nCacheEnd = nIndex;
				bmp.fAlpha = 1.0f;
				break;
			}
			++nIndex;
		}
		if (!m_pthread_thumbnail)
		{
			m_pthread_thumbnail = new std::thread(
				[this]()
				{
					for (auto& bmp : m_vecBitmaps)
					{
						if (m_bEndThreads)
							return;
						if (bmp.pBitmapThumbnail == nullptr)
						{
							HRESULT hr = S_OK;

							IWICBitmapSource* pWICBitmap = m_loader->LoadThumbnail(120, 90, bmp.strFileName.c_str());

							if (SUCCEEDED(hr))
								hr = m_pRenderTarget->CreateBitmapFromWicBitmap(
									pWICBitmap,
									NULL,
									&bmp.pBitmapThumbnail
								);
							if (SUCCEEDED(hr))
							{
								bmp.fAlpha = 1.0f;
							}
							pWICBitmap->Release();
						}
					}
				}
			);
		}
	}
	void ImageViewer::Show(IWICBitmapSource* pWICBitmap)
	{
		HRESULT hr;
		hr = m_pRenderTarget->CreateBitmapFromWicBitmap(
			pWICBitmap,
			NULL,
			&m_pImage
		);
		m_pTextureRV = TextureFromWICBitmap(pWICBitmap);

		m_szImage = SIZE{ (long)m_pImage->GetSize().width, (long)m_pImage->GetSize().height };

		if (m_szClient.cx < m_szImage.cx || m_szClient.cy < m_szImage.cy)
		{
			m_fScale = std::min(
				static_cast<float>(m_szClient.cx) / m_szImage.cx,
				static_cast<float>(m_szClient.cy) / m_szImage.cy);
		}
		else if (m_szImage.cx < m_szClient.cx / 2 || m_szImage.cy < m_szClient.cy / 2)
		{
			m_fScale = std::min(
				m_szClient.cx / 2.0f / m_szImage.cx,
				m_szClient.cy * 3.0f / 4.0f / m_szImage.cy);
		}
		m_fScaleTo = m_fScaleFrom = m_fScale;

		m_rcView = D2D1::RectF(
			(m_szClient.cx - m_szImage.cx * m_fScale ) / 2.0f,
			(m_szClient.cy - m_szImage.cy * m_fScale ) / 2.0f,
			(m_szClient.cx + m_szImage.cx * m_fScale ) / 2.0f,
			(m_szClient.cy + m_szImage.cy * m_fScale ) / 2.0f);
	}
	ID2D1Bitmap* ImageViewer::LoadD2DBitmap(const wchar_t* wszFileName )
	{
		HRESULT hr;
		IWICBitmapSource* pWICBitmap = m_loader->Load(wszFileName);
		ID2D1Bitmap* pD2DBitmap = nullptr;

		if (pWICBitmap)
			hr = m_pRenderTarget->CreateBitmapFromWicBitmap(
				pWICBitmap,
				NULL,
				&pD2DBitmap
			);
		pWICBitmap->Release();

		return pD2DBitmap;
	}
	void ImageViewer::PrevImage()
	{
		OutputDebugString(L"Prev\n");
		if (m_nIndex > 0)
		{
			m_nIndex--;
			if (!m_vecBitmaps[m_nIndex].pBitmap)
				m_vecBitmaps[m_nIndex].pBitmap = m_loader->Load(m_vecBitmaps[m_nIndex].strFileName.c_str());
			Show(m_vecBitmaps[m_nIndex].pBitmap);
		}
		UpdateCacheBackward();
	}

	void ImageViewer::NextImage()
	{
		OutputDebugString(L"Next\n");
		if (m_nIndex < m_vecBitmaps.size() - 1 && m_nIndex >= 0)
		{
			m_nIndex++;
			if (!m_vecBitmaps[m_nIndex].pBitmap)
				m_vecBitmaps[m_nIndex].pBitmap = m_loader->Load(m_vecBitmaps[m_nIndex].strFileName.c_str());
			Show(m_vecBitmaps[m_nIndex].pBitmap);
		}
		UpdateCacheForward();
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

		
		m_fScale = m_fScaleFrom = m_fScaleTo = 1.0f;

		{
			HRESULT hr;
			CComPtr<IWICBitmapSource> pWICBitmap = m_loader->Load(wszFileName);

			if (pWICBitmap)
			{
				hr = m_pRenderTarget->CreateBitmapFromWicBitmap(
					pWICBitmap,
					NULL,
					&m_pImage
				);
				m_pTextureRV = TextureFromWICBitmap(pWICBitmap);
			}
			Show( pWICBitmap );
		}


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
							if (m_bEndThreads)
								return;
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

		

		return true;
	}

	void ImageViewer::OnLBDown(HWND hWnd, LPARAM lParam)
	{
		::SetCapture(hWnd);
		m_bLBDown = true;
		m_ptDown.x = GET_X_LPARAM(lParam);
		m_ptDown.y = GET_Y_LPARAM(lParam);

		::ClientToScreen(hWnd, &m_ptDown);


		RECT rcClient;
		::GetClientRect(hWnd, &rcClient);

		int nCenterPos = (rcClient.right - rcClient.left - m_nThumbWidth) / 2;
		int nIndex = (m_ptDown.x - nCenterPos + m_nIndex * (m_nThumbWidth + m_nThumbSpacing)) / (m_nThumbWidth + m_nThumbSpacing);

		if (m_bShowThumbs && nIndex >= 0 && nIndex < m_vecBitmaps.size())
		{
			m_nIndex = nIndex;
			Show(m_vecBitmaps[m_nIndex].pBitmap);
		}
		else
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
		POINT ptMove{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

		::ClientToScreen(hWnd, &ptMove);

		if (m_bLBDown)
		{
			auto move_x = ptMove.x - m_ptDown.x;
			auto move_y = ptMove.y - m_ptDown.y;

			m_rcView.left += move_x;
			m_rcView.right += move_x;
			m_rcView.top += move_y;
			m_rcView.bottom += move_y;

			Draw(hWnd);

			m_ptDown = ptMove;
		}
		else
		{
			RECT rcClient;
			::GetClientRect(hWnd, &rcClient);

			bool bShowThumbs = (ptMove.y >= rcClient.bottom - m_nThumbHeight);
			if (bShowThumbs != m_bShowThumbs)
			{
				m_bShowThumbs = bShowThumbs;
			}
			if (m_bShowThumbs)
			{
				int nCenterPos = (rcClient.right - rcClient.left - m_nThumbWidth) / 2;
				int nIndex = (ptMove.x - nCenterPos + m_nIndex * (m_nThumbWidth + m_nThumbSpacing)) / (m_nThumbWidth + m_nThumbSpacing) ;

				if (nIndex >= 0 && nIndex < m_vecBitmaps.size() && ptMove.y >= rcClient.bottom - m_nThumbHeight)
				{
					if (m_vecBitmaps[nIndex].pBitmap)
					{
						m_nPreviewIndex = nIndex;
					}
					else
					{
						std::unique_lock<std::mutex> lock(m_mutex);
						m_deqCachesToLoad.push_back(nIndex);
						m_condition_load.notify_one();
					}
				}
				else
				{
					m_nPreviewIndex = -1;
				}
			}
			// if (ptMove.x >= rcClient.right - 30)
			//	NextImage();
		}
	}

	void ImageViewer::OnMouseWheel(HWND hWnd, WPARAM wParam, LPARAM lParam)
	{
		POINT ptWheel = POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		auto fwKeys = GET_KEYSTATE_WPARAM(wParam);
		auto zDelta = GET_WHEEL_DELTA_WPARAM(wParam);

		auto wheel = zDelta / WHEEL_DELTA;

		char szTemp[1024];

		sprintf(szTemp, "MouseWheel - ScaleFrom = %f, Scale = %f, ScaleTo = %f, Wheel = %d\n", m_fScaleFrom, m_fScale, m_fScaleTo, wheel);
		OutputDebugStringA(szTemp);

		auto fScaleDiff = m_fScaleTo - m_fScaleFrom;
		if (fScaleDiff == 0 || wheel * fScaleDiff < 0)
			m_fScaleFrom = m_fScaleTo = m_fScale;

		auto fScaleRatio = m_fScaleTo / m_fScaleFrom;
		float fDelta = 1 * fScaleRatio;
		if ( wheel > 0)
			fDelta = fDelta / (1 - fDelta / 100);

		auto fMin = std::min(m_fScaleFrom, m_fScaleTo);
		auto fMax = std::max(m_fScaleFrom, m_fScaleTo);
		if ( fMin != fMax )
			fDelta *= fMax / fMin * 5;

		sprintf(szTemp, "Delta = %f\n", fDelta);
		OutputDebugStringA(szTemp);

		float fDeltaRatio;
		fDeltaRatio = m_fScaleTo * fScaleRatio * fDelta / 100;

		m_fScaleTo += fDeltaRatio * wheel;

		if (m_fScaleTo < 0.1f)
			m_fScaleTo = 0.1f;
		else if (m_fScaleTo >= 64.0f)
			m_fScaleTo = 64.0f;

		m_fWheelU = 0.5f;
		m_fWheelV = 0.5f;

		auto old_w = m_rcView.right - m_rcView.left;
		auto old_h = m_rcView.bottom - m_rcView.top;

		if (ptWheel.x >= m_rcView.left && ptWheel.x < m_rcView.right  &&
			ptWheel.y >= m_rcView.top && ptWheel.y < m_rcView.bottom)
		{
			m_fWheelU = (ptWheel.x - m_rcView.left) / old_w;
			m_fWheelV = (ptWheel.y - m_rcView.top) / old_h;
		}

		
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

	void ImageViewer::Render()
	{
		// Update our time
		static auto tPrev = std::chrono::high_resolution_clock::now();
		auto t = std::chrono::high_resolution_clock::now();

		auto tDiff = t - tPrev;
		auto tDiffMilli = std::chrono::duration_cast<std::chrono::microseconds>(tDiff).count();

		m_World = DirectX::XMMatrixRotationY(tDiffMilli);

		// m_pImmediateContext->ClearRenderTargetView(m_pRenderTargetView, DirectX::Colors::MidnightBlue);
		m_pImmediateContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
		m_pImmediateContext->PSSetSamplers(0, 1, &m_pSamplerLinear);

		Draw(m_hWnd);

		/*
		CBChangesEveryFrame cb;
		cb.mWorld = DirectX::XMMatrixTranspose(m_World);
		m_pImmediateContext->UpdateSubresource(m_pCBChangesEveryFrame, 0, nullptr, &cb, 0, 0);

		//
		// Render the cube
		//
		m_pImmediateContext->VSSetShader(m_pVertexShader, nullptr, 0);
		m_pImmediateContext->VSSetConstantBuffers(0, 1, &m_pCBNeverChanges);
		m_pImmediateContext->VSSetConstantBuffers(1, 1, &m_pCBChangeOnResize);
		m_pImmediateContext->VSSetConstantBuffers(2, 1, &m_pCBChangesEveryFrame);
		m_pImmediateContext->PSSetShader(m_pPixelShader, nullptr, 0);
		m_pImmediateContext->PSSetConstantBuffers(2, 1, &m_pCBChangesEveryFrame);
		m_pImmediateContext->PSSetShaderResources(0, 1, &m_pTextureRV);
		m_pImmediateContext->PSSetSamplers(0, 1, &m_pSamplerLinear);
		m_pImmediateContext->DrawIndexed(36, 0, 0);
		*/

		
		m_pSwapChain->Present(0, 0);

		
		if (m_fScale != m_fScaleTo)
		{
			char szTemp[1024];

			sprintf(szTemp, "Render - ScaleFrom = %f, Scale = %f, ScaleTo = %f, tDiff = %lu\n", m_fScaleFrom, m_fScale, m_fScaleTo, tDiffMilli);
			// OutputDebugStringA(szTemp);

			auto fScaleDiff = m_fScaleTo - m_fScaleFrom;
			m_fScale += fScaleDiff * tDiffMilli / 400000.0f;
			if (fScaleDiff < 0)
			{
				if (m_fScale < m_fScaleTo)
					m_fScale = m_fScaleTo;
			}
			else
			{
				if (m_fScale > m_fScaleTo)
					m_fScale = m_fScaleTo;
			}
			auto old_w = m_rcView.right - m_rcView.left;
			auto old_h = m_rcView.bottom - m_rcView.top;

			auto new_w = m_fScale * m_szImage.cx;
			auto new_h = m_fScale * m_szImage.cy;

			m_rcView.left -= (new_w - old_w) * m_fWheelU;
			m_rcView.right = m_rcView.left + new_w;
			m_rcView.top -= (new_h - old_h) * m_fWheelV;
			m_rcView.bottom = m_rcView.top + new_h;

			if (m_fScale == m_fScaleTo)
				m_fScaleFrom = m_fScaleTo;
		}
		tPrev = t;
	}
}