#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_CORE_STAGE_HPP
#define DIAMOND_DOGS_BOOTSTRAP_CORE_STAGE_HPP
#include "ModuleDescriptor.hpp"
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace foundation
{
    class Scheduler;
}

namespace bootstrap
{
    struct BootResult;
    class LaunchStage;

    /** @brief The stage that every executable starts, the cooker included.
     *
     *  It constructs the async Scheduler on the calling thread, which then owns it, and starts every
     *  selected module that has StartCore, in list order. */
    class CoreStage
    {
    public:
        CoreStage() noexcept;
        /** @brief Calls Stop. */
        ~CoreStage();
        // later stages and started modules store a reference to this stage
        CoreStage(const CoreStage&) = delete;
        CoreStage& operator=(const CoreStage&) = delete;
        CoreStage(CoreStage&&) = delete;
        CoreStage& operator=(CoreStage&&) = delete;

        /** @brief Constructs the Scheduler, then starts the core modules. When a module fails, stops what started
         *  and returns that failure. `launch` must outlive this stage. */
        [[nodiscard]] BootResult Start(const LaunchStage& launch);
        /** @brief Destroys the started modules in the reverse order of start, then the Scheduler. */
        void Stop() noexcept;

        [[nodiscard]] bool Started() const noexcept;
        [[nodiscard]] foundation::Scheduler& Scheduler() noexcept;
        [[nodiscard]] const LaunchStage& Launch() const noexcept;
        /** @brief Returns the started modules, in start order. */
        [[nodiscard]] std::span<const ModuleInstance> Modules() const noexcept;
        /** @brief Returns the object of the started module with this descriptor name, or null. */
        [[nodiscard]] void* FindModuleObject(std::string_view name) const noexcept;

    private:
        [[nodiscard]] BootResult startModule(const ModuleDescriptor& descriptor);

        std::unique_ptr<foundation::Scheduler> scheduler;
        const LaunchStage* launch{ nullptr };
        std::vector<ModuleInstance> modules;
    };

    /** What a core module start function can reach: the launch stage, the core stage, and the modules that it
     *  declared in DependsOn. The context exists only while CoreStage::Start runs. */
    class CoreStartContext
    {
    public:
        CoreStartContext(const LaunchStage& _launch, CoreStage& _core, const ModuleDescriptor& _module) noexcept;

        [[nodiscard]] const LaunchStage& Launch() const noexcept;
        [[nodiscard]] CoreStage& Core() const noexcept;
        /** @brief Returns the descriptor of the module that is starting. */
        [[nodiscard]] const ModuleDescriptor& Module() const noexcept;

        /** @brief Returns the object of a started module. Sets `result` to UndeclaredDependency and returns null when
         *  the starting module did not list key.Name in DependsOn, so that DependsOn always lists the real
         *  dependencies. Leaves `result` unchanged on success. */
        template<typename T>
        [[nodiscard]] T* Require(const ModuleKey<T>& key, BootResult& result) const;

    private:
        [[nodiscard]] void* requireObject(std::string_view name, BootResult& result) const;

        const LaunchStage& launch;
        CoreStage& core;
        const ModuleDescriptor& module;
    };

    template<typename T>
    T* CoreStartContext::Require(const ModuleKey<T>& key, BootResult& result) const
    {
        return static_cast<T*>(requireObject(key.Name, result));
    }
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_CORE_STAGE_HPP
