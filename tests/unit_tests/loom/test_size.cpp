#include "Declarations.hpp"
#include "ResourceFlags.hpp"
#include "PassStatements.hpp"
#include "size/Size.hpp"
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
#include "utility/Delegate.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <string_view>
#include <gtest/gtest.h>
#include <utility>

// The Size stage, and Bind's view rule (LM307), against LoomSizeTest (tests/unit_tests/loom/shaders).
// Counters is 64 uints and CountersIn is a view of it as 16 uint4s: 256 bytes either way. Fixed is 32 floats (128
// bytes). Values states no size. Args is 3 uints of indirect dispatch arguments. Albedo states 64x32 and Small 16x16;
// Ids samples uint, Volume is 3D, and Lit is an rgba32f storage image.

using namespace freeze_tests;

namespace
{

    constexpr std::string_view k_Module = "LoomSizeTest";

    constexpr loom::ResourceKey<loom::Buffer> k_Counters{ "Size.Counters" };
    constexpr loom::ResourceKey<loom::Buffer> k_Other{ "Size.Other" };
    constexpr loom::ResourceKey<loom::Buffer> k_Values{ "Size.Values" };
    constexpr loom::ResourceKey<loom::Buffer> k_Args{ "Size.Args" };
    constexpr loom::PassKey k_Second{ "Test.Second" };
    constexpr loom::ResourceKey<loom::Image> k_Image{ "Size.Image" };
    constexpr loom::ResourceKey<loom::Image> k_Ids{ "Size.Ids" };
    constexpr loom::ResourceKey<loom::Image> k_Lit{ "Size.Lit" };

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

    loom::ImageDeclaration ImageOf(loom::ResourceKey<loom::Image> key, lodestone::TextureFormat format,
                                   loom::ExtentSource extent)
    {
        loom::ImageDeclaration image = MakeImageOfTarget(k_Backbuffer);
        image.Key = key;
        image.Format = format;
        image.Extent = extent;
        return image;
    }

    // Ids and Lit as ShadeCS states them: uint and rgba32f, 64x32. `albedo` is the image under test.
    void DeclareShadeImages(loom::GraphBuilder& graph, const loom::ImageDeclaration& albedo)
    {
        graph.Declare(albedo);
        graph.Declare(ImageOf(k_Ids, lodestone::TextureFormat::R32Uint, loom::ExtentSource::Fixed(64, 32, 1)));
        graph.Declare(ImageOf(k_Lit, lodestone::TextureFormat::RGBA32Float, loom::ExtentSource::Fixed(64, 32, 1)));
    }

    void ShadeWith(loom::DispatchBuilder& pass, loom::ResourceKey<loom::Image> albedo)
    {
        pass.Shader(k_Module, "ShadeCS");
        pass.Bind("Albedo", albedo);
        pass.Bind("Ids", k_Ids);
        pass.Bind("Lit", k_Lit);
        pass.DispatchThreads(8, 4, 1);
    }

    // SmallCS or VolumeCS, with its one sampled binding on `image`
    void ReadImageWith(loom::DispatchBuilder& pass, std::string_view entry_point, std::string_view binding,
                       loom::ResourceKey<loom::Image> image)
    {
        pass.Shader(k_Module, entry_point);
        pass.Bind(binding, image);
        pass.Bind("Lit", k_Lit);
        pass.DispatchThreads(8, 4, 1);
    }

    void RecordNothing(loom::RecordContext&)
    {
    }

    bool HasTextureUsage(lodestone::TextureUsageFlags usage, lodestone::TextureUsageFlags flag)
    {
        return (usage & flag) == flag;
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
    EXPECT_TRUE(HasUsage(SizedOf(run, k_Counters.ToRef()).BufferUsage, lodestone::BufferUsageFlags::Storage));
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
            EXPECT_TRUE(HasUsage(SizedOf(run, k_Args.ToRef()).BufferUsage, lodestone::BufferUsageFlags::Indirect));
            EXPECT_TRUE(HasUsage(SizedOf(run, k_Args.ToRef()).BufferUsage, lodestone::BufferUsageFlags::Storage));
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
    EXPECT_TRUE(HasUsage(SizedOf(run, k_Counters.ToRef()).BufferUsage, lodestone::BufferUsageFlags::Indirect));
    EXPECT_FALSE(HasUsage(SizedOf(run, k_Other.ToRef()).BufferUsage, lodestone::BufferUsageFlags::Indirect));
}

// Images take their extent, dimension and usage from their bindings. Albedo's derived extent is the 64x32 that the
// shader states. The fixed images keep their declared extent.
TEST(SizeImages, ExtentDimensionAndUsageComeFromTheBindings)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareShadeImages(graph, ImageOf(k_Image, lodestone::TextureFormat::RGBA8Unorm, loom::ExtentSource::Derived()));

