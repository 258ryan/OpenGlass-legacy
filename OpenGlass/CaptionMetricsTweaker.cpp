#include "pch.h"
#include "Shared.hpp"
#include "CaptionMetricsTweaker.hpp"

using namespace OpenGlass;

namespace OpenGlass::CaptionMetricsTweaker
{
	void MyCButton_SetSize(uDWM::CButton* This, const SIZE* size);
	HRESULT MyCTopLevelWindow_UpdateNCAreaPositionsAndSizes(uDWM::CTopLevelWindow* This);
	int WINAPI MyGetSystemMetricsForDpi(int index, UINT dpi);

	decltype(&MyGetSystemMetricsForDpi) g_GetSystemMetricsForDpi_Org{ nullptr };
	decltype(&MyCButton_SetSize) g_CButton_SetSize_Org{ nullptr };
	decltype(&MyCButton_SetSize)* g_CButton_SetSize_Org_Address{ nullptr };
	decltype(&MyCTopLevelWindow_UpdateNCAreaPositionsAndSizes) g_CTopLevelWindow_UpdateNCAreaPositionsAndSizes_Org{ nullptr };

	enum ButtonType : int
	{
		LoneButton = 0,
		MinButton,
		MaxButton,
		CloseButton,
	};
	SIZE CalculateButtonSize(int dpi, int buttonType);
}

// Windows Vista caption buttons, in pixels at 96 DPI and scaled with the window DPI:
// 18 high; minimize 26, maximize/restore 25, close 43 (the three together are 94 wide).
// a lone button (tool windows, dialogs with only a close button) is as wide as the close button.
SIZE CaptionMetricsTweaker::CalculateButtonSize(int dpi, int buttonType)
{
	constexpr int height{ 18 };
	constexpr int minWidth{ 26 };
	constexpr int maxWidth{ 25 };
	constexpr int closeWidth{ 43 };
	constexpr int loneWidth{ closeWidth };

	SIZE buttonSize = { 0, MulDiv(height, dpi, 96) };

	switch (buttonType)
	{
	case CloseButton:
		buttonSize.cx = MulDiv(closeWidth, dpi, 96);
		break;
	case MaxButton:
		buttonSize.cx = MulDiv(maxWidth, dpi, 96);
		break;
	case MinButton:
		buttonSize.cx = MulDiv(minWidth, dpi, 96);
		break;
	case LoneButton:
		buttonSize.cx = MulDiv(loneWidth, dpi, 96);
		break;
	default:
		break;
	}

	return buttonSize;
}

// Windows Vista frame, in pixels at 96 DPI and scaled with the window DPI:
// left/right/bottom 8 (4 sizing + 4 padded), title bar 29 (8 + 21 caption).
// uDWM derives its frame margins from these metrics, so only dwm.exe sees the override.
int WINAPI CaptionMetricsTweaker::MyGetSystemMetricsForDpi(int index, UINT dpi)
{
	switch (index)
	{
	case SM_CXSIZEFRAME:
	case SM_CYSIZEFRAME:
	case SM_CXPADDEDBORDER:
		return MulDiv(4, static_cast<int>(dpi), 96);
	case SM_CYCAPTION:
		return MulDiv(21, static_cast<int>(dpi), 96);
	default:
		return g_GetSystemMetricsForDpi_Org(index, dpi);
	}
}

HRESULT CaptionMetricsTweaker::MyCTopLevelWindow_UpdateNCAreaPositionsAndSizes(uDWM::CTopLevelWindow* This)
{
	const auto hr = g_CTopLevelWindow_UpdateNCAreaPositionsAndSizes_Org(This);

	auto data = This->GetData();
	if (!data)
	{
		return hr;
	}

	auto maximized = This->IsWindowMaximized();
	auto toolWindow = This->IsToolWindow();
	auto loneButton = This->IsLoneButton();

	auto& visibleMargins = This->GetFrameOutsideMargins(maximized);
	auto& borderMargins = This->GetBorderMargins();

	int cxLeft = borderMargins.cxLeftWidth ? borderMargins.cxLeftWidth : data->GetFrameThickness();

	auto UpdateButton = [&](int buttonType, int offsetRight, int offsetTop, SIZE buttonSize)
	{
		if (auto button = This->GetButton(buttonType); button)
		{
			MARGINS inset = { 0x7FFFFFFF, offsetRight, offsetTop, 0x7FFFFFFF };

			g_CButton_SetSize_Org(button, &buttonSize);
			button->SetInsetFromParent(inset);
			button->GetGlyphOpacity() = 1.f;

			return true;
		}
		return false;
	};

	const int dpi = static_cast<int>(data->GetWindowDPI());

	int offsetRight = maximized ? borderMargins.cxRightWidth + 2 : (borderMargins.cxRightWidth ? borderMargins.cxRightWidth - 2 : data->GetFrameThickness() - 2);
	int offsetTop = maximized ? visibleMargins.cyTopHeight - 1 : visibleMargins.cyTopHeight + 1;

	auto closeButtonSize = loneButton ? CalculateButtonSize(dpi, LoneButton) : CalculateButtonSize(dpi, CloseButton);
	auto maxButtonSize = CalculateButtonSize(dpi, MaxButton);
	auto minButtonSize = CalculateButtonSize(dpi, MinButton);

	if (UpdateButton(3, offsetRight, offsetTop, closeButtonSize) && !toolWindow)
	{
		offsetRight = closeButtonSize.cx + offsetRight;
	}

	if (UpdateButton(2, offsetRight, offsetTop, maxButtonSize))
	{
		offsetRight += maxButtonSize.cx;
	}

	if (UpdateButton(1, offsetRight, offsetTop, minButtonSize))
	{
		offsetRight += minButtonSize.cx;
	}

	UpdateButton(0, offsetRight, offsetTop, minButtonSize);

	if (toolWindow)
	{
		// Vista uses the small-caption metric raw for the square tool window button.
		int cySmSize = GetSystemMetricsForDpi(SM_CYSMSIZE, data->GetWindowDPI());
		SIZE toolButtonSize = { cySmSize , cySmSize };

		offsetTop = (borderMargins.cyTopHeight - toolButtonSize.cy - 4 > offsetTop) ? borderMargins.cyTopHeight - toolButtonSize.cy - 4 : offsetTop;
		UpdateButton(3, offsetRight, offsetTop, toolButtonSize);
		offsetRight = toolButtonSize.cx + offsetRight;
	}

	if (auto iconVisual = This->GetIconVisual(); iconVisual && cxLeft > 0)
	{
		iconVisual->SetInsetFromParentLeft(cxLeft);
	}

	if (auto textVisual = This->GetTextVisual(); textVisual)
	{
		if (auto iconVisual = This->GetIconVisual(); iconVisual && cxLeft > 0)
		{
			cxLeft += iconVisual->GetWidth() ? iconVisual->GetWidth() + 5 : 0;
		}
		MARGINS inset = { cxLeft, offsetRight, visibleMargins.cyTopHeight, 0x7FFFFFFF };
		textVisual->SetInsetFromParent(inset);
	}

	return hr;
}

