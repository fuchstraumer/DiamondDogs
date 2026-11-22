#include "WebGpuDisplay.hpp"
#include "ImageDataFormats.hpp"
#include "PlatformTypes.hpp"

#include <emscripten/em_js.h>

#include <cstdint>
#include <vector>

namespace
{

constexpr int32_t k_DynamicRangeHighBit = 1;
constexpr int32_t k_ColorGamutP3Bit = 2;
constexpr float k_FallbackPaperWhiteNits = 80.0f;
constexpr float k_FallbackHdrPeakNits = 1000.0f;
constexpr float k_FallbackSdrPeakNits = 80.0f;
constexpr float k_FallbackBlackNits = 0.0f;

[[nodiscard]] int32_t QueryDisplayCapabilitiesMask() noexcept;
void RegisterDisplayMediaListeners(uintptr_t observer_id, uint8_t* dirty_flag_ptr) noexcept;
void UnregisterDisplayMediaListeners(uintptr_t observer_id) noexcept;

} // namespace

namespace bootstrap
{

WebGpuDisplayObserver::WebGpuDisplayObserver() noexcept
{
    RegisterDisplayMediaListeners(reinterpret_cast<uintptr_t>(this), &dirtyFlag);
}

WebGpuDisplayObserver::~WebGpuDisplayObserver()
{
    UnregisterDisplayMediaListeners(reinterpret_cast<uintptr_t>(this));
}

bool WebGpuDisplayObserver::ConsumeCapabilitiesChanged() noexcept
{
    if (dirtyFlag == 0u)
    {
        return false;
    }

    dirtyFlag = 0u;
    return true;
}

DisplayColorCapabilities WebGpuDisplayObserver::QueryCapabilities() const noexcept
{
    const int32_t mask = QueryDisplayCapabilitiesMask();
    const bool dynamicRangeHigh = (mask & k_DynamicRangeHighBit) != 0;
    const bool colorGamutP3 = (mask & k_ColorGamutP3Bit) != 0;

    DisplayColorCapabilities capabilities{};
    capabilities.hdrSupported = dynamicRangeHigh;
    capabilities.hdrEnabled = dynamicRangeHigh;
    capabilities.wcgSupported = colorGamutP3;
    capabilities.wcgEnabled = colorGamutP3;
    capabilities.sdrWhiteLevel = k_FallbackPaperWhiteNits;
    capabilities.bitsPerColorChannel = dynamicRangeHigh ? 10u : 8u;
    capabilities.LuminanceSource = DisplayValueSource::Fallback;
    capabilities.ChromaticitySource = DisplayValueSource::Fallback;
    capabilities.MaxLuminance = dynamicRangeHigh ? k_FallbackHdrPeakNits : k_FallbackSdrPeakNits;
    capabilities.MinLuminance = k_FallbackBlackNits;
    capabilities.MaxAverageLuminance = capabilities.MaxLuminance;

    if (dynamicRangeHigh)
    {
        capabilities.ColorMode = DisplayColorCapabilities::ColorModes::HDR;
    }
    else if (colorGamutP3)
    {
        capabilities.ColorMode = DisplayColorCapabilities::ColorModes::WCG;
    }
    else
    {
        capabilities.ColorMode = DisplayColorCapabilities::ColorModes::SDR;
    }

    capabilities.DetectedColorSpace = colorGamutP3 ? ColorSpace::Display_P3_Nonlinear : ColorSpace::sRGB_Nonlinear;
    return capabilities;
}

std::vector<SurfaceFormatPair> WebGpuDisplayObserver::AvailableSurfacePairs(ImageFormat preferred_sdr_format) const
{
    const int32_t mask = QueryDisplayCapabilitiesMask();
    const bool dynamicRangeHigh = (mask & k_DynamicRangeHighBit) != 0;
    const bool colorGamutP3 = (mask & k_ColorGamutP3Bit) != 0;

    std::vector<SurfaceFormatPair> pairs;
    pairs.reserve(3u);
    pairs.push_back(SurfaceFormatPair{ preferred_sdr_format, ColorSpace::sRGB_Nonlinear });

    if (colorGamutP3)
    {
        pairs.push_back(SurfaceFormatPair{ preferred_sdr_format, ColorSpace::Display_P3_Nonlinear });
    }

    if (dynamicRangeHigh)
    {
        pairs.push_back(SurfaceFormatPair{
            ImageFormat{ ImageComponentFormats::RGBA16, ImageDataType::Float },
            ColorSpace::Extended_sRGB_Linear
        });
    }

    return pairs;
}

} // namespace bootstrap

namespace
{

// clang-format off
EM_JS(int32_t, QueryDisplayCapabilitiesMaskJs, (), {
    if (typeof window === 'undefined' || typeof window['matchMedia'] !== 'function')
    {
        return 0;
    }
    var hdrMql = window['matchMedia']('(dynamic-range: high)');
    var p3Mql = window['matchMedia']('(color-gamut: p3)');
    var mask = 0;
    if (hdrMql && hdrMql['matches'])
    {
        mask |= 1;
    }
    if (p3Mql && p3Mql['matches'])
    {
        mask |= 2;
    }
    return mask;
});

EM_JS(void, RegisterDisplayMediaListenersJs, (uintptr_t observer_id, uint8_t* dirty_flag_ptr), {
    if (typeof window === 'undefined' || typeof window['matchMedia'] !== 'function')
    {
        return;
    }
    var registry = globalThis['__ddWebGpuDisplayObservers'];
    if (!registry)
    {
        registry = new Map();
        globalThis['__ddWebGpuDisplayObservers'] = registry;
    }
    var hdrMql = window['matchMedia']('(dynamic-range: high)');
    var p3Mql = window['matchMedia']('(color-gamut: p3)');
    var onChange = function()
    {
        HEAPU8[dirty_flag_ptr] = 1;
    };
    if (hdrMql && typeof hdrMql['addEventListener'] === 'function')
    {
        hdrMql['addEventListener']('change', onChange);
    }
    if (p3Mql && typeof p3Mql['addEventListener'] === 'function')
    {
        p3Mql['addEventListener']('change', onChange);
    }
    registry['set'](observer_id, { 'hdrMql': hdrMql, 'p3Mql': p3Mql, 'onChange': onChange });
});

EM_JS(void, UnregisterDisplayMediaListenersJs, (uintptr_t observer_id), {
    var registry = globalThis['__ddWebGpuDisplayObservers'];
    if (!registry)
    {
        return;
    }
    var entry = registry['get'](observer_id);
    if (!entry)
    {
        return;
    }
    var hdrMql = entry['hdrMql'];
    var p3Mql = entry['p3Mql'];
    var onChange = entry['onChange'];
    if (hdrMql && typeof hdrMql['removeEventListener'] === 'function')
    {
        hdrMql['removeEventListener']('change', onChange);
    }
    if (p3Mql && typeof p3Mql['removeEventListener'] === 'function')
    {
        p3Mql['removeEventListener']('change', onChange);
    }
    registry['delete'](observer_id);
});
// clang-format on

int32_t QueryDisplayCapabilitiesMask() noexcept
{
    return QueryDisplayCapabilitiesMaskJs();
}

void RegisterDisplayMediaListeners(uintptr_t observer_id, uint8_t* dirty_flag_ptr) noexcept
{
    RegisterDisplayMediaListenersJs(observer_id, dirty_flag_ptr);
}

void UnregisterDisplayMediaListeners(uintptr_t observer_id) noexcept
{
    UnregisterDisplayMediaListenersJs(observer_id);
}

} // namespace