    const auto shade = [](loom::DispatchBuilder& pass)
    {
        ShadeWith(pass, k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, shade);

    SizeRun run;
    RunSize(graph, run);

    ASSERT_TRUE(run.Sized.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());

    const loom::SizedResource& image = SizedOf(run, k_Image.ToRef());
    EXPECT_EQ(image.Extent.Width, 64);
    EXPECT_EQ(image.Extent.Height, 32);
    EXPECT_EQ(image.Extent.Depth, 1);
    EXPECT_EQ(image.Dimension, lodestone::TextureDimension::Texture2D);
    EXPECT_EQ(image.TextureUsage, lodestone::TextureUsageFlags::Sampled);
    EXPECT_EQ(SizedOf(run, k_Lit.ToRef()).TextureUsage, lodestone::TextureUsageFlags::Storage);
    EXPECT_EQ(SizedOf(run, k_Ids.ToRef()).Extent.Width, 64);
}

// LM403: a storage binding's format must equal the image's. Lit is rgba32f in the shader. OtherWhere is the
// declaration, which states the format.
TEST(SizeImages, StorageFormatThatDiffersIsLM403)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(ImageOf(k_Image, lodestone::TextureFormat::RGBA8Unorm, loom::ExtentSource::Derived()));
    graph.Declare(ImageOf(k_Ids, lodestone::TextureFormat::R32Uint, loom::ExtentSource::Fixed(64, 32, 1)));
    const uint32_t declarationLine = std::source_location::current().line() + 1u;
    graph.Declare(ImageOf(k_Lit, lodestone::TextureFormat::RGBA16Float, loom::ExtentSource::Fixed(64, 32, 1)));

    const auto shade = [](loom::DispatchBuilder& pass)
    {
        ShadeWith(pass, k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, shade);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM403);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Binding, "Lit");
    EXPECT_EQ(diagnostic.Resource, k_Lit.ToRef());
    EXPECT_EQ(diagnostic.Expected.Text, "RGBA16Float");
    EXPECT_EQ(diagnostic.Actual.Text, "RGBA32Float");
    EXPECT_EQ(diagnostic.OtherWhere.line(), declarationLine);
}

// LM403: a sampled binding must read the scalar class of the format. Ids reads uint from an unorm image.
TEST(SizeImages, SampledClassThatDiffersIsLM403)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(ImageOf(k_Image, lodestone::TextureFormat::RGBA8Unorm, loom::ExtentSource::Derived()));
    graph.Declare(ImageOf(k_Ids, lodestone::TextureFormat::RGBA8Unorm, loom::ExtentSource::Fixed(64, 32, 1)));
    graph.Declare(ImageOf(k_Lit, lodestone::TextureFormat::RGBA32Float, loom::ExtentSource::Fixed(64, 32, 1)));

    const auto shade = [](loom::DispatchBuilder& pass)
    {
        ShadeWith(pass, k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, shade);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM403);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Binding, "Ids");
    EXPECT_EQ(diagnostic.Expected.Text, "RGBA8Unorm");
    EXPECT_EQ(diagnostic.Actual.FieldName, "SampleType");
    EXPECT_EQ(diagnostic.Actual.Text, "UnsignedInteger");
}

// A float binding reads a depth format: Albedo on a Depth32Float image is no error.
TEST(SizeImages, FloatBindingReadsADepthFormat)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareShadeImages(graph, ImageOf(k_Image, lodestone::TextureFormat::Depth32Float, loom::ExtentSource::Derived()));

    const auto shade = [](loom::DispatchBuilder& pass)
    {
        ShadeWith(pass, k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, shade);

    SizeRun run;
    RunSize(graph, run);

    ASSERT_TRUE(run.Sized.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());
}

