#pragma once
#ifndef THREADING_ATOMIC128_HPP
#define THREADING_ATOMIC128_HPP
#include <atomic>

/**
 * @brief 128-bit data structure for compare-and-swap operations
 * 
 * Provides a 16-byte aligned structure containing two 64-bit values
 * that can be atomically updated using 128-bit compare-and-swap operations.
 */
struct alignas(16) CasData128
{
    /// Default constructor - initializes both fields to zero
    CasData128() noexcept
    {
        memset(this, 0, sizeof(CasData128));
    }
    
    /**
     * @brief Constructor with initial values
     * 
     * @param _low Initial value for the low 64 bits
     * @param _high Initial value for the high 64 bits
     */
    CasData128(uint64_t _low, uint64_t _high) noexcept : low{ _low }, high{ _high } {}
    
    CasData128(const CasData128& other) noexcept : low{ other.low }, high{ other.high }
    {}

    CasData128(CasData128&& other) noexcept : low{ std::move(other.low) }, high{ std::move(other.high) }
    {}

    CasData128& operator=(const CasData128& other) noexcept
    {
        low = other.low;
        high = other.high;
        return *this;
    }

    CasData128& operator=(CasData128&& other) noexcept
    {
        if (this != &other)
        {
            low = std::move(other.low);
            high = std::move(other.high);
        }
        return *this;
    }
    
    /// Equality comparison
    constexpr bool operator==(const CasData128& other) const noexcept { return low == other.low && high == other.high; }
    /// Inequality comparison
    constexpr bool operator!=(const CasData128& other) const noexcept { return !(*this == other); }
    
    uint64_t low;   ///< Lower 64 bits of the 128-bit value
    uint64_t high;  ///< Upper 64 bits of the 128-bit value
};

#ifdef _MSC_VER

/**
 * @brief Atomic 128-bit data type for lock-free operations
 * 
 * Provides atomic operations on 128-bit data using platform-specific intrinsics.
 * Currently implemented for MSVC/x64 using cmpxchg16b instruction.
 * 
 * @note Always lock-free on supported platforms
 */
struct alignas(16) atomic128
{
    /// Default constructor
    atomic128() noexcept = default;
    /// Destructor
    ~atomic128() noexcept = default;
    /// Copy constructor deleted
    atomic128(const atomic128&) = delete;
    /// Copy assignment deleted
    atomic128& operator=(const atomic128&) = delete;

    /**
     * @brief Constructor from 128-bit value
     * 
     * @param value Initial value to store
     */
    atomic128(const CasData128 value) noexcept : data{ value } {}
    
    /**
     * @brief Constructor from two 64-bit values
     * 
     * @param lower Lower 64 bits
     * @param upper Upper 64 bits
     */
    atomic128(uint64_t lower, uint64_t upper) noexcept : data{ lower, upper } {}

    /// Move constructor
    atomic128(atomic128&& other) noexcept;
    /// Move assignment operator
    atomic128& operator=(atomic128&& other) noexcept;

    /**
     * @brief Atomically load the current value
     * 
     * @return Current 128-bit value
     */
    [[nodiscard]] CasData128 load() const noexcept;

    /**
     * @brief Atomically load the current value with specified memory ordering
     * 
     * @param order Memory ordering constraint
     * @return Current 128-bit value
     * @note Memory ordering parameter is ignored on x64 with current intrinsics
     */
    [[nodiscard]] CasData128 load(const std::memory_order order) const noexcept;

    /**
     * @brief Atomically exchange the stored value
     * 
     * @param value New value to store
     * @param order Memory ordering constraint
     * @return Previous value that was stored
     */
    CasData128 exchange(const CasData128 value, const std::memory_order order) noexcept;

    /**
     * @brief Atomically exchange the stored value with sequential consistency
     * 
     * @param value New value to store
     * @return Previous value that was stored
     */
    CasData128 exchange(const CasData128 value) noexcept;

    /**
     * @brief Strong compare-and-swap operation
     * 
     * @param expected Reference to expected value (updated with actual value if CAS fails)
     * @param desired New value to store if comparison succeeds
     * @param order Memory ordering constraint (ignored on x64)
     * @return True if exchange was successful, false otherwise
     */
    bool compare_exchange_strong(CasData128& expected, CasData128 desired,
        const std::memory_order order = std::memory_order_seq_cst) noexcept;

    /**
     * @brief Weak compare-and-swap operation (fallback to strong on x64)
     * 
     * @param expected Reference to expected value
     * @param desired New value to store if comparison succeeds  
     * @return True if exchange was successful, false otherwise
     * @note No weak CAS intrinsics available, falls back to strong CAS
     */
    bool compare_exchange_weak(CasData128& expected, CasData128 desired) noexcept;

    /**
     * @brief Atomically store a new value
     * 
     * @param value Value to store
     */
    void store(const CasData128 value) noexcept;

    /**
     * @brief Atomically store a new value with specified memory ordering
     * 
     * @param value Value to store
     * @param order Memory ordering constraint
     */
    void store(const CasData128 value, const std::memory_order order) noexcept;

    /// Indicates that atomic operations are always lock-free
    constexpr static bool is_always_lock_free = true;

private:

    mutable CasData128 data;
};

#else

using atomic128 = std::atomic<CasData128>;
static_assert(std::atomic<CasData128>::is_always_lock_free, "128bit cmpexchg not available on current platform.");

#endif

#endif //!THREADING_ATOMIC128_HPP