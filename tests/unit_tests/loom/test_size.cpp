#include "size/SizedGraph.hpp"
#include "bind/BoundGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "ShaderBundle.hpp"
#include "ShaderManifestDevice.hpp"
#include "ShaderManifestLayout.hpp"
#include "freeze_test_helpers.hpp"
#include <cstdint>
#include <optional>
#include <source_location>
#include <string_view>
#include <gtest/gtest.h>

// The Size stage (buffers), and Bind's view rule (LM307), against LoomSizeTest (tests/unit_tests/loom/shaders).
// Counters is 64 uints and CountersIn is a view of it as 16 uint4s: 256 bytes either way. Fixed is 32 floats (128
// bytes). Values states no size. Args is 3 uints of indirect dispatch arguments.

using namespace freeze_tests;

namespace
{

    constexpr std::string_view k_Module = "LoomSizeTest";

    constexpr loom::ResourceKey<loom::Buffer> k_Counters{ "Size.Counters" };
    constexpr loom::ResourceKey<loom::Buffer> k_Other{ "Size.Other" };
    constexpr loom::ResourceKey<loom::Buffer> k_Values{ "Size.Values" };
    constexpr loom::ResourceKey<loom::Buffer> k_Args{ "Size.Args" };
    constexpr loom::PassKey k_Second{ "Test.Second" };

    const rhi::ShaderBundle& SpirvBundle()
    {
        static std::optional<rhi::ShaderBundle> bundle;

        if (!bundle.has_value())
        {
            rhi::ShaderBundleResult<rhi::ShaderBundle> opened = rhi::ShaderBundle::Open(LOOM_TEST_BUNDLE, "spirv");
            EXPECT_TRUE(opened.has_value()) << rhi::ToString(opened.error());
            bundle.emplace(std::move(*opened));
        }

        return *bundle;
    }

    bool SupportsEverything(void*, std::string_view)
    {
        return true;
    }

    lodestone::manifest::DeviceLimits RoomyLimits()
    {
        return lodestone::manifest::DeviceLimits
        {
            .MaxWorkgroupMemoryBytes = 49152u,
            .MaxInvocationsPerWorkgroup = 1024u,
            .MaxWorkgroupSizeX = 1024u,
            .MaxWorkgroupSizeY = 1024u,
            .MaxWorkgroupSizeZ = 64u,
            .MaxPushConstantBytes = 256u,
        };
    }

    loom::BufferDeclaration DerivedBuffer(loom::ResourceKey<loom::Buffer> key)
    {
        loom::BufferDeclaration buffer = MakeBuffer();
        buffer.Key = key;
        buffer.Size = loom::SizeSource::Derived();
        return buffer;
    }

    loom::BufferDeclaration BufferOfBytes(loom::ResourceKey<loom::Buffer> key, int64_t bytes)
    {
        loom::BufferDeclaration buffer = MakeBuffer();
        buffer.Key = key;
        buffer.Size = loom::SizeSource::Bytes(bytes);
        return buffer;
    }

    struct SizeRun
    {
        std::optional<loom::FrozenGraph> Frozen;
        std::optional<loom::ResolvedGraph> Resolved;
        std::optional<loom::BoundGraph> Bound;
        std::optional<loom::SizedGraph> Sized;
        loom::CollectingSink Sink;
    };

    // Freeze, Resolve, Bind and Size. Each stage runs only when the stage before it succeeded.
    void RunSize(const loom::GraphBuilder& graph, SizeRun& run)
    {
        run.Frozen = loom::Freeze(graph.Record(), run.Sink);
        ASSERT_TRUE(run.Frozen.has_value()) << run.Sink.Diagnostics().size() << " Freeze diagnostics";

        const loom::ResolveInputs inputs
        {
            .Bundle = SpirvBundle(),
            .Supports = &SupportsEverything,
            .SupportsUser = nullptr,
            .Limits = RoomyLimits(),
        };

        run.Resolved = loom::Resolve(*run.Frozen, inputs, run.Sink);
        ASSERT_TRUE(run.Resolved.has_value()) << run.Sink.Diagnostics().size() << " Resolve diagnostics";

        run.Bound = loom::Bind(*run.Frozen, *run.Resolved, run.Sink);

        if (run.Bound.has_value())
        {
            run.Sized = loom::Size(*run.Frozen, *run.Bound, run.Sink);
        }
    }

    const loom::SizedResource& SizedOf(const SizeRun& run, loom::KeyRef key)
    {
        for (size_t row = 0u; row < run.Frozen->Resources.size(); ++row)
        {
            if (run.Frozen->Resources[row].Key == key)
            {
                return run.Sized->Resources[row];
            }
        }

        ADD_FAILURE() << key.Name << " is not in the frozen graph";
        return run.Sized->Resources.front();
    }

