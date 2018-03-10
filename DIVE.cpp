// DIVE.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "DIVE.h"
#include "ImageViewer.h"
#include <stdio.h>
#include <Objbase.h>

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

DIVE::ImageViewer* s_loader;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

	HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	s_loader = new DIVE::ImageViewer;

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_DIVE, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }


	MSG msg = { 0 };

    // Main message loop:
    while ( WM_QUIT != msg.message)
    {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			s_loader->Render();
			_sleep(0);
		}
    }
	s_loader->Destroy();
	delete s_loader;
	CoUninitialize();

    return (int) msg.wParam;
}


ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_DROPSHADOW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DIVE));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = 0;
    // wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_DIVE);
	wcex.lpszMenuName = 0;
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   RECT rcWS;
   SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWS, 0);

   auto width = rcWS.right - rcWS.left;
   auto height = rcWS.bottom - rcWS.top;


   HWND hWnd = CreateWindowExW(0, szWindowClass, szTitle, WS_POPUP,
      0, 0, width, height, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   s_loader->Initialize( hWnd );
   s_loader->Capture(width, height);
   s_loader->Load(L"D:\\Photos\\가족사진 이것저것\\couple.jpg");
   
   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc;

    switch (message)
    {
    case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		break;

	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_LEFT:
			s_loader->PrevImage(); break;
		case VK_RIGHT:
			s_loader->NextImage(); break;
		case VK_ESCAPE:
			PostQuitMessage(0); break;
		}
		break;

	case WM_ERASEBKGND:
		return TRUE;

	case WM_LBUTTONDOWN:
		s_loader->OnLBDown(hWnd, lParam); break;

	case WM_LBUTTONUP:
		s_loader->OnLBUp(); break;

	case WM_MOUSEWHEEL:
		s_loader->OnMouseWheel( hWnd, wParam, lParam ); break;

	case WM_MOUSEMOVE:
		s_loader->OnMouseMove(hWnd, lParam); break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
