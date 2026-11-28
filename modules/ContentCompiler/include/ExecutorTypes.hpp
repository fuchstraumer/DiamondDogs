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
    // content_compiler_gpu defines it; the core only passes the pointer on
    class GpuBatch;

    // An index into the GPU artifact table of a GpuBatch. 0 is invalid.
    struct GpuArtifactId
    {
        uint32_t Value{ 0 };

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return Value != 0;
        }

        friend constexpr bool operator==(const GpuArtifactId&, const GpuArtifactId&) noexcept = default;
    };

    // The owner of the GPU objects behind GPU artifacts. The artifact table calls it when the last holder of a GPU
    // artifact releases it, so the core frees GPU objects without knowing what they are.
    class GpuArtifactOwner
    {
    public:
        virtual void ReleaseGpuArtifact(GpuArtifactId _artifact) = 0;

    protected:
        GpuArtifactOwner() = default;
        ~GpuArtifactOwner() = default;
        GpuArtifactOwner(const GpuArtifactOwner&) = default;
        GpuArtifactOwner& operator=(const GpuArtifactOwner&) = default;
        GpuArtifactOwner(GpuArtifactOwner&&) = default;
        GpuArtifactOwner& operator=(GpuArtifactOwner&&) = default;
    };

    // The GPU half of an executor: the batch that GPU kinds record into, and the owner of their artifacts. In
    // content_compiler_gpu one GpuBatch is both.
    struct GpuSeam
    {
        GpuBatch* Batch{ nullptr };
        GpuArtifactOwner* Owner{ nullptr };
    };

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

    // CpuBytes holds Bytes. GpuImage and GpuBuffer hold Gpu: the batch's table has the object and its readiness.
    struct Artifact
    {
        ArtifactKind Kind{ ArtifactKind::Invalid };
        std::vector<std::byte> Bytes;
        GpuArtifactId Gpu;
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
        // _gpu is null when the executor has no GPU seam
        Context(Quality _level, int32_t output_count, RecipeId _recipe, GpuBatch* _gpu);

        [[nodiscard]] Quality Level() const noexcept;
        // the recipe that this run makes
        [[nodiscard]] RecipeId Recipe() const noexcept;
        // the batch that a GPU kind records into, or null when the executor has none
        [[nodiscard]] GpuBatch* Gpu() const noexcept;

        // Each output of the kind needs exactly one artifact of the kind that the output declares.
        void SetOutput(int32_t _output, Artifact _artifact);
        void Report(Diagnostic _diagnostic);

        // for the executor, after Run returns
        [[nodiscard]] std::span<Artifact> Outputs() noexcept;
        [[nodiscard]] std::span<const Diagnostic> Diagnostics() const noexcept;

    private:
        Quality level{ Quality::Invalid };
        RecipeId recipe;
        GpuBatch* gpu{ nullptr };
        // an artifact with Kind Invalid is an output that Run did not set
        std::vector<Artifact> outputs;
        std::vector<Diagnostic> diagnostics;
    };
}

#endif // !DIAMOND_DOGS_CC_EXECUTOR_TYPES_HPP
