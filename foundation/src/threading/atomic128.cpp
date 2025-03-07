#include "threading/atomic128.hpp"
#ifdef _MSC_VER
#include <intrin.h>

// volatile needed to avoid any potential reordering
template<typename DestType, typename SourceType>
[[nodiscard]] static volatile DestType* atomic_address_as(SourceType& source) noexcept
{
    return &reinterpret_cast<volatile DestType&>(source);
}

auto cas_data128_t::operator<=>(const cas_data128_t& other) const noexcept
{
    return (low <=> other.low) && (high <=> other.high);
}

constexpr atomic128::atomic128(atomic128&& other) noexcept : data{ std::move(other.data) } {}

constexpr atomic128& atomic128::operator=(atomic128&& other) noexcept
{
    if (this != &other)
    {
        data = std::move(other.data);
    }
    return *this;
}

cas_data128_t atomic128::load() const noexcept
{
    long long* const storagePtr = const_cast<long long*>(atomic_address_as<const long long>(data));
    cas_data128_t result{};
    _InterlockedCompareExchange128(storagePtr, 0, 0, &reinterpret_cast<long long&>(result.low));
    return reinterpret_cast<cas_data128_t&>(result);
}

cas_data128_t atomic128::load(const std::memory_order order) const noexcept
{
    return load();
}

cas_data128_t atomic128::exchange(const cas_data128_t value, const std::memory_order order) noexcept
{
    cas_data128_t result{ value };
    while (!compare_exchange_strong(result, value, order)) {}
    return result;
}

cas_data128_t atomic128::exchange(const cas_data128_t value) noexcept
{
    cas_data128_t result{ value };
    while (!compare_exchange_strong(result, value)) {}
    return result;
}

bool atomic128::compare_exchange_strong(cas_data128_t& expected, cas_data128_t desired, const std::memory_order order /*= std::memory_order_seq_cst*/) noexcept
{
    cas_data128_t desiredCopy{ desired };
    cas_data128_t expectedTemp{ expected };
    unsigned char result = _InterlockedCompareExchange128(&reinterpret_cast<long long&>(data.low), desiredCopy.high, desiredCopy.low,
        &reinterpret_cast<long long&>(expectedTemp.low));
    // if result == 0, copy expectedTemp to expected THEN return (comma denotes sequencing)
    return result != 0 ? true : (expected = expectedTemp, false);
}

bool atomic128::compare_exchange_weak(cas_data128_t& expected, cas_data128_t desired) noexcept
{
    return compare_exchange_strong(expected, desired);
}

void atomic128::store(const cas_data128_t value) noexcept
{
    (void)exchange(value);
}

void atomic128::store(const cas_data128_t value, const std::memory_order order) noexcept
{
    (void)exchange(value, order);
}

// will eventually need an else to implement this for linux/unix/etc. i think ARM should at least just support this.
#endif
