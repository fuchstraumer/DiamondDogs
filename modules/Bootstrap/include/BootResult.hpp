#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_BOOT_RESULT_HPP
#define DIAMOND_DOGS_BOOTSTRAP_BOOT_RESULT_HPP
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bootstrap
{
    enum class BootResultCode : uint8_t
    {
        Invalid = 0,
        Success = 1,
        // launch: the arguments, the config file, the profile, the module list
        MalformedArgument,
        ConfigUnreadable,
        ConfigInvalid,
        ProfileWithoutConfig,
        UnknownProfile,
        UnknownModule,
        DuplicateModule,
        InvalidDescriptor,
        ModuleAfterLastStage,
        DependencyNotSelected,
        DependencyNotEarlier,
        // module start
        UndeclaredDependency,
        ModuleStartFailed,
        ModuleInstanceInvalid,
        // content
        BundleNotFound,
        BundleInvalid,
        // device
        InstanceCreateFailed,
        SurfaceCreateFailed,
        NoDeviceMeetsRequirements,
        DeviceCreateFailed,
        // gpu
        SwapchainCreateFailed,
        ResourceContextFailed,
    };

    enum class Stage : uint8_t
    {
        Invalid = 0,
        Launch,
        Core,
        Content,
        Window,
        Device,
        Gpu,
    };

    /** The status of one boot step. A step that yields no value returns this record, with Code set to Success when
     *  the step succeeded. The step fills the fields, and FormatFailure turns them into text. */
    struct BootResult
    {
        BootResultCode Code{ BootResultCode::Invalid };
        Stage FailedStage{ Stage::Invalid };
        // the descriptor name of the module that failed, or that the failure came from
        std::string_view Module;
        // the item that failed: a capability, an extension, a file path, a module name, an argument
        std::string Subject;
        // where the item came from: "bundle/module/entry point" for a capability, a module name for an extension
        std::string Source;
        // the nearest valid names, for UnknownModule and UnknownProfile
        std::vector<std::string> Suggestions;

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    /** @brief Returns a result with Code set to Success, for the step that finished in `stage`. */
    [[nodiscard]] BootResult SuccessIn(Stage stage);
    [[nodiscard]] std::string_view ToString(BootResultCode code) noexcept;
    [[nodiscard]] std::string_view ToString(Stage stage) noexcept;
    /** @brief Returns the failure as a few readable lines, for the console. */
    [[nodiscard]] std::string FormatFailure(const BootResult& failure);
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_BOOT_RESULT_HPP
