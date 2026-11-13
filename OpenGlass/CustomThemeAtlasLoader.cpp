#include "pch.h"
#include "MsstyleInternals.hpp"
#include "Shared.hpp"
#include "HookHelper.hpp"
#include "uDWMProjection.hpp"
#include "CustomThemeAtlasLoader.hpp"

using namespace OpenGlass;
namespace OpenGlass::CustomThemeAtlasLoader
{
	decltype(&MyGetThemeStream) g_GetThemeStream_Org{};
	decltype(&MyGetThemeMargins) g_GetThemeMargins_Org{};
	decltype(&MyGetThemeRect) g_GetThemeRect_Org{};
	decltype(&MyGetThemeInt) g_GetThemeInt_Org{};
	decltype(&MyGetThemeColor) g_GetThemeColor_Org{};

	constexpr size_t make_theme_atlas_layout_key(
		int iPartId,
		int iStateId,
		int iPropId
	)
	{
		const auto u1 = static_cast<size_t>(static_cast<uint16_t>(iPartId));
		const auto u2 = static_cast<size_t>(static_cast<uint16_t>(iStateId));
		const auto ui = static_cast<size_t>(static_cast<uint32_t>(iPropId));
		return (u1 << 48) | (u2 << 32) | ui;
	}
	HRESULT LoadThemeAtlas(LPCWSTR themeAtlasPath);
	void LoadThemeAtlasLayout(LPCWSTR themeAtlasLayoutPath);
	HRESULT LoadTextGlowFromThemeAtlas();
	void LoadHighlights();
	void LoadTheme(
		LPCWSTR themeAtlasPath,
		LPCWSTR themeAtlasLayoutPath
	);

	enum class CompatibilityQuirk
	{
		None,
		RS1
	};
	auto g_themeAtlasLayoutQuirk = CompatibilityQuirk::None;
	std::unordered_map<size_t, MARGINS> g_themeAtlasLayoutMap{};
	std::unique_ptr<BYTE[]> g_themeAtlasStream{};
	UINT g_themeAtlasStreamSize{};
	wil::unique_htheme g_themeHandle{};
	void UnloadTheme()
	{
		Shared::g_textGlowBitmap.reset();

		g_themeAtlasStream.reset();
		g_themeAtlasStreamSize = 0;
		g_themeAtlasLayoutMap.clear();
		g_themeAtlasLayoutQuirk = CompatibilityQuirk::None;
		Shared::g_dontDeflateInactiveFrameGeometry = true;
		Shared::g_captionHeight = std::nullopt;

		g_themeHandle.reset();
	}
}

HRESULT CustomThemeAtlasLoader::MyGetThemeStream(
	HTHEME hTheme,
	int iPartId,
	int iStateId,
	int iPropId,
	VOID** ppvStream,
	DWORD* pcbStream,
	HINSTANCE hInst
)
{
	if (
		hTheme != g_themeHandle.get() ||
		iPartId ||
		iStateId ||
		iPropId != TMT_DISKSTREAM ||
		!g_themeAtlasStreamSize
	)
	{
		return g_GetThemeStream_Org(hTheme, iPartId, iStateId, iPropId, ppvStream, pcbStream, hInst);
	}

	*ppvStream = g_themeAtlasStream.get();
	*pcbStream = g_themeAtlasStreamSize;
	return S_OK;
}

HRESULT CustomThemeAtlasLoader::MyGetThemeMargins(
	HTHEME hTheme,
	HDC hdc,
	int iPartId,
	int iStateId,
	int iPropId,
	LPCRECT prc,
	MARGINS* pMargins
)
{
	decltype(g_themeAtlasLayoutMap)::iterator it;
	if (
		hTheme != g_themeHandle.get() ||
		(
			it = g_themeAtlasLayoutMap.find(
				make_theme_atlas_layout_key(
					iPartId,
					iStateId,
					iPropId
				)
			)
		) == g_themeAtlasLayoutMap.end()
	)
	{
		return g_GetThemeMargins_Org(hTheme, hdc, iPartId, iStateId, iPropId, prc, pMargins);
	}

	*pMargins = it->second;
	return S_OK;
}

