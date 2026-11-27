#pragma once
#ifndef DIAMOND_DOGS_EDITOR_UI_EDIT_PROPOSAL_HPP
#define DIAMOND_DOGS_EDITOR_UI_EDIT_PROPOSAL_HPP
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace foundation::reflect
{
    struct TypeInfo;
}

namespace dd::ui
{
    enum class CommitKind : uint8_t
    {
        Invalid = 0,
        // Live scrub while dragging: applies for visual preview without pushing an undo record.
        Preview,
        // Control released or text confirmed: commits to history and replay stream.
        Commit,
    };

    enum class ProposalOp : uint8_t
    {
        Invalid = 0,
        SetScalar,
        ResizeSequence,
        EmplaceOptional,
        ResetOptional,
        SetVariantIndex,
    };

    [[nodiscard]] std::string_view ToString(CommitKind kind) noexcept;
    [[nodiscard]] std::string_view ToString(ProposalOp op) noexcept;

    // Owned scalar value so deferred proposals safely outlive temporary ImGui buffers across frames.
    using OwnedScalarValue = std::variant<bool, int64_t, uint64_t, double, std::string>;

    struct EditProposal
    {
        uint64_t TargetId{ 0u };
        std::string Path;
        ProposalOp Op{ ProposalOp::SetScalar };
        CommitKind Commit{ CommitKind::Preview };
        OwnedScalarValue Value{};
        int64_t IntArgument{ 0 };
    };

    class EditProposalSink
    {
    public:
        virtual ~EditProposalSink() = default;
        virtual void Push(EditProposal proposal) = 0;
    };

    /** @brief Collects deferred EditProposal records on the UI frame thread for frame-start application. */
    class ProposalBuffer final : public EditProposalSink
    {
    public:
        void Push(EditProposal proposal) final;

        [[nodiscard]] std::span<const EditProposal> Proposals() const noexcept;
        void Clear() noexcept;
        [[nodiscard]] bool Empty() const noexcept;

    private:
        std::vector<EditProposal> items;
    };

    /** @brief Applies one deferred EditProposal to a reflected root object via ResolvePath and SetValue. */
    [[nodiscard]] bool ApplyEditProposal(const foundation::reflect::TypeInfo& root_type,
                                         void* root_object,
                                         const EditProposal& proposal);
}

#endif // !DIAMOND_DOGS_EDITOR_UI_EDIT_PROPOSAL_HPP
