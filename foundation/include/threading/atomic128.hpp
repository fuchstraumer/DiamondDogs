#pragma once
#ifndef THREADING_ATOMIC128_HPP
#define THREADING_ATOMIC128_HPP
#include <atomic>

struct alignas(16) cas_data128_t
{
    cas_data128_t() noexcept : low{ 0u }, high{ 0u } {}
    cas_data128_t(uint64_t _low, uint64_t _high) noexcept : low{ _low }, high{ _high } {}
    cas_data128_t(const cas_data128_t&) noexcept = default;
    cas_data128_t(cas_data128_t&&) noexcept = default;
    cas_data128_t& operator=(const cas_data128_t&) noexcept = default;
    cas_data128_t& operator=(cas_data128_t&&) noexcept = default;
    ~cas_data128_t() noexcept = default;
    constexpr bool operator==(const cas_data128_t& other) const noexcept { return low == other.low && high == other.high; }
    constexpr bool operator!=(const cas_data128_t& other) const noexcept { return !(*this == other); }
    uint64_t low;
    uint64_t high;
};

#ifdef _MSC_VER

struct alignas(16) atomic128
{
    atomic128() noexcept = default;
    ~atomic128() noexcept = default;
    atomic128(const atomic128&) = delete;
    atomic128& operator=(const atomic128&) = delete;

    atomic128(const cas_data128_t value) noexcept : data{ value } {}
    atomic128(uint64_t lower, uint64_t upper) noexcept : data{ lower, upper } {}

    atomic128(atomic128&& other) noexcept;
    atomic128& operator=(atomic128&& other) noexcept;

    [[nodiscard]] cas_data128_t load() const noexcept;

    [[nodiscard]] cas_data128_t load(const std::memory_order order) const noexcept;

    cas_data128_t exchange(const cas_data128_t value, const std::memory_order order) noexcept;

    cas_data128_t exchange(const cas_data128_t value) noexcept;

    // note: memory order has no effect on x64 with these intrinsics
    bool compare_exchange_strong(cas_data128_t& expected, cas_data128_t desired,
        const std::memory_order order = std::memory_order_seq_cst) noexcept;

    // No weak CAS intrinsics present on current OS/hardware, fall back to strong
    bool compare_exchange_weak(cas_data128_t& expected, cas_data128_t desired) noexcept;

    void store(const cas_data128_t value) noexcept;

    void store(const cas_data128_t value, const std::memory_order order) noexcept;

    // so we comply more to standard library interface
    constexpr static bool is_always_lock_free = true;

private:

    mutable cas_data128_t data;
};

#else

using atomic128 = std::atomic<cas_data128_t>;
static_assert(std::atomic<cas_data128_t>::is_always_lock_free, "128bit cmpexchg not available on current platform.");

#endif

#endif //!THREADING_ATOMIC128_HPP