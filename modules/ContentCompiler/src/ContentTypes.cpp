#include "ContentTypes.hpp"
#include <cstddef>

namespace cc
{
    size_t RecipeIdHasher::operator()(const RecipeId& id) const noexcept
    {
        return static_cast<size_t>(id.Value);
    }
}
