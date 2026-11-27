#include "EditProposal.hpp"
#include "reflect/TypeInfo.hpp"
#include "reflect/ValueAccess.hpp"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

namespace
{
    [[nodiscard]] bool UnwrapActiveContainer(foundation::reflect::ResolvedValue& resolved) noexcept;
    [[nodiscard]] std::expected<foundation::reflect::ResolvedValue, bool> ResolveWritableTarget(
        const foundation::reflect::TypeInfo& root_type,
        void* root_object,
        std::string_view path);
    [[nodiscard]] bool ApplyScalarProposal(const foundation::reflect::TypeInfo& root_type,
                                           void* root_object,
                                           const dd::ui::EditProposal& proposal);
    [[nodiscard]] bool ApplyResizeSequence(const foundation::reflect::TypeInfo& root_type,
                                           void* root_object,
                                           const dd::ui::EditProposal& proposal);
    [[nodiscard]] bool ApplyEmplaceOptional(const foundation::reflect::TypeInfo& root_type,
                                            void* root_object,
                                            const dd::ui::EditProposal& proposal);
    [[nodiscard]] bool ApplyResetOptional(const foundation::reflect::TypeInfo& root_type,
                                          void* root_object,
                                          const dd::ui::EditProposal& proposal);
    [[nodiscard]] bool ApplySetVariantIndex(const foundation::reflect::TypeInfo& root_type,
                                            void* root_object,
                                            const dd::ui::EditProposal& proposal);
}

namespace dd::ui
{
    std::string_view ToString(CommitKind kind) noexcept
    {
        switch (kind)
        {
        case CommitKind::Invalid:
            return "Invalid";
        case CommitKind::Preview:
            return "Preview";
        case CommitKind::Commit:
            return "Commit";
        }

        return "Invalid";
    }

    std::string_view ToString(ProposalOp op) noexcept
    {
        switch (op)
        {
        case ProposalOp::Invalid:
            return "Invalid";
        case ProposalOp::SetScalar:
            return "SetScalar";
        case ProposalOp::ResizeSequence:
            return "ResizeSequence";
        case ProposalOp::EmplaceOptional:
            return "EmplaceOptional";
        case ProposalOp::ResetOptional:
            return "ResetOptional";
        case ProposalOp::SetVariantIndex:
            return "SetVariantIndex";
        }

        return "Invalid";
    }

    void ProposalBuffer::Push(EditProposal proposal)
    {
        items.push_back(std::move(proposal));
    }

    std::span<const EditProposal> ProposalBuffer::Proposals() const noexcept
    {
        return items;
    }

    void ProposalBuffer::Clear() noexcept
    {
        items.clear();
    }

    bool ProposalBuffer::Empty() const noexcept
    {
        return items.empty();
    }

    bool ApplyEditProposal(const foundation::reflect::TypeInfo& root_type,
                           void* root_object,
                           const EditProposal& proposal)
    {
        if (root_object == nullptr)
        {
            return false;
        }

        switch (proposal.Op)
        {
        case ProposalOp::SetScalar:
            return ApplyScalarProposal(root_type, root_object, proposal);
        case ProposalOp::ResizeSequence:
            return ApplyResizeSequence(root_type, root_object, proposal);
        case ProposalOp::EmplaceOptional:
            return ApplyEmplaceOptional(root_type, root_object, proposal);
        case ProposalOp::ResetOptional:
            return ApplyResetOptional(root_type, root_object, proposal);
        case ProposalOp::SetVariantIndex:
            return ApplySetVariantIndex(root_type, root_object, proposal);
        case ProposalOp::Invalid:
            return false;
        }

        return false;
    }
}

namespace
{
    bool UnwrapActiveContainer(foundation::reflect::ResolvedValue& resolved) noexcept
    {
        while (resolved.Type->Kind == foundation::reflect::TypeKind::Optional
               || resolved.Type->Kind == foundation::reflect::TypeKind::Variant)
        {
            const bool isOptional = resolved.Type->Kind == foundation::reflect::TypeKind::Optional;
            const void* contained = isOptional ? resolved.Type->OptionalValue(resolved.Address)
                                               : resolved.Type->ActiveValue(resolved.Address);

            if (contained == nullptr)
            {
                return false;
            }

            const foundation::reflect::TypeInfo& containedType =
                isOptional ? resolved.Type->Element()
                           : resolved.Type->Alternatives[static_cast<size_t>(
                                 resolved.Type->ActiveIndex(resolved.Address))]();

            resolved.Type = &containedType;
            resolved.Address = contained;
        }

        return true;
    }

