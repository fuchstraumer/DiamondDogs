#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_GPU_STAGE_HPP
#define DIAMOND_DOGS_BOOTSTRAP_GPU_STAGE_HPP
#include "ModuleDescriptor.hpp"
#include <memory>
#include <span>
#include <string_view>
#include <vector>

class ResourceContext;

namespace bootstrap
{
    struct BootResult;
    class CoreStage;
    class ContentStage;
    class DeviceStage;
    class LaunchStage;
    class WindowStage;

    /** @brief The last stage: it creates ResourceContext and, with a window, the swapchain. Then it starts every
     *  selected module that has StartGpu, in list order. */
    class GpuStage
    {
    public:
        GpuStage() noexcept;
        /** @brief Calls Stop. */
        ~GpuStage();
        // started modules and the app store a reference to this stage
        GpuStage(const GpuStage&) = delete;
        GpuStage& operator=(const GpuStage&) = delete;
        GpuStage(GpuStage&&) = delete;
        GpuStage& operator=(GpuStage&&) = delete;

        /** @brief Creates ResourceContext, then the swapchain, then starts the GPU modules. Fails with
         *  ResourceContextFailed, SwapchainCreateFailed or a module failure, and then stops what it started.
         *  `_device` and `_window` must outlive this stage. */
        [[nodiscard]] BootResult Start(DeviceStage& _device, WindowStage& _window);
        /** @brief The headless path: no swapchain. */
        [[nodiscard]] BootResult StartHeadless(DeviceStage& _device);
        /** @brief Destroys the started modules in the reverse order of start, then the swapchain, then
         *  ResourceContext. */
        void Stop() noexcept;

        [[nodiscard]] bool Started() const noexcept;
        [[nodiscard]] DeviceStage& Device() const noexcept;
        [[nodiscard]] bool HasWindow() const noexcept;
        /** @brief The window stage. Only valid when HasWindow() is true. */
        [[nodiscard]] WindowStage& Window() const noexcept;
        [[nodiscard]] ResourceContext& Resources() const noexcept;
        /** @brief Returns the started modules, in start order. */
        [[nodiscard]] std::span<const ModuleInstance> Modules() const noexcept;
        /** @brief Returns the object of the started GPU module with this descriptor name, or null. */
        [[nodiscard]] void* FindModuleObject(std::string_view name) const noexcept;

    private:
        // `window_stage` is null on the headless path
        [[nodiscard]] BootResult start(DeviceStage& _device, WindowStage* window_stage);
        [[nodiscard]] BootResult startModule(const ModuleDescriptor& descriptor);

        DeviceStage* device{ nullptr };
        WindowStage* window{ nullptr };
        std::unique_ptr<ResourceContext> resources;
        std::vector<ModuleInstance> modules;
    };

    /** What a GPU module start function can reach: every stage, and the modules that it declared in DependsOn
     *  (core or GPU). The context exists only while GpuStage::Start runs. */
    class GpuStartContext
    {
    public:
        GpuStartContext(GpuStage& _gpu, const ModuleDescriptor& _module) noexcept;

        [[nodiscard]] const LaunchStage& Launch() const noexcept;
        // const: ContentStage holds the core stage as const, so no later stage reaches the Scheduler
        [[nodiscard]] const CoreStage& Core() const noexcept;
        [[nodiscard]] const ContentStage& Content() const noexcept;
        [[nodiscard]] DeviceStage& Device() const noexcept;
        [[nodiscard]] GpuStage& Gpu() const noexcept;
        /** @brief Returns the descriptor of the module that is starting. */
        [[nodiscard]] const ModuleDescriptor& Module() const noexcept;

        /** @brief Returns the object of a started module, core or GPU. Sets `result` to UndeclaredDependency and
         *  returns null when the starting module did not list key.Name in DependsOn. Leaves `result` unchanged on
         *  success. */
        template<typename T>
        [[nodiscard]] T* Require(const ModuleKey<T>& key, BootResult& result) const;

    private:
        [[nodiscard]] void* requireObject(std::string_view name, BootResult& result) const;

        GpuStage& gpu;
        const ModuleDescriptor& module;
    };

    template<typename T>
    T* GpuStartContext::Require(const ModuleKey<T>& key, BootResult& result) const
    {
        return static_cast<T*>(requireObject(key.Name, result));
    }
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_GPU_STAGE_HPP
