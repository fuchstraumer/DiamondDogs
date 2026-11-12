#include "BootResult.hpp"
#include <format>
#include <iterator>

namespace
{
    void AppendDetailLine(std::string& output, std::string_view label, std::string_view value);
}

namespace bootstrap
{
    bool BootResult::Succeeded() const noexcept
    {
        return Code == BootResultCode::Success;
    }

    BootResult SuccessIn(Stage stage)
    {
        return BootResult{ .Code = BootResultCode::Success, .FailedStage = stage };
    }

    std::string_view ToString(BootResultCode code) noexcept
    {
        switch (code)
        {
        case BootResultCode::Invalid: return "Invalid";
        case BootResultCode::Success: return "Success";
        case BootResultCode::MalformedArgument: return "MalformedArgument";
        case BootResultCode::ConfigUnreadable: return "ConfigUnreadable";
        case BootResultCode::ConfigInvalid: return "ConfigInvalid";
        case BootResultCode::ProfileWithoutConfig: return "ProfileWithoutConfig";
        case BootResultCode::UnknownProfile: return "UnknownProfile";
        case BootResultCode::UnknownModule: return "UnknownModule";
        case BootResultCode::DuplicateModule: return "DuplicateModule";
        case BootResultCode::InvalidDescriptor: return "InvalidDescriptor";
        case BootResultCode::ModuleAfterLastStage: return "ModuleAfterLastStage";
        case BootResultCode::DependencyNotSelected: return "DependencyNotSelected";
        case BootResultCode::DependencyNotEarlier: return "DependencyNotEarlier";
        case BootResultCode::UndeclaredDependency: return "UndeclaredDependency";
        case BootResultCode::ModuleStartFailed: return "ModuleStartFailed";
        case BootResultCode::ModuleInstanceInvalid: return "ModuleInstanceInvalid";
        case BootResultCode::BundleNotFound: return "BundleNotFound";
        case BootResultCode::BundleInvalid: return "BundleInvalid";
        case BootResultCode::BundleNameConflict: return "BundleNameConflict";
        case BootResultCode::InstanceCreateFailed: return "InstanceCreateFailed";
        case BootResultCode::SurfaceCreateFailed: return "SurfaceCreateFailed";
        case BootResultCode::NoDeviceMeetsRequirements: return "NoDeviceMeetsRequirements";
        case BootResultCode::DeviceCreateFailed: return "DeviceCreateFailed";
        case BootResultCode::SwapchainCreateFailed: return "SwapchainCreateFailed";
        case BootResultCode::ResourceContextFailed: return "ResourceContextFailed";
        }

        return "Invalid";
    }

    std::string_view ToString(Stage stage) noexcept
    {
        switch (stage)
        {
        case Stage::Invalid: return "Invalid";
        case Stage::Launch: return "Launch";
        case Stage::Core: return "Core";
        case Stage::Content: return "Content";
        case Stage::Window: return "Window";
        case Stage::Device: return "Device";
        case Stage::Gpu: return "Gpu";
        }

        return "Invalid";
    }

    std::string FormatFailure(const BootResult& failure)
    {
        std::string output = std::format("boot failed in {}: {}", ToString(failure.FailedStage), ToString(failure.Code));

        AppendDetailLine(output, "module:", failure.Module);
        AppendDetailLine(output, "subject:", failure.Subject);
        AppendDetailLine(output, "source:", failure.Source);

        for (const std::string& suggestion : failure.Suggestions)
        {
            AppendDetailLine(output, "did you mean:", suggestion);
        }

        return output;
    }
}

namespace
{
    void AppendDetailLine(std::string& output, std::string_view label, std::string_view value)
    {
        if (value.empty())
        {
            return;
        }

        std::format_to(std::back_inserter(output), "\n  {:<13} {}", label, value);
    }
}
