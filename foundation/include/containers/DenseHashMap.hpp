// SPDX-License-Identifier: MIT
//
// DenseHashMap is a port of the design of ankerl::unordered_dense by Martin Leitner-Ankerl
// (https://github.com/martinus/unordered_dense), which is MIT licensed. The algorithm is his, not ours: the entries
// are packed in one array, and a separate bucket array of 8-byte buckets indexes them with Robin Hood probing and
// backward-shift deletion. Robin Hood hashing is from Pedro Celis, "Robin Hood Hashing" (1986). We wrote this code
// from the design, in this repository's style. What we changed:
// - the dense entries are a SmallVector<value_type, 0>, so growth and aliasing safety are SmallVector's code
// - the home bucket and the fingerprint both come from the high bits of a Fibonacci (multiply-shift) mix, instead
//   of the original's 128-bit multiply mix
// - the maximum load factor is fixed at 0.8, and there is no segmented storage option
// - every member is noexcept, and an exception or a limit overflow calls std::terminate
//
// Copyright (c) Martin Leitner-Ankerl
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
#pragma once
#ifndef FOUNDATION_CONTAINERS_DENSE_HASH_MAP_HPP
#define FOUNDATION_CONTAINERS_DENSE_HASH_MAP_HPP
#include "SmallVector.hpp"
#include <algorithm>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

namespace foundation
{

namespace detail
{
    template<typename Hasher, typename Equals>
    concept DenseHashTransparent = requires {
        typename Hasher::is_transparent;
        typename Equals::is_transparent;
    };
}

/** @brief A drop-in replacement for std::unordered_map that stores every entry in one dense array. Lookup and
 * insert touch one 8-byte bucket per probe, and iteration walks the dense array. The member names follow the
 * standard library on purpose, so that a call site does not change when it swaps std::unordered_map for
 * DenseHashMap.
 *
 * Differences from std::unordered_map that a call site can notice:
 * - value_type is std::pair<Key, Value>, not std::pair<const Key, Value>. Do not change a key through an iterator.
 * - insert and emplace invalidate every iterator, pointer and reference when the dense array grows.
 * - erase moves the last entry into the erased slot, so it invalidates iterators to the last entry. erase(iterator)
 *   returns an iterator to the same position, which now contains the moved entry, so an erase loop still visits
 *   every entry.
 * - the iteration order is the insertion order until the first erase.
 * - there is no bucket interface, no node handles, and no max_load_factor setter.
 *
 * Every member is noexcept. An exception from Key, Value, Hasher, Equals or the allocator calls std::terminate. */
template<typename Key, typename Value, typename Hasher = std::hash<Key>, typename Equals = std::equal_to<Key>>
class DenseHashMap
{
    static_assert(std::is_nothrow_move_constructible_v<Key> && std::is_nothrow_move_constructible_v<Value>,
                  "DenseHashMap relocates entries by move construction, which must not throw");
    static_assert(std::is_nothrow_move_assignable_v<Key> && std::is_nothrow_move_assignable_v<Value>,
                  "erase moves the last entry into the erased slot by move assignment, which must not throw");

public:
    // the public names match std::unordered_map on purpose, so that DenseHashMap drops in without call site changes
    // NOLINTBEGIN(readability-identifier-naming)
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key, Value>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using hasher = Hasher;
    using key_equal = Equals;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = value_type*;
    using const_iterator = const value_type*;

    DenseHashMap() noexcept;
    explicit DenseHashMap(size_type bucket_count) noexcept;
    template<std::input_iterator InputIt>
    DenseHashMap(InputIt _first, InputIt _last) noexcept;
    DenseHashMap(std::initializer_list<value_type> _list) noexcept
        requires std::is_copy_constructible_v<value_type>;
    template<detail::ContainerCompatibleRange<std::pair<Key, Value>> Range>
    DenseHashMap(std::from_range_t, Range&& _range) noexcept;
    DenseHashMap(const DenseHashMap& _other) noexcept
        requires std::is_copy_constructible_v<value_type>;
    DenseHashMap(DenseHashMap&& _other) noexcept;
    ~DenseHashMap();

    DenseHashMap& operator=(const DenseHashMap& _other) noexcept
        requires std::is_copy_constructible_v<value_type>;
    DenseHashMap& operator=(DenseHashMap&& _other) noexcept;
    DenseHashMap& operator=(std::initializer_list<value_type> _list) noexcept
        requires std::is_copy_constructible_v<value_type>;

    [[nodiscard]] iterator begin() noexcept;
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator cbegin() const noexcept;
    [[nodiscard]] iterator end() noexcept;
    [[nodiscard]] const_iterator end() const noexcept;
    [[nodiscard]] const_iterator cend() const noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] constexpr size_type max_size() const noexcept;

    void clear() noexcept;
    std::pair<iterator, bool> insert(const value_type& _value) noexcept
        requires std::is_copy_constructible_v<value_type>;
    std::pair<iterator, bool> insert(value_type&& _value) noexcept;
    template<std::input_iterator InputIt>
    void insert(InputIt _first, InputIt _last) noexcept;
    void insert(std::initializer_list<value_type> _list) noexcept
        requires std::is_copy_constructible_v<value_type>;
    template<detail::ContainerCompatibleRange<std::pair<Key, Value>> Range>
    void insert_range(Range&& _range) noexcept;
    template<typename MappedArg>
    std::pair<iterator, bool> insert_or_assign(const Key& _key, MappedArg&& _value) noexcept;
    template<typename MappedArg>
    std::pair<iterator, bool> insert_or_assign(Key&& _key, MappedArg&& _value) noexcept;
    // constructs the entry before the lookup, and destroys it again when the key exists. prefer try_emplace
    template<typename... Args>
    std::pair<iterator, bool> emplace(Args&&... _args) noexcept;
    template<typename... Args>
    std::pair<iterator, bool> try_emplace(const Key& _key, Args&&... _args) noexcept;
    template<typename... Args>
    std::pair<iterator, bool> try_emplace(Key&& _key, Args&&... _args) noexcept;
    iterator erase(const_iterator _position) noexcept;
    iterator erase(const_iterator _first, const_iterator _last) noexcept;
    size_type erase(const Key& _key) noexcept;
    template<typename LookupKey>
        requires detail::DenseHashTransparent<Hasher, Equals>
    size_type erase(const LookupKey& _key) noexcept;
    void swap(DenseHashMap& _other) noexcept;

