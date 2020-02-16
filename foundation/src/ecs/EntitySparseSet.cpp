#include "EntitySparseSet.hpp"
#include <numeric>
#include <algorithm>
#include <execution>

namespace ecs
{

    constexpr bool entityComparison(const Entity ent0, const Entity ent1) noexcept
    {
        return ent0 < ent1;
    }

    EntitySparseSet::EntitySparseSet(const EntitySparseSet& other) : reverse{}, direct{ other.direct }
    {
        for (size_type position{}, last = other.reverse.size(); position < last; ++position)
        {
            if (other.reverse[position])
            {
                // make sure we have the page ready
                assure(position);
                // copy it over
                std::copy_n(other.reverse[position].get(), ENTITIES_PER_PAGE, reverse[position].get());
            }
        }
    }

    void EntitySparseSet::Reserve(const size_type capacity)
    {
        direct.reserve(capacity);
    }

    void EntitySparseSet::ShrinkToFit() noexcept
    {
        if (direct.empty())
        {
            reverse.clear();
        }

        reverse.shrink_to_fit();
        direct.shrink_to_fit();
    }

    bool EntitySparseSet::IsEmpty() const noexcept
    {
        return direct.empty();
    }

    const Entity* EntitySparseSet::Data() const noexcept
    {
        return direct.data();
    }

    EntitySparseSet::Iterator EntitySparseSet::Begin() const noexcept
    {
        const EntityTraits::DifferenceType pos = static_cast<EntityTraits::DifferenceType>(direct.size());
        return Iterator{ &direct, pos };
    }

    EntitySparseSet::Iterator EntitySparseSet::End() const noexcept
    {
        return Iterator{ &direct, {} };
    }

    bool EntitySparseSet::Contains(const Entity ent) const noexcept
    {
        auto [page, offset] = index(ent);
        return (page < reverse.size() && reverse[page] && reverse[page][offset] != NULL_ENTITY);
    }

    EntitySparseSet::size_type EntitySparseSet::Get(const Entity ent) const noexcept
    {
        assert(has(ent));
        auto [page, offset] = index(ent);
        return static_cast<size_t>(reverse[page][offset]);
    }

    void EntitySparseSet::Construct(const Entity ent)
    {
        assert(!has(ent));
        auto [page, offset] = index(ent);
        assure(page);
        reverse[page][offset] = static_cast<Entity>(direct.size());
        direct.emplace_back(ent);
    }

    void EntitySparseSet::Destroy(const Entity ent)
    {
        assert(has(ent));
        auto [from_page, from_offset] = index(ent);
        auto [to_page, to_offset] = index(direct.back());

        direct[static_cast<size_type>(reverse[from_page][from_offset])] = static_cast<Entity>(direct.back());
        reverse[to_page][to_offset] = reverse[from_page][from_offset];
        reverse[from_page][from_offset] = NULL_ENTITY;
        direct.pop_back();
    }

    EntitySparseSet::Iterator EntitySparseSet::Find(const Entity ent) const noexcept
    {
        return Contains(ent) ? --(End() - Get(ent)) : End();
    }

    void EntitySparseSet::Reset()
    {
        reverse.clear();
        direct.clear();
        direct.shrink_to_fit();
    }

    EntitySparseSet::size_type EntitySparseSet::Size() const noexcept
    {
        return direct.size();
    }

    EntitySparseSet::size_type EntitySparseSet::Extent() const noexcept
    {
        return reverse.size() * ENTITIES_PER_PAGE;
    }

    void EntitySparseSet::Swap(const size_t lhs, const size_t rhs) noexcept
    {
        auto [src_page, src_offset] = index(direct[lhs]);
        auto [dst_page, dst_offset] = index(direct[rhs]);
        std::swap(reverse[src_page][src_offset], reverse[dst_page][dst_offset]);
        std::swap(direct[lhs], direct[rhs]);
    }

    void EntitySparseSet::Sort(Iterator first, Iterator last, sort_fn_t sort_function)
    {
        if (sort_function == nullptr)
        {
            sort_function = &entityComparison;
        }

        auto sortLambda = [&sort_function](const Entity ent0, const Entity ent1)
        {
            return sort_function(ent0, ent1);
        };

        std::vector<size_type> copy(last - first);
        const size_t offset = std::distance(last, End());
        std::iota(copy.begin(), copy.end(), 0u);

        std::sort(std::execution::par_unseq, copy.rbegin(), copy.rend(), [this, offset, sortLambda](const auto lhs, const auto rhs)
        {
            return sortLambda(std::as_const(direct[lhs + offset]), std::as_const(direct[rhs + offset]));
        });

        const size_t length = copy.size();
        for (size_t i = 0u; i < length; ++i)
        {
            auto curr = i;
            auto next = copy[curr];

            while (curr != next)
            {
                Swap(copy[curr] + offset, copy[next] + offset);
                copy[curr] = curr;
                curr = next;
                next = copy[curr];
            }
        }

    }

    void EntitySparseSet::assure(const std::size_t page)
    {
        if (!(page < reverse.size()))
        {
            reverse.resize(page + 1u);
        }

        if (!reverse[page])
        {
            reverse[page] = std::make_unique<Entity[]>(ENTITIES_PER_PAGE);
            std::fill_n(reverse[page].get(), ENTITIES_PER_PAGE, NULL_ENTITY);
        }
    }

    auto EntitySparseSet::index(const Entity entity) const noexcept
    {
        const auto identifier = entity & EntityTraits::ENTITY_MASK;
        std::size_t pageIdx = static_cast<std::size_t>(identifier / ENTITIES_PER_PAGE);
        std::size_t offset = static_cast<std::size_t>(identifier & (ENTITIES_PER_PAGE - 1u));
        return std::make_pair(pageIdx, offset);
    }

}