HRESULT CustomThemeAtlasLoader::MyGetThemeRect(
	HTHEME hTheme,
	int iPartId,
	int iStateId,
	int iPropId,
	LPRECT pRect
)
{
	decltype(g_themeAtlasLayoutMap)::iterator it;
	if (
		hTheme != g_themeHandle.get() ||
		(
			it = g_themeAtlasLayoutMap.find(
				make_theme_atlas_layout_key(
					iPartId,
					iStateId,
					iPropId
				)
			)
		) == g_themeAtlasLayoutMap.end()
	)
	{
		return g_GetThemeRect_Org(hTheme, iPartId, iStateId, iPropId, pRect);
	}

	memcpy_s(
		pRect,
		sizeof(*pRect),
		&it->second,
		sizeof(it->second)
	);
	return S_OK;
}

// openglass exclusive
HRESULT CustomThemeAtlasLoader::MyGetThemeInt(
	HTHEME hTheme,
	int iPartId,
	int iStateId,
	int iPropId,
	int* piVal
)
{
	decltype(g_themeAtlasLayoutMap)::iterator it;
	if (
		hTheme != g_themeHandle.get() ||
		(
			it = g_themeAtlasLayoutMap.find(
				make_theme_atlas_layout_key(
					iPartId,
					iStateId,
					iPropId
				)
			)
		) == g_themeAtlasLayoutMap.end()
	)
	{
		return g_GetThemeInt_Org(hTheme, iPartId, iStateId, iPropId, piVal);
	}

	*piVal = it->second.cxLeftWidth;
	return S_OK;
}

HRESULT CustomThemeAtlasLoader::MyGetThemeColor(
	HTHEME hTheme,
	int iPartId,
	int iStateId,
	int iPropId,
	COLORREF* pColor
)
{
	decltype(g_themeAtlasLayoutMap)::iterator it;
	if (
		hTheme != g_themeHandle.get() ||
		(
			it = g_themeAtlasLayoutMap.find(
				make_theme_atlas_layout_key(
					iPartId,
					iStateId,
					iPropId
				)
			)
		) == g_themeAtlasLayoutMap.end()
	)
	{
		return g_GetThemeColor_Org(hTheme, iPartId, iStateId, iPropId, pColor);
	}

	*pColor = RGB(it->second.cxLeftWidth, it->second.cxRightWidth, it->second.cyTopHeight);
	return S_OK;
}

HTHEME CustomThemeAtlasLoader::GetThemeHandle()
{
	return g_themeHandle.get();
}

HRESULT CustomThemeAtlasLoader::LoadThemeAtlas(LPCWSTR themeAtlasPath)
{
	if (
		wcslen(themeAtlasPath) != 0 &&
		!PathIsRelativeW(themeAtlasPath) &&
		!PathIsNetworkPathW(themeAtlasPath) &&
		PathFileExistsW(themeAtlasPath)
	)
	{
		wil::unique_hfile file{ CreateFileW(themeAtlasPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, 0) };
		RETURN_LAST_ERROR_IF(!file.is_valid());

		LARGE_INTEGER fileSize{};
		RETURN_IF_WIN32_BOOL_FALSE(GetFileSizeEx(file.get(), &fileSize));

		g_themeAtlasStreamSize = static_cast<UINT>(fileSize.QuadPart);
		g_themeAtlasStream = std::make_unique<BYTE[]>(g_themeAtlasStreamSize);
		RETURN_IF_WIN32_BOOL_FALSE(ReadFile(file.get(), g_themeAtlasStream.get(), g_themeAtlasStreamSize, nullptr, nullptr));
	}
	else
	{
		WCHAR themeFileName[MAX_PATH]{};
		RETURN_IF_FAILED(
			GetCurrentThemeName(
				themeFileName,
				std::size(themeFileName),
				nullptr,
				0,
				nullptr,
				0
			)
		);

		wil::unique_hmodule themeResource{ LoadLibraryExW(themeFileName, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_SEARCH_SYSTEM32) };
		RETURN_LAST_ERROR_IF_NULL(themeResource);

		PVOID streamAddress;
		DWORD streamSize;
		RETURN_IF_FAILED(
			GetThemeStream(
				g_themeHandle.get(),
				static_cast<int>(DWM_WINDOW_THEME_PART::COMMON),
				0,
				TMT_DISKSTREAM,
				&streamAddress,
				&streamSize,
				themeResource.get()
			)
		);

		g_themeAtlasStreamSize = streamSize;
		g_themeAtlasStream = std::make_unique<BYTE[]>(g_themeAtlasStreamSize);
		memcpy_s(
			g_themeAtlasStream.get(),
			g_themeAtlasStreamSize,
			streamAddress,
			streamSize
		);
	}

	return S_OK;
}

