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

namespace foundation
{

namespace detail
{
    // the anonymous union stops the compiler from constructing or destroying the elements, so that
    // SmallVector controls the lifetime of each element
    template<typename T, std::size_t N>
    struct SmallVectorInlineStorage
    {
        SmallVectorInlineStorage() noexcept
        {
        }

        ~SmallVectorInlineStorage() noexcept
        {
        }

        [[nodiscard]] T* Data() noexcept
        {
            return Elements;
        }

        [[nodiscard]] const T* Data() const noexcept
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
        [[nodiscard]] T* Data() noexcept
        {
            return nullptr;
        }

        [[nodiscard]] const T* Data() const noexcept
        {
            return nullptr;
        }
    };

    template<typename Range, typename T>
    concept ContainerCompatibleRange =
        std::ranges::input_range<Range> && std::convertible_to<std::ranges::range_reference_t<Range>, T>;

    // a range whose length we can learn before we copy it, so that we allocate once
    template<typename Range>
    concept CountableRange = std::ranges::forward_range<Range> || std::ranges::sized_range<Range>;
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

    SmallVector() noexcept;
    explicit SmallVector(size_type _count) noexcept;
    SmallVector(size_type _count, const T& _value) noexcept;
    template<std::input_iterator InputIt>
    SmallVector(InputIt _first, InputIt _last) noexcept;
    SmallVector(std::initializer_list<T> _list) noexcept;
    template<detail::ContainerCompatibleRange<T> Range>
    SmallVector(std::from_range_t, Range&& _range) noexcept;
    SmallVector(const SmallVector& _other) noexcept
        requires std::is_copy_constructible_v<T>;
    SmallVector(SmallVector&& _other) noexcept;
    ~SmallVector();

    SmallVector& operator=(const SmallVector& _other) noexcept
        requires std::is_copy_constructible_v<T>;
    SmallVector& operator=(SmallVector&& _other) noexcept;
    SmallVector& operator=(std::initializer_list<T> _list) noexcept;

    void assign(size_type _count, const T& _value) noexcept;
    template<std::input_iterator InputIt>
    void assign(InputIt _first, InputIt _last) noexcept;
    void assign(std::initializer_list<T> _list) noexcept;
    template<detail::ContainerCompatibleRange<T> Range>
    void assign_range(Range&& _range) noexcept;

    // at() terminates on an index out of range, where std::vector throws std::out_of_range
    [[nodiscard]] reference at(size_type _index) noexcept;
    [[nodiscard]] const_reference at(size_type _index) const noexcept;
    [[nodiscard]] reference operator[](size_type _index) noexcept;
    [[nodiscard]] const_reference operator[](size_type _index) const noexcept;
    [[nodiscard]] reference front() noexcept;
    [[nodiscard]] const_reference front() const noexcept;
    [[nodiscard]] reference back() noexcept;
    [[nodiscard]] const_reference back() const noexcept;
    [[nodiscard]] T* data() noexcept;
    [[nodiscard]] const T* data() const noexcept;

    [[nodiscard]] iterator begin() noexcept;
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator cbegin() const noexcept;
    [[nodiscard]] iterator end() noexcept;
    [[nodiscard]] const_iterator end() const noexcept;
    [[nodiscard]] const_iterator cend() const noexcept;
    [[nodiscard]] reverse_iterator rbegin() noexcept;
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept;
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept;
    [[nodiscard]] reverse_iterator rend() noexcept;
    [[nodiscard]] const_reverse_iterator rend() const noexcept;
    [[nodiscard]] const_reverse_iterator crend() const noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] constexpr size_type max_size() const noexcept;
    [[nodiscard]] size_type capacity() const noexcept;
    // not in std::vector: true while the elements are in the inline buffer
    [[nodiscard]] bool is_inline() const noexcept;
    void reserve(size_type new_capacity) noexcept;
    void shrink_to_fit() noexcept;