// LM401: Albedo states 64x32 and Small 16x16 for one derived image. The first binding sets the extent, and the first
// axis that differs is reported.
TEST(SizeImages, StatedExtentsThatDisagreeAreLM401)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareShadeImages(graph, ImageOf(k_Image, lodestone::TextureFormat::RGBA8Unorm, loom::ExtentSource::Derived()));

    const auto shade = [](loom::DispatchBuilder& pass)
    {
        ShadeWith(pass, k_Image);
    };

    const auto small = [](loom::DispatchBuilder& pass)
    {
        ReadImageWith(pass, "SmallCS", "Small", k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, shade);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, small);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM401);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Binding, "Small");
    EXPECT_EQ(diagnostic.Pass.Hash, k_Second.Hash);
    EXPECT_EQ(diagnostic.Expected.FieldName, "Width");
    EXPECT_EQ(diagnostic.Expected.Integer, 64);
    EXPECT_EQ(diagnostic.Actual.Integer, 16);
}

// LM401: a fixed 64x16 image under Albedo's stated 64x32. The width agrees, so the height is reported. OtherWhere is
// the declaration.
TEST(SizeImages, FixedExtentThatDoesNotFitIsLM401)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    const loom::ImageDeclaration image = ImageOf(k_Image, lodestone::TextureFormat::RGBA8Unorm,
                                                 loom::ExtentSource::Fixed(64, 16, 1));
    const uint32_t declarationLine = std::source_location::current().line() + 1u;
    graph.Declare(image);
    graph.Declare(ImageOf(k_Ids, lodestone::TextureFormat::R32Uint, loom::ExtentSource::Fixed(64, 32, 1)));
    graph.Declare(ImageOf(k_Lit, lodestone::TextureFormat::RGBA32Float, loom::ExtentSource::Fixed(64, 32, 1)));

    const auto shade = [](loom::DispatchBuilder& pass)
    {
        ShadeWith(pass, k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, shade);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM401);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Expected.FieldName, "Height");
    EXPECT_EQ(diagnostic.Expected.Integer, 16);
    EXPECT_EQ(diagnostic.Actual.Integer, 32);
    EXPECT_EQ(diagnostic.OtherWhere.line(), declarationLine);
}

// LM402: a derived extent that no binding states. Ids states none.
TEST(SizeImages, DerivedExtentWithNoSourceIsLM402)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(ImageOf(k_Image, lodestone::TextureFormat::RGBA8Unorm, loom::ExtentSource::Derived()));
    graph.Declare(ImageOf(k_Ids, lodestone::TextureFormat::R32Uint, loom::ExtentSource::Derived()));
    graph.Declare(ImageOf(k_Lit, lodestone::TextureFormat::RGBA32Float, loom::ExtentSource::Fixed(64, 32, 1)));

    const auto shade = [](loom::DispatchBuilder& pass)
    {
        ShadeWith(pass, k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, shade);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM402);
    EXPECT_EQ(run.Sink.Diagnostics().front().Resource, k_Ids.ToRef());
}

// Layered states 16x16 with 3 layers. A derived image takes both: the extent's depth is 1, and the count is 3.
TEST(SizeImages, DerivedLayersComeFromTheBinding)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    loom::ImageDeclaration image = ImageOf(k_Image, lodestone::TextureFormat::RGBA8Unorm, loom::ExtentSource::Derived());
    image.Layers = loom::LayerSource::Derived();
    graph.Declare(image);
    graph.Declare(ImageOf(k_Lit, lodestone::TextureFormat::RGBA32Float, loom::ExtentSource::Fixed(64, 32, 1)));

    const auto layered = [](loom::DispatchBuilder& pass)
    {
        ReadImageWith(pass, "LayeredCS", "Layered", k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, layered);

    SizeRun run;
    RunSize(graph, run);

    ASSERT_TRUE(run.Sized.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());

    const loom::SizedResource& sized = SizedOf(run, k_Image.ToRef());
    EXPECT_EQ(sized.Extent.Width, 16);
    EXPECT_EQ(sized.Extent.Height, 16);
    EXPECT_EQ(sized.Extent.Depth, 1);
    EXPECT_EQ(sized.Layers, 3);
    EXPECT_TRUE(sized.Arrayed);
}

