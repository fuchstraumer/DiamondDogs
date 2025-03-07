#pragma once
#include <atomic>
#ifdef _MSC_VER
#include <intrin.h>
#endif

struct alignas(16) cas_data128_t
{
    constexpr cas_data128_t() noexcept : low{ 0u }, high{ 0u } {}
    constexpr cas_data128_t(uint64_t _low, uint64_t _high) noexcept : low{ _low }, high{ _high } {}
    constexpr cas_data128_t(const cas_data128_t&) noexcept = default;
    constexpr cas_data128_t(cas_data128_t&&) noexcept = default;
    constexpr cas_data128_t& operator=(const cas_data128_t&) noexcept = default;
    constexpr cas_data128_t& operator=(cas_data128_t&&) noexcept = default;
    constexpr ~cas_data128_t() noexcept = default;
    auto operator<=>(const cas_data128_t&) const noexcept = default;
    uint64_t low;
    uint64_t high;
};

#ifdef _MSC_VER

struct alignas(16) atomic128
{
    constexpr atomic128() noexcept = default;
    atomic128(const atomic128&) = delete;
    atomic128& operator=(const atomic128&) = delete;

    constexpr atomic128(const cas_data128_t value) noexcept : data{ value } {}
    constexpr atomic128(uint64_t lower, uint64_t upper) noexcept : data{ lower, upper } {}

    [[nodiscard]] cas_data128_t load() const noexcept
    {
        long long* const storagePtr = const_cast<long long*>(atomic_address_as<const long long>(data));
        cas_data128_t result{};
        _InterlockedCompareExchange128(storagePtr, 0, 0, &reinterpret_cast<long long&>(result.low));
        return reinterpret_cast<cas_data128_t&>(result);
    }

    [[nodiscard]] cas_data128_t load(const std::memory_order order) const noexcept
    {
        return load();
    }

    cas_data128_t exchange(const cas_data128_t value, const std::memory_order order) noexcept
    {
        cas_data128_t result{ value };
        while (!compare_exchange_strong(result, value, order)) {}
        return result;
    }

    cas_data128_t exchange(const cas_data128_t value) noexcept
    {
        cas_data128_t result{ value };
        while (!compare_exchange_strong(result, value)) {}
        return result;
    }

    // note: memory order has no effect on x64 with these intrinsics
    bool compare_exchange_strong(cas_data128_t& expected, cas_data128_t desired,
        const std::memory_order order = std::memory_order_seq_cst) noexcept
    {
        cas_data128_t desiredCopy{ desired };
        cas_data128_t expectedTemp{ expected };
        unsigned char result = _InterlockedCompareExchange128(&reinterpret_cast<long long&>(data.low), desiredCopy.high, desiredCopy.low,
            &reinterpret_cast<long long&>(expectedTemp.low));
        // if result == 0, copy expectedTemp to expected THEN return (comma denotes sequencing)
        return result != 0 ? true : (expected = expectedTemp, false);
    }

    // No weak CAS intrinsics present on current OS/hardware, fall back to strong
    bool compare_exchange_weak(cas_data128_t& expected, cas_data128_t desired) noexcept
    {
        return compare_exchange_strong(expected, desired);
    }

    void store(const cas_data128_t value) noexcept
    {
        (void)exchange(value);
    }

    void store(const cas_data128_t value, const std::memory_order order) noexcept
    {
        (void)exchange(value, order);
    }

    // so we comply more to standard library interface
    constexpr static bool is_always_lock_free = true;

private:

    // volatile needed to avoid any potential reordering
    template<typename DestType, typename SourceType>
    [[nodiscard]] static volatile DestType* atomic_address_as(SourceType& source) noexcept
    {
        return &reinterpret_cast<volatile DestType&>(source);
    }

    mutable cas_data128_t data;
};

#else

using atomic128 = std::atomic<cas_data128_t>;
static_assert(std::atomic<cas_data128_t>::is_always_lock_free, "128bit cmpexchg not available on current platform.");

#endif