#include "Registry.hpp"

namespace ecs
{

    Registry::Registry()
    {
        entities.reserve(16384);
    }

    Registry& Registry::Get() noexcept
    {
        static Registry reg;
        return reg;
    }

    size_t Registry::Size() const noexcept
    {
        return entities.size();
    }

    size_t Registry::Alive() const noexcept
    {
        return entities.size() - available;
    }

    void Registry::Reserve(const size_t amt) noexcept
    {
        entities.reserve(amt);
    }

    bool Registry::IsValid(const Entity ent) const noexcept
    {
        const auto pos = static_cast<size_t>(ent & EntityTraits::ENTITY_MASK);
        return (pos < entities.size() && entities[pos] == ent);
    }

    bool Registry::Empty() const noexcept
    {
        return entities.size() == available;
    }

    Entity Registry::Create() noexcept
    {
        Entity result{ NULL_ENTITY };

        if (available)
        {
            const Entity ent{ next };
            next = Entity{ entities[ent] & EntityTraits::ENTITY_MASK };
            const auto version = entities[ent] & (EntityTraits::VERSION_MASK << EntityTraits::ENTITY_SHIFT);
            result = Entity{ ent | version };
            entities[ent] = result;
            --available;
        }
        else
        {
            result = entities.emplace_back(Entity{ entities.size() });
        }

        return result;
    }

    void Registry::Destroy(const Entity ent)
    {
        assert(IsValid(ent));
        const Entity underlyingHandle = ent & EntityTraits::ENTITY_MASK;
        const Entity version = ((ent >> EntityTraits::ENTITY_SHIFT) + 1) << EntityTraits::ENTITY_SHIFT;
        const Entity node = (available ? next : ((underlyingHandle + 1) & EntityTraits::ENTITY_MASK)) | version;
        entities[underlyingHandle] = Entity{ node };
        next = Entity{ underlyingHandle };
        ++available;
    }

    void Registry::Clear()
    {
        size_t capacity = entities.capacity();
        entities.clear();
        entities.shrink_to_fit();
        entities.reserve(capacity);
        available = 0u;
        next = NULL_ENTITY;
    }

}
