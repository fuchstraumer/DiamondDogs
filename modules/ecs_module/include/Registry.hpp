#pragma once
#include "Entity.hpp"
#include <vector>
#include <cassert>

namespace ecs
{

    class Registry
    {

        Registry();
        Registry(const Registry&) = delete;
        Registry& operator=(const Registry&) = delete;

    public:

        static Registry& Get() noexcept;

        size_t Size() const noexcept;
        size_t Alive() const noexcept;
        void Reserve(const size_t amt) noexcept;
        bool IsValid(const Entity ent) const noexcept;
        bool Empty() const noexcept;
        Entity Create() noexcept;
        void Destroy(const Entity ent);
        void Clear();

    private:
        std::vector<Entity> entities;
        size_t available{ 0u };
        Entity next{ NULL_ENTITY };
    };

}