void CustomThemeAtlasLoader::LoadThemeAtlasLayout(LPCWSTR themeAtlasLayoutPath)
{
	FILE* file{};
	_wfopen_s(&file, themeAtlasLayoutPath, L"r");
	if (!file)
	{
		return;
	}

	WCHAR strideBuffer[255]{};
	while (!feof(file))
	{
		fgetws(strideBuffer, std::size(strideBuffer), file);

		if (strideBuffer[0] != L'#')
		{
			MARGINS part{};
			MARGINS data{};
			WCHAR keyBuffer[64]{};
			if (
				swscanf_s(
					strideBuffer,
					L"%[^=]=%d,%d,%d,%d",
					keyBuffer,
					static_cast<unsigned int>(std::size(keyBuffer)),
					&data.cxLeftWidth,
					&data.cxRightWidth,
					&data.cyTopHeight,
					&data.cyBottomHeight
				) == 5
			)
			{
				swscanf_s(
					keyBuffer,
					L"%d;%d;%d",
					&part.cxLeftWidth,
					&part.cxRightWidth,
					&part.cyTopHeight
				);

				if (g_themeAtlasLayoutQuirk == CompatibilityQuirk::RS1)
				{
					part.cxLeftWidth = part.cxLeftWidth > 10 ? part.cxLeftWidth - 1 : part.cxLeftWidth;
				}
				g_themeAtlasLayoutMap.try_emplace(
					make_theme_atlas_layout_key(
						part.cxLeftWidth,
						part.cxRightWidth,
						part.cyTopHeight
					),
					data
				);
			}
			else if (
				DWORD value{};
				swscanf_s(
					strideBuffer,
					L"%[^=]=%d",
					keyBuffer,
					static_cast<unsigned int>(std::size(keyBuffer)),
					&value
				) == 2
			)
			{
				if (!wcscmp(keyBuffer, L"DontDeflateInactiveFrameGeometry"))
				{
					Shared::g_dontDeflateInactiveFrameGeometry = static_cast<bool>(value);
				}
				else if (!wcscmp(keyBuffer, L"RS1Compatibility"))
				{
					g_themeAtlasLayoutQuirk = value ? CompatibilityQuirk::RS1 : g_themeAtlasLayoutQuirk;
				}
				else if (!wcscmp(keyBuffer, L"CaptionHeight"))
				{
					Shared::g_captionHeight = value;
				}
			}
		}
	}
	fclose(file);
}

HRESULT CustomThemeAtlasLoader::LoadTextGlowFromThemeAtlas()
{
	RECT textGlowAtlasRect{};
	RETURN_IF_FAILED(
		MyGetThemeRect(
			g_themeHandle.get(),
			static_cast<int>(DWM_WINDOW_THEME_PART::TEXTGLOW),
			0,
			TMT_ATLASRECT,
			&textGlowAtlasRect
		)
	);
	RETURN_IF_WIN32_BOOL_FALSE(InflateRect(&textGlowAtlasRect, -1, -1));
	winrt::com_ptr<IStream> stream{ SHCreateMemStream(g_themeAtlasStream.get(), g_themeAtlasStreamSize), winrt::take_ownership_from_abi };
	RETURN_HR_IF_NULL(E_OUTOFMEMORY, stream);
	winrt::com_ptr<IWICImagingFactory> wicFactory{ nullptr };
	wicFactory.copy_from(uDWM::CDesktopManager::GetInstance()->GetWICFactory());
	winrt::com_ptr<IWICBitmapDecoder> wicDecoder{ nullptr };
	RETURN_IF_FAILED(
		wicFactory->CreateDecoderFromStream(
			stream.get(),
			&GUID_VendorMicrosoft,
			WICDecodeMetadataCacheOnDemand,
			wicDecoder.put()
		)
	);
	winrt::com_ptr<IWICBitmapFrameDecode> wicFrame{ nullptr };
	RETURN_IF_FAILED(wicDecoder->GetFrame(0, wicFrame.put()));
	winrt::com_ptr<IWICFormatConverter> wicConverter{ nullptr };
	RETURN_IF_FAILED(wicFactory->CreateFormatConverter(wicConverter.put()));
	RETURN_IF_FAILED(
		wicConverter->Initialize(
			wicFrame.get(),
			GUID_WICPixelFormat32bppPBGRA,
			WICBitmapDitherTypeNone,
			nullptr,
			0,
			WICBitmapPaletteTypeCustom
		)
	);

	Shared::g_textGlowBitmapInfo =
	{
		{
			sizeof(Shared::g_textGlowBitmapInfo.bmiHeader),
			wil::rect_width(textGlowAtlasRect),
			-wil::rect_height(textGlowAtlasRect),
			1,
			32,
			BI_RGB
		}
	};
	Shared::g_textGlowBitmap.reset(
		CreateDIBSection(
			nullptr,
			&Shared::g_textGlowBitmapInfo,
			DIB_RGB_COLORS,
			&Shared::g_textGlowBitmapPixels,
			nullptr,
			0
		)
	);
	RETURN_LAST_ERROR_IF_NULL(Shared::g_textGlowBitmap);

	WICRect copyRect{ textGlowAtlasRect.left, textGlowAtlasRect.top, Shared::g_textGlowBitmapInfo.bmiHeader.biWidth, -Shared::g_textGlowBitmapInfo.bmiHeader.biHeight };
	RETURN_IF_FAILED(
		wicConverter->CopyPixels(
			&copyRect,
			Shared::g_textGlowBitmapInfo.bmiHeader.biWidth * 4,
			Shared::g_textGlowBitmapInfo.bmiHeader.biWidth * std::abs(Shared::g_textGlowBitmapInfo.bmiHeader.biHeight) * 4,
			reinterpret_cast<BYTE*>(Shared::g_textGlowBitmapPixels)
		)
	);

	return S_OK;
}

