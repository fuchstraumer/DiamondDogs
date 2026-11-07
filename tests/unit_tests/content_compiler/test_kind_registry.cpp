#include <gtest/gtest.h>

#include "test_kinds.hpp"
#include "ContentDiagnostic.hpp"
#include "KindRegistry.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace badkinds
{
    // each kind below breaks one rule of the Kind concept
    struct InvalidAcceptsKind
    {
        static constexpr std::string_view Name{ "bad.InvalidAccepts" };
        static constexpr uint32_t Version{ 1 };
        using Params = cctest::ScaleParams;
        static constexpr std::array Slots{ cc::Slot{ "Source", cc::SlotUse::Required, cc::ArtifactKind::Invalid } };
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::Result> Run(const Params&, cc::Inputs, cc::Context&);
    };

    struct DuplicateSlotKind
    {
        static constexpr std::string_view Name{ "bad.DuplicateSlot" };
        static constexpr uint32_t Version{ 1 };
        using Params = cctest::ScaleParams;
        static constexpr std::array Slots
        {
            cc::Slot{ "Source", cc::SlotUse::Required, cc::ArtifactKind::CpuBytes },
            cc::Slot{ "Source", cc::SlotUse::Optional, cc::ArtifactKind::CpuBytes },
        };
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::Result> Run(const Params&, cc::Inputs, cc::Context&);
    };

    struct NoOutputsKind
    {
        static constexpr std::string_view Name{ "bad.NoOutputs" };
        static constexpr uint32_t Version{ 1 };
        using Params = cctest::ScaleParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array<cc::Output, 0> Outputs{};

        static foundation::Task<cc::Result> Run(const Params&, cc::Inputs, cc::Context&);
    };

    struct EmptyOutputNameKind
    {
        static constexpr std::string_view Name{ "bad.EmptyOutputName" };
        static constexpr uint32_t Version{ 1 };
        using Params = cctest::ScaleParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs{ cc::Output{ "", cc::ArtifactKind::CpuBytes } };

        static foundation::Task<cc::Result> Run(const Params&, cc::Inputs, cc::Context&);
    };

    struct DuplicateOutputNameKind
    {
        static constexpr std::string_view Name{ "bad.DuplicateOutputName" };
        static constexpr uint32_t Version{ 1 };
        using Params = cctest::ScaleParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ "Height", cc::ArtifactKind::GpuImage },
            cc::Output{ "Height", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::Result> Run(const Params&, cc::Inputs, cc::Context&);
    };

    struct InvalidProducesKind
    {
        static constexpr std::string_view Name{ "bad.InvalidProduces" };
        static constexpr uint32_t Version{ 1 };
        using Params = cctest::ScaleParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs{ cc::Output{ "Value", cc::ArtifactKind::Invalid } };

        static foundation::Task<cc::Result> Run(const Params&, cc::Inputs, cc::Context&);
    };

    struct UndescribedParams
    {
        int32_t Number{ 0 };
    };

    struct UndescribedParamsKind
    {
        static constexpr std::string_view Name{ "bad.UndescribedParams" };
        static constexpr uint32_t Version{ 1 };
        using Params = UndescribedParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::Result> Run(const Params&, cc::Inputs, cc::Context&);
    };

    struct WrongRunKind
    {
        static constexpr std::string_view Name{ "bad.WrongRun" };
        static constexpr uint32_t Version{ 1 };
        using Params = cctest::ScaleParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static int Run(const Params&, cc::Inputs, cc::Context&);
    };
}

static_assert(cc::Kind<cctest::ConstKind>);
static_assert(cc::Kind<cctest::ScaleKind>);
static_assert(cc::Kind<cctest::BlendKind>);
static_assert(!cc::Kind<badkinds::InvalidAcceptsKind>);
static_assert(!cc::Kind<badkinds::DuplicateSlotKind>);
static_assert(cc::Kind<cctest::MultiKind>);
static_assert(!cc::Kind<badkinds::NoOutputsKind>);
static_assert(!cc::Kind<badkinds::EmptyOutputNameKind>);
static_assert(!cc::Kind<badkinds::DuplicateOutputNameKind>);
static_assert(!cc::Kind<badkinds::InvalidProducesKind>);
static_assert(!cc::Kind<badkinds::UndescribedParamsKind>);
static_assert(!cc::Kind<badkinds::WrongRunKind>);

