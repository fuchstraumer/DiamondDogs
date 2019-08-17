#pragma once
#include "EntitySparseSet.hpp"
#include <numeric>
#include <algorithm>
#include <execution>

namespace ecs
{

    namespace detail
    {
        // sort algorithms for placeholder use
    }

    /*
        Use in systems as your storage for component data that you want associated with entities.
        Frees slots when entities are destroyed, effectively "compacting" the storage. Can be easily
        sorted as needed, from here
    */
    template<typename ComponentType>
    class ComponentStorage : public EntitySparseSet
    {
        using underlying_type = EntitySparseSet;

        template<bool CONST>
        struct iterator
        {
        private:
            friend class ComponentStorage<ComponentType>;
            // to avoid pulling in std::vector here, we will be using C array semantics
            using storage_type = std::conditional_t<CONST, const ComponentType*, ComponentType*>;
            using index_type = EntityTraits::DifferenceType;

            iterator(storage_type* data, const index_type idx) noexcept : m_storage(data), m_index(idx) {}

        public:

            using difference_type = EntityTraits::DifferenceType;
            using value_type = ComponentType;
            using pointer = std::conditional_t<CONST, const ComponentType*, ComponentType*>;
            using reference = std::conditional_t<CONST, const ComponentType&, ComponentType&>;
            using iterator_category = std::random_access_iterator_tag;

            iterator() noexcept = default;

            iterator& operator++() noexcept
            {
                --m_index;
                return *this;
            }

            iterator operator++(int) noexcept
            {
                iterator original = *this;
                --m_index;
                return original;
            }

            iterator& operator--() noexcept
            {
                ++m_index;
                return *this;
            }

            iterator operator--(int) noexcept
            {
                iterator original = *this;
                ++m_index;
                return original;
            }

            iterator& operator+=(const difference_type amt) noexcept
            {
                m_index -= amt;
                return *this;
            }

            iterator operator+(const difference_type amt) const noexcept
            {
                return iterator{ m_storage, m_index - amt };
            }

            iterator& operator-=(const difference_type amt) noexcept
            {
                m_index += amt;
                return *this;
            }

            iterator operator-(const difference_type amt) const noexcept
            {
                return iterator{ m_storage, m_index + amt };
            }

            difference_type operator-(const iterator& other) const noexcept
            {
                return other.m_index - m_index;
            }

            reference operator[](const difference_type value) const noexcept
            {
                const index_type idx = static_cast<index_type>(m_index - value - 1u);
                return m_storage[idx];
            }

            constexpr bool operator==(const iterator& other) const noexcept
            {
                return other.m_index == m_index;
            }

            constexpr bool operator!=(const iterator& other) const noexcept
            {
                return other.m_index != m_index;
            }

            constexpr bool operator<(const iterator& other) const noexcept
            {
                return m_index > other.m_index;
            }

            constexpr bool operator>(const iterator& other) const noexcept
            {
                return m_index < other.m_index;
            }

            constexpr bool operator<=(const iterator& other) const noexcept
            {
                return !(*this > other);
            }

            constexpr bool operator>=(const iterator& other) const noexcept
            {
                return !(*this < other);
            }

            pointer operator->() const noexcept
            {
                const index_type idx = static_cast<index_type>(m_index - 1);
                return &(m_storage[idx]);
            }

            reference operator*() const noexcept
            {
                const index_type idx = static_cast<index_type>(m_index - 1);
                return m_storage[idx];
            }

        private:
            storage_type* m_storage;
            index_type m_index;
        };

    public:

        using Iterator = iterator<false>;
        using ConstIterator = iterator<true>;
        using ComponentSortFunction = bool(*)(const ComponentType& lhs, const ComponentType& rhs);

        void Reserve(const size_t capacity) override
        {
            EntitySparseSet::Reserve(capacity);
            instances.reserve(capacity);
        }

        void ShrinkToFit() override
        {
            EntitySparseSet::ShrinkToFit();
            instances.shrink_to_fit();
        }

        const ComponentType* RawPtr() const noexcept
        {
            return instances.data();
        }

        ComponentType* RawPtr() noexcept
        {
            return instances.data();
        }

        ConstIterator begin() const noexcept
        {
            const EntityTraits::DifferenceType pos = EntitySparseSet::Size();
            return ConstIterator{ instances.data(), pos };
        }

        ConstIterator end() const noexcept
        {
            return ConstIterator{ instances.data(), {} };
        }

        Iterator begin() noexcept
        {
            const EntityTraits::DifferenceType pos = EntitySparseSet::Size();
            return Iterator{ instances.data(), pos };
        }

        Iterator end() noexcept
        {
            return Iterator{ instances.data(), {} };
        }
        
        const ComponentType& GetInstance(const Entity ent) const noexcept
        {
            return instances[EntitySparseSet::Get(ent)];
        }

        ComponentType& GetInstanceMutable(const Entity ent) noexcept
        {
            return instances[EntitySparseSet::Get(ent)];
        }

        const ComponentType* TryGetInstance(const Entity ent) const noexcept
        {
            return EntitySparseSet::Contains(ent) ? &instances[EntitySparseSet::Get(ent)] : nullptr;
        }

        ComponentType* TryGetInstanceMutable(const Entity ent) noexcept
        {
            return EntitySparseSet::Contains(ent) ? &instances[EntitySparseSet::Get(ent)] : nullptr;
        }

        template<typename...Args>
        ComponentType& Construct(const Entity ent, Args&& ...args)
        {
            if constexpr (std::is_aggregate_v<ComponentType>)
            {
                instances.emplace_back(ComponentType{ std::forward<Args>(args)... });
            }
            else
            {
                instances.emplace_back(std::forward<Args>(args)...);
            }

            EntitySparseSet::Construct(ent);
            return instances.back();
        }

        void Destroy(const Entity ent) override
        {
            ComponentType other = std::move(instances.back());
            instances[EntitySparseSet::Get(ent)] = std::move(other);
            instances.pop_back();
            EntitySparseSet::Destroy(ent);
        }

        void SortComponents(Iterator first, Iterator last, ComponentSortFunction sortFn)
        {
            const auto from = EntitySparseSet::Begin() + std::distance(begin(), first);
            const auto to = from + std::distance(first, last);

            if (sortFn != nullptr)
            {
                
                auto sortLambda = [&sortFn](const ComponentType& lhs, const ComponentType& rhs)
                {

                };
            }
        }
        
    private:

        std::vector<ComponentType> instances;
    };

}