    std::expected<foundation::reflect::ResolvedValue, bool> ResolveWritableTarget(
        const foundation::reflect::TypeInfo& root_type,
        void* root_object,
        std::string_view path)
    {
        std::expected<foundation::reflect::ResolvedValue, foundation::reflect::PathResult> resolved =
            foundation::reflect::ResolvePath(root_type, root_object, path);

        if (!resolved.has_value() || !resolved->Writable)
        {
            return std::unexpected(false);
        }

        return *resolved;
    }

    bool ApplyScalarProposal(const foundation::reflect::TypeInfo& root_type,
                             void* root_object,
                             const dd::ui::EditProposal& proposal)
    {
        const auto toScalarView = [](const auto& owned) -> foundation::reflect::ScalarValue
        {
            return owned;
        };

        const foundation::reflect::ScalarValue scalar = std::visit(toScalarView, proposal.Value);
        const foundation::reflect::PathResult result =
            foundation::reflect::SetValue(root_type, root_object, proposal.Path, scalar);
        return result.Succeeded();
    }

    bool ApplyResizeSequence(const foundation::reflect::TypeInfo& root_type,
                             void* root_object,
                             const dd::ui::EditProposal& proposal)
    {
        if (proposal.IntArgument < 0)
        {
            return false;
        }

        std::expected<foundation::reflect::ResolvedValue, bool> resolved =
            ResolveWritableTarget(root_type, root_object, proposal.Path);

        if (!resolved.has_value())
        {
            return false;
        }

        if (!UnwrapActiveContainer(*resolved))
        {
            return false;
        }

        if (resolved->Type->Kind != foundation::reflect::TypeKind::Sequence
            || resolved->Type->ResizeSequence == nullptr)
        {
            return false;
        }

        void* target = const_cast<void*>(resolved->Address);
        return resolved->Type->ResizeSequence(target, proposal.IntArgument);
    }

    bool ApplyEmplaceOptional(const foundation::reflect::TypeInfo& root_type,
                              void* root_object,
                              const dd::ui::EditProposal& proposal)
    {
        std::expected<foundation::reflect::ResolvedValue, bool> resolved =
            ResolveWritableTarget(root_type, root_object, proposal.Path);

        if (!resolved.has_value())
        {
            return false;
        }

        if (resolved->Type->Kind != foundation::reflect::TypeKind::Optional
            || resolved->Type->EmplaceOptional == nullptr)
        {
            return false;
        }

        void* target = const_cast<void*>(resolved->Address);
        return resolved->Type->EmplaceOptional(target) != nullptr;
    }

    bool ApplyResetOptional(const foundation::reflect::TypeInfo& root_type,
                            void* root_object,
                            const dd::ui::EditProposal& proposal)
    {
        std::expected<foundation::reflect::ResolvedValue, bool> resolved =
            ResolveWritableTarget(root_type, root_object, proposal.Path);

        if (!resolved.has_value())
        {
            return false;
        }

        if (resolved->Type->Kind != foundation::reflect::TypeKind::Optional
            || resolved->Type->ResetOptional == nullptr)
        {
            return false;
        }

        void* target = const_cast<void*>(resolved->Address);
        resolved->Type->ResetOptional(target);
        return true;
    }

    bool ApplySetVariantIndex(const foundation::reflect::TypeInfo& root_type,
                              void* root_object,
                              const dd::ui::EditProposal& proposal)
    {
        std::expected<foundation::reflect::ResolvedValue, bool> resolved =
            ResolveWritableTarget(root_type, root_object, proposal.Path);

        if (!resolved.has_value())
        {
            return false;
        }

        if (resolved->Type->Kind == foundation::reflect::TypeKind::Optional
            && !UnwrapActiveContainer(*resolved))
        {
            return false;
        }

        if (resolved->Type->Kind != foundation::reflect::TypeKind::Variant
            || resolved->Type->EmplaceVariant == nullptr)
        {
            return false;
        }

        const int64_t altCount = static_cast<int64_t>(std::ssize(resolved->Type->Alternatives));

        if (proposal.IntArgument < 0 || proposal.IntArgument >= altCount)
        {
            return false;
        }

        void* target = const_cast<void*>(resolved->Address);
        return resolved->Type->EmplaceVariant(target, static_cast<int32_t>(proposal.IntArgument)) != nullptr;
    }
}