// the Vista frame highlights (art and opacities) are fixed, see Shared.hpp. a theme reload only has to make
// the render threads rebuild their highlight bitmaps.
void CustomThemeAtlasLoader::LoadHighlights()
{
	Shared::g_highlightGeneration++;
}

void CustomThemeAtlasLoader::LoadTheme(LPCWSTR themeAtlasPath, LPCWSTR themeAtlasLayoutPath)
{
	g_themeHandle.reset(OpenThemeData(nullptr, L"DWMWindow"));

	if (SUCCEEDED(LoadThemeAtlas(themeAtlasPath)))
	{
		LoadThemeAtlasLayout(themeAtlasLayoutPath);
		LoadTextGlowFromThemeAtlas();
	}
}

void CustomThemeAtlasLoader::Update(GlassEngine::UpdateType type)
{
	if (type & GlassEngine::UpdateType::Theme)
	{
		WCHAR themeAtlasPath[MAX_PATH]{}, themeAtlasLayoutPath[MAX_PATH]{};
		GlassEngine::GetStringFromRegistry(
			L"CustomThemeAtlas",
			themeAtlasPath
		);
		PathUnquoteSpacesW(themeAtlasPath);
		wcscpy_s(themeAtlasLayoutPath, themeAtlasPath);
		wcscat_s(themeAtlasLayoutPath, L".layout");

		UnloadTheme();
		LoadTheme(
			themeAtlasPath,
			themeAtlasLayoutPath
		);
		LoadHighlights();
	}
	if (type & GlassEngine::UpdateType::Backdrop || type & GlassEngine::UpdateType::Theme)
	{
		// Windows Vista Aero defaults; the registry values (when present) still override them.
		// 0xFFFFFFFE / 0xFFFFFFFF ("auto" / "from theme") fall back to the Vista default.
		const auto percent = [](DWORD raw, float vistaDefault)
		{
			return (raw == 0xFFFFFFFE || raw == 0xFFFFFFFF) ? vistaDefault : std::clamp(static_cast<float>(raw) / 100.f, 0.f, 1.f);
		};

		// reflection.png carries the sheen's own vertical alpha profile, so it is drawn at full strength:
		// GetAdjustedReflectionIntensity() turns 100% * 100% intensity into a factor of 1 after clamping.
		// inactive windows get half that (25% -> factor 0.5), so the active window stands out.
		auto active = GlassEngine::GetDwordFromRegistry(L"ColorizationGlassReflectionOpacity", 0xFFFFFFFE);
		auto inactive = GlassEngine::GetDwordFromRegistry(L"ColorizationGlassReflectionOpacityInactive", active);
		auto maximized = GlassEngine::GetDwordFromRegistry(L"ColorizationGlassReflectionOpacityMaximized", active);
		auto inactiveMaximized = GlassEngine::GetDwordFromRegistry(L"ColorizationGlassReflectionOpacityInactiveMaximized", inactive);
		Shared::g_reflectionOpacity = percent(active, 1.f);
		Shared::g_reflectionOpacityInactive = percent(inactive, .25f);
		Shared::g_reflectionOpacityMaximized = percent(maximized, 1.f);
		Shared::g_reflectionOpacityInactiveMaximized = percent(inactiveMaximized, .25f);

		// colorization opacity: active, inactive, maximized, inactive maximized
		active = GlassEngine::GetDwordFromRegistry(L"ColorizationOpacity", 0xFFFFFFFE);
		inactive = GlassEngine::GetDwordFromRegistry(L"ColorizationOpacityInactive", active);
		maximized = GlassEngine::GetDwordFromRegistry(L"ColorizationOpacityMaximized", active);
		inactiveMaximized = GlassEngine::GetDwordFromRegistry(L"ColorizationOpacityInactiveMaximized", inactive);
		// active glass is the more see-through one (55%), inactive glass is the more opaque one (100%).
		Shared::g_colorizationOpacity = percent(active, .55f);
		Shared::g_colorizationOpacityInactive = percent(inactive, 1.f);
		Shared::g_colorizationOpacityMaximized = percent(maximized, .75f);
		Shared::g_colorizationOpacityInactiveMaximized = percent(inactiveMaximized, .75f);

		// base colors: maximized windows blend onto opaque black (Vista prefers ColorizationBaseMaximized
		// when glass is opaque), the opaque fallback is the light gray used when transparency is off.
		const auto base = [](PCWSTR key, DWORD vistaDefault)
		{
			const auto raw = GlassEngine::GetDwordFromRegistry(key, 0xFFFFFFFE);
			return Color::FromArgb((raw == 0xFFFFFFFE || raw == 0xFFFFFFFF) ? vistaDefault : raw, false);
		};
		Shared::g_colorizationBaseTransparent = base(L"ColorizationBaseTransparent", 0x00000000);
		Shared::g_colorizationBaseMaximized = base(L"ColorizationBaseMaximized", 0xFF000000);
		Shared::g_colorizationBaseOpaque = base(L"ColorizationBaseOpaque", 0xFFDFDFDF);
	}
}

