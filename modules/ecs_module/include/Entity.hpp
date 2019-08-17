#pragma once
#include <cstdint>

namespace ecs
{

    struct EntityTraits
    {
        using RawEntityType = uint64_t;
        using VersionType = uint32_t;
        using DifferenceType = int64_t;
        static constexpr uint64_t ENTITY_MASK = 0xFFFFFFFF;
        static constexpr uint64_t VERSION_MASK = 0xFFFFFFFF;
        // auto as it'll be used in different spots
        static constexpr auto ENTITY_SHIFT = 32;
    };

    using Entity = EntityTraits::RawEntityType;
    using EntityVersion = EntityTraits::VersionType;

    namespace detail
    {

        /*
            null entity type. constructs from and converts to
            Entity as needed, but can stand clearly apart for safety reasons
        */

        struct null_entity
        {
            constexpr operator Entity() const noexcept
            {
                return Entity{ EntityTraits::ENTITY_MASK | (EntityTraits::VERSION_MASK << EntityTraits::ENTITY_SHIFT) };
            }

            constexpr bool operator==(null_entity) const noexcept
            {
                return true;
            }

            constexpr bool operator!=(null_entity) const noexcept
            {
                return false;
            }

            constexpr bool operator==(const Entity& entity) const noexcept
            {
                return entity == static_cast<Entity>(*this);
            }

            constexpr bool operator!=(const Entity& entity) const noexcept
            {
                return entity != static_cast<Entity>(*this);
            }

        };

        constexpr bool operator==(const Entity entity, null_entity other) noexcept
        {
            return other == entity;
        }

        constexpr bool operator==(const Entity entity, null_entity other) noexcept
        {
            return other != entity;
        }

    }

    constexpr auto NULL_ENTITY = detail::null_entity{};

}
