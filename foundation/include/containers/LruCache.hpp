#pragma once
#ifndef FOUNDATION_INCLUDE_CONTAINERS_LRU_CACHE_HPP
#define FOUNDATION_INCLUDE_CONTAINERS_LRU_CACHE_HPP
#include <algorithm>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace foundation::containers
{

namespace detail
{
    template<typename Hasher, typename Equals>
    concept TransparentLookup = requires {
        typename Hasher::is_transparent;
        typename Equals::is_transparent;
    };

    // a transparent hasher and comparer take any key type that they accept. otherwise the lookup key must
    // convert to Key, and it converts once per call
    template<typename LookupKey, typename Key, typename Hasher, typename Equals>
    concept LruLookupKey =
        TransparentLookup<Hasher, Equals> || std::convertible_to<const LookupKey&, const Key&>;
}

/** @brief A least-recently-used cache with a fixed capacity. The constructor makes the only allocation: every entry,
 * the recency list and the hash index live in one block, so Put, Find and eviction never allocate.
 *
 * The index is an open-addressing table with linear probing, kept at a load factor of 0.5 or less, with
 * backward-shift deletion (no tombstones). The recency list is a doubly-linked list of int32_t indices, stored apart
 * from the entries so that a recency update touches 8-byte links only.
 *
 * Every member is noexcept. An exception from Key, Value, Hasher, Equals or the allocator calls std::terminate.
 * The cache is single-threaded. A moved-from cache may only be destroyed or assigned to. */
template<typename Key, typename Value, typename Hasher = std::hash<Key>, typename Equals = std::equal_to<Key>>
class LruCache
{
public:
    // the bucket count is the next power of two at or above twice the capacity, and it must fit an int32_t mask
    static constexpr int32_t k_MaxCapacity = int32_t{ 1 } << 29;

    explicit LruCache(int32_t _capacity) noexcept;
    LruCache(const LruCache&) = delete;
    LruCache(LruCache&& _other) noexcept;
    ~LruCache();

    LruCache& operator=(const LruCache&) = delete;
    LruCache& operator=(LruCache&& _other) noexcept;

    /** @brief Returns the value stored for the key, and marks the entry as the most recently used. Returns nullptr
     * when the key is not in the cache. The pointer is valid until the next call to Put, Erase or Clear. */
    template<typename LookupKey>
        requires detail::LruLookupKey<LookupKey, Key, Hasher, Equals>
    [[nodiscard]] Value* Find(const LookupKey& _key) noexcept;

    /** @brief Returns the value stored for the key, and does not change the recency order. Returns nullptr when
     * the key is not in the cache. */
    template<typename LookupKey>
        requires detail::LruLookupKey<LookupKey, Key, Hasher, Equals>
    [[nodiscard]] const Value* Peek(const LookupKey& _key) const noexcept;

    /** @brief Returns true when the key is in the cache. Does not change the recency order. */
    template<typename LookupKey>
        requires detail::LruLookupKey<LookupKey, Key, Hasher, Equals>
    [[nodiscard]] bool Contains(const LookupKey& _key) const noexcept;

    /** @brief Stores the value for the key, and marks the entry as the most recently used. When the key is new and
     * the cache is full, Put first evicts the least recently used entry. Returns the stored value. Both parameters
     * are sink parameters: pass with std::move to avoid a copy. */
    Value& Put(Key _key, Value _value) noexcept;

    /** @brief Removes the entry for the key. Returns false when the key is not in the cache. */
    template<typename LookupKey>
        requires detail::LruLookupKey<LookupKey, Key, Hasher, Equals>
    bool Erase(const LookupKey& _key) noexcept;

    void Clear() noexcept;

    [[nodiscard]] int32_t Size() const noexcept;
    [[nodiscard]] int32_t Capacity() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;

private:
    static constexpr int32_t k_None = -1;

    struct Entry
    {
        Key StoredKey;
        Value StoredValue;
        // the high 32 bits of the mixed hash. the top bits select the home bucket, so the index can find an
        // entry's bucket again without a call to Hasher
        uint32_t HashTag{ 0 };
    };

    struct Link
    {
        int32_t Previous{ k_None };
        int32_t Next{ k_None };
    };

    struct Bucket
    {
        uint32_t HashTag{ 0 };
        int32_t EntryIndex{ k_None };
    };

    // the fractional part of the golden ratio, as a 64-bit fixed point value. a multiply by it spreads the bits of
    // a weak hash (an identity hash on integers, for example) into the high bits that select the bucket
    static constexpr uint64_t k_FibonacciMultiplier = 0x9E3779B97F4A7C15ull;
    static constexpr std::size_t k_BlockAlignment = std::max(alignof(Entry), alignof(Bucket));

    [[nodiscard]] static std::size_t blockByteCount(int32_t entry_capacity, int32_t bucket_count) noexcept;

    template<typename LookupKey>
    [[nodiscard]] int32_t lookupBucket(const LookupKey& _key) const noexcept;
    template<typename ComparedKey>
    [[nodiscard]] uint32_t hashTagOf(const ComparedKey& _key) const noexcept;
    template<typename ComparedKey>
    [[nodiscard]] int32_t findBucket(const ComparedKey& _key, uint32_t hash_tag) const noexcept;
    [[nodiscard]] int32_t homeBucket(uint32_t hash_tag) const noexcept;
    [[nodiscard]] int32_t findBucketOfEntry(int32_t entry_index) const noexcept;
    void insertBucket(uint32_t hash_tag, int32_t entry_index) noexcept;
    void eraseBucket(int32_t bucket_index) noexcept;
    void removeEntry(int32_t bucket_index) noexcept;
    void linkFront(int32_t entry_index) noexcept;
    void unlink(int32_t entry_index) noexcept;
    void moveToFront(int32_t entry_index) noexcept;
    void resetIndex() noexcept;
    void destroyEntries() noexcept;
    void releaseBlock() noexcept;
    void takeFrom(LruCache& _other) noexcept;

    // entries, links and buckets all point into one block, which entries owns
    // the defaults are the moved-from state: no block, and every index at k_None
    Entry* entries{ nullptr };
    Link* links{ nullptr };
    Bucket* buckets{ nullptr };
    int32_t capacity{ 0 };
    int32_t entryCount{ 0 };
    int32_t bucketMask{ 0 };
    int32_t homeShift{ 0 };
    int32_t mostRecent{ k_None };
    int32_t leastRecent{ k_None };
    // unused entry slots form a singly-linked list through Link::Next
    int32_t freeHead{ k_None };
    Hasher hasher{};
    Equals equals{};
};


template<typename Key, typename Value, typename Hasher, typename Equals>
LruCache<Key, Value, Hasher, Equals>::LruCache(int32_t _capacity) noexcept :
    capacity{ _capacity }
{
    assert(_capacity > 0 && _capacity <= k_MaxCapacity);

    const uint32_t bucketCount = std::bit_ceil(static_cast<uint32_t>(_capacity) * 2u);
    bucketMask = static_cast<int32_t>(bucketCount - 1u);
    homeShift = 32 - std::countr_zero(bucketCount);

    const std::size_t byteCount = blockByteCount(_capacity, static_cast<int32_t>(bucketCount));
    std::byte* block = static_cast<std::byte*>(::operator new(byteCount, std::align_val_t{ k_BlockAlignment }));

    // Entry is a multiple of alignof(uint32_t) in size, so the links and the buckets that follow it are aligned.
    // no Entry exists until Put constructs one
    entries = reinterpret_cast<Entry*>(block);
    links = reinterpret_cast<Link*>(block + sizeof(Entry) * static_cast<std::size_t>(_capacity));
    buckets = reinterpret_cast<Bucket*>(links + _capacity);
    std::uninitialized_default_construct_n(links, _capacity);
    std::uninitialized_default_construct_n(buckets, bucketCount);
    resetIndex();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
LruCache<Key, Value, Hasher, Equals>::LruCache(LruCache&& _other) noexcept
{
    takeFrom(_other);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
LruCache<Key, Value, Hasher, Equals>::~LruCache()
{
    destroyEntries();
    releaseBlock();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
LruCache<Key, Value, Hasher, Equals>& LruCache<Key, Value, Hasher, Equals>::operator=(LruCache&& _other) noexcept
{
    if (this != &_other)
    {
        destroyEntries();
        releaseBlock();
        takeFrom(_other);
    }

    return *this;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename LookupKey>
    requires detail::LruLookupKey<LookupKey, Key, Hasher, Equals>
Value* LruCache<Key, Value, Hasher, Equals>::Find(const LookupKey& _key) noexcept
{
    const int32_t bucketIndex = lookupBucket(_key);

    if (bucketIndex == k_None)
    {
        return nullptr;
    }

    const int32_t entryIndex = buckets[bucketIndex].EntryIndex;
    moveToFront(entryIndex);
    return &entries[entryIndex].StoredValue;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename LookupKey>
    requires detail::LruLookupKey<LookupKey, Key, Hasher, Equals>
const Value* LruCache<Key, Value, Hasher, Equals>::Peek(const LookupKey& _key) const noexcept
{
    const int32_t bucketIndex = lookupBucket(_key);

    if (bucketIndex == k_None)
    {
        return nullptr;
    }

    const int32_t entryIndex = buckets[bucketIndex].EntryIndex;
    return &entries[entryIndex].StoredValue;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename LookupKey>
    requires detail::LruLookupKey<LookupKey, Key, Hasher, Equals>
bool LruCache<Key, Value, Hasher, Equals>::Contains(const LookupKey& _key) const noexcept
{
    return lookupBucket(_key) != k_None;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
Value& LruCache<Key, Value, Hasher, Equals>::Put(Key _key, Value _value) noexcept
{
    const uint32_t hashTag = hashTagOf(_key);
    const int32_t foundBucket = findBucket(_key, hashTag);

    if (foundBucket != k_None)
    {
        const int32_t foundEntry = buckets[foundBucket].EntryIndex;
        Entry& entry = entries[foundEntry];
        entry.StoredValue = std::move(_value);
        moveToFront(foundEntry);
        return entry.StoredValue;
    }

    if (entryCount == capacity)
    {
        removeEntry(findBucketOfEntry(leastRecent));
    }

    const int32_t entryIndex = freeHead;
    freeHead = links[entryIndex].Next;

    Entry* entry = std::construct_at(entries + entryIndex, std::move(_key), std::move(_value), hashTag);
    insertBucket(hashTag, entryIndex);
    linkFront(entryIndex);
    ++entryCount;
    return entry->StoredValue;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename LookupKey>
    requires detail::LruLookupKey<LookupKey, Key, Hasher, Equals>
bool LruCache<Key, Value, Hasher, Equals>::Erase(const LookupKey& _key) noexcept
{
    const int32_t bucketIndex = lookupBucket(_key);

    if (bucketIndex == k_None)
    {
        return false;
    }

    removeEntry(bucketIndex);
    return true;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void LruCache<Key, Value, Hasher, Equals>::Clear() noexcept
{
    destroyEntries();
    resetIndex();
}

template<typename Key, typename Value, typename Hasher, typename Equals>
int32_t LruCache<Key, Value, Hasher, Equals>::Size() const noexcept
{
    return entryCount;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
int32_t LruCache<Key, Value, Hasher, Equals>::Capacity() const noexcept
{
    return capacity;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
bool LruCache<Key, Value, Hasher, Equals>::Empty() const noexcept
{
    return entryCount == 0;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
std::size_t LruCache<Key, Value, Hasher, Equals>::blockByteCount(int32_t entry_capacity, int32_t bucket_count) noexcept
{
    const std::size_t entryBytes = sizeof(Entry) * static_cast<std::size_t>(entry_capacity);
    const std::size_t linkBytes = sizeof(Link) * static_cast<std::size_t>(entry_capacity);
    const std::size_t bucketBytes = sizeof(Bucket) * static_cast<std::size_t>(bucket_count);
    return entryBytes + linkBytes + bucketBytes;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename LookupKey>
int32_t LruCache<Key, Value, Hasher, Equals>::lookupBucket(const LookupKey& _key) const noexcept
{
    if constexpr (detail::TransparentLookup<Hasher, Equals>)
    {
        return findBucket(_key, hashTagOf(_key));
    }
    else
    {
        // converts once here when LookupKey is not Key, instead of once in the hash and once per compare
        const Key& convertedKey = _key;
        return findBucket(convertedKey, hashTagOf(convertedKey));
    }
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename ComparedKey>
uint32_t LruCache<Key, Value, Hasher, Equals>::hashTagOf(const ComparedKey& _key) const noexcept
{
    const uint64_t hash = static_cast<uint64_t>(hasher(_key));
    const uint64_t mixedHash = hash * k_FibonacciMultiplier;
    return static_cast<uint32_t>(mixedHash >> 32);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
template<typename ComparedKey>
int32_t LruCache<Key, Value, Hasher, Equals>::findBucket(const ComparedKey& _key, uint32_t hash_tag) const noexcept
{
    // the load factor is 0.5 or less, so an empty bucket always ends the probe
    int32_t bucketIndex = homeBucket(hash_tag);

    for (;;)
    {
        const Bucket& bucket = buckets[bucketIndex];

        if (bucket.EntryIndex == k_None)
        {
            return k_None;
        }

        if (bucket.HashTag == hash_tag && equals(entries[bucket.EntryIndex].StoredKey, _key))
        {
            return bucketIndex;
        }

        bucketIndex = (bucketIndex + 1) & bucketMask;
    }
}

template<typename Key, typename Value, typename Hasher, typename Equals>
int32_t LruCache<Key, Value, Hasher, Equals>::homeBucket(uint32_t hash_tag) const noexcept
{
    return static_cast<int32_t>(hash_tag >> homeShift);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
int32_t LruCache<Key, Value, Hasher, Equals>::findBucketOfEntry(int32_t entry_index) const noexcept
{
    int32_t bucketIndex = homeBucket(entries[entry_index].HashTag);

    while (buckets[bucketIndex].EntryIndex != entry_index)
    {
        bucketIndex = (bucketIndex + 1) & bucketMask;
    }

    return bucketIndex;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void LruCache<Key, Value, Hasher, Equals>::insertBucket(uint32_t hash_tag, int32_t entry_index) noexcept
{
    int32_t bucketIndex = homeBucket(hash_tag);

    while (buckets[bucketIndex].EntryIndex != k_None)
    {
        bucketIndex = (bucketIndex + 1) & bucketMask;
    }

    buckets[bucketIndex] = Bucket{ hash_tag, entry_index };
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void LruCache<Key, Value, Hasher, Equals>::eraseBucket(int32_t bucket_index) noexcept
{
    // backward-shift deletion: pull each later bucket of the probe run into the hole when its home bucket is at
    // or before the hole, so that no lookup ever stops early at the hole
    int32_t holeIndex = bucket_index;
    int32_t probeIndex = (holeIndex + 1) & bucketMask;

    while (buckets[probeIndex].EntryIndex != k_None)
    {
        const int32_t probeHome = homeBucket(buckets[probeIndex].HashTag);
        // the subtractions wrap around the table through the mask
        const int32_t homeToProbe = (probeIndex - probeHome) & bucketMask;
        const int32_t holeToProbe = (probeIndex - holeIndex) & bucketMask;

        if (homeToProbe >= holeToProbe)
        {
            buckets[holeIndex] = buckets[probeIndex];
            holeIndex = probeIndex;
        }

        probeIndex = (probeIndex + 1) & bucketMask;
    }

    buckets[holeIndex] = Bucket{};
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void LruCache<Key, Value, Hasher, Equals>::removeEntry(int32_t bucket_index) noexcept
{
    const int32_t entryIndex = buckets[bucket_index].EntryIndex;
    eraseBucket(bucket_index);
    unlink(entryIndex);
    std::destroy_at(entries + entryIndex);

    links[entryIndex].Next = freeHead;
    freeHead = entryIndex;
    --entryCount;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void LruCache<Key, Value, Hasher, Equals>::linkFront(int32_t entry_index) noexcept
{
    links[entry_index] = Link{ k_None, mostRecent };

    if (mostRecent != k_None)
    {
        links[mostRecent].Previous = entry_index;
    }
    else
    {
        leastRecent = entry_index;
    }

    mostRecent = entry_index;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void LruCache<Key, Value, Hasher, Equals>::unlink(int32_t entry_index) noexcept
{
    const Link link = links[entry_index];

    if (link.Previous != k_None)
    {
        links[link.Previous].Next = link.Next;
    }
    else
    {
        mostRecent = link.Next;
    }

    if (link.Next != k_None)
    {
        links[link.Next].Previous = link.Previous;
    }
    else
    {
        leastRecent = link.Previous;
    }
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void LruCache<Key, Value, Hasher, Equals>::moveToFront(int32_t entry_index) noexcept
{
    if (entry_index == mostRecent)
    {
        return;
    }

    unlink(entry_index);
    linkFront(entry_index);
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void LruCache<Key, Value, Hasher, Equals>::resetIndex() noexcept
{
    std::fill_n(buckets, bucketMask + 1, Bucket{});

    for (int32_t entryIndex = 0; entryIndex < capacity; ++entryIndex)
    {
        links[entryIndex] = Link{ k_None, entryIndex + 1 };
    }

    links[capacity - 1].Next = k_None;
    freeHead = 0;
    mostRecent = k_None;
    leastRecent = k_None;
    entryCount = 0;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void LruCache<Key, Value, Hasher, Equals>::destroyEntries() noexcept
{
    if constexpr (!std::is_trivially_destructible_v<Entry>)
    {
        for (int32_t entryIndex = mostRecent; entryIndex != k_None; entryIndex = links[entryIndex].Next)
        {
            std::destroy_at(entries + entryIndex);
        }
    }
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void LruCache<Key, Value, Hasher, Equals>::releaseBlock() noexcept
{
    if (entries == nullptr)
    {
        return;
    }

    const std::size_t byteCount = blockByteCount(capacity, bucketMask + 1);
    ::operator delete(entries, byteCount, std::align_val_t{ k_BlockAlignment });
    entries = nullptr;
}

template<typename Key, typename Value, typename Hasher, typename Equals>
void LruCache<Key, Value, Hasher, Equals>::takeFrom(LruCache& _other) noexcept
{
    // requires this to own no block. leaves _other empty, with no block
    entries = std::exchange(_other.entries, nullptr);
    links = std::exchange(_other.links, nullptr);
    buckets = std::exchange(_other.buckets, nullptr);
    capacity = std::exchange(_other.capacity, 0);
    entryCount = std::exchange(_other.entryCount, 0);
    bucketMask = std::exchange(_other.bucketMask, 0);
    homeShift = std::exchange(_other.homeShift, 0);
    mostRecent = std::exchange(_other.mostRecent, k_None);
    leastRecent = std::exchange(_other.leastRecent, k_None);
    freeHead = std::exchange(_other.freeHead, k_None);
    hasher = std::move(_other.hasher);
    equals = std::move(_other.equals);
}

}

#endif // !FOUNDATION_INCLUDE_CONTAINERS_LRU_CACHE_HPP
