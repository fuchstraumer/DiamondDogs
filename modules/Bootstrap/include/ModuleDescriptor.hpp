#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_MODULE_DESCRIPTOR_HPP
#define DIAMOND_DOGS_BOOTSTRAP_MODULE_DESCRIPTOR_HPP
#include <memory>
#include <span>
#include <string_view>

namespace bootstrap
{
    struct BootResult;
    class CoreStartContext;
    // defined by the linked backend (bootstrap_vulkan), so one executable links exactly one backend
    class GpuStartContext;
    class ModuleInstance;

    using CoreStartFn = BootResult (*)(const CoreStartContext& context, ModuleInstance& instance);
    using GpuStartFn = BootResult (*)(const GpuStartContext& context, ModuleInstance& instance);

    /** One shader bundle that an executable or a module needs. ContentStage looks for FileName beside the
     *  executable first, then at BuildTreePath. */
    struct BundleRequest
    {
        std::string_view FileName;
        // the build-tree path, for an executable that runs without the copy beside it
        std::string_view BuildTreePath;
        std::string_view Profile;
    };

    /** The extensions, capabilities and bundles that one module needs. Constant data, gathered into the device
     *  requirements before the device exists. */
    struct ModuleNeeds
    {
        std::span<const std::string_view> RequiredInstanceExtensions;
        std::span<const std::string_view> WantedInstanceExtensions;
        std::span<const std::string_view> RequiredDeviceExtensions;
        std::span<const std::string_view> WantedDeviceExtensions;
        // capability names in Lodestone's vocabulary (SPIR-V capability and extension names)
        std::span<const std::string_view> RequiredCapabilities;
        std::span<const std::string_view> WantedCapabilities;
        std::span<const BundleRequest> Bundles;
    };

    /** One module that a profile can select. Exactly one start function is set, and the context type of that
     *  function decides the stage where the module starts. LaunchStage refuses a descriptor with neither or both. */
    struct ModuleDescriptor
    {
        std::string_view Name;
        // modules that must start before this one. Each one is a descriptor name earlier in the linked list
        std::span<const std::string_view> DependsOn;
        ModuleNeeds Needs;
        CoreStartFn StartCore{ nullptr };
        GpuStartFn StartGpu{ nullptr };
    };

    /** @brief The typed key of a started module. Name is the descriptor name of that module. Each module defines one
     *  key, beside its descriptor, so that T always matches the object that the module owns. */
    template<typename T>
    struct ModuleKey
    {
        std::string_view Name;
    };

    /** The object that one started module owns. The start function hands its object over with Own, and the stage
     *  destroys it in Stop, in the reverse order of start. */
    class ModuleInstance
    {
    public:
        ModuleInstance() noexcept;
        ~ModuleInstance();
        ModuleInstance(const ModuleInstance&) = delete;
        ModuleInstance& operator=(const ModuleInstance&) = delete;
        ModuleInstance(ModuleInstance&& other) noexcept;
        ModuleInstance& operator=(ModuleInstance&& other) noexcept;

        /** @brief Takes ownership of the module object. The stage checks that key.Name is the descriptor name. */
        template<typename T>
        void Own(const ModuleKey<T>& key, std::unique_ptr<T> owned_object);

        /** @brief Returns the key name that Own was given, or an empty view before Own. */
        [[nodiscard]] std::string_view KeyName() const noexcept;
        [[nodiscard]] void* Object() const noexcept;
        /** @brief Destroys the object, and leaves the instance empty. */
        void Reset() noexcept;

    private:
        using DestroyFn = void (*)(void* owned_object);

        template<typename T>
        static void destroyObject(void* owned_object) noexcept;

        void adopt(std::string_view key_name, void* owned_object, DestroyFn destroy_fn) noexcept;

        std::string_view keyName;
        void* object{ nullptr };
        DestroyFn destroy{ nullptr };
    };

    template<typename T>
    void ModuleInstance::Own(const ModuleKey<T>& key, std::unique_ptr<T> owned_object)
    {
        adopt(key.Name, owned_object.release(), &ModuleInstance::destroyObject<T>);
    }

    template<typename T>
    void ModuleInstance::destroyObject(void* owned_object) noexcept
    {
        delete static_cast<T*>(owned_object);
    }
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_MODULE_DESCRIPTOR_HPP