    // at() terminates when the key is not in the map, where std::unordered_map throws std::out_of_range
    [[nodiscard]] Value& at(const Key& _key) noexcept;
    [[nodiscard]] const Value& at(const Key& _key) const noexcept;
    Value& operator[](const Key& _key) noexcept;
    Value& operator[](Key&& _key) noexcept;
    [[nodiscard]] size_type count(const Key& _key) const noexcept;
    template<typename LookupKey>
        requires detail::DenseHashTransparent<Hasher, Equals>
    [[nodiscard]] size_type count(const LookupKey& _key) const noexcept;
    [[nodiscard]] iterator find(const Key& _key) noexcept;
    [[nodiscard]] const_iterator find(const Key& _key) const noexcept;
    template<typename LookupKey>
        requires detail::DenseHashTransparent<Hasher, Equals>
    [[nodiscard]] iterator find(const LookupKey& _key) noexcept;
    template<typename LookupKey>
        requires detail::DenseHashTransparent<Hasher, Equals>
    [[nodiscard]] const_iterator find(const LookupKey& _key) const noexcept;
    [[nodiscard]] bool contains(const Key& _key) const noexcept;
    template<typename LookupKey>
        requires detail::DenseHashTransparent<Hasher, Equals>
    [[nodiscard]] bool contains(const LookupKey& _key) const noexcept;

    [[nodiscard]] size_type bucket_count() const noexcept;
    [[nodiscard]] float load_factor() const noexcept;
    [[nodiscard]] constexpr float max_load_factor() const noexcept;
    void rehash(size_type bucket_count) noexcept;
    void reserve(size_type entry_count) noexcept;

    [[nodiscard]] hasher hash_function() const noexcept;
    [[nodiscard]] key_equal key_eq() const noexcept;

    // hidden friends, so that these are found only through ADL on a DenseHashMap
    friend bool operator==(const DenseHashMap& left, const DenseHashMap& right) noexcept
        requires std::equality_comparable<Value>
    {
        if (left.size() != right.size())
        {
            return false;
        }

        for (const value_type& entry : left)
        {
            const const_iterator match = right.find(entry.first);

            if (match == right.end() || !(match->second == entry.second))
            {
                return false;
            }
        }

        return true;
    }

    friend void swap(DenseHashMap& left, DenseHashMap& right) noexcept
    {
        left.swap(right);
    }
    // NOLINTEND(readability-identifier-naming)

private:
    static constexpr int32_t k_None = -1;
    // a bucket's DistanceAndFingerprint stores the probe distance plus one in the high 24 bits, and 8 bits of the
    // hash in the low 8 bits. 0 marks an empty bucket, and every occupied bucket compares greater than it
    static constexpr uint32_t k_DistanceIncrement = 1u << 8;
    static constexpr uint32_t k_FingerprintMask = k_DistanceIncrement - 1u;
    // a probe distance that reaches this value would wrap the 24 distance bits
    static constexpr uint32_t k_DistanceLimit = ~k_FingerprintMask;
    static constexpr int32_t k_MinBucketCount = 8;
    static constexpr int64_t k_MaxBucketCount = int64_t{ 1 } << 30;
    // the maximum load factor is 0.8, as k_LoadNumerator / k_LoadDenominator
    static constexpr int64_t k_LoadNumerator = 4;
    static constexpr int64_t k_LoadDenominator = 5;
    static constexpr int64_t k_MaxEntryCount = k_MaxBucketCount * k_LoadNumerator / k_LoadDenominator;
    // the fractional part of the golden ratio, as a 64-bit fixed point value. a multiply by it spreads the bits of
    // a weak hash (an identity hash on integers, for example) into the high bits that select the bucket
    static constexpr uint64_t k_FibonacciMultiplier = 0x9E3779B97F4A7C15ull;

    struct Bucket
    {
        uint32_t DistanceAndFingerprint{ 0 };
        int32_t EntryIndex{ k_None };
    };

    // where a probe for a key stopped. when Found is true, BucketIndex is the bucket of the key. otherwise the key
    // belongs at BucketIndex, with DistanceAndFingerprint as its bucket value
    struct InsertProbe
    {
        int32_t BucketIndex{ k_None };
        uint32_t DistanceAndFingerprint{ 0 };
        bool Found{ false };
    };

    [[nodiscard]] static Bucket* allocateBuckets(int64_t bucket_count) noexcept;
    static void deallocateBuckets(Bucket* bucket_array, int64_t bucket_count) noexcept;
    [[nodiscard]] static int64_t bucketCountFor(int64_t entry_count) noexcept;

    template<typename ComparedKey>
    [[nodiscard]] uint64_t mixedHashOf(const ComparedKey& _key) const noexcept;
    [[nodiscard]] int32_t homeBucketOf(uint64_t mixed_hash) const noexcept;
    [[nodiscard]] uint32_t homeDistanceAndFingerprintOf(uint64_t mixed_hash) const noexcept;
    [[nodiscard]] int32_t nextBucket(int32_t bucket_index) const noexcept;
    [[nodiscard]] int64_t currentBucketCount() const noexcept;

