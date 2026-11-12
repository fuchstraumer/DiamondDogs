#include "ModuleDescriptor.hpp"
#include <utility>

namespace bootstrap
{
    ModuleInstance::ModuleInstance() noexcept = default;

    ModuleInstance::~ModuleInstance()
    {
        Reset();
    }

    ModuleInstance::ModuleInstance(ModuleInstance&& other) noexcept :
        keyName{ std::exchange(other.keyName, std::string_view{}) },
        object{ std::exchange(other.object, nullptr) },
        destroy{ std::exchange(other.destroy, nullptr) }
    {
    }

    ModuleInstance& ModuleInstance::operator=(ModuleInstance&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            keyName = std::exchange(other.keyName, std::string_view{});
            object = std::exchange(other.object, nullptr);
            destroy = std::exchange(other.destroy, nullptr);
        }

        return *this;
    }

    std::string_view ModuleInstance::KeyName() const noexcept
    {
        return keyName;
    }

    void* ModuleInstance::Object() const noexcept
    {
        return object;
    }

    void ModuleInstance::Reset() noexcept
    {
        if (object != nullptr)
        {
            destroy(object);
        }

        keyName = std::string_view{};
        object = nullptr;
        destroy = nullptr;
    }

    void ModuleInstance::adopt(std::string_view key_name, void* owned_object, DestroyFn destroy_fn) noexcept
    {
        Reset();
        keyName = key_name;
        object = owned_object;
        destroy = destroy_fn;
    }
}
