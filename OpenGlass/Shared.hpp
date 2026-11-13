#pragma once
#include "framework.hpp"
#include "cpprt.hpp"

namespace OpenGlass::Shared
{
	inline bool g_xxSaver{ false };
	inline bool g_transparencyEnabled{ true };

	// OpenGlass renders Windows Vista Aero only. "Blur" is the Vista glass model
	// (colorization blended over a blurred backdrop); "Aero" is the Windows 7 balance model,
	// kept only as dead code in the renderers and never selected.
	enum class GlassType : UINT
	{
		Blur,
		Aero,

		Invalid = 0xFFFFFFFF
	};
	inline constexpr GlassType g_type{ GlassType::Blur };
	inline enum ReflectionPolicy : UINT
	{
		NonClient = 1 << 0,
		LivePreview = 1 << 2,
		AnimatedGlassSheet = 1 << 3,
	} g_reflectionPolicy{ 0 };

	inline bool g_disableOnBattery{ true };
	inline constexpr bool g_overrideAccent{ false };
	// Vista has no modern borders
	inline constexpr bool g_disableModernBorders{ true };
	// Vista: 8
	inline int g_roundRectRadius{ 8 };
	inline float g_blurAmount{};
	inline D2D1_GAUSSIANBLUR_OPTIMIZATION g_blurOptimization{};
	inline float g_glassOpacity{};
	inline float g_glassOpacityInactive{};
	inline constexpr bool g_useD3DRendering{ true };

	inline D2D1_COLOR_F g_color{};
	inline D2D1_COLOR_F g_colorInactive{};

	// vista: 1.f
	// w7: 1.f
	inline float g_colorizationOpacity{};
	// vista: 0.55f
	// w7: 0.4f
	inline float g_colorizationOpacityInactive{};
	// vista: 0.75f
	// w7: 1.f
	inline float g_colorizationOpacityMaximized{};
	// vista: 0.75f
	// w7: 0.4f
	inline float g_colorizationOpacityInactiveMaximized{};

	inline float g_glowOpacity{};
	inline float g_glowOpacityInactive{};
	inline float g_glowOpacityMaximized{};
	inline float g_glowOpacityInactiveMaximized{};

	inline enum OpaqueBlendPriority : UINT
	{
		Vista,
		Win7
	} g_opaqueBlendPriority{ OpaqueBlendPriority::Vista };
	inline bool g_opaqueBlend{};
	inline D2D1_COLOR_F g_colorizationBaseTransparent{};
	inline D2D1_COLOR_F g_colorizationBaseOpaque{};
	inline D2D1_COLOR_F g_colorizationBaseMaximized{};

	inline BITMAPINFO g_textGlowBitmapInfo{};
	inline PVOID g_textGlowBitmapPixels{};
	inline wil::unique_hbitmap g_textGlowBitmap{};
	inline int g_textGlowMode{ 1 };
	inline bool g_dontDeflateInactiveFrameGeometry{ true };
	inline std::optional<LONG> g_captionHeight{};

	inline float g_reflectionIntensity{};
	inline float g_reflectionOpacity{};
	inline float g_reflectionOpacityInactive{};
	inline float g_reflectionOpacityMaximized{};
	inline float g_reflectionOpacityInactiveMaximized{};

	inline float g_reflectionParallaxIntensity{};
	inline std::wstring g_reflectionTexturePath{};

	inline std::wstring g_highlightTexturePath{};
	inline std::wstring g_sideHighlightTexturePath{};

	// Vista frame highlights. corner: active, inactive, maximized, inactive maximized
	// (Vista hides the corner glare on maximized windows). side: active, inactive.
	inline constexpr std::array<float, 4> g_highlightOpacity{ .75f, .75f, 0.f, 0.f };
	inline constexpr std::array<float, 2> g_sideHighlightOpacity{ 1.f, .5f };
	inline constexpr int g_sideHighlightSizingMargin{ 2 };
	// bumped after every theme reload so render threads rebuild their bitmaps.
	inline std::atomic<UINT> g_highlightGeneration{};

	inline float g_materialIntensity{};
	inline std::wstring g_materialTexturePath{};

	enum DisabledHooks : UINT
	{
		DisabledHooks_CaptionTextHandler,
		DisabledHooks_AccentOverrider,
		DisabledHooks_GlassFrameHandler,
		DisabledHooks_GlassReflectionHandler,
		DisabledHooks_CaptionMetricsTweaker,
	};
	inline std::bitset<8> g_disabledHooks{};

	FORCEINLINE bool IsTransparencyDisabled()
	{
		if (g_opaqueBlend)
		{
			return true;
		}
		if (g_xxSaver && g_disableOnBattery)
		{
			return true;
		}
		if (!g_transparencyEnabled)
		{
			return true;
		}

		return false;
	}
	FORCEINLINE bool IsGlassFullyOpaque(
		float alpha,
		float blurBalance,
		float afterglowBalance
	)
	{
		if (
			g_type == GlassType::Blur &&
			alpha == 1.f
		)
		{
			return true;
		}
		// uDWM!CCapturedGlassColorizationParameters::IsGlassFullyOpaque (Windows 7)
		if (
			g_type == GlassType::Aero &&
			blurBalance == 0.f &&
			afterglowBalance == 0.f
		)
		{
			return true;
		}

		return false;
	}
}