void CaptionMetricsTweaker::MyCButton_SetSize(uDWM::CButton* This, const SIZE* size)
{
	if (Shared::g_captionHeight.has_value())
	{
		const SIZE replacedSize
		{
			size->cx,
			std::min(size->cy, static_cast<LONG>(Shared::g_captionHeight.value() * uDWM::CDesktopManager::GetInstance()->GetDPIValue()))
		};
		return g_CButton_SetSize_Org(This, &replacedSize);
	}

	return g_CButton_SetSize_Org(This, size);
}

void CaptionMetricsTweaker::Update([[maybe_unused]] GlassEngine::UpdateType type)
{
	// the Vista caption metrics are fixed, there is nothing to reload.
}

void CaptionMetricsTweaker::Startup()
{
	if (Shared::g_disabledHooks.test(Shared::DisabledHooks_CaptionMetricsTweaker))
	{
		return;
	}

	uDWM::g_projectionArray.ApplyToVariable("CTopLevelWindow::UpdateNCAreaPositionsAndSizes", g_CTopLevelWindow_UpdateNCAreaPositionsAndSizes_Org);

	PVOID CVisual_SetSize_Org{ nullptr };
	uDWM::g_projectionArray.ApplyToVariable("CVisual::SetSize", CVisual_SetSize_Org);
	for (auto& vf : std::span{ uDWM::CButton::vftable, 20 })
	{
		if (vf == CVisual_SetSize_Org)
		{
			g_CButton_SetSize_Org_Address = reinterpret_cast<decltype(g_CButton_SetSize_Org_Address)>(&vf);
			HookHelper::PatchPointerT(g_CButton_SetSize_Org_Address, MyCButton_SetSize, &g_CButton_SetSize_Org);
		}
	}

	HookHelper::PatchFunctions(
		std::initializer_list<HookHelper::DetourInfo>
		{
			{ &g_CTopLevelWindow_UpdateNCAreaPositionsAndSizes_Org, &MyCTopLevelWindow_UpdateNCAreaPositionsAndSizes },
		},
		true
	);

	g_GetSystemMetricsForDpi_Org = GetSystemMetricsForDpi;
	HookHelper::PatchIAT(
		uDWM::g_moduleHandle,
		std::initializer_list<HookHelper::ImportDllDetourInfo>
		{
			{
				"user32.dll",
				std::initializer_list<HookHelper::ImportFunctionDetourInfo>
				{
					{ "GetSystemMetricsForDpi", &g_GetSystemMetricsForDpi_Org, &MyGetSystemMetricsForDpi }
				}
			}
		}
	);
}

void CaptionMetricsTweaker::Shutdown()
{
	if (Shared::g_disabledHooks.test(Shared::DisabledHooks_CaptionMetricsTweaker))
	{
		return;
	}

	HookHelper::PatchFunctions(
		std::initializer_list<HookHelper::DetourInfo>
		{
			{ &g_CTopLevelWindow_UpdateNCAreaPositionsAndSizes_Org, &MyCTopLevelWindow_UpdateNCAreaPositionsAndSizes },
		},
		false
	);

	SwitchToThread();

	HookHelper::PatchIAT(
		uDWM::g_moduleHandle,
		std::initializer_list<HookHelper::ImportDllDetourInfo>
		{
			{
				"user32.dll",
				std::initializer_list<HookHelper::ImportFunctionDetourInfo>
				{
					{ "GetSystemMetricsForDpi", &g_GetSystemMetricsForDpi_Org, g_GetSystemMetricsForDpi_Org }
				}
			}
		}
	);

	if (g_CButton_SetSize_Org)
	{
		HookHelper::PatchPointerT(g_CButton_SetSize_Org_Address, g_CButton_SetSize_Org);
	}
}
