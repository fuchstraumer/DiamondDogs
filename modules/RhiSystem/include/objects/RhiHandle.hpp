#pragma once
#ifndef DIAMOND_DOGS_RHI_HANDLE_HPP
#define DIAMOND_DOGS_RHI_HANDLE_HPP
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
        // no dtor: wrappers will implement the proper destruction process for the handle, based on API
        // Handles cannot be copied, only moved
        constexpr RhiHandle(const RhiHandle&) noexcept = delete;
        constexpr RhiHandle& operator=(const RhiHandle&) noexcept = delete;

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

        /** @brief Use the `As()` function to cast the type to the appropriate concrete RHI handle type */
        template<typename T>
        explicit constexpr As() const noexcept
        {
            return reinterpret_cast<T>(handle);
        }

        constexpr explicit operator bool() const noexcept
        {
            return handle != 0u;
        }

        constexpr explicit bool IsValid() const noexcept
        {
            return handle != 0u;
        }

    };

}

#endif // !DIAMOND_DOGS_RHI_HANDLE_HPP