    loom::DiagnosticCode OnlyCode(const loom::CollectingSink& sink)
    {
        if (sink.Diagnostics().size() != 1u)
        {
            ADD_FAILURE() << sink.Diagnostics().size() << " diagnostics, expected 1";
            return loom::DiagnosticCode::Invalid;
        }

        return sink.Diagnostics().front().Code;
    }

    bool HasUsage(lodestone::BufferUsageFlags usage, lodestone::BufferUsageFlags flag)
    {
        return (usage & flag) == flag;
    }

    // CountCS with both views of Counters bound to `counters` and `view`
    void CountWith(loom::DispatchBuilder& pass, loom::ResourceKey<loom::Buffer> counters,
                   loom::ResourceKey<loom::Buffer> view)
    {
        pass.Shader(k_Module, "CountCS");
        pass.Bind("Counters", counters);
        pass.Bind("CountersIn", view);
        pass.Bind("Values", k_Values);
        pass.DispatchThreads(64, 1, 1);
    }

    void ReadWith(loom::DispatchBuilder& pass, loom::ResourceKey<loom::Buffer> view)
    {
        pass.Shader(k_Module, "ReadCS");
        pass.Bind("CountersIn", view);
        pass.Bind("Values", k_Values);
        pass.DispatchThreads(64, 1, 1);
    }

}

// Two typed views of one buffer: 64 uints and 16 uint4s. Their strides differ, their bytes agree, so the derived
// size is 256 and no diagnostic is reported.
TEST(Size, ViewsOfOneBufferAgreeInBytes)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(DerivedBuffer(k_Counters));
    graph.Declare(BufferOfBytes(k_Values, 256));

    const auto count = [](loom::DispatchBuilder& pass)
    {
        CountWith(pass, k_Counters, k_Counters);
    };

    const auto read = [](loom::DispatchBuilder& pass)
    {
        ReadWith(pass, k_Counters);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, count);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, read);

    SizeRun run;
    RunSize(graph, run);

    ASSERT_TRUE(run.Sized.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());
    EXPECT_EQ(SizedOf(run, k_Counters.ToRef()).ByteSize, 256);
    EXPECT_TRUE(HasUsage(SizedOf(run, k_Counters.ToRef()).Usage, lodestone::BufferUsageFlags::Storage));
    EXPECT_EQ(SizedOf(run, k_Values.ToRef()).ByteSize, 256);
}

// LM401: two exact counts that disagree in bytes. Fixed is 128 bytes, CountersIn 256.
TEST(Size, ExactSizesThatDisagreeAreLM401)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(DerivedBuffer(k_Other));
    graph.Declare(BufferOfBytes(k_Values, 256));

    const auto fixed = [](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, "FixedCS");
        pass.Bind("Fixed", k_Other);
        pass.Bind("Values", k_Values);
        pass.DispatchThreads(64, 1, 1);
    };

    const auto read = [](loom::DispatchBuilder& pass)
    {
        ReadWith(pass, k_Other);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, fixed);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, read);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM401);

    // the first pass's binding sets the size; the second disagrees with it
    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Expected.Integer, 128);
    EXPECT_EQ(diagnostic.Actual.Integer, 256);
    EXPECT_EQ(diagnostic.Binding, "CountersIn");
    EXPECT_EQ(diagnostic.Pass.Hash, k_Second.Hash);
    EXPECT_EQ(diagnostic.Resource, k_Other.ToRef());
}

// LM401: a declared size smaller than a binding's exact count. OtherWhere is the declaration.
TEST(Size, DeclaredSizeThatDoesNotFitIsLM401)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    const uint32_t declarationLine = std::source_location::current().line() + 1u;
    graph.Declare(BufferOfBytes(k_Counters, 64));
    graph.Declare(BufferOfBytes(k_Values, 256));

    const auto read = [](loom::DispatchBuilder& pass)
    {
        ReadWith(pass, k_Counters);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, read);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM401);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Expected.Integer, 64);
    EXPECT_EQ(diagnostic.Actual.Integer, 256);
    EXPECT_EQ(diagnostic.OtherWhere.line(), declarationLine);
}

// LM402: a derived size that no binding states. Values has no element count.
TEST(Size, DerivedSizeWithNoSourceIsLM402)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(BufferOfBytes(k_Counters, 256));
    graph.Declare(DerivedBuffer(k_Values));

    const auto read = [](loom::DispatchBuilder& pass)
    {
        ReadWith(pass, k_Counters);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, read);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM402);
    EXPECT_EQ(run.Sink.Diagnostics().front().Resource, k_Values.ToRef());
}