// LM401: Layered states 3 layers and Layered4 states 4 for one derived image. The first binding sets the count.
TEST(SizeImages, StatedLayersThatDisagreeAreLM401)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    loom::ImageDeclaration image = ImageOf(k_Image, lodestone::TextureFormat::RGBA8Unorm, loom::ExtentSource::Derived());
    image.Layers = loom::LayerSource::Derived();
    graph.Declare(image);
    graph.Declare(ImageOf(k_Lit, lodestone::TextureFormat::RGBA32Float, loom::ExtentSource::Fixed(64, 32, 1)));

    const auto layered = [](loom::DispatchBuilder& pass)
    {
        ReadImageWith(pass, "LayeredCS", "Layered", k_Image);
    };

    const auto layered4 = [](loom::DispatchBuilder& pass)
    {
        ReadImageWith(pass, "Layered4CS", "Layered4", k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, layered);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, layered4);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM401);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Binding, "Layered4");
    EXPECT_EQ(diagnostic.Pass.Hash, k_Second.Hash);
    EXPECT_EQ(diagnostic.Expected.FieldName, "Layers");
    EXPECT_EQ(diagnostic.Expected.Integer, 3);
    EXPECT_EQ(diagnostic.Actual.Integer, 4);
}

// LM401: a fixed count of 5 under Layered's stated 3. The fixed 16x16x1 extent fits, because the stated third value
// is the layer count and not a depth. OtherWhere is the declaration.
TEST(SizeImages, FixedLayersThatDoNotFitAreLM401)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    loom::ImageDeclaration image = ImageOf(k_Image, lodestone::TextureFormat::RGBA8Unorm,
                                           loom::ExtentSource::Fixed(16, 16, 1));
    image.Layers = loom::LayerSource::Fixed(5);
    const uint32_t declarationLine = std::source_location::current().line() + 1u;
    graph.Declare(image);
    graph.Declare(ImageOf(k_Lit, lodestone::TextureFormat::RGBA32Float, loom::ExtentSource::Fixed(64, 32, 1)));

    const auto layered = [](loom::DispatchBuilder& pass)
    {
        ReadImageWith(pass, "LayeredCS", "Layered", k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, layered);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM401);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Expected.FieldName, "Layers");
    EXPECT_EQ(diagnostic.Expected.Integer, 5);
    EXPECT_EQ(diagnostic.Actual.Integer, 3);
    EXPECT_EQ(diagnostic.OtherWhere.line(), declarationLine);
}

// LM402: a derived count that no binding states. Albedo is a 2D texture, so it states an extent and no layer count.
TEST(SizeImages, DerivedLayersWithNoSourceIsLM402)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    loom::ImageDeclaration image = ImageOf(k_Image, lodestone::TextureFormat::RGBA8Unorm, loom::ExtentSource::Derived());
    image.Layers = loom::LayerSource::Derived();
    DeclareShadeImages(graph, image);

    const auto shade = [](loom::DispatchBuilder& pass)
    {
        ShadeWith(pass, k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, shade);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM402);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Resource, k_Image.ToRef());
    EXPECT_EQ(diagnostic.Expected.FieldName, "Layers");
}

// LM403: Albedo sees the image as 2D and Volume as 3D. The first binding sets the dimension.
TEST(SizeImages, DimensionsThatDisagreeAreLM403)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareShadeImages(graph, ImageOf(k_Image, lodestone::TextureFormat::RGBA8Unorm, loom::ExtentSource::Derived()));

    const auto shade = [](loom::DispatchBuilder& pass)
    {
        ShadeWith(pass, k_Image);
    };

    const auto volume = [](loom::DispatchBuilder& pass)
    {
        ReadImageWith(pass, "VolumeCS", "Volume", k_Image);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, shade);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, volume);

    SizeRun run;
    RunSize(graph, run);

    EXPECT_FALSE(run.Sized.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM403);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Binding, "Volume");
    EXPECT_EQ(diagnostic.Expected.Text, "Texture2D");
    EXPECT_EQ(diagnostic.Actual.Text, "Texture3D");
}