    void clear() noexcept;
    iterator insert(const_iterator _position, const T& _value) noexcept;
    iterator insert(const_iterator _position, T&& _value) noexcept;
    iterator insert(const_iterator _position, size_type _count, const T& _value) noexcept;
    template<std::input_iterator InputIt>
    iterator insert(const_iterator _position, InputIt _first, InputIt _last) noexcept;
    iterator insert(const_iterator _position, std::initializer_list<T> _list) noexcept;
    template<detail::ContainerCompatibleRange<T> Range>
    iterator insert_range(const_iterator _position, Range&& _range) noexcept;
    template<typename... Args>
    iterator emplace(const_iterator _position, Args&&... _args) noexcept;
    iterator erase(const_iterator _position) noexcept;
    iterator erase(const_iterator _first, const_iterator _last) noexcept;
    void push_back(const T& _value) noexcept;
    void push_back(T&& _value) noexcept;
    template<typename... Args>
    reference emplace_back(Args&&... _args) noexcept;
    template<detail::ContainerCompatibleRange<T> Range>
    void append_range(Range&& _range) noexcept;
    void pop_back() noexcept;
    void resize(size_type _count) noexcept;
    void resize(size_type _count, const T& _value) noexcept;
    void swap(SmallVector& _other) noexcept;

