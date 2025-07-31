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
struct alignas(16) cas_data128_t
{
    /// Default constructor - initializes both fields to zero
    cas_data128_t() noexcept : low{ 0u }, high{ 0u }
    {
        memset(this, 0, sizeof(cas_data128_t));
    }
    
    /**
     * @brief Constructor with initial values
     * 
     * @param _low Initial value for the low 64 bits
     * @param _high Initial value for the high 64 bits
     */
    cas_data128_t(uint64_t _low, uint64_t _high) noexcept : low{ _low }, high{ _high } {}
    
    cas_data128_t(const cas_data128_t&) noexcept = default;
    cas_data128_t(cas_data128_t&&) noexcept = default;
    cas_data128_t& operator=(const cas_data128_t&) noexcept = default;
    cas_data128_t& operator=(cas_data128_t&&) noexcept = default;
    ~cas_data128_t() noexcept = default;
    
    /// Equality comparison
    constexpr bool operator==(const cas_data128_t& other) const noexcept { return low == other.low && high == other.high; }
    /// Inequality comparison
    constexpr bool operator!=(const cas_data128_t& other) const noexcept { return !(*this == other); }
    
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
    atomic128(const cas_data128_t value) noexcept : data{ value } {}
    
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
    [[nodiscard]] cas_data128_t load() const noexcept;

    /**
     * @brief Atomically load the current value with specified memory ordering
     * 
     * @param order Memory ordering constraint
     * @return Current 128-bit value
     * @note Memory ordering parameter is ignored on x64 with current intrinsics
     */
    [[nodiscard]] cas_data128_t load(const std::memory_order order) const noexcept;

    /**
     * @brief Atomically exchange the stored value
     * 
     * @param value New value to store
     * @param order Memory ordering constraint
     * @return Previous value that was stored
     */
    cas_data128_t exchange(const cas_data128_t value, const std::memory_order order) noexcept;

    /**
     * @brief Atomically exchange the stored value with sequential consistency
     * 
     * @param value New value to store
     * @return Previous value that was stored
     */
    cas_data128_t exchange(const cas_data128_t value) noexcept;

    /**
     * @brief Strong compare-and-swap operation
     * 
     * @param expected Reference to expected value (updated with actual value if CAS fails)
     * @param desired New value to store if comparison succeeds
     * @param order Memory ordering constraint (ignored on x64)
     * @return True if exchange was successful, false otherwise
     */
    bool compare_exchange_strong(cas_data128_t& expected, cas_data128_t desired,
        const std::memory_order order = std::memory_order_seq_cst) noexcept;

    /**
     * @brief Weak compare-and-swap operation (fallback to strong on x64)
     * 
     * @param expected Reference to expected value
     * @param desired New value to store if comparison succeeds  
     * @return True if exchange was successful, false otherwise
     * @note No weak CAS intrinsics available, falls back to strong CAS
     */
    bool compare_exchange_weak(cas_data128_t& expected, cas_data128_t desired) noexcept;

    /**
     * @brief Atomically store a new value
     * 
     * @param value Value to store
     */
    void store(const cas_data128_t value) noexcept;

    /**
     * @brief Atomically store a new value with specified memory ordering
     * 
     * @param value Value to store
     * @param order Memory ordering constraint
     */
    void store(const cas_data128_t value, const std::memory_order order) noexcept;

    /// Indicates that atomic operations are always lock-free
    constexpr static bool is_always_lock_free = true;

private:

    mutable cas_data128_t data;
};

#else

using atomic128 = std::atomic<cas_data128_t>;
static_assert(std::atomic<cas_data128_t>::is_always_lock_free, "128bit cmpexchg not available on current platform.");

#endif

#endif //!THREADING_ATOMIC128_HPP