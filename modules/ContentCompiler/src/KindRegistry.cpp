#include "KindRegistry.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace cc
{
    void KindRegistry::Freeze() noexcept
    {
        frozen = true;
    }

    bool KindRegistry::IsFrozen() const noexcept
    {
        return frozen;
    }

    std::optional<KindId> KindRegistry::Find(std::string_view _name) const
    {
        const auto found = std::ranges::find(kinds, _name, &KindOps::Name);

        if (found == kinds.end())
        {
            return std::nullopt;
        }

        const ptrdiff_t index = found - kinds.begin();

        return KindId{ static_cast<uint32_t>(index) + 1 };
    }

    const KindOps& KindRegistry::Ops(KindId _id) const
    {
        assert(_id.Value >= 1 && _id.Value <= kinds.size() && "the id was not made by this registry");

        return kinds[_id.Value - 1];
    }

    size_t KindRegistry::Count() const noexcept
    {
        return kinds.size();
    }

    std::expected<KindId, Diagnostic> KindRegistry::addKind(KindOps _ops)
    {
        assert(!frozen && "Register after Freeze");

        if (Find(_ops.Name).has_value())
        {
            return std::unexpected(Diagnostic
            {
                .Level = Severity::Error,
                .Code = DiagnosticCode::DuplicateKindName,
                .Message = std::format("a kind named \"{}\" is registered already", _ops.Name),
            });
        }

        kinds.push_back(std::move(_ops));

        return KindId{ static_cast<uint32_t>(kinds.size()) };
    }
}