    // hidden friends, so that these are found only through ADL on a SmallVector
    friend bool operator==(const SmallVector& left, const SmallVector& right) noexcept
        requires std::equality_comparable<T>
    {
        return std::ranges::equal(left, right);
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
    [[nodiscard]] static T* allocate(size_type element_count) noexcept;
    static void deallocate(T* buffer, size_type element_count) noexcept;
    static void relocate(T* source, size_type element_count, T* destination) noexcept;
    static void relocateUpward(T* source, size_type element_count, T* destination) noexcept;

    [[nodiscard]] size_type grownCapacity(size_type required_count) const noexcept;
    void growToFit(size_type required_count) noexcept;
    void reallocate(size_type new_capacity) noexcept;
    void adoptBuffer(T* buffer, size_type buffer_capacity) noexcept;
    void releaseHeap() noexcept;
    void resetToInline() noexcept;
    void moveContentsFrom(SmallVector& _other) noexcept;
    void truncate(size_type new_count) noexcept;
    [[nodiscard]] T* openGap(size_type gap_index, size_type gap_count) noexcept;
    template<typename... Args>
    reference growAndEmplaceBack(Args&&... _args) noexcept;
    template<typename Iterator>
    void appendCounted(Iterator _source, size_type element_count) noexcept;
    template<typename Iterator>
    void assignCounted(Iterator _source, size_type element_count) noexcept;
    void appendFill(size_type fill_count, const T& _value) noexcept;

    // items points to the inline buffer or to the heap buffer. a self-pointer costs a fixup on move, and in
    // return data() and every element access never branch on where the elements are. every constructor points
    // items at the inline buffer in its body, so nullptr marks an object that is not constructed yet
    T* items{ nullptr };
    uint32_t itemCount{ 0 };
    uint32_t itemCapacity{ 0 };
    // not initialized on purpose: the union must not construct any element
    detail::SmallVectorInlineStorage<T, N> inlineItems;
};

// std::vector names, as in the class declaration
// NOLINTBEGIN(readability-identifier-naming)
/** @brief Erases every element equal to value. Returns the number of erased elements. This is the SmallVector
 * version of std::erase, and callers find it through ADL: write `erase(vector, value)`, not `std::erase`. */
template<typename T, std::size_t N, typename U>
typename SmallVector<T, N>::size_type erase(SmallVector<T, N>& vector, const U& value) noexcept;

/** @brief Erases every element that satisfies the predicate. Returns the number of erased elements. Callers find it
 * through ADL, as with erase. */
template<typename T, std::size_t N, typename Predicate>
typename SmallVector<T, N>::size_type erase_if(SmallVector<T, N>& vector, Predicate predicate) noexcept;


template<typename T, std::size_t N>
SmallVector<T, N>::SmallVector() noexcept
{
    // the members are set in the body, because items points into inlineItems, which a member initializer must
    // not touch before it is constructed
    resetToInline();
}

template<typename T, std::size_t N>
SmallVector<T, N>::SmallVector(size_type _count) noexcept
{
    resetToInline();
    resize(_count);
}

template<typename T, std::size_t N>
SmallVector<T, N>::SmallVector(size_type _count, const T& _value) noexcept
{
    resetToInline();
    appendFill(_count, _value);
}

template<typename T, std::size_t N>
template<std::input_iterator InputIt>
SmallVector<T, N>::SmallVector(InputIt _first, InputIt _last) noexcept
{
    resetToInline();
    assign_range(std::ranges::subrange(std::move(_first), std::move(_last)));
}

template<typename T, std::size_t N>
SmallVector<T, N>::SmallVector(std::initializer_list<T> _list) noexcept
{
    resetToInline();
    assignCounted(_list.begin(), _list.size());
}

template<typename T, std::size_t N>
template<detail::ContainerCompatibleRange<T> Range>
SmallVector<T, N>::SmallVector(std::from_range_t, Range&& _range) noexcept
{
    resetToInline();
    assign_range(std::forward<Range>(_range));
}

template<typename T, std::size_t N>
SmallVector<T, N>::SmallVector(const SmallVector& _other) noexcept
    requires std::is_copy_constructible_v<T>
{
    resetToInline();
    assignCounted(_other.items, _other.size());
}

template<typename T, std::size_t N>
SmallVector<T, N>::SmallVector(SmallVector&& _other) noexcept
{
    resetToInline();
    moveContentsFrom(_other);
}

template<typename T, std::size_t N>
SmallVector<T, N>::~SmallVector()
{
    std::destroy_n(items, itemCount);
    releaseHeap();
}

template<typename T, std::size_t N>
SmallVector<T, N>& SmallVector<T, N>::operator=(const SmallVector& _other) noexcept
    requires std::is_copy_constructible_v<T>
{
    if (this != &_other)
    {
        assignCounted(_other.items, _other.size());
    }

    return *this;
}

template<typename T, std::size_t N>
SmallVector<T, N>& SmallVector<T, N>::operator=(SmallVector&& _other) noexcept
{
    if (this != &_other)
    {
        clear();
        moveContentsFrom(_other);
    }

    return *this;
}

template<typename T, std::size_t N>
SmallVector<T, N>& SmallVector<T, N>::operator=(std::initializer_list<T> _list) noexcept
{
    assignCounted(_list.begin(), _list.size());
    return *this;
}

template<typename T, std::size_t N>
void SmallVector<T, N>::assign(size_type _count, const T& _value) noexcept
{
    // copy first, because _value can refer to an element that this call overwrites or destroys, as in
    // `vector.assign(4, vector[0])`. clang-tidy flags this copy as unnecessary; a reference here is a
    // use-after-free, so keep the copy
    const T fillValue = _value;

    if (_count > itemCapacity)
    {
        clear();
        reserve(_count);
        appendFill(_count, fillValue);
        return;
    }

    const size_type assignedCount = std::min<size_type>(_count, itemCount);
    std::fill_n(items, assignedCount, fillValue);

    if (_count > itemCount)
    {
        appendFill(_count - itemCount, fillValue);
        return;
    }

    truncate(_count);
}

template<typename T, std::size_t N>
template<std::input_iterator InputIt>
void SmallVector<T, N>::assign(InputIt _first, InputIt _last) noexcept
{
    assign_range(std::ranges::subrange(std::move(_first), std::move(_last)));
}

template<typename T, std::size_t N>
void SmallVector<T, N>::assign(std::initializer_list<T> _list) noexcept
{
    assignCounted(_list.begin(), _list.size());
}

template<typename T, std::size_t N>
template<detail::ContainerCompatibleRange<T> Range>
void SmallVector<T, N>::assign_range(Range&& _range) noexcept
{
    if constexpr (detail::CountableRange<Range>)
    {
        const size_type rangeCount = static_cast<size_type>(std::ranges::distance(_range));
        assignCounted(std::ranges::begin(_range), rangeCount);
    }
    else
    {
        clear();
        append_range(std::forward<Range>(_range));
    }
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::reference SmallVector<T, N>::at(size_type _index) noexcept
{
    if (_index >= itemCount) [[unlikely]]
    {
        std::terminate();
    }

    return items[_index];
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_reference SmallVector<T, N>::at(size_type _index) const noexcept
{
    if (_index >= itemCount) [[unlikely]]
    {
        std::terminate();
    }

    return items[_index];
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::reference SmallVector<T, N>::operator[](size_type _index) noexcept
{
    assert(_index < itemCount);
    return items[_index];
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_reference SmallVector<T, N>::operator[](size_type _index) const noexcept
{
    assert(_index < itemCount);
    return items[_index];
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::reference SmallVector<T, N>::front() noexcept
{
    assert(itemCount != 0);
    return items[0];
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_reference SmallVector<T, N>::front() const noexcept
{
    assert(itemCount != 0);
    return items[0];
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::reference SmallVector<T, N>::back() noexcept
{
    assert(itemCount != 0);
    return items[itemCount - 1];
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_reference SmallVector<T, N>::back() const noexcept
{
    assert(itemCount != 0);
    return items[itemCount - 1];
}

template<typename T, std::size_t N>
T* SmallVector<T, N>::data() noexcept
{
    return items;
}

template<typename T, std::size_t N>
const T* SmallVector<T, N>::data() const noexcept
{
    return items;
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::iterator SmallVector<T, N>::begin() noexcept
{
    return items;
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_iterator SmallVector<T, N>::begin() const noexcept
{
    return items;
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_iterator SmallVector<T, N>::cbegin() const noexcept
{
    return items;
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::iterator SmallVector<T, N>::end() noexcept
{
    return items + itemCount;
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_iterator SmallVector<T, N>::end() const noexcept
{
    return items + itemCount;
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_iterator SmallVector<T, N>::cend() const noexcept
{
    return items + itemCount;
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::reverse_iterator SmallVector<T, N>::rbegin() noexcept
{
    return reverse_iterator(end());
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_reverse_iterator SmallVector<T, N>::rbegin() const noexcept
{
    return const_reverse_iterator(end());
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_reverse_iterator SmallVector<T, N>::crbegin() const noexcept
{
    return const_reverse_iterator(end());
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::reverse_iterator SmallVector<T, N>::rend() noexcept
{
    return reverse_iterator(begin());
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_reverse_iterator SmallVector<T, N>::rend() const noexcept
{
    return const_reverse_iterator(begin());
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_reverse_iterator SmallVector<T, N>::crend() const noexcept
{
    return const_reverse_iterator(begin());
}

template<typename T, std::size_t N>
bool SmallVector<T, N>::empty() const noexcept
{
    return itemCount == 0;
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::size_type SmallVector<T, N>::size() const noexcept
{
    return itemCount;
}

template<typename T, std::size_t N>
constexpr typename SmallVector<T, N>::size_type SmallVector<T, N>::max_size() const noexcept
{
    constexpr size_type k_CountLimit = std::numeric_limits<uint32_t>::max();
    constexpr size_type k_ByteLimit =
        static_cast<size_type>(std::numeric_limits<difference_type>::max()) / sizeof(T);
    return std::min(k_CountLimit, k_ByteLimit);
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::size_type SmallVector<T, N>::capacity() const noexcept
{
    return itemCapacity;
}

template<typename T, std::size_t N>
bool SmallVector<T, N>::is_inline() const noexcept
{
    return items == inlineItems.Data();
}

template<typename T, std::size_t N>
void SmallVector<T, N>::reserve(size_type new_capacity) noexcept
{
    if (new_capacity > itemCapacity)
    {
        reallocate(new_capacity);
    }
}

template<typename T, std::size_t N>
void SmallVector<T, N>::shrink_to_fit() noexcept
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
void SmallVector<T, N>::clear() noexcept
{
    std::destroy_n(items, itemCount);
    itemCount = 0;
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::iterator SmallVector<T, N>::insert(const_iterator _position, const T& _value) noexcept
{
    return emplace(_position, _value);
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::iterator SmallVector<T, N>::insert(const_iterator _position, T&& _value) noexcept
{
    return emplace(_position, std::move(_value));
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::iterator SmallVector<T, N>::insert(const_iterator _position,
                                                               size_type _count,
                                                               const T& _value) noexcept
{
    const size_type insertIndex = static_cast<size_type>(_position - items);
    // copy first, because _value can refer to an element that openGap moves, as in
    // `vector.insert(vector.begin(), 2, vector.back())`. clang-tidy flags this copy as unnecessary; a reference
    // here reads a moved-from or destroyed element, so keep the copy
    const T fillValue = _value;
    T* gap = openGap(insertIndex, _count);
    std::uninitialized_fill_n(gap, _count, fillValue);
    return gap;
}

template<typename T, std::size_t N>
template<std::input_iterator InputIt>
typename SmallVector<T, N>::iterator SmallVector<T, N>::insert(const_iterator _position,
                                                               InputIt _first,
                                                               InputIt _last) noexcept
{
    return insert_range(_position, std::ranges::subrange(std::move(_first), std::move(_last)));
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::iterator SmallVector<T, N>::insert(const_iterator _position,
                                                               std::initializer_list<T> _list) noexcept
{
    return insert_range(_position, _list);
}

template<typename T, std::size_t N>
template<detail::ContainerCompatibleRange<T> Range>
typename SmallVector<T, N>::iterator SmallVector<T, N>::insert_range(const_iterator _position,
                                                                     Range&& _range) noexcept
{
    const size_type insertIndex = static_cast<size_type>(_position - items);

    if constexpr (detail::CountableRange<Range>)
    {
        const size_type insertCount = static_cast<size_type>(std::ranges::distance(_range));
        T* gap = openGap(insertIndex, insertCount);
        std::ranges::uninitialized_copy_n(std::ranges::begin(_range),
                                          static_cast<std::ranges::range_difference_t<Range>>(insertCount),
                                          gap,
                                          gap + insertCount);
        return gap;
    }
    else
    {
        // a single-pass range has no length up front: append it, then rotate it into place
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
typename SmallVector<T, N>::iterator SmallVector<T, N>::emplace(const_iterator _position, Args&&... _args) noexcept
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
typename SmallVector<T, N>::iterator SmallVector<T, N>::erase(const_iterator _position) noexcept
{
    return erase(_position, _position + 1);
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::iterator SmallVector<T, N>::erase(const_iterator _first, const_iterator _last) noexcept
{
    T* eraseFirst = items + (_first - items);
    T* eraseLast = items + (_last - items);

    if (eraseFirst != eraseLast)
    {
        T* newEnd = std::move(eraseLast, end(), eraseFirst);
        truncate(static_cast<size_type>(newEnd - items));
    }

    return eraseFirst;
}

template<typename T, std::size_t N>
void SmallVector<T, N>::push_back(const T& _value) noexcept
{
    emplace_back(_value);
}

template<typename T, std::size_t N>
void SmallVector<T, N>::push_back(T&& _value) noexcept
{
    emplace_back(std::move(_value));
}

template<typename T, std::size_t N>
template<typename... Args>
typename SmallVector<T, N>::reference SmallVector<T, N>::emplace_back(Args&&... _args) noexcept
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
void SmallVector<T, N>::append_range(Range&& _range) noexcept
{
    if constexpr (detail::CountableRange<Range>)
    {
        const size_type rangeCount = static_cast<size_type>(std::ranges::distance(_range));
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
void SmallVector<T, N>::pop_back() noexcept
{
    assert(itemCount != 0);
    --itemCount;
    std::destroy_at(items + itemCount);
}

template<typename T, std::size_t N>
void SmallVector<T, N>::resize(size_type _count) noexcept
{
    if (_count <= itemCount)
    {
        truncate(_count);
        return;
    }

    growToFit(_count);
    std::uninitialized_value_construct_n(items + itemCount, _count - itemCount);
    itemCount = static_cast<uint32_t>(_count);
}

template<typename T, std::size_t N>
void SmallVector<T, N>::resize(size_type _count, const T& _value) noexcept
{
    if (_count <= itemCount)
    {
        truncate(_count);
        return;
    }

    appendFill(_count - itemCount, _value);
}

template<typename T, std::size_t N>
void SmallVector<T, N>::swap(SmallVector& _other) noexcept
{
    if (this == &_other)
    {
        return;
    }

    if (!is_inline() && !_other.is_inline())
    {
        std::swap(items, _other.items);
        std::swap(itemCount, _other.itemCount);
        std::swap(itemCapacity, _other.itemCapacity);
        return;
    }

    // at least one side is inline, so its elements must move one by one
    SmallVector temporary(std::move(_other));
    _other = std::move(*this);
    *this = std::move(temporary);
}
// NOLINTEND(readability-identifier-naming)

template<typename T, std::size_t N>
T* SmallVector<T, N>::allocate(size_type element_count) noexcept
{
    const size_type byteCount = element_count * sizeof(T);

    if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    {
        return static_cast<T*>(::operator new(byteCount, std::align_val_t{ alignof(T) }));
    }
    else
    {
        return static_cast<T*>(::operator new(byteCount));
    }
}

template<typename T, std::size_t N>
void SmallVector<T, N>::deallocate(T* buffer, size_type element_count) noexcept
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
void SmallVector<T, N>::relocate(T* source, size_type element_count, T* destination) noexcept
{
    // moves each element to the destination and ends the lifetime of the source. the destination is
    // uninitialized storage that does not overlap the source, or that starts below it
    if constexpr (std::is_trivially_copyable_v<T>)
    {
        // element_count is 0 with null pointers for an empty SmallVector<T, 0>, and memmove(nullptr) is undefined
        if (element_count != 0)
        {
            std::memmove(destination, source, element_count * sizeof(T));
        }
    }
    else
    {
        const std::ptrdiff_t signedCount = static_cast<std::ptrdiff_t>(element_count);

        for (std::ptrdiff_t index = 0; index < signedCount; ++index)
        {
            std::construct_at(destination + index, std::move(source[index]));
            std::destroy_at(source + index);
        }
    }
}

template<typename T, std::size_t N>
void SmallVector<T, N>::relocateUpward(T* source, size_type element_count, T* destination) noexcept
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
typename SmallVector<T, N>::size_type SmallVector<T, N>::grownCapacity(size_type required_count) const noexcept
{
    if (required_count > max_size()) [[unlikely]]
    {
        std::terminate();
    }

    const size_type doubledCapacity = static_cast<size_type>(itemCapacity) * 2;
    return std::clamp(doubledCapacity, required_count, max_size());
}

template<typename T, std::size_t N>
void SmallVector<T, N>::growToFit(size_type required_count) noexcept
{
    if (required_count > itemCapacity)
    {
        reallocate(grownCapacity(required_count));
    }
}

template<typename T, std::size_t N>
void SmallVector<T, N>::reallocate(size_type new_capacity) noexcept
{
    T* buffer = allocate(new_capacity);
    relocate(items, itemCount, buffer);
    adoptBuffer(buffer, new_capacity);
}

template<typename T, std::size_t N>
void SmallVector<T, N>::adoptBuffer(T* buffer, size_type buffer_capacity) noexcept
{
    releaseHeap();
    items = buffer;
    itemCapacity = static_cast<uint32_t>(buffer_capacity);
}

template<typename T, std::size_t N>
void SmallVector<T, N>::releaseHeap() noexcept
{
    if (!is_inline())
    {
        deallocate(items, itemCapacity);
    }
}

template<typename T, std::size_t N>
void SmallVector<T, N>::resetToInline() noexcept
{
    items = inlineItems.Data();
    itemCount = 0;
    itemCapacity = static_cast<uint32_t>(N);
}

template<typename T, std::size_t N>
void SmallVector<T, N>::moveContentsFrom(SmallVector& _other) noexcept
{
    // requires this to be empty. a heap buffer is stolen whole; inline elements relocate one by one, and they
    // always fit, because this has at least the inline capacity
    if (!_other.is_inline())
    {
        adoptBuffer(_other.items, _other.itemCapacity);
        itemCount = _other.itemCount;
        _other.resetToInline();
        return;
    }

    relocate(_other.items, _other.itemCount, items);
    itemCount = _other.itemCount;
    _other.itemCount = 0;
}

template<typename T, std::size_t N>
void SmallVector<T, N>::truncate(size_type new_count) noexcept
{
    std::destroy(items + new_count, items + itemCount);
    itemCount = static_cast<uint32_t>(new_count);
}

template<typename T, std::size_t N>
T* SmallVector<T, N>::openGap(size_type gap_index, size_type gap_count) noexcept
{
    // returns gap_count slots of uninitialized storage at gap_index, already counted in size()
    if (gap_count == 0)
    {
        // fixed: an empty gap used to relocate the tail onto itself, which move-constructed each element from
        // itself and then destroyed it
        return items + gap_index;
    }

    const size_type tailCount = itemCount - gap_index;
    const size_type newCount = itemCount + gap_count;

    if (newCount > itemCapacity)
    {
        const size_type newCapacity = grownCapacity(newCount);
        T* buffer = allocate(newCapacity);
        relocate(items, gap_index, buffer);
        relocate(items + gap_index, tailCount, buffer + gap_index + gap_count);
        adoptBuffer(buffer, newCapacity);
    }
    else
    {
        relocateUpward(items + gap_index, tailCount, items + gap_index + gap_count);
    }

    itemCount = static_cast<uint32_t>(newCount);
    return items + gap_index;
}

template<typename T, std::size_t N>
template<typename... Args>
typename SmallVector<T, N>::reference SmallVector<T, N>::growAndEmplaceBack(Args&&... _args) noexcept
{
    const size_type newCapacity = grownCapacity(static_cast<size_type>(itemCount) + 1);
    T* buffer = allocate(newCapacity);
    // construct the new element before the old ones move, so that arguments that refer into the old buffer
    // are still valid
    T* element = std::construct_at(buffer + itemCount, std::forward<Args>(_args)...);
    relocate(items, itemCount, buffer);
    adoptBuffer(buffer, newCapacity);
    ++itemCount;
    return *element;
}

template<typename T, std::size_t N>
template<typename Iterator>
void SmallVector<T, N>::appendCounted(Iterator _source, size_type element_count) noexcept
{
    const size_type newCount = itemCount + element_count;
    growToFit(newCount);
    std::ranges::uninitialized_copy_n(std::move(_source),
                                      static_cast<std::iter_difference_t<Iterator>>(element_count),
                                      items + itemCount,
                                      items + newCount);
    itemCount = static_cast<uint32_t>(newCount);
}

template<typename T, std::size_t N>
template<typename Iterator>
void SmallVector<T, N>::assignCounted(Iterator _source, size_type element_count) noexcept
{
    // copy-assign over the live elements where T allows it, so that elements that own memory reuse it
    constexpr bool k_CanAssign = std::is_assignable_v<T&, std::iter_reference_t<Iterator>>;

    if (!k_CanAssign || element_count > itemCapacity)
    {
        clear();
        reserve(element_count);
        appendCounted(std::move(_source), element_count);
        return;
    }

    if constexpr (k_CanAssign)
    {
        const size_type assignedCount = std::min<size_type>(element_count, itemCount);
        const std::iter_difference_t<Iterator> assignedDifference = static_cast<std::iter_difference_t<Iterator>>(assignedCount);
        Iterator rest = std::ranges::copy_n(std::move(_source), assignedDifference, items).in;

        if (element_count > itemCount)
        {
            appendCounted(std::move(rest), element_count - itemCount);
            return;
        }

        truncate(element_count);
    }
}

template<typename T, std::size_t N>
void SmallVector<T, N>::appendFill(size_type fill_count, const T& _value) noexcept
{
    const size_type newCount = itemCount + fill_count;

    if (newCount > itemCapacity)
    {
        // copy first, because _value can refer to an element in the buffer that reallocate frees, as in
        // `vector.resize(vector.capacity() + 1, vector[0])`. clang-tidy flags this copy as unnecessary; a
        // reference here is a use-after-free, so keep the copy
        const T fillValue = _value;
        reallocate(grownCapacity(newCount));
        std::uninitialized_fill_n(items + itemCount, fill_count, fillValue);
    }
    else
    {
        std::uninitialized_fill_n(items + itemCount, fill_count, _value);
    }

    itemCount = static_cast<uint32_t>(newCount);
}

// NOLINTBEGIN(readability-identifier-naming)
template<typename T, std::size_t N, typename U>
typename SmallVector<T, N>::size_type erase(SmallVector<T, N>& vector, const U& value) noexcept
{
    const std::ranges::subrange removed = std::ranges::remove(vector, value);
    const typename SmallVector<T, N>::size_type removedCount = removed.size();
    vector.erase(removed.begin(), removed.end());
    return removedCount;
}

template<typename T, std::size_t N, typename Predicate>
typename SmallVector<T, N>::size_type erase_if(SmallVector<T, N>& vector, Predicate predicate) noexcept
{
    const std::ranges::subrange removed = std::ranges::remove_if(vector, std::move(predicate));
    const typename SmallVector<T, N>::size_type removedCount = removed.size();
    vector.erase(removed.begin(), removed.end());
    return removedCount;
}
// NOLINTEND(readability-identifier-naming)

// the range contract. std::ranges::to builds a SmallVector through the std::from_range_t constructor
static_assert(std::ranges::contiguous_range<SmallVector<int32_t, 4>>);
static_assert(std::ranges::sized_range<SmallVector<int32_t, 4>>);
static_assert(std::same_as<decltype(std::views::iota(0, 4) | std::ranges::to<SmallVector<int32_t, 4>>()),
                           SmallVector<int32_t, 4>>);

}

#endif // !FOUNDATION_CONTAINERS_SMALLVECTOR_HPP
