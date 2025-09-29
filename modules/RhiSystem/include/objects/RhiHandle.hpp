#pragma once
#ifndef DIAMOND_DOGS_RHI_HANDLE_HPP
#define DIAMOND_DOGS_RHI_HANDLE_HPP
#include "RhiDefines.hpp"
#include <cstdint>

namespace rhi
{

    /** @brief Opaque handle to a graphics API object. This lets us avoid exposing graphics API headers in our code, but 
     *  still have type safety and some level of debugging/ease-of-use help thanks to the tags we apply.
     *  Each type is a distinct type instantiated with an empty `struct TagType{};` so that it's still clear what the handle represents in code.
     *  e.g, a Semaphore handle is `RhiHandle<struct SemaphoreTag>`, and in code just becomes `Semaphore` quite nicely.
     */
    template<typename T>
    class RhiHandle
    {
        uint64_t handle;
    public:
        constexpr RhiHandle() noexcept : handle{ 0u } {}
        explicit constexpr RhiHandle(uint64_t _handle) noexcept : handle{ _handle } {}
        
        // Handles can be copied, unlike the actual objects they represent.
        // This allows multiple people to use the same handle, but still makes sure they don't accidentally use it after it's been destroyed
        // or that it's not destroyed at all.

        constexpr RhiHandle(const RhiHandle& other) noexcept : handle{ other.handle } {}
        constexpr RhiHandle& operator=(const RhiHandle& other) noexcept
        {
            if (this != &other)
            {
                handle = other.handle;
            }
            return *this;
        }

        constexpr RhiHandle(RhiHandle&& other) noexcept : handle{ other.handle }
        {
            other.handle = 0u;
        }

        constexpr RhiHandle& operator=(RhiHandle&& other) noexcept
        {
            if (this != &other)
            {
                handle = other.handle;
                other.handle = 0u;
            }
            return *this;
        }

        constexpr bool operator==(const RhiHandle& other) const noexcept
        {
            return handle == other.handle;
        }

        constexpr bool operator!=(const RhiHandle& other) const noexcept
        {
            return handle != other.handle;
        }

        constexpr uint64_t Get() const noexcept
        {
            return handle;
        }

        template<typename T>
        constexpr void Set(T _handle) noexcept
        {
            handle = reinterpret_cast<uint64_t>(_handle);
        }

        /** @brief Use the `As()` function to cast the type to the appropriate concrete RHI handle type */
        template<typename T>
        constexpr T As() const noexcept
        {
            static_assert(sizeof(T) == sizeof(uint64_t), "Size of destination type in As<T>() must be 64 bits");
            return reinterpret_cast<T>(handle);
        }

        constexpr explicit operator bool() const noexcept
        {
            return handle != 0u;
        }

        constexpr bool IsValid() const noexcept
        {
            return handle != 0u;
        }

    };

    // Define some of our most common and "core" handle types here
    // Especially beneficial with things like Device, which is used everywhere to init objects when all we really
    // need is the handle for 90% of the operations
    namespace detail
    {
        // as a general rule, we place tags in a detail namespace to avoid polluting the rhi namespace
        // all of these tag structs
        struct InstanceTag {};
        struct PhysicalDeviceTag {};
        struct DeviceTag {};
        struct QueueTag {};
        struct BinarySemaphoreTag {};
        struct TimelineSemaphoreTag {};
        struct FenceTag {};
        struct CommandBufferTag {};
        struct BufferTag {};
        struct BufferViewTag {};
        struct ImageTag {};
        struct ImageViewTag {};
        struct GraphicsPipelineTag {};
        struct ComputePipelineTag {};
        struct DescriptorSetTag {};
        struct DescriptorSetLayoutTag {};
        struct DescriptorPoolTag {};
        struct DescriptorUpdateTemplateTag {};
        struct ShaderObjectTag {};
    }

    using InstanceHandle = RhiHandle<detail::InstanceTag>;
    using PhysicalDeviceHandle = RhiHandle<detail::PhysicalDeviceTag>;
    using DeviceHandle = RhiHandle<detail::DeviceTag>;
    using QueueHandle = RhiHandle<detail::QueueTag>;
    using BinarySemaphoreHandle = RhiHandle<detail::BinarySemaphoreTag>;
    using TimelineSemaphoreHandle = RhiHandle<detail::TimelineSemaphoreTag>;
    using FenceHandle = RhiHandle<detail::FenceTag>;
    using CommandBufferHandle = RhiHandle<detail::CommandBufferTag>;
    using BufferHandle = RhiHandle<detail::BufferTag>;
    using BufferViewHandle = RhiHandle<detail::BufferViewTag>;
    using ImageHandle = RhiHandle<detail::ImageTag>;
    using ImageViewHandle = RhiHandle<detail::ImageViewTag>;
    using GraphicsPipelineHandle = RhiHandle<detail::GraphicsPipelineTag>;
    using ComputePipelineHandle = RhiHandle<detail::ComputePipelineTag>;
    using DescriptorSetHandle = RhiHandle<detail::DescriptorSetTag>;
    using DescriptorSetLayoutHandle = RhiHandle<detail::DescriptorSetLayoutTag>;
    using DescriptorPoolHandle = RhiHandle<detail::DescriptorPoolTag>;
    using DescriptorUpdateTemplateHandle = RhiHandle<detail::DescriptorUpdateTemplateTag>;
    using ShaderObjectHandle = RhiHandle<detail::ShaderObjectTag>;

}

#endif // !DIAMOND_DOGS_RHI_HANDLE_HPP
