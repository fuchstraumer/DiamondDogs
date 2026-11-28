#include "test_executor_kinds.hpp"
#include "ContentDiagnostic.hpp"
#include "ExecutorTypes.hpp"
#include "KindRegistry.hpp"
#include "async/Task.hpp"
#include "async/Yield.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace cctest
{
    RunCounts& Counts()
    {
        static RunCounts counts;

        return counts;
    }

    std::vector<float>& RunLog()
    {
        static std::vector<float> log;

        return log;
    }

    void ResetRuns()
    {
        Counts() = RunCounts{};
        RunLog().clear();
    }

    cc::Artifact MakeFloatArtifact(float value)
    {
        cc::Artifact artifact;
        artifact.Kind = cc::ArtifactKind::CpuBytes;
        artifact.Bytes.resize(sizeof(float));
        std::memcpy(artifact.Bytes.data(), &value, sizeof(float));

        return artifact;
    }

    float FloatOf(const cc::Artifact& artifact)
    {
        assert(artifact.Bytes.size() == sizeof(float) && "the artifact is not a float");

        float value = 0.0f;
        std::memcpy(&value, artifact.Bytes.data(), sizeof(float));

        return value;
    }

    foundation::Task<cc::KindResult> NumberKind::Run(const Params& params, cc::Inputs, cc::Context& context)
    {
        ++Counts().Number;
        RunLog().push_back(params.Value);

        context.SetOutput(0, MakeFloatArtifact(params.Value));

        co_return cc::KindResult{ .Status = cc::KindStatus::Succeeded };
    }

    foundation::Task<cc::KindResult> SumKind::Run(const Params& params, cc::Inputs inputs, cc::Context& context)
    {
        ++Counts().Sum;
        RunLog().push_back(static_cast<float>(params.Tag));

        const float left = FloatOf(inputs.Get(0));
        const float right = FloatOf(inputs.Get(1));
        context.SetOutput(0, MakeFloatArtifact(left + right));

        co_return cc::KindResult{ .Status = cc::KindStatus::Succeeded };
    }

    foundation::Task<cc::KindResult> OffsetKind::Run(const Params& params, cc::Inputs inputs, cc::Context& context)
    {
        ++Counts().Offset;
        RunLog().push_back(params.Delta);

        const float source = FloatOf(inputs.Get(0));
        context.SetOutput(0, MakeFloatArtifact(source + params.Delta));

        co_return cc::KindResult{ .Status = cc::KindStatus::Succeeded };
    }

    foundation::Task<cc::KindResult> PairKind::Run(const Params& params, cc::Inputs, cc::Context& context)
    {
        ++Counts().Pair;
        RunLog().push_back(static_cast<float>(params.Tag));

        const float first = static_cast<float>(params.Tag);
        context.SetOutput(0, MakeFloatArtifact(first));
        context.SetOutput(1, MakeFloatArtifact(first + 0.5f));

        co_return cc::KindResult{ .Status = cc::KindStatus::Succeeded };
    }

    foundation::Task<cc::KindResult> FailKind::Run(const Params& params, cc::Inputs, cc::Context& context)
    {
        ++Counts().Fail;
        RunLog().push_back(static_cast<float>(params.Tag));

        cc::Diagnostic diagnostic;
        diagnostic.Level = cc::Severity::Error;
        diagnostic.Message = "exec.Fail always fails";
        context.Report(std::move(diagnostic));

        co_return cc::KindResult{ .Status = cc::KindStatus::Failed };
    }

    foundation::Task<cc::KindResult> ForgetKind::Run(const Params& params, cc::Inputs, cc::Context&)
    {
        ++Counts().Forget;
        RunLog().push_back(static_cast<float>(params.Tag));

        co_return cc::KindResult{ .Status = cc::KindStatus::Succeeded };
    }

    foundation::Task<cc::KindResult> SlowKind::Run(const Params& params, cc::Inputs inputs, cc::Context& context)
    {
        ++Counts().Slow;
        RunLog().push_back(params.Delta);

        co_await foundation::Yield();

        const float source = FloatOf(inputs.Get(0));
        context.SetOutput(0, MakeFloatArtifact(source + params.Delta));

        co_return cc::KindResult{ .Status = cc::KindStatus::Succeeded };
    }

    cc::KindRegistry MakeFrozenExecutorRegistry()
    {
        cc::KindRegistry registry;

        const bool allRegistered = registry.Register<NumberKind>().has_value() &&
                                   registry.Register<SumKind>().has_value() &&
                                   registry.Register<OffsetKind>().has_value() &&
                                   registry.Register<PairKind>().has_value() &&
                                   registry.Register<FailKind>().has_value() &&
                                   registry.Register<ForgetKind>().has_value() &&
                                   registry.Register<SlowKind>().has_value();
        assert(allRegistered);
        (void)allRegistered;

        registry.Freeze();

        return registry;
    }
}
