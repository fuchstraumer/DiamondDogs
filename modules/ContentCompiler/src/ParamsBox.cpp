#include "ParamsBox.hpp"
#include "content/CanonicalSink.hpp"
#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cc
{
    ParamsBox::ParamsBox(const ParamsBox& _other) :
      data{ _other.ops != nullptr ? _other.ops->Clone(_other.data) : nullptr },
      ops{ _other.ops }
    {}

    ParamsBox::ParamsBox(ParamsBox&& _other) noexcept :
      data{ std::exchange(_other.data, nullptr) },
      ops{ std::exchange(_other.ops, nullptr) }
    {}

    ParamsBox& ParamsBox::operator=(const ParamsBox& _other)
    {
        ParamsBox copy{ _other };
        *this = std::move(copy);

        return *this;
    }

    ParamsBox& ParamsBox::operator=(ParamsBox&& _other) noexcept
    {
        if (this == &_other)
        {
            return *this;
        }

        if (ops != nullptr)
        {
            ops->Destroy(data);
        }

        data = std::exchange(_other.data, nullptr);
        ops = std::exchange(_other.ops, nullptr);

        return *this;
    }

    ParamsBox::~ParamsBox()
    {
        if (ops != nullptr)
        {
            ops->Destroy(data);
        }
    }

    bool ParamsBox::IsEmpty() const noexcept
    {
        return ops == nullptr;
    }

    const void* ParamsBox::TypeIdentity() const noexcept
    {
        if (ops == nullptr)
        {
            return nullptr;
        }

        return ops->Identity;
    }

    std::string_view ParamsBox::TypeName() const noexcept
    {
        if (ops == nullptr)
        {
            return {};
        }

        return ops->TypeName;
    }

    const void* ParamsBox::Data() const noexcept
    {
        return data;
    }

    void ParamsBox::Encode(foundation::content::CanonicalSink& _sink) const
    {
        assert(ops != nullptr && "Encode needs a box that holds a value");
        ops->Encode(data, _sink);
    }

    std::optional<std::string> ParamsBox::FindNaN() const
    {
        assert(ops != nullptr && "FindNaN needs a box that holds a value");

        return ops->FindNaN(data);
    }

    ParamsBox::ParamsBox(void* _data, const ParamsOps* _ops) noexcept :
      data{ _data },
      ops{ _ops }
    {}
}
