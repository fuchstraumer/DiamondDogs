#pragma once
#ifndef DIAMOND_DOGS_CC_EXECUTOR_TYPES_HPP
#define DIAMOND_DOGS_CC_EXECUTOR_TYPES_HPP
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// What a kind's Run sees: its inputs, its context, and the result it returns.
namespace cc
{
    class ArtifactTable;

    // A handle into the executor's artifact table. Generation 0 is never used, so a default handle is invalid.
    struct ArtifactRef
    {
        uint32_t Slot{ 0 };
        uint32_t Generation{ 0 };

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return Generation != 0;
        }

        friend constexpr bool operator==(const ArtifactRef&, const ArtifactRef&) noexcept = default;
    };

    // v0 holds CPU bytes only. A GPU artifact (an rhi::GpuWait and a resource handle) comes with the first GPU kind.
    struct Artifact
    {
        ArtifactKind Kind{ ArtifactKind::Invalid };
        std::vector<std::byte> Bytes;
    };

    enum class ContentErrorCode : uint8_t
    {
        Invalid = 0,
        // Run returned Failed
        KindFailed,
        // an input failed, so this node did not run
        InputFailed,
        // Run returned Succeeded and left an output unset
        MissingOutput,
    };

    struct FailureId
    {
        uint32_t Value{ 0 };

        friend constexpr bool operator==(const FailureId&, const FailureId&) noexcept = default;
    };

    // Trivially copyable. The diagnostics stay in the executor's failure memo under Failure.
    struct ContentError
    {
        ContentErrorCode Code{ ContentErrorCode::Invalid };
        // the node that failed first
        RecipeId Origin;
        FailureId Failure;
    };

    enum class KindStatus : uint8_t
    {
        Invalid = 0,
        Succeeded,
        Failed,
    };

    // What Run returns. The artifacts go through Context::SetOutput and the diagnostics through Context::Report.
    struct KindResult
    {
        KindStatus Status{ KindStatus::Invalid };

        [[nodiscard]] constexpr bool Succeeded() const noexcept
        {
            return Status == KindStatus::Succeeded;
        }
    };

    // The inputs of one run, in slot order. An optional slot with no edge has no artifact.
    class Inputs
    {
    public:
        Inputs(const ArtifactTable& _table, std::span<const ArtifactRef> _refs) noexcept;

        [[nodiscard]] int32_t Count() const noexcept;
        [[nodiscard]] bool Has(int32_t _slot) const noexcept;
        // The slot must have an artifact. The reference is valid until Run returns.
        [[nodiscard]] const Artifact& Get(int32_t _slot) const;

    private:
        const ArtifactTable* table{ nullptr };
        std::span<const ArtifactRef> refs;
    };

    // One per run. It lives in the executor's run frame, so it outlives the kind's Run.
    class Context
    {
    public:
        Context(Quality _level, int32_t output_count);

        [[nodiscard]] Quality Level() const noexcept;

        // Each output of the kind needs exactly one artifact of the kind that the output declares.
        void SetOutput(int32_t _output, Artifact _artifact);
        void Report(Diagnostic _diagnostic);

        // for the executor, after Run returns
        [[nodiscard]] std::span<Artifact> Outputs() noexcept;
        [[nodiscard]] std::span<const Diagnostic> Diagnostics() const noexcept;

    private:
        Quality level{ Quality::Invalid };
        // an artifact with Kind Invalid is an output that Run did not set
        std::vector<Artifact> outputs;
        std::vector<Diagnostic> diagnostics;
    };
}

#endif // !DIAMOND_DOGS_CC_EXECUTOR_TYPES_HPP
