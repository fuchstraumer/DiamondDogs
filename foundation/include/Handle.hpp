#pragma once
#ifndef FOUNDATION_FABRIC_OBJECT_HANDLES_HPP
#define FOUNDATION_FABRIC_OBJECT_HANDLES_HPP
#include <type_traits>
#include <cstdint>
#include <exception>

namespace foundation
{
    /**
     * @brief Memory safety modes for object handles
     * 
     * Different modes provide varying levels of safety and performance tradeoffs.
     */
    enum class memory_mode : uint8_t
    {
        /// Sentinel value for invalid usage
        invalid = 0,
        /**
         * @brief Fastest mode with minimal safety guarantees
         * 
         * Provides performance similar to raw pointers with no safety checks.
         * @note Removes ability for replays, safety checks, and memory compacting
         */
       fast = 1,
       /**
        * @brief Minimal safety mode with exception-based error handling
        * 
        * Provides basic safety by throwing exceptions instead of causing crashes.
        */
      minimal_safety = 2,
      /**
       * @brief Full safety mode with memory relocation and compacting
       * 
       * Includes all safety features plus memory defragmentation capabilities.
       * @note Especially useful for long-running applications like dedicated servers
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

    /**
     * @brief Exception type for fabric handle operations
     * 
     * Thrown when handle operations fail in safety modes.
     */
    struct fabric_exception : public std::exception
    {

        /**
         * @brief Types of fabric exceptions that can be thrown
         */
        enum class type : uint8_t
        {
            Invalid,                              ///< Invalid exception type
            MinSafetyPtrIdMismatch,              ///< ID mismatch in minimal safety mode
            FailureToRemapInvalidPointer,        ///< Failed to remap pointer in full safety mode
            PointerIdMismatchAndMissingRelocFn   ///< ID mismatch with no relocation function
        };

        /**
         * @brief Construct fabric exception with specific type
         * 
         * @param exception_type The type of exception being thrown
         */
        fabric_exception(type exception_type) : std::exception(), exceptionType(exception_type) {}

        /**
         * @brief Get human-readable description of the exception
         * 
         * @return C-string describing the exception
         */
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

        type exceptionType{ type::Invalid };  ///< The specific exception type
    };

    /**
     * @brief Type-safe handle for managing object lifetime and access
     * 
     * Provides different safety guarantees based on the memory_mode template parameter.
     * Handles can safely access objects even when memory is relocated or compacted.
     * 
     * @tparam T The type of object being managed
     * @tparam MemoryMode The safety mode for this handle type
     */
    template<typename T, memory_mode MemoryMode>
    struct object_handle
    {
    public:

        /// Function type for relocating objects when memory is moved
        using relocation_lookup_fn_t = void*(*)(uint64_t ID, void* allocator);

        /**
         * @brief Set the relocation function for this handle type
         * 
         * @param fn Function to call when object needs to be relocated
         * @note Required for full_safety mode when memory compaction occurs
         */
        static void SetRelocationFunction(relocation_lookup_fn_t fn);

        /**
         * @brief Get mutable pointer to the managed object
         * 
         * @return Mutable pointer to object
         * @throws fabric_exception If object is invalid and safety mode enables exceptions
         * @note Behavior varies based on memory_mode template parameter
         */
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

        /**
         * @brief Safely attempt to get mutable pointer without throwing exceptions
         * 
         * @return Mutable pointer to object, or nullptr if invalid
         * @note Returns nullptr instead of throwing exceptions on failure
         */
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

        /**
         * @brief Get const pointer to the managed object
         * 
         * @return Const pointer to object
         * @throws fabric_exception If object is invalid and safety mode enables exceptions
         */
        const T* get() const
        {
            return get_mutable();
        }

        /**
         * @brief Safely attempt to get const pointer without throwing exceptions
         * 
         * @return Const pointer to object, or nullptr if invalid
         */
        const T* try_and_get() const
        {
            return try_and_get_mutable();
        }

        /**
         * @brief Check if the handle points to a valid object
         * 
         * @return True if handle is valid, false otherwise
         * @note Non-blocking operation that never throws exceptions
         */
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
