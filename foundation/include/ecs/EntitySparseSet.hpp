#pragma once
#include <vector>
#include <memory>
#include <type_traits>
#include <cassert>
#include "Entity.hpp"

namespace ecs
{

    constexpr auto ECS_PAGE_SIZE = 32768u;

    class EntitySparseSet
    {
        static_assert(ECS_PAGE_SIZE && ((ECS_PAGE_SIZE & (ECS_PAGE_SIZE - 1))) == 0);
        static constexpr auto ENTITIES_PER_PAGE = ECS_PAGE_SIZE / sizeof(Entity);

        class Iterator
        {
            friend class EntitySparseSet;
            using storage_type = const std::vector<Entity>;
            using index_type = EntityTraits::DifferenceType;

            Iterator(storage_type* ptr, const index_type _index) noexcept : data{ ptr }, index{ _index } {}

            storage_type* data;
            index_type index;

        public:

            using difference_type = index_type;
            using value_type = Entity;
            using pointer = const value_type*;
            using reference = const value_type&;
            using iterator_category = std::random_access_iterator_tag;

            Iterator() noexcept = default;

            Iterator& operator++() noexcept
            {
                --index;
                return *this;
            }

            Iterator& operator++(int) noexcept
            {
                Iterator original = *this;
                ++(*this);
                return original;
            }

            Iterator& operator--() noexcept
            {
                ++index;
                return *this;
            }

            Iterator& operator--() noexcept
            {
                Iterator original = *this;
                --(*this);
                return original;
            }

            Iterator& operator+=(const difference_type amount) noexcept
            {
                index -= amount;
                return *this;
            }

            Iterator operator+(const difference_type amount) const noexcept
            {
                return Iterator{ data, index - amount };
            }

            Iterator& operator-=(const difference_type amount) noexcept
            {
                index += amount;
                return *this;
            }

            Iterator operator-(const difference_type amount) const noexcept
            {
                return Iterator{ data, index + amount };
            }

            difference_type operator-(const Iterator& other) const noexcept
            {
                return other.index - index;
            }

            reference operator[](const difference_type value) const noexcept
            {
                const auto pos = static_cast<std::size_t>(index - value - 1);
                return (*data)[pos];
            }

            bool operator==(const Iterator& other) const noexcept
            {
                return other.index == index;
            }

            bool operator!=(const Iterator& other) const noexcept
            {
                return other.index != index;
            }

            bool operator<(const Iterator& other) const noexcept
            {
                return index > other.index;
            }

            bool operator>(const Iterator& other) const noexcept
            {
                return index < other.index;
            }

            bool operator<=(const Iterator& other) const noexcept
            {
                return !(*this > other);
            }

            bool operator>=(const Iterator& other) const noexcept
            {
                return !(*this < other);
            }

            pointer operator->() const noexcept
            {
                const auto pos = static_cast<std::size_t>(index - 1);
                return &(*data)[pos]; // this sucks lol
            }

            reference operator*() const noexcept
            {
                return *operator->();
            }

        };

    public:

        using size_type = std::size_t;
        using sort_fn_t = bool(*)(const Entity ent0, const Entity ent1);

        EntitySparseSet() noexcept = default;
        EntitySparseSet(const EntitySparseSet& other);

        virtual void Reserve(const size_type capacity);
        virtual void ShrinkToFit() noexcept;
        bool IsEmpty() const noexcept;
        const Entity* Data() const noexcept;
        Iterator Begin() const noexcept;
        Iterator End() const noexcept;
        bool Contains(const Entity ent) const noexcept;
        size_type Get(const Entity ent) const noexcept;
        void Construct(const Entity ent);
        virtual void Destroy(const Entity ent);
        Iterator Find(const Entity ent) const noexcept;
        virtual void Reset();
        size_type Size() const noexcept;
        size_type Extent() const noexcept;
        virtual void Swap(const size_t lhs, const size_t rhs) noexcept;
        // sort_function can be null: just uses std::less
        void Sort(Iterator first, Iterator last, sort_fn_t sort_function);

    private:

        void assure(const std::size_t page);
        auto index(const Entity entity) const noexcept;

        std::vector<Entity> direct;
        std::vector<std::unique_ptr<Entity[]>> reverse;
    };

}