void CustomThemeAtlasLoader::Startup()
{
	const auto build_before_w10_2004 = uDWM::g_versionInfo.build < os::build_w10_2004;
	HookHelper::PatchDelayloadIAT(
		uDWM::g_moduleHandle,
		std::initializer_list<HookHelper::ImportDllDetourInfo>
		{
			{
				"ext-ms-win-uxtheme-themes-l1-1-0.dll",
				std::initializer_list<HookHelper::ImportFunctionDetourInfo>
				{
					{ "GetThemeMargins", &g_GetThemeMargins_Org, &MyGetThemeMargins },
					{ "GetThemeInt", &g_GetThemeInt_Org, &MyGetThemeInt },
					{ "GetThemeColor", &g_GetThemeColor_Org, &MyGetThemeColor }
				}
			},
			{
				build_before_w10_2004 ? "UxTheme.dll" : "ext-ms-win-uxtheme-themes-l1-1-2.dll",
				std::initializer_list<HookHelper::ImportFunctionDetourInfo>
				{
					{ "GetThemeStream", &g_GetThemeStream_Org, &MyGetThemeStream },
					{ "GetThemeRect", &g_GetThemeRect_Org, &MyGetThemeRect }
				}
			}
		}
	);

	PostMessageW(FindWindowW(L"DWM", nullptr), WM_THEMECHANGED, 0, 0);
}
void CustomThemeAtlasLoader::Shutdown()
{
	const auto build_before_w10_2004 = uDWM::g_versionInfo.build < os::build_w10_2004;
	HookHelper::PatchDelayloadIAT(
		uDWM::g_moduleHandle,
		std::initializer_list<HookHelper::ImportDllDetourInfo>
		{
			{
				"ext-ms-win-uxtheme-themes-l1-1-0.dll",
				std::initializer_list<HookHelper::ImportFunctionDetourInfo>
				{
					{ "GetThemeMargins", &g_GetThemeMargins_Org, g_GetThemeMargins_Org },
					{ "GetThemeInt", &g_GetThemeInt_Org, g_GetThemeInt_Org },
					{ "GetThemeColor", &g_GetThemeColor_Org, g_GetThemeColor_Org }
				}
			},
			{
				build_before_w10_2004 ? "UxTheme.dll" : "ext-ms-win-uxtheme-themes-l1-1-2.dll",
				std::initializer_list<HookHelper::ImportFunctionDetourInfo>
				{
					{ "GetThemeStream", &g_GetThemeStream_Org, g_GetThemeStream_Org },
					{ "GetThemeRect", &g_GetThemeRect_Org, g_GetThemeRect_Org }
				}
			}
		}
	);

	SendMessageW(FindWindowW(L"DWM", nullptr), WM_THEMECHANGED, 0, 0);

	UnloadTheme();
}
