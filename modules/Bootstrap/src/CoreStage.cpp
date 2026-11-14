#include "CoreStage.hpp"
#include "BootResult.hpp"
#include "BootTimings.hpp"
#include "LaunchStage.hpp"
#include "async/Scheduler.hpp"
#include <algorithm>
#include <ranges>
#include <string>

namespace
{
    [[nodiscard]] bootstrap::BootResult CoreFailure(bootstrap::BootResultCode code,
                                                    std::string_view module,
                                                    std::string_view subject,
                                                    std::string_view source);
}

namespace bootstrap
{
    CoreStage::CoreStage() noexcept = default;

    CoreStage::~CoreStage()
    {
        Stop();
    }

    BootResult CoreStage::Start(const LaunchStage& _launch)
    {
        Stop();

        const ScopedBootSpan stageSpan(_launch.Timings(), Stage::Core, BootSpanKind::Stage, ToString(Stage::Core));

        launch = &_launch;
        scheduler = std::make_unique<foundation::Scheduler>();

        for (const ModuleDescriptor* descriptor : _launch.Selected())
        {
            if (descriptor->StartCore == nullptr)
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

        return SuccessIn(Stage::Core);
    }

    void CoreStage::Stop() noexcept
    {
        while (!modules.empty())
        {
            modules.back().Reset();
            modules.pop_back();
        }

        scheduler.reset();
        launch = nullptr;
    }

    bool CoreStage::Started() const noexcept
    {
        return scheduler != nullptr;
    }

    foundation::Scheduler& CoreStage::Scheduler() noexcept
    {
        return *scheduler;
    }

    const LaunchStage& CoreStage::Launch() const noexcept
    {
        return *launch;
    }

    std::span<const ModuleInstance> CoreStage::Modules() const noexcept
    {
        return modules;
    }

    void* CoreStage::FindModuleObject(std::string_view name) const noexcept
    {
        const auto found = std::ranges::find(modules, name, &ModuleInstance::KeyName);

        if (found == modules.end())
        {
            return nullptr;
        }

        return found->Object();
    }

    BootResult CoreStage::startModule(const ModuleDescriptor& descriptor)
    {
        const ScopedBootSpan moduleSpan(launch->Timings(), Stage::Core, BootSpanKind::Module, descriptor.Name);
        const CoreStartContext context(*launch, *this, descriptor);
        ModuleInstance instance;
        BootResult started = descriptor.StartCore(context, instance);

        if (!started.Succeeded())
        {
            // the start function knows what failed, and the stage knows where
            started.FailedStage = Stage::Core;
            started.Module = descriptor.Name;
            return started;
        }

        if (instance.Object() == nullptr || instance.KeyName() != descriptor.Name)
        {
            return CoreFailure(BootResultCode::ModuleInstanceInvalid,
                               descriptor.Name,
                               instance.KeyName(),
                               "the start function must call Own with the key of its own descriptor");
        }

        modules.push_back(std::move(instance));
        return started;
    }

    CoreStartContext::CoreStartContext(const LaunchStage& _launch,
                                       CoreStage& _core,
                                       const ModuleDescriptor& _module) noexcept :
        launch{ _launch },
        core{ _core },
        module{ _module }
    {
    }

    const LaunchStage& CoreStartContext::Launch() const noexcept
    {
        return launch;
    }

    CoreStage& CoreStartContext::Core() const noexcept
    {
        return core;
    }

    const ModuleDescriptor& CoreStartContext::Module() const noexcept
    {
        return module;
    }

    void* CoreStartContext::requireObject(std::string_view name, BootResult& result) const
    {
        if (!std::ranges::contains(module.DependsOn, name))
        {
            result = CoreFailure(BootResultCode::UndeclaredDependency, module.Name, name, "DependsOn");
            return nullptr;
        }

        void* object = core.FindModuleObject(name);

        if (object == nullptr)
        {
            result = CoreFailure(BootResultCode::ModuleStartFailed, module.Name, name, "DependsOn");
            return nullptr;
        }

        return object;
    }
}

namespace
{
    bootstrap::BootResult CoreFailure(bootstrap::BootResultCode code,
                                      std::string_view module,
                                      std::string_view subject,
                                      std::string_view source)
    {
        return bootstrap::BootResult
        {
            .Code = code,
            .FailedStage = bootstrap::Stage::Core,
            .Module = module,
            .Subject = std::string{ subject },
            .Source = std::string{ source },
        };
    }
}
