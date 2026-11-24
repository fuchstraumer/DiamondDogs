#pragma once
#ifndef FOUNDATION_CONTAINERS_SMALLVECTOR_HPP
#define FOUNDATION_CONTAINERS_SMALLVECTOR_HPP
#include <algorithm>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
    #define FOUNDATION_SMALLVECTOR_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
    #define FOUNDATION_SMALLVECTOR_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

#if defined(_MSC_VER) && !defined(__clang__)
    #define FOUNDATION_SMALLVECTOR_FORCEINLINE __forceinline
    #define FOUNDATION_SMALLVECTOR_NOINLINE __declspec(noinline)
    #define FOUNDATION_SMALLVECTOR_ASSUME(condition) __assume(condition)
#elif defined(__clang__)
    #define FOUNDATION_SMALLVECTOR_FORCEINLINE [[gnu::always_inline]] inline
    #define FOUNDATION_SMALLVECTOR_NOINLINE [[gnu::noinline]]
    #define FOUNDATION_SMALLVECTOR_ASSUME(condition) __builtin_assume(condition)
#else
    #define FOUNDATION_SMALLVECTOR_FORCEINLINE [[gnu::always_inline]] inline
    #define FOUNDATION_SMALLVECTOR_NOINLINE [[gnu::noinline]]
    #define FOUNDATION_SMALLVECTOR_ASSUME(condition) [[assume(condition)]]
#endif

namespace foundation
{

namespace detail
{
    // the anonymous union stops the compiler from constructing or destroying the elements, so that
    // SmallVector controls the lifetime of each element
    template<typename T, std::size_t N>
    struct SmallVectorInlineStorage
    {
        FOUNDATION_SMALLVECTOR_FORCEINLINE SmallVectorInlineStorage() noexcept
        {
        }

        FOUNDATION_SMALLVECTOR_FORCEINLINE ~SmallVectorInlineStorage() noexcept
        {
        }