    template<typename ComparedKey>
    [[nodiscard]] int32_t findBucket(const ComparedKey& _key) const noexcept;
    template<typename ComparedKey>
    [[nodiscard]] InsertProbe probeForInsert(const ComparedKey& _key) const noexcept;
    [[nodiscard]] int32_t findBucketOfEntry(int32_t entry_index) const noexcept;
    template<typename KeyArg, typename... Args>
    std::pair<iterator, bool> tryEmplaceKey(KeyArg&& _key, Args&&... _args) noexcept;
    template<typename KeyArg, typename MappedArg>
    std::pair<iterator, bool> insertOrAssignKey(KeyArg&& _key, MappedArg&& _value) noexcept;
    template<typename KeyArg, typename... Args>
    iterator emplaceAtProbe(const InsertProbe& _probe, KeyArg&& _key, Args&&... _args) noexcept;
    void placeBucket(Bucket bucket, int32_t bucket_index) noexcept;
    void eraseBucket(int32_t bucket_index) noexcept;
    void removeAtBucket(int32_t bucket_index) noexcept;
    void growForInsert() noexcept;
    void rebuildBuckets(int64_t bucket_count) noexcept;
    void copyBucketsFrom(const DenseHashMap& _other) noexcept;
    void releaseBuckets() noexcept;
    void takeBucketsFrom(DenseHashMap& _other) noexcept;

    // the defaults are the empty state, which is also the moved-from state: no bucket array, and a grow threshold
    // of 0, so that the first insert allocates the buckets
    SmallVector<value_type, 0> entries{};
    Bucket* buckets{ nullptr };
    int32_t bucketMask{ 0 };
    // 64 minus log2 of the bucket count. the home bucket is the mixed hash shifted right by this
    int32_t homeShift{ 0 };
    // the entry count at which the next insert grows the bucket array
    int32_t growThreshold{ 0 };
    Hasher hashFunction{};
    Equals keyEquals{};
};

// std::unordered_map names, as in the class declaration
// NOLINTBEGIN(readability-identifier-naming)
/** @brief Erases every entry that satisfies the predicate. Returns the number of erased entries. This is the
 * DenseHashMap version of std::erase_if, and callers find it through ADL: write `erase_if(map, predicate)`, not
 * `std::erase_if`. */
template<typename Key, typename Value, typename Hasher, typename Equals, typename Predicate>
typename DenseHashMap<Key, Value, Hasher, Equals>::size_type erase_if(DenseHashMap<Key, Value, Hasher, Equals>& map,
                                                                      Predicate predicate) noexcept;


template<typename Key, typename Value, typename Hasher, typename Equals>
DenseHashMap<Key, Value, Hasher, Equals>::DenseHashMap() noexcept = default;

