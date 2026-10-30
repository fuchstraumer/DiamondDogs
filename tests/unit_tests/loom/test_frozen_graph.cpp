#include "FrozenGraph.hpp"
#include <gtest/gtest.h>

// Freeze's own tests go here: one broken graph per LM1xx code, each checking the code and the line of the call.

TEST(FrozenGraph, IndicesStartInvalid)
{
    const loom::FrozenPass pass{};

    EXPECT_FALSE(pass.Sequence.IsValid());
    EXPECT_FALSE(pass.Dispatch.Resource.IsValid());
    EXPECT_TRUE(loom::k_BeginFrameAnchorIndex.IsValid());
    EXPECT_NE(loom::k_BeginFrameAnchorIndex, loom::k_EndFrameAnchorIndex);
}
