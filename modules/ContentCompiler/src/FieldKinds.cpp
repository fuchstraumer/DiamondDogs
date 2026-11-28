#include "FieldKinds.hpp"
#include "ContentDiagnostic.hpp"
#include "ExecutorTypes.hpp"
#include "KindRegistry.hpp"
#include "async/Task.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr size_t k_HeaderBytes{ 2 * sizeof(uint32_t) };

    // Reports one error to the run and returns the result that fails it.
    [[nodiscard]] cc::KindResult FailRun(cc::Context& context, std::string message);

    [[nodiscard]] cc::KindResult SucceedRun() noexcept;
}

static_assert(cc::Kind<cc::field::ConstKind>);
static_assert(cc::Kind<cc::field::ScaleBiasKind>);
static_assert(cc::Kind<cc::field::AddKind>);

namespace cc::field
{
    Artifact MakeFieldArtifact(const FieldSamples& field)
    {
        const size_t sampleCount = static_cast<size_t>(field.Width) * field.Height;
        assert(field.Samples.size() == sampleCount && "a field needs Width * Height samples");

        Artifact artifact;
        artifact.Kind = ArtifactKind::CpuBytes;
        artifact.Bytes.resize(k_HeaderBytes + sampleCount * sizeof(float));

        std::byte* bytes = artifact.Bytes.data();
        std::memcpy(bytes, &field.Width, sizeof(uint32_t));
        std::memcpy(bytes + sizeof(uint32_t), &field.Height, sizeof(uint32_t));
        std::memcpy(bytes + k_HeaderBytes, field.Samples.data(), sampleCount * sizeof(float));

        return artifact;
    }

    std::expected<FieldSamples, std::string> ReadFieldArtifact(const Artifact& artifact)
    {
        if (artifact.Kind != ArtifactKind::CpuBytes || artifact.Bytes.size() < k_HeaderBytes)
        {
            return std::unexpected(std::string{ "the artifact is not a field: it has no field header" });
        }

        FieldSamples field;
        const std::byte* bytes = artifact.Bytes.data();
        std::memcpy(&field.Width, bytes, sizeof(uint32_t));
        std::memcpy(&field.Height, bytes + sizeof(uint32_t), sizeof(uint32_t));

        // 64 bits, so that two large sizes cannot wrap to a small count
        const uint64_t sampleCount = static_cast<uint64_t>(field.Width) * field.Height;

        if (sampleCount > k_MaxSampleCount)
        {
            return std::unexpected(std::format("the artifact is not a field: its header says {} x {}, more than {} "
                                               "samples", field.Width, field.Height, k_MaxSampleCount));
        }

        const uint64_t expectedBytes = k_HeaderBytes + sampleCount * sizeof(float);

        if (artifact.Bytes.size() != expectedBytes)
        {
            return std::unexpected(std::format("the artifact is not a field: a {} x {} field needs {} bytes, and it "
                                               "has {}", field.Width, field.Height, expectedBytes,
                                               artifact.Bytes.size()));
        }

        field.Samples.resize(static_cast<size_t>(sampleCount));
        std::memcpy(field.Samples.data(), bytes + k_HeaderBytes, field.Samples.size() * sizeof(float));

        return field;
    }

    foundation::Task<KindResult> ConstKind::Run(const Params& params, Inputs, Context& context)
    {
        const uint64_t sampleCount = static_cast<uint64_t>(params.Width) * params.Height;

        if (sampleCount == 0 || sampleCount > k_MaxSampleCount)
        {
            co_return FailRun(context, std::format("field.Const: {} x {} is not a size; each side must be at "
                                                   "least 1, and the field at most {} samples", params.Width,
                                                   params.Height, k_MaxSampleCount));
        }

        FieldSamples field;
        field.Width = params.Width;
        field.Height = params.Height;
        field.Samples.assign(static_cast<size_t>(sampleCount), params.Value);

        context.SetOutput(0, MakeFieldArtifact(field));

        co_return SucceedRun();
    }

    foundation::Task<KindResult> ScaleBiasKind::Run(const Params& params, Inputs inputs, Context& context)
    {
        std::expected<FieldSamples, std::string> source = ReadFieldArtifact(inputs.Get(0));

        if (!source.has_value())
        {
            co_return FailRun(context, std::format("field.ScaleBias: Source: {}", source.error()));
        }

        for (float& sample : source->Samples)
        {
            sample = sample * params.Scale + params.Bias;
        }

        context.SetOutput(0, MakeFieldArtifact(*source));

        co_return SucceedRun();
    }

    foundation::Task<KindResult> AddKind::Run(const Params&, Inputs inputs, Context& context)
    {
        std::expected<FieldSamples, std::string> left = ReadFieldArtifact(inputs.Get(0));
        const std::expected<FieldSamples, std::string> right = ReadFieldArtifact(inputs.Get(1));

        if (!left.has_value())
        {
            co_return FailRun(context, std::format("field.Add: Left: {}", left.error()));
        }

        if (!right.has_value())
        {
            co_return FailRun(context, std::format("field.Add: Right: {}", right.error()));
        }

        const bool sameSize = left->Width == right->Width && left->Height == right->Height;

        if (!sameSize)
        {
            co_return FailRun(context, std::format("field.Add: Left is {} x {} and Right is {} x {}; the sizes "
                                                   "must be equal", left->Width, left->Height, right->Width,
                                                   right->Height));
        }

        for (size_t index = 0; index < left->Samples.size(); ++index)
        {
            left->Samples[index] += right->Samples[index];
        }

        context.SetOutput(0, MakeFieldArtifact(*left));

        co_return SucceedRun();
    }

    std::vector<Diagnostic> RegisterFieldKinds(KindRegistry& registry)
    {
        std::vector<Diagnostic> diagnostics;

        std::expected<KindId, Diagnostic> results[]
        {
            registry.Register<ConstKind>(),
            registry.Register<ScaleBiasKind>(),
            registry.Register<AddKind>(),
        };

        for (std::expected<KindId, Diagnostic>& result : results)
        {
            if (!result.has_value())
            {
                diagnostics.push_back(std::move(result.error()));
            }
        }

        return diagnostics;
    }
}

namespace
{
    cc::KindResult FailRun(cc::Context& context, std::string message)
    {
        context.Report(cc::Diagnostic
        {
            .Level = cc::Severity::Error,
            .Message = std::move(message),
        });

        return cc::KindResult{ .Status = cc::KindStatus::Failed };
    }

    cc::KindResult SucceedRun() noexcept
    {
        return cc::KindResult{ .Status = cc::KindStatus::Succeeded };
    }
}
