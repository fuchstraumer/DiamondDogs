#pragma once
#ifndef FOUNDATION_FABRIC_OBJECT_HANDLES_HPP
#define FOUNDATION_FABRIC_OBJECT_HANDLES_HPP
#include <type_traits>
#include <cstdint>
#include <exception>

namespace foundation
{
    enum class memory_mode : uint8_t
    {
        // Just a sentinel value for prohibiting invalid usage/behavior
        invalid = 0,
        /*
            In "Fast" mode, a strong_handle is akin to a unique_ptr, and weak_handles
            + raw_handles are effectively just raw pointers. This will provide the 
            fastest behavior, but removes the ability for replays, safety checks,
            or the reallocation/defrag/compacting functionality.
        */
       fast = 1,
       /*
            In MinimalSafety mode we mostly benefit from some mild safety. This mode does
            cause a performance decline, but can be worth it for allowing large
            amounts of easier scaling to multiple cores. The main change is that
            dangling pointers don't cause segfaults or memory corruption, but
            instead throw exceptions that can be caught by user systems.
       */
      minimal_safety = 2,
      /*
            In FullSafety mode, we get the above safety with additional memory relocation
            and compacting routines, which can be especially useful for games that have
            long runtimes / uptimes (e.g, dedicated servers benefit from such things, potentially).
            Defragmenting of memory can also be performed too, performed by memory
            "category" set by the user. This way memory categories can be moved closer together, 
            then using supplied comparator functions and type hashes to further sort items 
            into new in-memory locations.
      */
     full_safety = 3
    };

    class Container;

    namespace detail
    {

        struct fast_mode_metadata
        {
            void* datumPtr;
        };

        struct minimal_safety_mode_metadata
        {
            uint64_t ID{ 0u };
            void* datumPtr;
        };

        struct full_safety_mode_metadata
        {
            uint64_t ID{ 0u };
            void* datumPtr;
            void* parentAlloc;
            uint64_t padding; // round up to 256 explicitly
        };

        constexpr uint64_t INVALID_HANDLE = std::numeric_limits<uint64_t>::max();

    }

    struct fabric_exception : public std::exception
    {

        enum class type : uint8_t
        {
            Invalid,
            MinSafetyPtrIdMismatch,
            FailureToRemapInvalidPointer,
            PointerIdMismatchAndMissingRelocFn
        };

        fabric_exception(type exception_type) : std::exception(), exceptionType(exception_type) {}

        const char* what() const
        {
            switch (exceptionType)
            {
            case type::MinSafetyPtrIdMismatch:
                return "object_handle's stored ID and ID from pointer didn't match: object invalid!";
            case type::FailureToRemapInvalidPointer:
                return "unable to map pointer ID to new memory location!";
            case type::PointerIdMismatchAndMissingRelocFn:
                return "object_handle's stored ID and ID from pointer didn't match, and there was no relocation function for this type!";
            default:
                return "Unhandled new exception type in fabric object_handle code!";
            };
        }

        type exceptionType{ type::Invalid };
    };

    template<typename T, memory_mode MemoryMode>
    struct object_handle
    {
    public:

        using relocation_lookup_fn_t = void*(*)(uint64_t ID, void* allocator);

        static void SetRelocationFunction(relocation_lookup_fn_t fn);

        // Returns mutable pointer to object, and will throw exceptions as appropriate based on
        // safety mode.
        T* get_mutable()
        {
            using namespace fabric::detail;
            if constexpr (MemoryMode == memory_mode::fast)
            {
                fast_mode_metadata* metadata = reinterpret_cast<fast_mode_metadata*>(datumPtr);
                return reinterpret_cast<T*>(metadata->datumPtr);
            }
            else if constexpr (MemoryMode == memory_mode::minimal_safety)
            {
                minimal_safety_mode_metadata* metadata = reinterpret_cast<minimal_safety_mode_metadata*>(datumPtr);
                // even if this data is garbage or invalid, merely casting it to an ID should NOT crash
                const uint64_t ptrID = get_datum_value(metadata->datumPtr);
                if (ptrID == metadata->ID)
                {
                    return get_object_address(metadata->datumPtr);
                }
                else
                {
                    throw fabric_exception(fabric_exception::type::MinSafetyPtrIdMismatch);
                }
            }
            else if constexpr (MemoryMode == memory_mode::full_safety)
            {
                full_safety_mode_metadata* metadata = reinterpret_cast<full_safety_mode_metadata*>(datumPtr);
                const uint64_t ptrID = get_datum_value(metadata->datumPtr);
                if (ptrID == metadata->ID)
                {
                    return get_object_address(metadata->datumPtr);
                }
                else if (relocationFn != nullptr)
                {
                    void* relocatedAddress = relocationFn(ptrID, metadata->parentAlloc);
                    if (relocatedAddress != nullptr)
                    {
                        resolve_address(relocatedAddress);
                        return get_object_address(relocatedAddress);
                    }
                    else
                    {
                        throw fabric_exception(fabric_exception::type::FailureToRemapInvalidPointer);
                    }
                }
                else
                {
                    throw fabric_exception(fabric_exception::type::PointerIdMismatchAndMissingRelocFn);
                }
            }
        }

