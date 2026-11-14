#include "GpuStage.hpp"
#include "BootResult.hpp"
#include "BootTimings.hpp"
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "DeviceStage.hpp"
#include "LaunchStage.hpp"
#include "ModuleDescriptor.hpp"
#include "PlatformSystem.hpp"
#include "ResourceContext.hpp"
#include "RhiFlags.hpp"
#include "RhiSystem.hpp"
#include "WindowStage.hpp"
#include <algorithm>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    [[nodiscard]] bootstrap::BootResult GpuFailure(bootstrap::BootResultCode code,
                                                   std::string_view module,
                                                   std::string_view subject,
                                                   std::string_view detail);
}

namespace bootstrap
{
    GpuStage::GpuStage() noexcept = default;

    GpuStage::~GpuStage()
    {
        Stop();
    }

    BootResult GpuStage::Start(DeviceStage& _device, WindowStage& _window)
    {
        return start(_device, std::addressof(_window));
    }

    BootResult GpuStage::StartHeadless(DeviceStage& _device)
    {
        return start(_device, nullptr);
    }

    void GpuStage::Stop() noexcept
    {
        while (!modules.empty())
        {
            modules.back().Reset();
            modules.pop_back();
        }

        if (window != nullptr)
        {
            window->Platform().DestroySwapchain();
        }

        resources.reset();
        window = nullptr;
        device = nullptr;
    }

    bool GpuStage::Started() const noexcept
    {
        return resources != nullptr;
    }

    DeviceStage& GpuStage::Device() const noexcept
    {
        return *device;
    }

    bool GpuStage::HasWindow() const noexcept
    {
        return window != nullptr;
    }

    WindowStage& GpuStage::Window() const noexcept
    {
        return *window;
    }

    ResourceContext& GpuStage::Resources() const noexcept
    {
        return *resources;
    }

    std::span<const ModuleInstance> GpuStage::Modules() const noexcept
    {
        return modules;
    }

    void* GpuStage::FindModuleObject(std::string_view name) const noexcept
    {
        const auto found = std::ranges::find(modules, name, &ModuleInstance::KeyName);

        if (found == modules.end())
        {
            return nullptr;
        }

        return found->Object();
    }

    BootResult GpuStage::start(DeviceStage& _device, WindowStage* window_stage)
    {
        Stop();

        const LaunchStage& launch = _device.Content().Core().Launch();
        BootTimings& timings = launch.Timings();
        const ScopedBootSpan stageSpan(timings, Stage::Gpu, BootSpanKind::Stage, ToString(Stage::Gpu));
        rhi::RhiSystem& rhiSystem = _device.Rhi();

        device = &_device;
        window = window_stage;
        resources = std::make_unique<ResourceContext>();

        const ResourceContextCreateInfo resourceInfo
        {
            .Instance = rhiSystem.GetInstance(),
            .Device = rhiSystem.GetDevice(),
            .VulkanApiVersion = rhiSystem.GetVulkanApiVersion(),
            .ValidationEnabled = _device.Validation() != rhi::ValidationLayers::None,
        };
        ResourceContextError initialized = ResourceContextError::Invalid;

        {
            const ScopedBootSpan resourcesSpan(timings, Stage::Gpu, BootSpanKind::Step, "resource context");

            initialized = resources->Initialize(resourceInfo);
        }

        if (initialized != ResourceContextError::Success)
        {
            Stop();
            return GpuFailure(BootResultCode::ResourceContextFailed, "", ToString(initialized), "");
        }

        if (window != nullptr)
        {
            const ScopedBootSpan swapchainSpan(timings, Stage::Gpu, BootSpanKind::Step, "swapchain");

            try
            {
                window->Platform().CreateDefaultSwapchain(rhiSystem.GetInstance(), rhiSystem.GetDevice());
            }
            catch (const std::exception& error)
            {
                Stop();
                return GpuFailure(BootResultCode::SwapchainCreateFailed, "", "", error.what());
            }
        }

        for (const ModuleDescriptor* descriptor : launch.Selected())
        {
            if (descriptor->StartGpu == nullptr)
            {
                continue;
            }

            BootResult started = startModule(*descriptor);

            if (!started.Succeeded())
            {
                Stop();
                return started;
            }
        }

        return SuccessIn(Stage::Gpu);
    }

    BootResult GpuStage::startModule(const ModuleDescriptor& descriptor)
    {
        const ScopedBootSpan moduleSpan(device->Content().Core().Launch().Timings(),
                                        Stage::Gpu,
                                        BootSpanKind::Module,
                                        descriptor.Name);
        const GpuStartContext context(*this, descriptor);
        ModuleInstance instance;
        BootResult started = descriptor.StartGpu(context, instance);

        if (!started.Succeeded())
        {
            // the start function knows what failed, and the stage knows where
            started.FailedStage = Stage::Gpu;
            started.Module = descriptor.Name;
            return started;
        }

        if (instance.Object() == nullptr || instance.KeyName() != descriptor.Name)
        {
            return GpuFailure(BootResultCode::ModuleInstanceInvalid,
                              descriptor.Name,
                              instance.KeyName(),
                              "the start function must call Own with the key of its own descriptor");
        }

        modules.push_back(std::move(instance));
        return started;
    }

    GpuStartContext::GpuStartContext(GpuStage& _gpu, const ModuleDescriptor& _module) noexcept :
        gpu{ _gpu },
        module{ _module }
    {
    }

    const LaunchStage& GpuStartContext::Launch() const noexcept
    {
        return gpu.Device().Content().Core().Launch();
    }

    const CoreStage& GpuStartContext::Core() const noexcept
    {
        return gpu.Device().Content().Core();
    }

    const ContentStage& GpuStartContext::Content() const noexcept
    {
        return gpu.Device().Content();
    }

    DeviceStage& GpuStartContext::Device() const noexcept
    {
        return gpu.Device();
    }

    GpuStage& GpuStartContext::Gpu() const noexcept
    {
        return gpu;
    }

    const ModuleDescriptor& GpuStartContext::Module() const noexcept
    {
        return module;
    }

    void* GpuStartContext::requireObject(std::string_view name, BootResult& result) const
    {
        if (!std::ranges::contains(module.DependsOn, name))
        {
            result = GpuFailure(BootResultCode::UndeclaredDependency, module.Name, name, "");
            result.Source = "DependsOn";
            return nullptr;
        }

        // LaunchStage checked that every dependency starts earlier: in this stage, or in the core stage
        void* gpuObject = gpu.FindModuleObject(name);

        if (gpuObject != nullptr)
        {
            return gpuObject;
        }

        void* coreObject = Core().FindModuleObject(name);

        if (coreObject == nullptr)
        {
            result = GpuFailure(BootResultCode::ModuleStartFailed, module.Name, name, "");
            result.Source = "DependsOn";
            return nullptr;
        }

        return coreObject;
    }
}

namespace
{
    bootstrap::BootResult GpuFailure(bootstrap::BootResultCode code,
                                     std::string_view module,
                                     std::string_view subject,
                                     std::string_view detail)
    {
        return bootstrap::BootResult
        {
            .Code = code,
            .FailedStage = bootstrap::Stage::Gpu,
            .Module = module,
            .Subject = std::string{ subject },
            .Detail = std::string{ detail },
        };
    }
}
