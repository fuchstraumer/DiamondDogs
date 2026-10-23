#include "ColorPipeError.hpp"
#include <string_view>

namespace color
{
    std::string_view ToString(ColorPipeError error) noexcept
    {
        switch (error)
        {
        case ColorPipeError::Invalid: return "Invalid";
        case ColorPipeError::Success: return "Success";
        case ColorPipeError::SurfaceUnusable: return "SurfaceUnusable";
        case ColorPipeError::CompositeModuleMissing: return "CompositeModuleMissing";
        case ColorPipeError::CompositeVariantMissing: return "CompositeVariantMissing";
        case ColorPipeError::ProgramCreationFailed: return "ProgramCreationFailed";
        case ColorPipeError::ShaderLayoutMismatch: return "ShaderLayoutMismatch";
        case ColorPipeError::ResourceCreationFailed: return "ResourceCreationFailed";
        case ColorPipeError::DescriptorSetupFailed: return "DescriptorSetupFailed";
        case ColorPipeError::TonemapperUnavailable: return "TonemapperUnavailable";
        case ColorPipeError::SwapchainMissing: return "SwapchainMissing";
        default: return "Unknown";
        }
    }
}