        [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE T* Data() noexcept
        {
            return Elements;
        }

        [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const T* Data() const noexcept
        {
            return Elements;
        }

        union
        {
            T Elements[N];
        };
    };

    template<typename T>
    struct SmallVectorInlineStorage<T, 0>
    {
        [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE T* Data() noexcept
        {
            return nullptr;
        }

        [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const T* Data() const noexcept
        {
            return nullptr;
        }
    };

    template<typename Range, typename T>
    concept ContainerCompatibleRange =
        std::ranges::input_range<Range> && std::convertible_to<std::ranges::range_reference_t<Range>, T>;

    // only ranges with O(1) known size: walking a non-sized forward_range (e.g. views::filter) with distance()
    // evaluates the range twice even when all elements fit inside the inline buffer
    template<typename Range>
    concept CountableRange = std::ranges::sized_range<Range>;

    template<typename Iterator, typename T>
    concept TrivialContiguousSource =
        std::contiguous_iterator<Iterator> &&
        std::is_trivially_copyable_v<T> &&
        std::is_lvalue_reference_v<std::iter_reference_t<Iterator>> &&
        !std::is_volatile_v<std::remove_reference_t<std::iter_reference_t<Iterator>>> &&
        std::same_as<std::remove_cvref_t<std::iter_reference_t<Iterator>>, std::remove_cv_t<T>>;
}

/** @brief A drop-in replacement for std::vector that stores up to N elements inside the object, and moves them to
 * the heap only when it grows past N. The member names follow the standard library on purpose, so that a call
 * site does not change when it swaps std::vector for SmallVector.
 *
 * Every member is noexcept. An exception from T or from the allocator calls std::terminate. Elements relocate by
 * move construction, so T must have a noexcept move constructor. Iterators are raw pointers, so the type is a
 * contiguous range for std::ranges and std::views. */
template<typename T, std::size_t N>
class SmallVector
{
    static_assert(std::is_nothrow_move_constructible_v<T>, "SmallVector relocates by move construction, which must not throw");
    static_assert(std::is_nothrow_destructible_v<T>, "SmallVector requires a noexcept destructor");
    static_assert(N <= std::numeric_limits<uint32_t>::max(), "SmallVector stores its size and capacity as uint32_t");

public:
    // the public names match std::vector on purpose, so that SmallVector drops in without call site changes
    // NOLINTBEGIN(readability-identifier-naming)
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static constexpr size_type inline_capacity = N;

    FOUNDATION_SMALLVECTOR_FORCEINLINE SmallVector() noexcept;
    inline explicit SmallVector(size_type _count) noexcept;
    inline SmallVector(size_type _count, const T& _value) noexcept;
    template<std::input_iterator InputIt>
    inline SmallVector(InputIt _first, InputIt _last) noexcept;
    inline SmallVector(std::initializer_list<T> _list) noexcept;
    template<detail::ContainerCompatibleRange<T> Range>
    inline SmallVector(std::from_range_t, Range&& _range) noexcept;
    inline SmallVector(const SmallVector& _other) noexcept
        requires std::is_copy_constructible_v<T>;
    inline SmallVector(SmallVector&& _other) noexcept;
    FOUNDATION_SMALLVECTOR_FORCEINLINE ~SmallVector() noexcept;

    inline SmallVector& operator=(const SmallVector& _other) noexcept
        requires std::is_copy_constructible_v<T>;
    inline SmallVector& operator=(SmallVector&& _other) noexcept;
    inline SmallVector& operator=(std::initializer_list<T> _list) noexcept;

    inline void assign(size_type _count, const T& _value) noexcept;
    template<std::input_iterator InputIt>
    inline void assign(InputIt _first, InputIt _last) noexcept;
    inline void assign(std::initializer_list<T> _list) noexcept;
    template<detail::ContainerCompatibleRange<T> Range>
    inline void assign_range(Range&& _range) noexcept;

    // at() terminates on an index out of range, where std::vector throws std::out_of_range
    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE reference at(size_type _index) noexcept
    {
        if (_index >= itemCount) [[unlikely]]
        {
            std::terminate();
        }

        return items[_index];
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_reference at(size_type _index) const noexcept
    {
        if (_index >= itemCount) [[unlikely]]
        {
            std::terminate();
        }

        return items[_index];
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE reference operator[](size_type _index) noexcept
    {
        assert(_index < itemCount);
        return items[_index];
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_reference operator[](size_type _index) const noexcept
    {
        assert(_index < itemCount);
        return items[_index];
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE reference front() noexcept
    {
        assert(itemCount != 0);
        return items[0];
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_reference front() const noexcept
    {
        assert(itemCount != 0);
        return items[0];
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE reference back() noexcept
    {
        assert(itemCount != 0);
        return items[itemCount - 1];
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_reference back() const noexcept
    {
        assert(itemCount != 0);
        return items[itemCount - 1];
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE T* data() noexcept
    {
        return items;
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const T* data() const noexcept
    {
        return items;
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE iterator begin() noexcept
    {
        return items;
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_iterator begin() const noexcept
    {
        return items;
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_iterator cbegin() const noexcept
    {
        return items;
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE iterator end() noexcept
    {
        return items + itemCount;
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_iterator end() const noexcept
    {
        return items + itemCount;
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_iterator cend() const noexcept
    {
        return items + itemCount;
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE reverse_iterator rbegin() noexcept
    {
        return reverse_iterator(end());
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_reverse_iterator rbegin() const noexcept
    {
        return const_reverse_iterator(end());
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_reverse_iterator crbegin() const noexcept
    {
        return const_reverse_iterator(end());
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE reverse_iterator rend() noexcept
    {
        return reverse_iterator(begin());
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_reverse_iterator rend() const noexcept
    {
        return const_reverse_iterator(begin());
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE const_reverse_iterator crend() const noexcept
    {
        return const_reverse_iterator(begin());
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE bool empty() const noexcept
    {
        return itemCount == 0;
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE size_type size() const noexcept
    {
        return itemCount;
    }

    [[nodiscard]] static constexpr size_type max_size() noexcept
    {
        constexpr size_type k_CountLimit = std::numeric_limits<uint32_t>::max();
        constexpr size_type k_ByteLimit =
            static_cast<size_type>(std::numeric_limits<difference_type>::max()) / sizeof(T);
        return k_CountLimit < k_ByteLimit ? k_CountLimit : k_ByteLimit;
    }

    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE size_type capacity() const noexcept
    {
        return itemCapacity;
    }

    // not in std::vector: true while the elements are in the inline buffer. heap buffers always have
    // capacity > N (shrink_to_fit moves elements back inline when size <= N), so comparing itemCapacity == N
    // avoids computing inlineItems.Data()
    [[nodiscard]] FOUNDATION_SMALLVECTOR_FORCEINLINE bool is_inline() const noexcept
    {
        return itemCapacity == static_cast<uint32_t>(N);
    }

    inline void reserve(size_type new_capacity) noexcept;
    inline void shrink_to_fit() noexcept;

    FOUNDATION_SMALLVECTOR_FORCEINLINE void clear() noexcept;
    inline iterator insert(const_iterator _position, const T& _value) noexcept;
    inline iterator insert(const_iterator _position, T&& _value) noexcept;
    inline iterator insert(const_iterator _position, size_type _count, const T& _value) noexcept;
    template<std::input_iterator InputIt>
    inline iterator insert(const_iterator _position, InputIt _first, InputIt _last) noexcept;
    inline iterator insert(const_iterator _position, std::initializer_list<T> _list) noexcept;
    template<detail::ContainerCompatibleRange<T> Range>
    inline iterator insert_range(const_iterator _position, Range&& _range) noexcept;
    template<typename... Args>
    inline iterator emplace(const_iterator _position, Args&&... _args) noexcept;
    inline iterator erase(const_iterator _position) noexcept;
    inline iterator erase(const_iterator _first, const_iterator _last) noexcept;
    FOUNDATION_SMALLVECTOR_FORCEINLINE void push_back(const T& _value) noexcept;
    FOUNDATION_SMALLVECTOR_FORCEINLINE void push_back(T&& _value) noexcept;
    template<typename... Args>
    FOUNDATION_SMALLVECTOR_FORCEINLINE reference emplace_back(Args&&... _args) noexcept;
    template<detail::ContainerCompatibleRange<T> Range>
    inline void append_range(Range&& _range) noexcept;
    FOUNDATION_SMALLVECTOR_FORCEINLINE void pop_back() noexcept;
    inline void resize(size_type _count) noexcept;
    inline void resize(size_type _count, const T& _value) noexcept;
    inline void swap(SmallVector& _other) noexcept;

    // hidden friends, so that these are found only through ADL on a SmallVector
    friend bool operator==(const SmallVector& left, const SmallVector& right) noexcept
        requires std::equality_comparable<T>
    {
        if (left.itemCount != right.itemCount)
        {
            return false;
        }

        return std::equal(left.items, left.items + left.itemCount, right.items);
    }

    friend auto operator<=>(const SmallVector& left, const SmallVector& right) noexcept
        requires std::three_way_comparable<T>
    {
        return std::lexicographical_compare_three_way(left.begin(), left.end(), right.begin(), right.end());
    }

    friend void swap(SmallVector& left, SmallVector& right) noexcept
    {
        left.swap(right);
    }
    // NOLINTEND(readability-identifier-naming)

private:
    // up to one cache line (two AVX2 ymm registers), fixed-size inline memcpy compiles to 1-4 register moves
    // with zero CRT call and zero branches on element count
    static constexpr size_type k_FixedInlineCopyByteLimit = 64;
public:
    // in hindsight, I'm making this public: I can check it with a static_assert when using it to 
    // see if we're getting this optimization at a given callsite!
    static constexpr bool k_UseFixedInlineCopy =
        std::is_trivially_copyable_v<T> && (N > 0) && (N * sizeof(T) <= k_FixedInlineCopyByteLimit);
private:

    [[nodiscard]] static inline T* allocate(size_type element_count) noexcept;
    static inline void deallocate(T* buffer, size_type element_count) noexcept;
    static inline void relocate(T* source, size_type element_count, T* destination) noexcept;
    static inline void relocateUpward(T* source, size_type element_count, T* destination) noexcept;
    FOUNDATION_SMALLVECTOR_FORCEINLINE static void destroyRange(T* first, size_type element_count) noexcept;
    static inline void uninitializedValueConstructN(T* destination, size_type element_count) noexcept;
    static inline void uninitializedFillN(T* destination, size_type element_count, const T& _value) noexcept;
    template<typename Iterator>
    static inline void uninitializedCopyN(Iterator _source, size_type element_count, T* destination) noexcept;

    [[nodiscard]] inline size_type grownCapacity(size_type required_count) const noexcept;
    inline void growToFit(size_type required_count) noexcept;
    FOUNDATION_SMALLVECTOR_NOINLINE void allocateInitialHeap(size_type exact_capacity) noexcept;
    FOUNDATION_SMALLVECTOR_NOINLINE void reallocate(size_type new_capacity) noexcept;
    FOUNDATION_SMALLVECTOR_NOINLINE T* openGapReallocate(size_type gap_index, size_type gap_count) noexcept;
    FOUNDATION_SMALLVECTOR_NOINLINE void growAndAppendFill(size_type fill_count, const T& _value) noexcept;
    template<typename Iterator>
    FOUNDATION_SMALLVECTOR_NOINLINE void reallocateAndAssignCounted(Iterator _source, size_type element_count) noexcept;
    inline void adoptBuffer(T* buffer, size_type buffer_capacity) noexcept;
    FOUNDATION_SMALLVECTOR_FORCEINLINE void releaseHeap() noexcept;
    FOUNDATION_SMALLVECTOR_FORCEINLINE void resetToInline() noexcept;
    inline void truncate(size_type new_count) noexcept;
    [[nodiscard]] inline T* openGap(size_type gap_index, size_type gap_count) noexcept;
    template<typename... Args>
    FOUNDATION_SMALLVECTOR_NOINLINE reference growAndEmplaceBack(Args&&... _args) noexcept;
    template<typename Iterator>
    inline void initFromCounted(Iterator _source, size_type element_count) noexcept;
    template<typename Iterator>
    inline void appendCounted(Iterator _source, size_type element_count) noexcept;
    template<typename Iterator>
    inline void assignCounted(Iterator _source, size_type element_count) noexcept;
    inline void appendFill(size_type fill_count, const T& _value) noexcept;

    // items points to the inline buffer or to the heap buffer. a self-pointer costs a fixup on move, and in
    // return data() and every element access never branch on where the elements are. every constructor points
    // items at the inline buffer in its body, after inlineItems begins its lifetime
    T* items;
    uint32_t itemCount;
    uint32_t itemCapacity;
    // not initialized on purpose: the union must not construct any element
    FOUNDATION_SMALLVECTOR_NO_UNIQUE_ADDRESS detail::SmallVectorInlineStorage<T, N> inlineItems;
};

// std::vector names, as in the class declaration
// NOLINTBEGIN(readability-identifier-naming)
/** @brief Erases every element equal to value. Returns the number of erased elements. This is the SmallVector
 * version of std::erase, and callers find it through ADL: write `erase(vector, value)`, not `std::erase`. */
template<typename T, std::size_t N, typename U>
inline typename SmallVector<T, N>::size_type erase(SmallVector<T, N>& vector, const U& value) noexcept;

/** @brief Erases every element that satisfies the predicate. Returns the number of erased elements. Callers find it
 * through ADL, as with erase. */
template<typename T, std::size_t N, typename Predicate>
inline typename SmallVector<T, N>::size_type erase_if(SmallVector<T, N>& vector, Predicate predicate) noexcept;


template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_FORCEINLINE SmallVector<T, N>::SmallVector() noexcept
{
    // the members are set in the body, because items points into inlineItems, which a member initializer must
    // not touch before it is constructed
    resetToInline();
}

template<typename T, std::size_t N>
inline SmallVector<T, N>::SmallVector(size_type _count) noexcept
{
    resetToInline();

    if (_count > N) [[unlikely]]
    {
        allocateInitialHeap(_count);
    }

    uninitializedValueConstructN(items, _count);
    itemCount = static_cast<uint32_t>(_count);
}

template<typename T, std::size_t N>
inline SmallVector<T, N>::SmallVector(size_type _count, const T& _value) noexcept
{
    resetToInline();

    if (_count > N) [[unlikely]]
    {
        allocateInitialHeap(_count);
    }

    uninitializedFillN(items, _count, _value);
    itemCount = static_cast<uint32_t>(_count);
}

template<typename T, std::size_t N>
template<std::input_iterator InputIt>
inline SmallVector<T, N>::SmallVector(InputIt _first, InputIt _last) noexcept
{
    resetToInline();

    if constexpr (std::sized_sentinel_for<InputIt, InputIt>)
    {
        const size_type count = static_cast<size_type>(_last - _first);
        initFromCounted(std::move(_first), count);
    }
    else
    {
        for (; _first != _last; ++_first)
        {
            emplace_back(*_first);
        }
    }
}

template<typename T, std::size_t N>
inline SmallVector<T, N>::SmallVector(std::initializer_list<T> _list) noexcept
{
    resetToInline();
    initFromCounted(_list.begin(), _list.size());
}

template<typename T, std::size_t N>
template<detail::ContainerCompatibleRange<T> Range>
inline SmallVector<T, N>::SmallVector(std::from_range_t, Range&& _range) noexcept
{
    resetToInline();

    if constexpr (detail::CountableRange<Range>)
    {
        const size_type rangeCount = static_cast<size_type>(std::ranges::size(_range));
        initFromCounted(std::ranges::begin(_range), rangeCount);
    }
    else
    {
        for (auto&& element : _range)
        {
            emplace_back(std::forward<decltype(element)>(element));
        }
    }
}

template<typename T, std::size_t N>
inline SmallVector<T, N>::SmallVector(const SmallVector& _other) noexcept
    requires std::is_copy_constructible_v<T>
{
    resetToInline();
    const uint32_t count = _other.itemCount;

    if (count > N) [[unlikely]]
    {
        allocateInitialHeap(count);
        uninitializedCopyN(_other.items, count, items);
    }
    else if constexpr (k_UseFixedInlineCopy)
    {
        std::memcpy(items, _other.items, N * sizeof(T));
    }
    else
    {
        FOUNDATION_SMALLVECTOR_ASSUME(count <= N);
        uninitializedCopyN(_other.items, count, items);
    }

    itemCount = count;
}

template<typename T, std::size_t N>
inline SmallVector<T, N>::SmallVector(SmallVector&& _other) noexcept
{
    const uint32_t count = _other.itemCount;

    if (!_other.is_inline()) [[unlikely]]
    {
        items = _other.items;
        itemCount = count;
        itemCapacity = _other.itemCapacity;
        _other.resetToInline();
        return;
    }

    resetToInline();

    if constexpr (k_UseFixedInlineCopy)
    {
        std::memcpy(items, _other.inlineItems.Data(), N * sizeof(T));
    }
    else
    {
        FOUNDATION_SMALLVECTOR_ASSUME(count <= N);
        relocate(_other.inlineItems.Data(), count, items);
    }

    itemCount = count;
    _other.itemCount = 0;
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_FORCEINLINE SmallVector<T, N>::~SmallVector() noexcept
{
    destroyRange(items, itemCount);
    releaseHeap();
}

template<typename T, std::size_t N>
inline SmallVector<T, N>& SmallVector<T, N>::operator=(const SmallVector& _other) noexcept
    requires std::is_copy_constructible_v<T>
{
    if (this != &_other)
    {
        if constexpr (k_UseFixedInlineCopy)
        {
            const uint32_t count = _other.itemCount;

            if (count <= N) [[likely]]
            {
                std::memcpy(items, _other.items, N * sizeof(T));
                itemCount = count;
                return *this;
            }
        }

        assignCounted(_other.items, _other.itemCount);
    }

    return *this;
}

template<typename T, std::size_t N>
inline SmallVector<T, N>& SmallVector<T, N>::operator=(SmallVector&& _other) noexcept
{
    if (this != &_other)
    {
        destroyRange(items, itemCount);

        if (!_other.is_inline()) [[unlikely]]
        {
            adoptBuffer(_other.items, _other.itemCapacity);
            itemCount = _other.itemCount;
            _other.resetToInline();
            return *this;
        }

        const uint32_t count = _other.itemCount;

        if constexpr (k_UseFixedInlineCopy)
        {
            std::memcpy(items, _other.inlineItems.Data(), N * sizeof(T));
        }
        else
        {
            FOUNDATION_SMALLVECTOR_ASSUME(count <= N);
            relocate(_other.inlineItems.Data(), count, items);
        }

        itemCount = count;
        _other.itemCount = 0;
    }

    return *this;
}

template<typename T, std::size_t N>
inline SmallVector<T, N>& SmallVector<T, N>::operator=(std::initializer_list<T> _list) noexcept
{
    assignCounted(_list.begin(), _list.size());
    return *this;
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::assign(size_type _count, const T& _value) noexcept
{
    if (_count == 0)
    {
        clear();
        return;
    }

    // copy first, because _value can refer to an element that this call overwrites or destroys, as in
    // `vector.assign(4, vector[0])`. clang-tidy flags this copy as unnecessary; a reference here is a
    // use-after-free, so keep the copy
    const T fillValue = _value;

    if (_count > itemCapacity) [[unlikely]]
    {
        if (_count > max_size()) [[unlikely]]
        {
            std::terminate();
        }

        T* newBuffer = allocate(_count);
        uninitializedFillN(newBuffer, _count, fillValue);
        destroyRange(items, itemCount);
        adoptBuffer(newBuffer, _count);
        itemCount = static_cast<uint32_t>(_count);
        return;
    }

    if constexpr (std::is_trivially_copyable_v<T>)
    {
        uninitializedFillN(items, _count, fillValue);
        itemCount = static_cast<uint32_t>(_count);
    }
    else
    {
        const size_type assignedCount = std::min<size_type>(_count, itemCount);
        std::fill_n(items, assignedCount, fillValue);

        if (_count > itemCount)
        {
            uninitializedFillN(items + itemCount, _count - itemCount, fillValue);
            itemCount = static_cast<uint32_t>(_count);
            return;
        }

        truncate(_count);
    }
}

template<typename T, std::size_t N>
template<std::input_iterator InputIt>
inline void SmallVector<T, N>::assign(InputIt _first, InputIt _last) noexcept
{
    if constexpr (std::sized_sentinel_for<InputIt, InputIt>)
    {
        assignCounted(std::move(_first), static_cast<size_type>(_last - _first));
    }
    else
    {
        clear();

        for (; _first != _last; ++_first)
        {
            emplace_back(*_first);
        }
    }
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::assign(std::initializer_list<T> _list) noexcept
{
    assignCounted(_list.begin(), _list.size());
}

template<typename T, std::size_t N>
template<detail::ContainerCompatibleRange<T> Range>
inline void SmallVector<T, N>::assign_range(Range&& _range) noexcept
{
    if constexpr (detail::CountableRange<Range>)
    {
        const size_type rangeCount = static_cast<size_type>(std::ranges::size(_range));
        assignCounted(std::ranges::begin(_range), rangeCount);
    }
    else
    {
        clear();
        append_range(std::forward<Range>(_range));
    }
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::reserve(size_type new_capacity) noexcept
{
    if (new_capacity > itemCapacity)
    {
        if (new_capacity > max_size()) [[unlikely]]
        {
            std::terminate();
        }

        reallocate(new_capacity);
    }
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::shrink_to_fit() noexcept
{
    if (is_inline() || itemCount == itemCapacity)
    {
        return;
    }

    if (itemCount > N)
    {
        reallocate(itemCount);
        return;
    }

    // the elements fit inline again, so move them back and free the heap buffer
    T* heapItems = items;
    const size_type heapCapacity = itemCapacity;
    items = inlineItems.Data();
    itemCapacity = static_cast<uint32_t>(N);
    relocate(heapItems, itemCount, items);
    deallocate(heapItems, heapCapacity);
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_FORCEINLINE void SmallVector<T, N>::clear() noexcept
{
    destroyRange(items, itemCount);
    itemCount = 0;
}

template<typename T, std::size_t N>
inline typename SmallVector<T, N>::iterator SmallVector<T, N>::insert(const_iterator _position, const T& _value) noexcept
{
    return emplace(_position, _value);
}

template<typename T, std::size_t N>
inline typename SmallVector<T, N>::iterator SmallVector<T, N>::insert(const_iterator _position, T&& _value) noexcept
{
    return emplace(_position, std::move(_value));
}

template<typename T, std::size_t N>
inline typename SmallVector<T, N>::iterator SmallVector<T, N>::insert(const_iterator _position,
                                                                      size_type _count,
                                                                      const T& _value) noexcept
{
    const size_type insertIndex = static_cast<size_type>(_position - items);

    if (_count == 0)
    {
        return items + insertIndex;
    }

    // copy first, because _value can refer to an element that openGap moves, as in
    // `vector.insert(vector.begin(), 2, vector.back())`. clang-tidy flags this copy as unnecessary; a reference
    // here reads a moved-from or destroyed element, so keep the copy
    const T fillValue = _value;
    T* gap = openGap(insertIndex, _count);
    uninitializedFillN(gap, _count, fillValue);
    return gap;
}

template<typename T, std::size_t N>
template<std::input_iterator InputIt>
inline typename SmallVector<T, N>::iterator SmallVector<T, N>::insert(const_iterator _position,
                                                                      InputIt _first,
                                                                      InputIt _last) noexcept
{
    const size_type insertIndex = static_cast<size_type>(_position - items);

    if constexpr (std::sized_sentinel_for<InputIt, InputIt>)
    {
        const size_type insertCount = static_cast<size_type>(_last - _first);
        T* gap = openGap(insertIndex, insertCount);
        uninitializedCopyN(std::move(_first), insertCount, gap);
        return gap;
    }
    else
    {
        const size_type oldCount = itemCount;

        for (; _first != _last; ++_first)
        {
            emplace_back(*_first);
        }

        std::ranges::rotate(items + insertIndex, items + oldCount, items + itemCount);
        return items + insertIndex;
    }
}

template<typename T, std::size_t N>
inline typename SmallVector<T, N>::iterator SmallVector<T, N>::insert(const_iterator _position,
                                                                      std::initializer_list<T> _list) noexcept
{
    const size_type insertIndex = static_cast<size_type>(_position - items);
    const size_type insertCount = _list.size();
    T* gap = openGap(insertIndex, insertCount);
    uninitializedCopyN(_list.begin(), insertCount, gap);
    return gap;
}

template<typename T, std::size_t N>
template<detail::ContainerCompatibleRange<T> Range>
inline typename SmallVector<T, N>::iterator SmallVector<T, N>::insert_range(const_iterator _position,
                                                                            Range&& _range) noexcept
{
    const size_type insertIndex = static_cast<size_type>(_position - items);

    if constexpr (detail::CountableRange<Range>)
    {
        const size_type insertCount = static_cast<size_type>(std::ranges::size(_range));
        T* gap = openGap(insertIndex, insertCount);
        uninitializedCopyN(std::ranges::begin(_range), insertCount, gap);
        return gap;
    }
    else
    {
        // an unsized range has no O(1) length up front: append in one pass, then rotate into place
        const size_type oldCount = itemCount;

        for (auto&& element : _range)
        {
            emplace_back(std::forward<decltype(element)>(element));
        }

        std::ranges::rotate(items + insertIndex, items + oldCount, items + itemCount);
        return items + insertIndex;
    }
}

template<typename T, std::size_t N>
template<typename... Args>
inline typename SmallVector<T, N>::iterator SmallVector<T, N>::emplace(const_iterator _position, Args&&... _args) noexcept
{
    const size_type insertIndex = static_cast<size_type>(_position - items);

    if (insertIndex == itemCount)
    {
        emplace_back(std::forward<Args>(_args)...);
        return items + insertIndex;
    }

    // construct first, because the arguments can refer to an element that openGap moves
    T element(std::forward<Args>(_args)...);
    T* gap = openGap(insertIndex, 1);
    std::construct_at(gap, std::move(element));
    return gap;
}

template<typename T, std::size_t N>
inline typename SmallVector<T, N>::iterator SmallVector<T, N>::erase(const_iterator _position) noexcept
{
    return erase(_position, _position + 1);
}

template<typename T, std::size_t N>
inline typename SmallVector<T, N>::iterator SmallVector<T, N>::erase(const_iterator _first, const_iterator _last) noexcept
{
    T* eraseFirst = items + (_first - items);
    T* eraseLast = items + (_last - items);

    if (eraseFirst != eraseLast)
    {
        T* currentEnd = items + itemCount;

        if (eraseLast != currentEnd)
        {
            std::move(eraseLast, currentEnd, eraseFirst);
        }

        const size_type newCount =
            static_cast<size_type>(eraseFirst - items) + static_cast<size_type>(currentEnd - eraseLast);
        truncate(newCount);
    }

    return eraseFirst;
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_FORCEINLINE void SmallVector<T, N>::push_back(const T& _value) noexcept
{
    emplace_back(_value);
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_FORCEINLINE void SmallVector<T, N>::push_back(T&& _value) noexcept
{
    emplace_back(std::move(_value));
}

template<typename T, std::size_t N>
template<typename... Args>
FOUNDATION_SMALLVECTOR_FORCEINLINE typename SmallVector<T, N>::reference SmallVector<T, N>::emplace_back(
    Args&&... _args) noexcept
{
    if (itemCount == itemCapacity) [[unlikely]]
    {
        return growAndEmplaceBack(std::forward<Args>(_args)...);
    }

    T* element = std::construct_at(items + itemCount, std::forward<Args>(_args)...);
    ++itemCount;
    return *element;
}

template<typename T, std::size_t N>
template<detail::ContainerCompatibleRange<T> Range>
inline void SmallVector<T, N>::append_range(Range&& _range) noexcept
{
    if constexpr (detail::CountableRange<Range>)
    {
        const size_type rangeCount = static_cast<size_type>(std::ranges::size(_range));
        appendCounted(std::ranges::begin(_range), rangeCount);
    }
    else
    {
        for (auto&& element : _range)
        {
            emplace_back(std::forward<decltype(element)>(element));
        }
    }
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_FORCEINLINE void SmallVector<T, N>::pop_back() noexcept
{
    assert(itemCount != 0);
    --itemCount;

    if constexpr (!std::is_trivially_destructible_v<T>)
    {
        std::destroy_at(items + itemCount);
    }
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::resize(size_type _count) noexcept
{
    if (_count <= itemCount)
    {
        truncate(_count);
        return;
    }

    growToFit(_count);
    uninitializedValueConstructN(items + itemCount, _count - itemCount);
    itemCount = static_cast<uint32_t>(_count);
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::resize(size_type _count, const T& _value) noexcept
{
    if (_count <= itemCount)
    {
        truncate(_count);
        return;
    }

    appendFill(_count - itemCount, _value);
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::swap(SmallVector& _other) noexcept
{
    if (this == &_other)
    {
        return;
    }

    const bool thisInline = is_inline();
    const bool otherInline = _other.is_inline();

    if (!thisInline && !otherInline)
    {
        std::swap(items, _other.items);
        std::swap(itemCount, _other.itemCount);
        std::swap(itemCapacity, _other.itemCapacity);
        return;
    }

    if (thisInline && otherInline)
    {
        if constexpr (k_UseFixedInlineCopy)
        {
            alignas(T) std::byte scratch[N * sizeof(T)];
            std::memcpy(scratch, inlineItems.Data(), N * sizeof(T));
            std::memcpy(inlineItems.Data(), _other.inlineItems.Data(), N * sizeof(T));
            std::memcpy(_other.inlineItems.Data(), scratch, N * sizeof(T));
            std::swap(itemCount, _other.itemCount);
        }
        else
        {
            SmallVector* shorter = itemCount <= _other.itemCount ? this : &_other;
            SmallVector* longer = itemCount <= _other.itemCount ? &_other : this;
            const uint32_t commonCount = shorter->itemCount;
            const uint32_t extraCount = longer->itemCount - commonCount;

            for (uint32_t index = 0; index < commonCount; ++index)
            {
                using std::swap;
                swap(shorter->items[index], longer->items[index]);
            }

            relocate(longer->items + commonCount, extraCount, shorter->items + commonCount);
            longer->itemCount = commonCount;
            shorter->itemCount = commonCount + extraCount;
        }

        return;
    }

    // exactly one side is inline: relocate its elements directly into the heap side's unused inline storage
    SmallVector* inlineSide = thisInline ? this : &_other;
    SmallVector* heapSide = thisInline ? &_other : this;

    T* heapBuffer = heapSide->items;
    const uint32_t heapCount = heapSide->itemCount;
    const uint32_t heapCapacity = heapSide->itemCapacity;
    const uint32_t inlineCount = inlineSide->itemCount;

    if constexpr (k_UseFixedInlineCopy)
    {
        std::memcpy(heapSide->inlineItems.Data(), inlineSide->inlineItems.Data(), N * sizeof(T));
    }
    else
    {
        FOUNDATION_SMALLVECTOR_ASSUME(inlineCount <= N);
        relocate(inlineSide->inlineItems.Data(), inlineCount, heapSide->inlineItems.Data());
    }

    heapSide->items = heapSide->inlineItems.Data();
    heapSide->itemCount = inlineCount;
    heapSide->itemCapacity = static_cast<uint32_t>(N);

    inlineSide->items = heapBuffer;
    inlineSide->itemCount = heapCount;
    inlineSide->itemCapacity = heapCapacity;
}
// NOLINTEND(readability-identifier-naming)

template<typename T, std::size_t N>
inline T* SmallVector<T, N>::allocate(size_type element_count) noexcept
{
    const size_type byteCount = element_count * sizeof(T);
    void* buffer = nullptr;

    // nothrow operator new avoids C++ EH unwind tables (__CxxFrameHandler3/4) inside noexcept methods under /EHsc
    if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    {
        buffer = ::operator new(byteCount, std::align_val_t{ alignof(T) }, std::nothrow);
    }
    else
    {
        buffer = ::operator new(byteCount, std::nothrow);
    }

    if (buffer == nullptr) [[unlikely]]
    {
        std::terminate();
    }

    return static_cast<T*>(buffer);
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::deallocate(T* buffer, size_type element_count) noexcept
{
    const size_type byteCount = element_count * sizeof(T);

    if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    {
        ::operator delete(buffer, byteCount, std::align_val_t{ alignof(T) });
    }
    else
    {
        ::operator delete(buffer, byteCount);
    }
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::relocate(T* source, size_type element_count, T* destination) noexcept
{
    // moves each element to disjoint uninitialized storage and ends the lifetime of the source
    if constexpr (std::is_trivially_copyable_v<T>)
    {
        // element_count is 0 with null pointers for an empty SmallVector<T, 0>, and memcpy(nullptr) is undefined
        if (element_count != 0)
        {
            std::memcpy(destination, source, element_count * sizeof(T));
        }
    }
    else
    {
        for (size_type index = 0; index < element_count; ++index)
        {
            std::construct_at(destination + index, std::move(source[index]));
            std::destroy_at(source + index);
        }
    }
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::relocateUpward(T* source, size_type element_count, T* destination) noexcept
{
    // relocate for a destination above the source, where the two ranges can overlap. walking from the back
    // means each destination slot was vacated before it is written
    if constexpr (std::is_trivially_copyable_v<T>)
    {
        if (element_count != 0)
        {
            std::memmove(destination, source, element_count * sizeof(T));
        }
    }
    else
    {
        for (std::ptrdiff_t index = static_cast<std::ptrdiff_t>(element_count) - 1; index >= 0; --index)
        {
            std::construct_at(destination + index, std::move(source[index]));
            std::destroy_at(source + index);
        }
    }
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_FORCEINLINE void SmallVector<T, N>::destroyRange(T* first, size_type element_count) noexcept
{
    if constexpr (!std::is_trivially_destructible_v<T>)
    {
        for (size_type index = 0; index < element_count; ++index)
        {
            std::destroy_at(first + index);
        }
    }
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::uninitializedValueConstructN(T* destination, size_type element_count) noexcept
{
    if constexpr (std::is_trivially_default_constructible_v<T> && !std::is_member_pointer_v<T>)
    {
        if (element_count != 0)
        {
            std::memset(destination, 0, element_count * sizeof(T));
        }
    }
    else
    {
        // plain loop instead of std::uninitialized_value_construct_n: any exception calls std::terminate via
        // noexcept, so STL's partial-destruction rollback guard only spills loop registers and bloats .xdata
        for (size_type index = 0; index < element_count; ++index)
        {
            std::construct_at(destination + index);
        }
    }
}

// replaces std::uninitialized_fill_n with a custom implementation optimized for small trivially copyable types,
// which results in much much better codegen
template<typename T, std::size_t N>
inline void SmallVector<T, N>::uninitializedFillN(T* destination, size_type element_count, const T& _value) noexcept
{
    if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 16)
    {
        // load into a local value before looping so the compiler knows destination writes cannot alias _value
        const T localValue = _value;

        for (size_type index = 0; index < element_count; ++index)
        {
            std::construct_at(destination + index, localValue);
        }
    }
    else
    {
        for (size_type index = 0; index < element_count; ++index)
        {
            std::construct_at(destination + index, _value);
        }
    }
}

// same as above: replaces std::uninitialized_copy_n with a custom implementation optimized for small trivially
// copyable types, which results in much better codegen
template<typename T, std::size_t N>
template<typename Iterator>
inline void SmallVector<T, N>::uninitializedCopyN(Iterator _source, size_type element_count, T* destination) noexcept
{
    if constexpr (detail::TrivialContiguousSource<Iterator, T>)
    {
        if (element_count != 0)
        {
            std::memcpy(destination, std::to_address(_source), element_count * sizeof(T));
        }
    }
    else if constexpr (std::random_access_iterator<Iterator>)
    {
        for (size_type index = 0; index < element_count; ++index)
        {
            std::construct_at(destination + index, _source[static_cast<std::iter_difference_t<Iterator>>(index)]);
        }
    }
    else
    {
        for (size_type index = 0; index < element_count; ++index, ++_source)
        {
            std::construct_at(destination + index, *_source);
        }
    }
}

template<typename T, std::size_t N>
inline typename SmallVector<T, N>::size_type SmallVector<T, N>::grownCapacity(size_type required_count) const noexcept
{
    constexpr size_type k_MaxSize = max_size();

    if (required_count > k_MaxSize) [[unlikely]]
    {
        std::terminate();
    }

    // avoid 0 -> 1 -> 2 -> 4 tiny heap allocations when N is 0 or very small
    constexpr size_type k_CacheLineFloor = (64 / sizeof(T)) > 4 ? (64 / sizeof(T)) : 4;
    constexpr size_type k_MinHeapCapacity = k_CacheLineFloor < k_MaxSize ? k_CacheLineFloor : k_MaxSize;

    FOUNDATION_SMALLVECTOR_ASSUME(itemCapacity >= N);
    const size_type doubledCapacity = static_cast<size_type>(itemCapacity) * 2;
    size_type targetCapacity = doubledCapacity < k_MinHeapCapacity ? k_MinHeapCapacity : doubledCapacity;
    // std::max isn't as certain to generate optimal code (i trust no one after auditing this objects .asm)
    //NOLINTNEXTLINE(readability-use-std-min-max)
    if (targetCapacity < required_count)
    {
        targetCapacity = required_count;
    }

    return targetCapacity > k_MaxSize ? k_MaxSize : targetCapacity;
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::growToFit(size_type required_count) noexcept
{
    if (required_count > itemCapacity) [[unlikely]]
    {
        reallocate(grownCapacity(required_count));
    }
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_NOINLINE void SmallVector<T, N>::allocateInitialHeap(size_type exact_capacity) noexcept
{
    if (exact_capacity > max_size()) [[unlikely]]
    {
        std::terminate();
    }

    items = allocate(exact_capacity);
    itemCapacity = static_cast<uint32_t>(exact_capacity);
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_NOINLINE void SmallVector<T, N>::reallocate(size_type new_capacity) noexcept
{
    T* buffer = allocate(new_capacity);
    relocate(items, itemCount, buffer);
    adoptBuffer(buffer, new_capacity);
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_NOINLINE T* SmallVector<T, N>::openGapReallocate(size_type gap_index, size_type gap_count) noexcept
{
    const size_type tailCount = itemCount - gap_index;
    const size_type newCount = itemCount + gap_count;
    const size_type newCapacity = grownCapacity(newCount);
    T* buffer = allocate(newCapacity);
    relocate(items, gap_index, buffer);
    relocate(items + gap_index, tailCount, buffer + gap_index + gap_count);
    adoptBuffer(buffer, newCapacity);
    itemCount = static_cast<uint32_t>(newCount);
    return buffer + gap_index;
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_NOINLINE void SmallVector<T, N>::growAndAppendFill(size_type fill_count, const T& _value) noexcept
{
    const size_type newCount = itemCount + fill_count;
    // copy first, because _value can refer to an element in the buffer that reallocate frees, as in
    // `vector.resize(vector.capacity() + 1, vector[0])`. clang-tidy flags this copy as unnecessary; a
    // reference here is a use-after-free, so keep the copy
    const T fillValue = _value;
    reallocate(grownCapacity(newCount));
    uninitializedFillN(items + itemCount, fill_count, fillValue);
    itemCount = static_cast<uint32_t>(newCount);
}

template<typename T, std::size_t N>
template<typename Iterator>
FOUNDATION_SMALLVECTOR_NOINLINE void SmallVector<T, N>::reallocateAndAssignCounted(Iterator _source,
                                                                                   size_type element_count) noexcept
{
    if (element_count > max_size()) [[unlikely]]
    {
        std::terminate();
    }

    if (element_count > itemCapacity)
    {
        T* newBuffer = allocate(element_count);
        uninitializedCopyN(std::move(_source), element_count, newBuffer);
        destroyRange(items, itemCount);
        adoptBuffer(newBuffer, element_count);
        itemCount = static_cast<uint32_t>(element_count);
        return;
    }

    // !k_CanAssign && element_count <= itemCapacity: rebuild in place after destroying live elements
    clear();
    uninitializedCopyN(std::move(_source), element_count, items);
    itemCount = static_cast<uint32_t>(element_count);
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::adoptBuffer(T* buffer, size_type buffer_capacity) noexcept
{
    releaseHeap();
    items = buffer;
    itemCapacity = static_cast<uint32_t>(buffer_capacity);
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_FORCEINLINE void SmallVector<T, N>::releaseHeap() noexcept
{
    if (!is_inline()) [[unlikely]]
    {
        deallocate(items, itemCapacity);
    }
}

template<typename T, std::size_t N>
FOUNDATION_SMALLVECTOR_FORCEINLINE void SmallVector<T, N>::resetToInline() noexcept
{
    items = inlineItems.Data();
    itemCount = 0;
    itemCapacity = static_cast<uint32_t>(N);
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::truncate(size_type new_count) noexcept
{
    destroyRange(items + new_count, itemCount - new_count);
    itemCount = static_cast<uint32_t>(new_count);
}

template<typename T, std::size_t N>
inline T* SmallVector<T, N>::openGap(size_type gap_index, size_type gap_count) noexcept
{
    // returns gap_count slots of uninitialized storage at gap_index, already counted in size()
    if (gap_count == 0)
    {
        // fixed: an empty gap used to relocate the tail onto itself, which move-constructed each element from
        // itself and then destroyed it
        return items + gap_index;
    }

    const size_type newCount = itemCount + gap_count;

    if (newCount > itemCapacity) [[unlikely]]
    {
        return openGapReallocate(gap_index, gap_count);
    }

    const size_type tailCount = itemCount - gap_index;
    relocateUpward(items + gap_index, tailCount, items + gap_index + gap_count);
    itemCount = static_cast<uint32_t>(newCount);
    return items + gap_index;
}

template<typename T, std::size_t N>
template<typename... Args>
FOUNDATION_SMALLVECTOR_NOINLINE typename SmallVector<T, N>::reference SmallVector<T, N>::growAndEmplaceBack(
    Args&&... _args) noexcept
{
    const uint32_t oldCount = itemCount;
    const size_type newCapacity = grownCapacity(static_cast<size_type>(oldCount) + 1);
    T* buffer = allocate(newCapacity);
    // construct the new element before the old ones move, so that arguments that refer into the old buffer
    // are still valid
    T* element = std::construct_at(buffer + oldCount, std::forward<Args>(_args)...);
    relocate(items, oldCount, buffer);
    adoptBuffer(buffer, newCapacity);
    itemCount = oldCount + 1;
    return *element;
}

template<typename T, std::size_t N>
template<typename Iterator>
inline void SmallVector<T, N>::initFromCounted(Iterator _source, size_type element_count) noexcept
{
    if (element_count > N) [[unlikely]]
    {
        allocateInitialHeap(element_count);
    }

    uninitializedCopyN(std::move(_source), element_count, items);
    itemCount = static_cast<uint32_t>(element_count);
}

template<typename T, std::size_t N>
template<typename Iterator>
inline void SmallVector<T, N>::appendCounted(Iterator _source, size_type element_count) noexcept
{
    const size_type newCount = itemCount + element_count;
    growToFit(newCount);
    uninitializedCopyN(std::move(_source), element_count, items + itemCount);
    itemCount = static_cast<uint32_t>(newCount);
}

template<typename T, std::size_t N>
template<typename Iterator>
inline void SmallVector<T, N>::assignCounted(Iterator _source, size_type element_count) noexcept
{
    // copy-assign over the live elements where T allows it, so that elements that own memory reuse it
    constexpr bool k_CanAssign = std::is_assignable_v<T&, std::iter_reference_t<Iterator>>;

    if (!k_CanAssign || element_count > itemCapacity) [[unlikely]]
    {
        reallocateAndAssignCounted(std::move(_source), element_count);
        return;
    }

    if constexpr (std::is_trivially_copyable_v<T>)
    {
        uninitializedCopyN(std::move(_source), element_count, items);
        itemCount = static_cast<uint32_t>(element_count);
    }
    else if constexpr (k_CanAssign)
    {
        const size_type assignedCount = std::min<size_type>(element_count, itemCount);
        const std::iter_difference_t<Iterator> assignedDifference =
            static_cast<std::iter_difference_t<Iterator>>(assignedCount);
        Iterator rest = std::ranges::copy_n(std::move(_source), assignedDifference, items).in;

        if (element_count > itemCount)
        {
            uninitializedCopyN(std::move(rest), element_count - itemCount, items + itemCount);
            itemCount = static_cast<uint32_t>(element_count);
            return;
        }

        truncate(element_count);
    }
}

template<typename T, std::size_t N>
inline void SmallVector<T, N>::appendFill(size_type fill_count, const T& _value) noexcept
{
    const size_type newCount = itemCount + fill_count;

    if (newCount > itemCapacity) [[unlikely]]
    {
        growAndAppendFill(fill_count, _value);
        return;
    }

    uninitializedFillN(items + itemCount, fill_count, _value);
    itemCount = static_cast<uint32_t>(newCount);
}

// NOLINTBEGIN(readability-identifier-naming)
template<typename T, std::size_t N, typename U>
inline typename SmallVector<T, N>::size_type erase(SmallVector<T, N>& vector, const U& value) noexcept
{
    const std::ranges::subrange removed = std::ranges::remove(vector, value);
    const typename SmallVector<T, N>::size_type removedCount = removed.size();

    if (removedCount != 0)
    {
        vector.erase(removed.begin(), removed.end());
    }

    return removedCount;
}

template<typename T, std::size_t N, typename Predicate>
inline typename SmallVector<T, N>::size_type erase_if(SmallVector<T, N>& vector, Predicate predicate) noexcept
{
    const std::ranges::subrange removed = std::ranges::remove_if(vector, std::move(predicate));
    const typename SmallVector<T, N>::size_type removedCount = removed.size();

    if (removedCount != 0)
    {
        vector.erase(removed.begin(), removed.end());
    }

    return removedCount;
}
// NOLINTEND(readability-identifier-naming)

// the range contract. std::ranges::to builds a SmallVector through the std::from_range_t constructor
static_assert(std::ranges::contiguous_range<SmallVector<int32_t, 4>>);
static_assert(std::ranges::sized_range<SmallVector<int32_t, 4>>);
static_assert(std::same_as<decltype(std::views::iota(0, 4) | std::ranges::to<SmallVector<int32_t, 4>>()),
                           SmallVector<int32_t, 4>>);
static_assert(sizeof(SmallVector<int32_t, 0>) == sizeof(int32_t*) + 2 * sizeof(uint32_t));

}

#undef FOUNDATION_SMALLVECTOR_ASSUME
#undef FOUNDATION_SMALLVECTOR_NOINLINE
#undef FOUNDATION_SMALLVECTOR_FORCEINLINE
#undef FOUNDATION_SMALLVECTOR_NO_UNIQUE_ADDRESS

#endif // !FOUNDATION_CONTAINERS_SMALLVECTOR_HPP