TEST(KindRegistryTest, RegisterReturnsIdsThatStartAtOne)
{
    cc::KindRegistry registry;

    const std::expected<cc::KindId, cc::Diagnostic> first = registry.Register<cctest::ConstKind>();
    const std::expected<cc::KindId, cc::Diagnostic> second = registry.Register<cctest::ScaleKind>();

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->Value, 1u);
    EXPECT_EQ(second->Value, 2u);
    EXPECT_EQ(registry.Count(), 2u);
}

TEST(KindRegistryTest, FindReturnsTheIdOfARegisteredName)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();

    const std::optional<cc::KindId> scale = registry.Find("test.Scale");

    ASSERT_TRUE(scale.has_value());
    EXPECT_EQ(registry.Ops(*scale).Name, "test.Scale");
    EXPECT_FALSE(registry.Find("test.Missing").has_value());
}

TEST(KindRegistryTest, OpsStoreWhatTheKindDeclared)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    const cc::KindOps& blend = registry.Ops(*registry.Find("test.Blend"));

    EXPECT_EQ(blend.Version, 2u);
    EXPECT_EQ(blend.ParamsType, cc::ParamsTypeIdentity<cctest::BlendParams>());
    EXPECT_EQ(blend.ParamsTypeName, "BlendParams");
    EXPECT_NE(blend.Run, nullptr);

    ASSERT_EQ(blend.Slots.size(), 3u);
    EXPECT_EQ(blend.Slots[0].Name, "A");
    EXPECT_EQ(blend.Slots[0].Use, cc::SlotUse::Required);
    EXPECT_EQ(blend.Slots[2].Name, "Mask");
    EXPECT_EQ(blend.Slots[2].Use, cc::SlotUse::Optional);

    const cc::KindOps& image = registry.Ops(*registry.Find("test.Image"));

    ASSERT_EQ(blend.Outputs.size(), 1u);
    EXPECT_EQ(blend.Outputs[0].Name, "Value");
    EXPECT_EQ(blend.Outputs[0].Produces, cc::ArtifactKind::CpuBytes);

    ASSERT_EQ(image.Outputs.size(), 1u);
    EXPECT_EQ(image.Outputs[0].Name, "Image");
    EXPECT_EQ(image.Outputs[0].Produces, cc::ArtifactKind::GpuImage);
    EXPECT_TRUE(image.Slots.empty());

    const cc::KindOps& multi = registry.Ops(*registry.Find("test.Multi"));

    ASSERT_EQ(multi.Outputs.size(), 3u);
    EXPECT_EQ(multi.Outputs[0].Name, "Height");
    EXPECT_EQ(multi.Outputs[1].Name, "Normal");
    EXPECT_EQ(multi.Outputs[2].Name, "Stats");
    EXPECT_EQ(multi.Outputs[2].Produces, cc::ArtifactKind::CpuBytes);
}

TEST(KindRegistryTest, ASecondRegisterOfTheSameNameReturnsDuplicateKindName)
{
    cc::KindRegistry registry;
    ASSERT_TRUE(registry.Register<cctest::ScaleKind>().has_value());

    const std::expected<cc::KindId, cc::Diagnostic> again = registry.Register<cctest::ScaleKind>();

    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().Code, cc::DiagnosticCode::DuplicateKindName);
    EXPECT_EQ(again.error().Level, cc::Severity::Error);
    EXPECT_EQ(registry.Count(), 1u);
}

TEST(KindRegistryTest, FreezeIsReported)
{
    cc::KindRegistry registry;

    EXPECT_FALSE(registry.IsFrozen());

    registry.Freeze();

    EXPECT_TRUE(registry.IsFrozen());
}