template<typename Key, typename Value, typename Hasher, typename Equals>
DenseHashMap<Key, Value, Hasher, Equals>::DenseHashMap(size_type bucket_count) noexcept
{
    rehash(bucket_count);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<std::input_iterator InputIt>
DenseHashMap<Key, Value, Hasher, Equals>::DenseHashMap(InputIt _first, InputIt _last) noexcept
{
    insert_range(std::ranges::subrange(std::move(_first), std::move(_last)));
}

template<typename Key, typename Value, typename Hasher, typename Equals>
DenseHashMap<Key, Value, Hasher, Equals>::DenseHashMap(std::initializer_list<value_type> _list) noexcept
    requires std::is_copy_constructible_v<value_type>
{
    insert_range(_list);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<detail::ContainerCompatibleRange<std::pair<Key, Value>> Range>
DenseHashMap<Key, Value, Hasher, Equals>::DenseHashMap(std::from_range_t, Range&& _range) noexcept
{
    insert_range(std::forward<Range>(_range));
}

template<typename Key, typename Value, typename Hasher, typename Equals>
DenseHashMap<Key, Value, Hasher, Equals>::DenseHashMap(const DenseHashMap& _other) noexcept
    requires std::is_copy_constructible_v<value_type>
    :
    entries{ _other.entries },
    hashFunction{ _other.hashFunction },
    keyEquals{ _other.keyEquals }
{
    copyBucketsFrom(_other);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
DenseHashMap<Key, Value, Hasher, Equals>::DenseHashMap(DenseHashMap&& _other) noexcept :
    entries{ std::move(_other.entries) },
    hashFunction{ std::move(_other.hashFunction) },
    keyEquals{ std::move(_other.keyEquals) }
{
    takeBucketsFrom(_other);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
DenseHashMap<Key, Value, Hasher, Equals>::~DenseHashMap()
{
    releaseBuckets();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
DenseHashMap<Key, Value, Hasher, Equals>& DenseHashMap<Key, Value, Hasher, Equals>::operator=(
    const DenseHashMap& _other) noexcept
    requires std::is_copy_constructible_v<value_type>
{
    if (this != &_other)
    {
        entries = _other.entries;
        hashFunction = _other.hashFunction;
        keyEquals = _other.keyEquals;
        copyBucketsFrom(_other);
    }

    return *this;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
DenseHashMap<Key, Value, Hasher, Equals>& DenseHashMap<Key, Value, Hasher, Equals>::operator=(
    DenseHashMap&& _other) noexcept
{
    if (this != &_other)
    {
        entries = std::move(_other.entries);
        hashFunction = std::move(_other.hashFunction);
        keyEquals = std::move(_other.keyEquals);
        releaseBuckets();
        takeBucketsFrom(_other);
    }

    return *this;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
DenseHashMap<Key, Value, Hasher, Equals>& DenseHashMap<Key, Value, Hasher, Equals>::operator=(
    std::initializer_list<value_type> _list) noexcept
    requires std::is_copy_constructible_v<value_type>
{
    clear();
    insert_range(_list);
    return *this;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::iterator DenseHashMap<Key, Value, Hasher, Equals>::begin() noexcept
{
    return entries.begin();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::const_iterator DenseHashMap<Key, Value, Hasher, Equals>::begin()
    const noexcept
{
    return entries.begin();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::const_iterator DenseHashMap<Key, Value, Hasher, Equals>::cbegin()
    const noexcept
{
    return entries.cbegin();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::iterator DenseHashMap<Key, Value, Hasher, Equals>::end() noexcept
{
    return entries.end();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::const_iterator DenseHashMap<Key, Value, Hasher, Equals>::end()
    const noexcept
{
    return entries.end();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::const_iterator DenseHashMap<Key, Value, Hasher, Equals>::cend()
    const noexcept
{
    return entries.cend();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
bool DenseHashMap<Key, Value, Hasher, Equals>::empty() const noexcept
{
    return entries.empty();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::size_type DenseHashMap<Key, Value, Hasher, Equals>::size()
    const noexcept
{
    return entries.size();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
constexpr typename DenseHashMap<Key, Value, Hasher, Equals>::size_type DenseHashMap<Key, Value, Hasher, Equals>::
    max_size() const noexcept
{
    return static_cast<size_type>(k_MaxEntryCount);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::clear() noexcept
{
    // keeps the dense array and the bucket array allocated, as std::unordered_map keeps its buckets
    entries.clear();
    std::fill_n(buckets, currentBucketCount(), Bucket{});
}

template<typename Key, typename Value, typename Hasher, typename Equals>
std::pair<typename DenseHashMap<Key, Value, Hasher, Equals>::iterator, bool> DenseHashMap<Key, Value, Hasher, Equals>::
    insert(const value_type& _value) noexcept
    requires std::is_copy_constructible_v<value_type>
{
    return tryEmplaceKey(_value.first, _value.second);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
std::pair<typename DenseHashMap<Key, Value, Hasher, Equals>::iterator, bool> DenseHashMap<Key, Value, Hasher, Equals>::
    insert(value_type&& _value) noexcept
{
    // moves from _value only when the key is new, as std::unordered_map does
    return tryEmplaceKey(std::move(_value.first), std::move(_value.second));
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<std::input_iterator InputIt>
void DenseHashMap<Key, Value, Hasher, Equals>::insert(InputIt _first, InputIt _last) noexcept
{
    insert_range(std::ranges::subrange(std::move(_first), std::move(_last)));
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::insert(std::initializer_list<value_type> _list) noexcept
    requires std::is_copy_constructible_v<value_type>
{
    insert_range(_list);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<detail::ContainerCompatibleRange<std::pair<Key, Value>> Range>
void DenseHashMap<Key, Value, Hasher, Equals>::insert_range(Range&& _range) noexcept
{
    if constexpr (std::ranges::sized_range<Range>)
    {
        // duplicate keys make this an over-reservation, never an under-reservation
        reserve(size() + static_cast<size_type>(std::ranges::size(_range)));
    }

    for (auto&& element : _range)
    {
        emplace(std::forward<decltype(element)>(element));
    }
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename MappedArg>
std::pair<typename DenseHashMap<Key, Value, Hasher, Equals>::iterator, bool> DenseHashMap<Key, Value, Hasher, Equals>::
    insert_or_assign(const Key& _key, MappedArg&& _value) noexcept
{
    return insertOrAssignKey(_key, std::forward<MappedArg>(_value));
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename MappedArg>
std::pair<typename DenseHashMap<Key, Value, Hasher, Equals>::iterator, bool> DenseHashMap<Key, Value, Hasher, Equals>::
    insert_or_assign(Key&& _key, MappedArg&& _value) noexcept
{
    return insertOrAssignKey(std::move(_key), std::forward<MappedArg>(_value));
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename... Args>
std::pair<typename DenseHashMap<Key, Value, Hasher, Equals>::iterator, bool> DenseHashMap<Key, Value, Hasher, Equals>::
    emplace(Args&&... _args) noexcept
{
    growForInsert();

    // the arguments may refer to an entry of this map, which is safe for the same reason as in emplaceAtProbe
    const value_type& added = entries.emplace_back(std::forward<Args>(_args)...);
    const InsertProbe probe = probeForInsert(added.first);

    if (probe.Found)
    {
        entries.pop_back();
        const int32_t existingIndex = buckets[probe.BucketIndex].EntryIndex;
        return { begin() + existingIndex, false };
    }

    const int32_t addedIndex = static_cast<int32_t>(entries.size()) - 1;
    placeBucket(Bucket{ probe.DistanceAndFingerprint, addedIndex }, probe.BucketIndex);
    return { begin() + addedIndex, true };
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename... Args>
std::pair<typename DenseHashMap<Key, Value, Hasher, Equals>::iterator, bool> DenseHashMap<Key, Value, Hasher, Equals>::
    try_emplace(const Key& _key, Args&&... _args) noexcept
{
    return tryEmplaceKey(_key, std::forward<Args>(_args)...);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename... Args>
std::pair<typename DenseHashMap<Key, Value, Hasher, Equals>::iterator, bool> DenseHashMap<Key, Value, Hasher, Equals>::
    try_emplace(Key&& _key, Args&&... _args) noexcept
{
    return tryEmplaceKey(std::move(_key), std::forward<Args>(_args)...);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::iterator DenseHashMap<Key, Value, Hasher, Equals>::erase(
    const_iterator _position) noexcept
{
    const int32_t entryIndex = static_cast<int32_t>(_position - entries.cbegin());
    removeAtBucket(findBucketOfEntry(entryIndex));
    return begin() + entryIndex;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::iterator DenseHashMap<Key, Value, Hasher, Equals>::erase(
    const_iterator _first, const_iterator _last) noexcept
{
    const int32_t firstIndex = static_cast<int32_t>(_first - entries.cbegin());
    const int32_t lastIndex = static_cast<int32_t>(_last - entries.cbegin());

    // walks backward: each erase moves the last entry into the erased slot, and that entry is always outside
    // the part of the range that is still to be erased
    for (int32_t entryIndex = lastIndex - 1; entryIndex >= firstIndex; --entryIndex)
    {
        removeAtBucket(findBucketOfEntry(entryIndex));
    }

    return begin() + firstIndex;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::size_type DenseHashMap<Key, Value, Hasher, Equals>::erase(
    const Key& _key) noexcept
{
    const int32_t bucketIndex = findBucket(_key);

    if (bucketIndex == k_None)
    {
        return 0;
    }

    removeAtBucket(bucketIndex);
    return 1;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename LookupKey>
    requires detail::DenseHashTransparent<Hasher, Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::size_type DenseHashMap<Key, Value, Hasher, Equals>::erase(
    const LookupKey& _key) noexcept
{
    const int32_t bucketIndex = findBucket(_key);

    if (bucketIndex == k_None)
    {
        return 0;
    }

    removeAtBucket(bucketIndex);
    return 1;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::swap(DenseHashMap& _other) noexcept
{
    entries.swap(_other.entries);
    std::swap(buckets, _other.buckets);
    std::swap(bucketMask, _other.bucketMask);
    std::swap(homeShift, _other.homeShift);
    std::swap(growThreshold, _other.growThreshold);
    std::swap(hashFunction, _other.hashFunction);
    std::swap(keyEquals, _other.keyEquals);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
Value& DenseHashMap<Key, Value, Hasher, Equals>::at(const Key& _key) noexcept
{
    const int32_t bucketIndex = findBucket(_key);

    if (bucketIndex == k_None)
    {
        std::terminate();
    }

    const int32_t entryIndex = buckets[bucketIndex].EntryIndex;
    return entries[static_cast<size_type>(entryIndex)].second;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
const Value& DenseHashMap<Key, Value, Hasher, Equals>::at(const Key& _key) const noexcept
{
    const int32_t bucketIndex = findBucket(_key);

    if (bucketIndex == k_None)
    {
        std::terminate();
    }

    const int32_t entryIndex = buckets[bucketIndex].EntryIndex;
    return entries[static_cast<size_type>(entryIndex)].second;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
Value& DenseHashMap<Key, Value, Hasher, Equals>::operator[](const Key& _key) noexcept
{
    const iterator entry = tryEmplaceKey(_key).first;
    return entry->second;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
Value& DenseHashMap<Key, Value, Hasher, Equals>::operator[](Key&& _key) noexcept
{
    const iterator entry = tryEmplaceKey(std::move(_key)).first;
    return entry->second;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::size_type DenseHashMap<Key, Value, Hasher, Equals>::count(
    const Key& _key) const noexcept
{
    return findBucket(_key) == k_None ? 0 : 1;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename LookupKey>
    requires detail::DenseHashTransparent<Hasher, Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::size_type DenseHashMap<Key, Value, Hasher, Equals>::count(
    const LookupKey& _key) const noexcept
{
    return findBucket(_key) == k_None ? 0 : 1;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::iterator DenseHashMap<Key, Value, Hasher, Equals>::find(
    const Key& _key) noexcept
{
    const int32_t bucketIndex = findBucket(_key);

    if (bucketIndex == k_None)
    {
        return end();
    }

    return begin() + buckets[bucketIndex].EntryIndex;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::const_iterator DenseHashMap<Key, Value, Hasher, Equals>::find(
    const Key& _key) const noexcept
{
    const int32_t bucketIndex = findBucket(_key);

    if (bucketIndex == k_None)
    {
        return end();
    }

    return begin() + buckets[bucketIndex].EntryIndex;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename LookupKey>
    requires detail::DenseHashTransparent<Hasher, Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::iterator DenseHashMap<Key, Value, Hasher, Equals>::find(
    const LookupKey& _key) noexcept
{
    const int32_t bucketIndex = findBucket(_key);

    if (bucketIndex == k_None)
    {
        return end();
    }

    return begin() + buckets[bucketIndex].EntryIndex;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename LookupKey>
    requires detail::DenseHashTransparent<Hasher, Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::const_iterator DenseHashMap<Key, Value, Hasher, Equals>::find(
    const LookupKey& _key) const noexcept
{
    const int32_t bucketIndex = findBucket(_key);

    if (bucketIndex == k_None)
    {
        return end();
    }

    return begin() + buckets[bucketIndex].EntryIndex;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
bool DenseHashMap<Key, Value, Hasher, Equals>::contains(const Key& _key) const noexcept
{
    return findBucket(_key) != k_None;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename LookupKey>
    requires detail::DenseHashTransparent<Hasher, Equals>
bool DenseHashMap<Key, Value, Hasher, Equals>::contains(const LookupKey& _key) const noexcept
{
    return findBucket(_key) != k_None;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::size_type DenseHashMap<Key, Value, Hasher, Equals>::bucket_count()
    const noexcept
{
    return static_cast<size_type>(currentBucketCount());
}

template<typename Key, typename Value, typename Hasher, typename Equals>
float DenseHashMap<Key, Value, Hasher, Equals>::load_factor() const noexcept
{
    const int64_t bucketCount = currentBucketCount();

    if (bucketCount == 0)
    {
        return 0.0f;
    }

    return static_cast<float>(entries.size()) / static_cast<float>(bucketCount);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
constexpr float DenseHashMap<Key, Value, Hasher, Equals>::max_load_factor() const noexcept
{
    return static_cast<float>(k_LoadNumerator) / static_cast<float>(k_LoadDenominator);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::rehash(size_type bucket_count) noexcept
{
    // as std::unordered_map: at least bucket_count buckets, and at least enough for the current entries
    if (bucket_count > static_cast<size_type>(k_MaxBucketCount))
    {
        std::terminate();
    }

    // rehash(0) on an empty map frees the bucket array, and the next insert allocates it again
    if (bucket_count == 0 && entries.empty())
    {
        releaseBuckets();
        return;
    }

    const int64_t requestedCount = std::max<int64_t>(k_MinBucketCount, static_cast<int64_t>(bucket_count));
    const int64_t requestedPowerOfTwo = static_cast<int64_t>(std::bit_ceil(static_cast<uint64_t>(requestedCount)));
    const int64_t neededCount = std::max(requestedPowerOfTwo, bucketCountFor(std::ssize(entries)));

    if (neededCount != currentBucketCount())
    {
        rebuildBuckets(neededCount);
    }
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::reserve(size_type entry_count) noexcept
{
    if (entry_count > static_cast<size_type>(k_MaxEntryCount))
    {
        std::terminate();
    }

    if (entry_count == 0)
    {
        return;
    }

    entries.reserve(entry_count);
    const int64_t neededCount = bucketCountFor(static_cast<int64_t>(entry_count));

    if (neededCount > currentBucketCount())
    {
        rebuildBuckets(neededCount);
    }
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::hasher DenseHashMap<Key, Value, Hasher, Equals>::hash_function()
    const noexcept
{
    return hashFunction;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::key_equal DenseHashMap<Key, Value, Hasher, Equals>::key_eq()
    const noexcept
{
    return keyEquals;
}
// NOLINTEND(readability-identifier-naming)

template<typename Key, typename Value, typename Hasher, typename Equals>
typename DenseHashMap<Key, Value, Hasher, Equals>::Bucket* DenseHashMap<Key, Value, Hasher, Equals>::allocateBuckets(
    int64_t bucket_count) noexcept
{
    const std::size_t byteCount = sizeof(Bucket) * static_cast<std::size_t>(bucket_count);
    Bucket* bucketArray = static_cast<Bucket*>(::operator new(byteCount));
    std::uninitialized_fill_n(bucketArray, bucket_count, Bucket{});
    return bucketArray;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::deallocateBuckets(Bucket* bucket_array, int64_t bucket_count) noexcept
{
    const std::size_t byteCount = sizeof(Bucket) * static_cast<std::size_t>(bucket_count);
    ::operator delete(bucket_array, byteCount);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
int64_t DenseHashMap<Key, Value, Hasher, Equals>::bucketCountFor(int64_t entry_count) noexcept
{
    // the smallest power of two that keeps entry_count at or below the maximum load factor
    const int64_t minimumCount = ((entry_count * k_LoadDenominator) + k_LoadNumerator - 1) / k_LoadNumerator;
    const int64_t clampedCount = std::max<int64_t>(k_MinBucketCount, minimumCount);
    return static_cast<int64_t>(std::bit_ceil(static_cast<uint64_t>(clampedCount)));
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename ComparedKey>
uint64_t DenseHashMap<Key, Value, Hasher, Equals>::mixedHashOf(const ComparedKey& _key) const noexcept
{
    const uint64_t hash = static_cast<uint64_t>(hashFunction(_key));
    return hash * k_FibonacciMultiplier;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
int32_t DenseHashMap<Key, Value, Hasher, Equals>::homeBucketOf(uint64_t mixed_hash) const noexcept
{
    return static_cast<int32_t>(mixed_hash >> homeShift);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
uint32_t DenseHashMap<Key, Value, Hasher, Equals>::homeDistanceAndFingerprintOf(uint64_t mixed_hash) const noexcept
{
    // the fingerprint is the 8 bits just below the home bucket bits. those are the best mixed bits that the home
    // bucket does not already use. homeShift is at least 34, so the shift never goes negative
    const uint32_t fingerprint = static_cast<uint32_t>(mixed_hash >> (homeShift - 8)) & k_FingerprintMask;
    return k_DistanceIncrement | fingerprint;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
int32_t DenseHashMap<Key, Value, Hasher, Equals>::nextBucket(int32_t bucket_index) const noexcept
{
    return (bucket_index + 1) & bucketMask;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
int64_t DenseHashMap<Key, Value, Hasher, Equals>::currentBucketCount() const noexcept
{
    if (buckets == nullptr)
    {
        return 0;
    }

    return int64_t{ bucketMask } + 1;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename ComparedKey>
int32_t DenseHashMap<Key, Value, Hasher, Equals>::findBucket(const ComparedKey& _key) const noexcept
{
    // also covers a map that has no bucket array yet
    if (entries.empty())
    {
        return k_None;
    }

    const uint64_t mixedHash = mixedHashOf(_key);
    uint32_t distanceAndFingerprint = homeDistanceAndFingerprintOf(mixedHash);
    int32_t bucketIndex = homeBucketOf(mixedHash);

    // Robin Hood order: a bucket that is closer to its own home than this key would be at the same slot ends the
    // probe, because the key would have displaced it. an empty bucket compares as 0, so it ends the probe too
    for (;;)
    {
        const Bucket& bucket = buckets[bucketIndex];

        if (bucket.DistanceAndFingerprint == distanceAndFingerprint)
        {
            const value_type& entry = entries[static_cast<size_type>(bucket.EntryIndex)];

            if (keyEquals(entry.first, _key))
            {
                return bucketIndex;
            }
        }
        else if (bucket.DistanceAndFingerprint < distanceAndFingerprint)
        {
            return k_None;
        }

        distanceAndFingerprint += k_DistanceIncrement;
        bucketIndex = nextBucket(bucketIndex);
    }
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename ComparedKey>
typename DenseHashMap<Key, Value, Hasher, Equals>::InsertProbe DenseHashMap<Key, Value, Hasher, Equals>::
    probeForInsert(const ComparedKey& _key) const noexcept
{
    // requires a bucket array with room for one more entry. the same walk as findBucket, but it also reports
    // where the probe stopped, so that the insert can place the key there
    const uint64_t mixedHash = mixedHashOf(_key);
    uint32_t distanceAndFingerprint = homeDistanceAndFingerprintOf(mixedHash);
    int32_t bucketIndex = homeBucketOf(mixedHash);

    for (;;)
    {
        const Bucket& bucket = buckets[bucketIndex];

        if (bucket.DistanceAndFingerprint == distanceAndFingerprint)
        {
            const value_type& entry = entries[static_cast<size_type>(bucket.EntryIndex)];

            if (keyEquals(entry.first, _key))
            {
                return InsertProbe{ bucketIndex, distanceAndFingerprint, true };
            }
        }
        else if (bucket.DistanceAndFingerprint < distanceAndFingerprint)
        {
            return InsertProbe{ bucketIndex, distanceAndFingerprint, false };
        }

        distanceAndFingerprint += k_DistanceIncrement;
        bucketIndex = nextBucket(bucketIndex);
    }
}

template<typename Key, typename Value, typename Hasher, typename Equals>
int32_t DenseHashMap<Key, Value, Hasher, Equals>::findBucketOfEntry(int32_t entry_index) const noexcept
{
    const value_type& entry = entries[static_cast<size_type>(entry_index)];
    int32_t bucketIndex = homeBucketOf(mixedHashOf(entry.first));

    while (buckets[bucketIndex].EntryIndex != entry_index)
    {
        bucketIndex = nextBucket(bucketIndex);
    }

    return bucketIndex;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename KeyArg, typename... Args>
std::pair<typename DenseHashMap<Key, Value, Hasher, Equals>::iterator, bool> DenseHashMap<Key, Value, Hasher, Equals>::
    tryEmplaceKey(KeyArg&& _key, Args&&... _args) noexcept
{
    // grows before the probe, so that the probe result stays valid for the insert. a full map grows even when the
    // key is already in it, which costs one early rehash and never a wrong result
    growForInsert();
    const InsertProbe probe = probeForInsert(_key);

    if (probe.Found)
    {
        const int32_t existingIndex = buckets[probe.BucketIndex].EntryIndex;
        return { begin() + existingIndex, false };
    }

    const iterator added = emplaceAtProbe(probe, std::forward<KeyArg>(_key), std::forward<Args>(_args)...);
    return { added, true };
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename KeyArg, typename MappedArg>
std::pair<typename DenseHashMap<Key, Value, Hasher, Equals>::iterator, bool> DenseHashMap<Key, Value, Hasher, Equals>::
    insertOrAssignKey(KeyArg&& _key, MappedArg&& _value) noexcept
{
    growForInsert();
    const InsertProbe probe = probeForInsert(_key);

    if (probe.Found)
    {
        const int32_t existingIndex = buckets[probe.BucketIndex].EntryIndex;
        const iterator existing = begin() + existingIndex;
        existing->second = std::forward<MappedArg>(_value);
        return { existing, false };
    }

    const iterator added = emplaceAtProbe(probe, std::forward<KeyArg>(_key), std::forward<MappedArg>(_value));
    return { added, true };
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename KeyArg, typename... Args>
typename DenseHashMap<Key, Value, Hasher, Equals>::iterator DenseHashMap<Key, Value, Hasher, Equals>::emplaceAtProbe(
    const InsertProbe& _probe, KeyArg&& _key, Args&&... _args) noexcept
{
    // the arguments may refer to an entry of this map. emplace_back constructs the new entry before it relocates
    // the old ones, so that reference stays valid for the construction
    entries.emplace_back(std::piecewise_construct,
                         std::forward_as_tuple(std::forward<KeyArg>(_key)),
                         std::forward_as_tuple(std::forward<Args>(_args)...));

    const int32_t addedIndex = static_cast<int32_t>(entries.size()) - 1;
    placeBucket(Bucket{ _probe.DistanceAndFingerprint, addedIndex }, _probe.BucketIndex);
    return begin() + addedIndex;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::placeBucket(Bucket bucket, int32_t bucket_index) noexcept
{
    // puts the bucket at bucket_index, and shifts each occupied bucket after it one slot further from its home,
    // up to the first empty bucket. this keeps the Robin Hood order that findBucket depends on
    int32_t bucketIndex = bucket_index;

    while (buckets[bucketIndex].DistanceAndFingerprint != 0)
    {
        bucket = std::exchange(buckets[bucketIndex], bucket);
        bucket.DistanceAndFingerprint += k_DistanceIncrement;

        // only a hasher that sends millions of keys to one bucket gets here
        if (bucket.DistanceAndFingerprint >= k_DistanceLimit)
        {
            std::terminate();
        }

        bucketIndex = nextBucket(bucketIndex);
    }

    buckets[bucketIndex] = bucket;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::eraseBucket(int32_t bucket_index) noexcept
{
    // backward-shift deletion: pull each later bucket one slot back toward its home, up to the first bucket that
    // is empty or already at its home. no tombstones, so a lookup never walks past erased entries
    int32_t holeIndex = bucket_index;
    int32_t nextIndex = nextBucket(holeIndex);

    while (buckets[nextIndex].DistanceAndFingerprint >= 2 * k_DistanceIncrement)
    {
        const Bucket& movedBucket = buckets[nextIndex];
        buckets[holeIndex] = Bucket{ movedBucket.DistanceAndFingerprint - k_DistanceIncrement, movedBucket.EntryIndex };
        holeIndex = nextIndex;
        nextIndex = nextBucket(nextIndex);
    }

    buckets[holeIndex] = Bucket{};
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::removeAtBucket(int32_t bucket_index) noexcept
{
    const int32_t entryIndex = buckets[bucket_index].EntryIndex;
    eraseBucket(bucket_index);

    const int32_t lastIndex = static_cast<int32_t>(entries.size()) - 1;

    // the last entry moves into the hole, so the dense array stays packed. its bucket must point to its new slot
    if (entryIndex != lastIndex)
    {
        const int32_t lastBucket = findBucketOfEntry(lastIndex);
        buckets[lastBucket].EntryIndex = entryIndex;
        entries[static_cast<size_type>(entryIndex)] = std::move(entries.back());
    }

    entries.pop_back();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::growForInsert() noexcept
{
    if (std::ssize(entries) < growThreshold)
    {
        return;
    }

    const int64_t currentCount = currentBucketCount();
    const int64_t grownCount = currentCount == 0 ? k_MinBucketCount : currentCount * 2;

    if (grownCount > k_MaxBucketCount)
    {
        std::terminate();
    }

    rebuildBuckets(grownCount);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::rebuildBuckets(int64_t bucket_count) noexcept
{
    releaseBuckets();
    buckets = allocateBuckets(bucket_count);
    bucketMask = static_cast<int32_t>(bucket_count - 1);
    homeShift = 64 - std::countr_zero(static_cast<uint64_t>(bucket_count));
    growThreshold = static_cast<int32_t>(bucket_count * k_LoadNumerator / k_LoadDenominator);

    // the keys are unique already, so each entry goes straight to the end of its probe without a compare
    const int32_t entryCount = static_cast<int32_t>(entries.size());

    for (int32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
    {
        const value_type& entry = entries[static_cast<size_type>(entryIndex)];
        const uint64_t mixedHash = mixedHashOf(entry.first);
        uint32_t distanceAndFingerprint = homeDistanceAndFingerprintOf(mixedHash);
        int32_t bucketIndex = homeBucketOf(mixedHash);

        while (distanceAndFingerprint <= buckets[bucketIndex].DistanceAndFingerprint)
        {
            distanceAndFingerprint += k_DistanceIncrement;
            bucketIndex = nextBucket(bucketIndex);
        }

        placeBucket(Bucket{ distanceAndFingerprint, entryIndex }, bucketIndex);
    }
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::copyBucketsFrom(const DenseHashMap& _other) noexcept
{
    // the bucket array depends only on the hashes and the insertion history, so a copy of it is valid for the
    // copied entries. this saves a hasher call per entry
    const int64_t otherCount = _other.currentBucketCount();

    if (otherCount != currentBucketCount())
    {
        releaseBuckets();
        buckets = otherCount == 0 ? nullptr : allocateBuckets(otherCount);
    }

    std::copy_n(_other.buckets, otherCount, buckets);
    bucketMask = _other.bucketMask;
    homeShift = _other.homeShift;
    growThreshold = _other.growThreshold;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::releaseBuckets() noexcept
{
    if (buckets == nullptr)
    {
        return;
    }

    deallocateBuckets(buckets, currentBucketCount());
    buckets = nullptr;
    bucketMask = 0;
    homeShift = 0;
    growThreshold = 0;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void DenseHashMap<Key, Value, Hasher, Equals>::takeBucketsFrom(DenseHashMap& _other) noexcept
{
    // requires this to own no bucket array. leaves _other with no bucket array, which is the empty state
    buckets = std::exchange(_other.buckets, nullptr);
    bucketMask = std::exchange(_other.bucketMask, 0);
    homeShift = std::exchange(_other.homeShift, 0);
    growThreshold = std::exchange(_other.growThreshold, 0);
}

// NOLINTBEGIN(readability-identifier-naming)
template<typename Key, typename Value, typename Hasher, typename Equals, typename Predicate>
typename DenseHashMap<Key, Value, Hasher, Equals>::size_type erase_if(DenseHashMap<Key, Value, Hasher, Equals>& map,
                                                                      Predicate predicate) noexcept
{
    using Map = DenseHashMap<Key, Value, Hasher, Equals>;
    const typename Map::size_type sizeBefore = map.size();
    typename Map::iterator position = map.begin();

    // erase returns the same position, which now contains the moved last entry, so that entry is tested too
    while (position != map.end())
    {
        if (predicate(*position))
        {
            position = map.erase(position);
        }
        else
        {
            ++position;
        }
    }

    return sizeBefore - map.size();
}
// NOLINTEND(readability-identifier-naming)

}

static_assert(std::ranges::contiguous_range<foundation::DenseHashMap<int32_t, int32_t>>);
static_assert(std::ranges::sized_range<foundation::DenseHashMap<int32_t, int32_t>>);

#endif // !FOUNDATION_CONTAINERS_DENSE_HASH_MAP_HPP