// A target statement needs a format of its kind: a color target a color format, a depth target a depth format. A
// fitting target gives RenderAttachment usage and a 2D image.
TEST(SizeImages, TargetFormatsMustFitTheirUse)
{
    struct TargetCase
    {
        lodestone::TextureFormat Format;
        loom::ImageUse Use;
        bool Fits;
    };

    const std::array<TargetCase, 4u> cases
    {
        TargetCase{ lodestone::TextureFormat::RGBA8Unorm, loom::ImageUse::ColorTarget, true },
        TargetCase{ lodestone::TextureFormat::Depth32Float, loom::ImageUse::DepthTarget, true },
        TargetCase{ lodestone::TextureFormat::Depth32Float, loom::ImageUse::ColorTarget, false },
        TargetCase{ lodestone::TextureFormat::RGBA8Unorm, loom::ImageUse::DepthTarget, false },
    };

    for (const TargetCase& target : cases)
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        graph.Declare(ImageOf(k_Image, target.Format, loom::ExtentSource::Fixed(64, 32, 1)));

        const auto build = [&target](loom::RecordBuilder& pass)
        {
            pass.Uses(k_Image, loom::ResourceAccess::Write, target.Use);
            pass.Record(Delegate<void(loom::RecordContext&)>::Create<&RecordNothing>());
        };

        graph.AddRecord(k_First, k_EveryFrame, k_Graphics, build);

        SizeRun run;
        RunSize(graph, run);

        if (target.Fits)
        {
            ASSERT_TRUE(run.Sized.has_value());
            EXPECT_TRUE(run.Sink.Diagnostics().empty());

            const loom::SizedResource& image = SizedOf(run, k_Image.ToRef());
            EXPECT_TRUE(HasTextureUsage(image.TextureUsage, lodestone::TextureUsageFlags::RenderAttachment));
            EXPECT_EQ(image.Dimension, lodestone::TextureDimension::Texture2D);
            continue;
        }

        EXPECT_FALSE(run.Sized.has_value());
        ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM403);
        EXPECT_EQ(run.Sink.Diagnostics().front().Actual.FieldName, "Use");
    }
}

// An Exact import promises its extent, so stated extents are checked against it: 64x16 under Albedo's 64x32 is
// LM401. An AnyExtent import brings its extent at Realize, so Size checks nothing and leaves the extent unknown.
TEST(SizeImages, ImportsCheckOnlyAPromisedExtent)
{
    for (const bool exact : { true, false })
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        const lodestone::TextureFormat format = lodestone::TextureFormat::RGBA8Unorm;
        const loom::ImageExpectation expectation = exact ? loom::ImageExpectation::Exact(format, 64, 16, 1, 1, 1) :
                                                           loom::ImageExpectation::AnyExtent(format, 1);
        graph.Import(loom::ImportDeclaration{ .Key = k_Image, .Expectation = expectation });
        graph.Declare(ImageOf(k_Ids, lodestone::TextureFormat::R32Uint, loom::ExtentSource::Fixed(64, 32, 1)));
        graph.Declare(ImageOf(k_Lit, lodestone::TextureFormat::RGBA32Float, loom::ExtentSource::Fixed(64, 32, 1)));

        const auto shade = [](loom::DispatchBuilder& pass)
        {
            ShadeWith(pass, k_Image);
        };

        graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, shade);

        SizeRun run;
        RunSize(graph, run);

        if (exact)
        {
            EXPECT_FALSE(run.Sized.has_value());
            ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM401);
            EXPECT_EQ(run.Sink.Diagnostics().front().Expected.FieldName, "Height");
            continue;
        }

        ASSERT_TRUE(run.Sized.has_value());
        EXPECT_TRUE(run.Sink.Diagnostics().empty());

        const loom::SizedResource& image = SizedOf(run, k_Image.ToRef());
        EXPECT_EQ(image.Extent.Width, loom::k_UnknownExtent);
        EXPECT_EQ(image.TextureUsage, lodestone::TextureUsageFlags::Sampled);
    }
}