        // In fast mode, the only difference in this function is that we'll check validity and return null if not valid. In safety modes, 
        // we return nullptr instead of throwing an exception in our failure paths
        T* try_and_get_mutable()
        {
            using namespace::fabric::detail;
            if constexpr (MemoryMode == memory_mode::fast)
            {
                fast_mode_metadata* metadata = reinterpret_cast<fast_mode_metadata*>(datumPtr);
                const uint64_t datumValue = get_datum_value(metadata->datumPtr);
                return datumValue != INVALID_HANDLE ? get_object_address(metadata->datumPtr) : nullptr;
            }
            else if constexpr (MemoryMode == memory_mode::minimal_safety)
            {
                minimal_safety_mode_metadata* metadata = reinterpret_cast<minimal_safety_mode_metadata*>(datumPtr);
                const uint64_t ptrID = get_datum_value(metadata->datumPtr);
                if (ptrID == metadata->ID)
                {
                    return get_object_address(metadata->datumPtr);
                }
                else
                {
                    return nullptr;
                }
            }
            else if constexpr (MemoryMode == memory_mode::full_safety)
            {
                full_safety_mode_metadata* metadata = reinterpret_cast<full_safety_mode_metadata*>(datumPtr);
                const uint64_t ptrID = get_datum_value(metadata->datumPtr);
                if (ptrID == metadata->ID)
                {
                    return get_object_address(metadata->datumPtr);
                }
                else if (relocationFn != nullptr)
                {
                    // use the pointer ID since it's more likely to be what's in the lookup map
                    void* relocatedAddress = relocationFn(ptrID, metadata->parentAlloc);
                    if (relocatedAddress != nullptr)
                    {
                        resolve_address(relocatedAddress);
                        return get_object_address(relocatedAddress);
                    }
                    else
                    {
                        return nullptr;
                    }
                }
                else
                {
                    return nullptr;
                }
            }
        }

        const T* get() const
        {
            return get_mutable();
        }

        const T* try_and_get() const
        {
            return try_and_get_mutable();
        }

        bool valid() const noexcept
        {
            using namespace fabric::detail;
            if constexpr (MemoryMode == memory_mode::fast)
            {
                const fast_mode_metadata* metadata = reinterpret_cast<fast_mode_metadata*>(datumPtr);
                const uint64_t handleValue = get_datum_value(metadata->datumPtr);
                return handleValue != INVALID_HANDLE;
            }
            else if constexpr (MemoryMode == memory_mode::minimal_safety)
            {
                const minimal_safety_mode_metadata* metadata = reinterpret_cast<minimal_safety_mode_metadata*>(datumPtr);
                const uint64_t ptrID = *reinterpret_cast<const uint64_t*>(metadata->datumPtr);
                return ptrID == metadata->ID;
            }
            else if constexpr (MemoryMode == memory_mode::full_safety)
            {
                const full_safety_mode_metadata* metadata = reinterpret_cast<full_safety_mode_metadata*>(datumPtr);
                const uint64_t ptrID = *reinterpret_cast<const uint64_t*>(metadata->datumPtr);
                if (ptrID == metadata->ID)
                {
                    return true;                    
                }
                else if (relocationFn != nullptr)
                {
                    void* relocatedAddress = relocationFn(ptrID, metadata->parentAlloc);
                    // (comma just makes sure we resolve the address before returning true, as it's now valid for sure!)
                    relocatedAddress != nullptr ? resolve_address(relocatedAddress), true : false;
                }
                else
                {
                    return false;
                }
            }
        }

    private:

        constexpr static uint64_t get_datum_value(void* datum) noexcept
        {
            return *reinterpret_cast<const uint64_t*>(datum);
        }

        // Allocation models for these handles MUST use the format of (uint64_t, object) when allocating
        // memory for our objects. safety_mode is per handle type, but the data storage for these objects
        // needs to still store both the handle and the pointer. this way
        constexpr static T* get_object_address(void* handleAddr) noexcept
        {
            return reinterpret_cast<T*>(handleAddr + sizeof(uint64_t));
        }

        void resolve_address(void* relocated_address)
        {
            metadata->ID = *reinterpret_cast<const uint64_t*>(relocatedAddress);
            metadata->datumPtr = relocatedAddress;
        }

        // this will be unique per instantiation of this class, so each type has it's own function
        static relocation_lookup_fn_t relocationFn{ nullptr };
        // Depending on mode, this points to various types. Marked mutable so const functions can just
        // call non-consts that might do relocation in full_safety mode
        mutable void* datumPtr;
    };

}

#endif
