#include <gtest/gtest.h>

#include "test_executor_kinds.hpp"
#include "ArtifactTable.hpp"
#include "ExecutorTypes.hpp"

#include <cstdint>

TEST(ArtifactTableTest, AnAddedArtifactIsLiveAndReleaseFreesIt)
{
    cc::ArtifactTable table;

    const cc::ArtifactRef handle = table.Add(cctest::MakeFloatArtifact(3.0f));

    EXPECT_TRUE(handle.IsValid());
    EXPECT_TRUE(table.IsLive(handle));
    EXPECT_EQ(table.LiveCount(), 1);
    EXPECT_EQ(cctest::FloatOf(table.Get(handle)), 3.0f);

    table.Release(handle);

    EXPECT_FALSE(table.IsLive(handle));
    EXPECT_EQ(table.LiveCount(), 0);
}

TEST(ArtifactTableTest, ADefaultHandleIsNeverLive)
{
    cc::ArtifactTable table;
    const cc::ArtifactRef added = table.Add(cctest::MakeFloatArtifact(1.0f));

    EXPECT_FALSE(table.IsLive(cc::ArtifactRef{}));
    EXPECT_FALSE(table.IsLive(cc::ArtifactRef{ added.Slot, 0 }));
    EXPECT_FALSE(table.IsLive(cc::ArtifactRef{ added.Slot + 7, added.Generation }));

    table.Release(added);
}

TEST(ArtifactTableTest, AFreedSlotIsReusedWithANewGeneration)
{
    cc::ArtifactTable table;

    const cc::ArtifactRef first = table.Add(cctest::MakeFloatArtifact(1.0f));
    table.Release(first);

    const cc::ArtifactRef second = table.Add(cctest::MakeFloatArtifact(2.0f));

    EXPECT_EQ(second.Slot, first.Slot);
    EXPECT_NE(second.Generation, first.Generation);
    EXPECT_NE(second, first);
    EXPECT_FALSE(table.IsLive(first));
    EXPECT_TRUE(table.IsLive(second));
    EXPECT_EQ(cctest::FloatOf(table.Get(second)), 2.0f);
    EXPECT_EQ(table.LiveCount(), 1);

    table.Release(second);
}

TEST(ArtifactTableTest, HoldNeedsOneMoreReleaseBeforeTheArtifactIsFreed)
{
    cc::ArtifactTable table;

    const cc::ArtifactRef handle = table.Add(cctest::MakeFloatArtifact(4.0f));
    table.Hold(handle);

    EXPECT_EQ(table.LiveCount(), 1);

    table.Release(handle);

    EXPECT_TRUE(table.IsLive(handle));
    EXPECT_EQ(table.LiveCount(), 1);
    EXPECT_EQ(cctest::FloatOf(table.Get(handle)), 4.0f);

    table.Release(handle);

    EXPECT_FALSE(table.IsLive(handle));
    EXPECT_EQ(table.LiveCount(), 0);
}
