#pragma once

#include <string>
#include <windowsx.h>
#include <d2d1_1.h>

namespace DIVE
{

	class ImageLoader
	{
	public:
		ImageLoader();
		~ImageLoader();

		IWICBitmapSource* Load(const wchar_t* szFileName);
		bool CanLoad(const wchar_t* wszFileName);

	private:
		IWICImagingFactory* m_pImagingFactory;
	};

}