// Indirect arguments: ArgsCS writes 12 bytes, and a later dispatch reads them. The usage gains Indirect. At offset 4,
// the dispatch needs 16 bytes, which the 12-byte buffer does not have: LM401.
TEST(Size, IndirectArgumentsNeedTheirBytes)
{
    for (const int64_t offset : { int64_t{ 0 }, int64_t{ 4 } })
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        graph.Declare(DerivedBuffer(k_Args));
        graph.Declare(BufferOfBytes(k_Counters, 256));
        graph.Declare(BufferOfBytes(k_Values, 256));

        const auto args = [](loom::DispatchBuilder& pass)
        {
            pass.Shader(k_Module, "ArgsCS");
            pass.Bind("Args", k_Args);
            pass.DispatchThreads(1, 1, 1);
        };

        const auto indirect = [offset](loom::DispatchBuilder& pass)
        {
            pass.Shader(k_Module, "ReadCS");
            pass.Bind("CountersIn", k_Counters);
            pass.Bind("Values", k_Values);
            pass.DispatchIndirect(k_Args, offset);
        };

        graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, args);
        graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, indirect);

        SizeRun run;
        RunSize(graph, run);

        if (offset == 0)
        {
            ASSERT_TRUE(run.Sized.has_value());
            EXPECT_TRUE(run.Sink.Diagnostics().empty());
            EXPECT_EQ(SizedOf(run, k_Args.ToRef()).ByteSize, 12);
            EXPECT_TRUE(HasUsage(SizedOf(run, k_Args.ToRef()).Usage, lodestone::BufferUsageFlags::Indirect));
            EXPECT_TRUE(HasUsage(SizedOf(run, k_Args.ToRef()).Usage, lodestone::BufferUsageFlags::Storage));
            continue;
        }

        EXPECT_FALSE(run.Sized.has_value());
        ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM401);
        EXPECT_EQ(run.Sink.Diagnostics().front().Expected.Integer, 12);
        EXPECT_EQ(run.Sink.Diagnostics().front().Actual.Integer, 16);
        EXPECT_EQ(run.Sink.Diagnostics().front().Pass.Hash, k_Second.Hash);
    }
}

// LM307: in one pass, CountersIn (a view of Counters) and Counters are bound to different keys. Where is the view's
// bind, OtherWhere the root's.
TEST(BindViews, ViewAndRootOnDifferentKeysIsLM307)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(BufferOfBytes(k_Counters, 256));
    graph.Declare(BufferOfBytes(k_Other, 256));
    graph.Declare(BufferOfBytes(k_Values, 256));
    uint32_t rootLine = 0u;
    uint32_t viewLine = 0u;

    const auto count = [&](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, "CountCS");
        rootLine = std::source_location::current().line() + 1u;
        pass.Bind("Counters", k_Counters);
        viewLine = std::source_location::current().line() + 1u;
        pass.Bind("CountersIn", k_Other);
        pass.Bind("Values", k_Values);
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, count);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Bound.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM307);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Binding, "CountersIn");
    EXPECT_EQ(diagnostic.Resource, k_Other.ToRef());
    EXPECT_EQ(diagnostic.Where.line(), viewLine);
    EXPECT_EQ(diagnostic.OtherWhere.line(), rootLine);
}

// LM307's limit: it compares keys inside one pass. A pass that reads only the view has no root to compare with, so
// a view bound to another key than the root in another pass is not found. That check needs a graph-wide
// name-to-key map, which Loom does not have.
TEST(BindViews, ViewWithoutItsRootIsNotCompared)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(BufferOfBytes(k_Counters, 256));
    graph.Declare(BufferOfBytes(k_Other, 256));
    graph.Declare(BufferOfBytes(k_Values, 256));

    const auto count = [](loom::DispatchBuilder& pass)
    {
        CountWith(pass, k_Counters, k_Counters);
    };

    const auto read = [](loom::DispatchBuilder& pass)
    {
        ReadWith(pass, k_Other);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, count);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, read);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_TRUE(run.Sized.has_value());
    EXPECT_EQ(run.Sink.ErrorCount(), 0);
}

// The DispatchIndirect statement gives Indirect usage by itself. Counters has no indirect role in the shader, so only
// the statement can add it.
TEST(Size, IndirectStatementAddsIndirectUsage)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(BufferOfBytes(k_Counters, 256));
    graph.Declare(BufferOfBytes(k_Other, 256));
    graph.Declare(BufferOfBytes(k_Values, 256));

    const auto count = [](loom::DispatchBuilder& pass)
    {
        CountWith(pass, k_Counters, k_Counters);
    };

    const auto indirect = [](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, "ReadCS");
        pass.Bind("CountersIn", k_Other);
        pass.Bind("Values", k_Values);
        pass.DispatchIndirect(k_Counters, 0);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, count);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, indirect);

    SizeRun run;
    RunSize(graph, run);

    ASSERT_TRUE(run.Sized.has_value());
    EXPECT_TRUE(HasUsage(SizedOf(run, k_Counters.ToRef()).Usage, lodestone::BufferUsageFlags::Indirect));
    EXPECT_FALSE(HasUsage(SizedOf(run, k_Other.ToRef()).Usage, lodestone::BufferUsageFlags::Indirect));
}
