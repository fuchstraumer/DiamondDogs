#include "PlanetGpuErrors.hpp"

namespace planet
{
    std::string_view ToString(PlanetGpuError error) noexcept
    {
        switch (error)
        {
        case PlanetGpuError::Invalid:
            return "Invalid";
        case PlanetGpuError::Success:
            return "Success";
        case PlanetGpuError::ModuleNotFound:
            return "ModuleNotFound";
        case PlanetGpuError::VariantNotFound:
            return "VariantNotFound";
        case PlanetGpuError::NoVariantFitsDevice:
            return "NoVariantFitsDevice";
        case PlanetGpuError::EntryPointNotFound:
            return "EntryPointNotFound";
        case PlanetGpuError::ProgramCreationFailed:
            return "ProgramCreationFailed";
        case PlanetGpuError::UniformNotFound:
            return "UniformNotFound";
        case PlanetGpuError::DescriptorPoolFailed:
            return "DescriptorPoolFailed";
        case PlanetGpuError::DescriptorWriteFailed:
            return "DescriptorWriteFailed";
        case PlanetGpuError::BufferCreationFailed:
            return "BufferCreationFailed";
        case PlanetGpuError::BufferNotFound:
            return "BufferNotFound";
        case PlanetGpuError::SubmitFailed:
            return "SubmitFailed";
        case PlanetGpuError::ReadbackFailed:
            return "ReadbackFailed";
        case PlanetGpuError::EmptyStep:
            return "EmptyStep";
        default:
            return "Unknown";
        }
    }
}
