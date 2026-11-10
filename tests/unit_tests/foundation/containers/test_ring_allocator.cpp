#include <gtest/gtest.h>

#include "containers/RingAllocator.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <vector>

namespace containers
{

namespace
{
    /** The second opinion for RingAllocator. It shares no code with it and no representation either. Addresses are
     *  virtual and never wrap: virtual address v is the ring offset v % capacity in pass v / capacity. A range is
     *  valid when it stays inside one pass, when its ring offset is a multiple of the alignment, and when the span from
     *  the oldest live range to its end is at most one capacity. The policy is the one RingAllocator documents: try the
     *  bytes after the newest range, then the start of the next pass. */
    class RingModel
    {
    public:
        RingModel(uint64_t _capacity, int32_t max_live_ranges);
        [[nodiscard]] std::optional<RingRange> Allocate(uint64_t size, uint64_t alignment, uint64_t tag);
        void ReleaseThrough(uint64_t tag);
        [[nodiscard]] uint64_t UsedBytes() const;
        [[nodiscard]] int32_t LiveRanges() const;
        /** The live ranges as ring offsets, oldest first. */
        [[nodiscard]] std::vector<RingRange> LiveRingRanges() const;

    private:
        struct ModelRange
        {
            uint64_t VirtualStart;
            uint64_t Size;
            uint64_t Tag;
        };

        [[nodiscard]] bool fits(uint64_t virtual_start, uint64_t size) const;
        [[nodiscard]] uint64_t oldestVirtualStart() const;

        uint64_t capacity;
        int32_t maxRanges;
        uint64_t virtualTail{ 0u };
        std::deque<ModelRange> live;
    };

    [[nodiscard]] uint64_t NextPassStart(uint64_t virtual_address, uint64_t capacity);
    [[nodiscard]] bool RangesOverlap(const RingRange& first, const RingRange& second);

    constexpr std::array<uint64_t, 10> k_Alignments{ 1u, 2u, 3u, 4u, 8u, 12u, 16u, 24u, 64u, 100u };
}

class RingAllocatorTest : public ::testing::Test
{
protected:
    struct Config
    {
        uint64_t Capacity;
        int32_t MaxRanges;
        uint64_t MaxSize;
    };

    // runs the allocator and the model together and compares every answer
    void RunAgainstModel(const Config& config, uint32_t seed, int32_t steps);
};

void RingAllocatorTest::RunAgainstModel(const Config& config, uint32_t seed, int32_t steps)
{
    RingAllocator<uint64_t> ring(config.Capacity, config.MaxRanges);
    RingModel model(config.Capacity, config.MaxRanges);
    std::mt19937 random(seed);
    std::uniform_int_distribution<uint64_t> sizeDistribution(1u, config.MaxSize);
    std::uniform_int_distribution<size_t> alignmentDistribution(0u, k_Alignments.size() - 1u);
    std::uniform_int_distribution<int32_t> percent(0, 99);

    uint64_t newestTag = 0u;
    uint64_t releasedTag = 0u;
    int32_t successes = 0;
    int32_t refusals = 0;

    for (int32_t step = 0; step < steps; ++step)
    {
        if (percent(random) < 60)
        {
            // two ranges in three share the tag of the one before, as the transfers of one batch do
            if (percent(random) < 33)
            {
                newestTag += 1u;
            }

            const uint64_t size = sizeDistribution(random);
            const uint64_t alignment = k_Alignments[alignmentDistribution(random)];
            const std::optional<RingRange> expected = model.Allocate(size, alignment, newestTag);
            const std::optional<RingRange> actual = ring.Allocate(size, alignment, newestTag);

            ASSERT_EQ(actual.has_value(), expected.has_value()) << "step " << step;

            if (actual.has_value())
            {
                successes += 1;
                ASSERT_EQ(actual->Offset, expected->Offset) << "step " << step;
                ASSERT_EQ(actual->Size, expected->Size) << "step " << step;
                ASSERT_EQ(actual->Offset % alignment, 0u) << "step " << step;
                ASSERT_LE(actual->Offset + actual->Size, config.Capacity) << "step " << step;

                // the model lists the live ranges, the new one last. none of the others may overlap it
                const std::vector<RingRange> liveRanges = model.LiveRingRanges();
                for (size_t i = 0u; i + 1u < liveRanges.size(); ++i)
                {
                    ASSERT_FALSE(RangesOverlap(*actual, liveRanges[i])) << "step " << step << " live range " << i;
                }
            }
            else
            {
                refusals += 1;
            }
        }
        else
        {
            std::uniform_int_distribution<uint64_t> tagDistribution(releasedTag, newestTag);
            releasedTag = percent(random) < 10 ? newestTag : tagDistribution(random);
            model.ReleaseThrough(releasedTag);
            ring.ReleaseThrough(releasedTag);
        }

        ASSERT_EQ(ring.UsedBytes(), model.UsedBytes()) << "step " << step;
        ASSERT_EQ(ring.LiveRanges(), model.LiveRanges()) << "step " << step;
        ASSERT_LE(ring.UsedBytes(), ring.Capacity()) << "step " << step;
    }

    // a run that only succeeds, or only refuses, proves little
    EXPECT_GT(successes, steps / 10);
    EXPECT_GT(refusals, steps / 100);
}

TEST_F(RingAllocatorTest, MatchesTheModelWithAPowerOfTwoCapacity)
{
    RunAgainstModel(Config{ 4096u, 64, 1024u }, 1u, 100'000);
}

TEST_F(RingAllocatorTest, MatchesTheModelWithACapacityThatIsNotAMultipleOfAnyAlignment)
{
    RunAgainstModel(Config{ 1000u, 16, 400u }, 2u, 100'000);
}

TEST_F(RingAllocatorTest, MatchesTheModelWithManyTinyRangesAndARoomyRecordArray)
{
    RunAgainstModel(Config{ 397u, 1000, 60u }, 3u, 100'000);
}

TEST_F(RingAllocatorTest, MatchesTheModelWhenRangesCanFillTheRing)
{
    RunAgainstModel(Config{ 512u, 8, 700u }, 4u, 100'000);
}

TEST_F(RingAllocatorTest, AllocatesInOrderAndAligns)
{
    RingAllocator<uint64_t> ring(1024u, 8);

    const std::optional<RingRange> first = ring.Allocate(5u, 1u, 1u);
    const std::optional<RingRange> second = ring.Allocate(4u, 12u, 1u);
    const std::optional<RingRange> third = ring.Allocate(8u, 16u, 2u);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(first->Offset, 0u);
    EXPECT_EQ(second->Offset, 12u);
    EXPECT_EQ(third->Offset, 16u);
    EXPECT_EQ(ring.LiveRanges(), 3);
    EXPECT_EQ(ring.UsedBytes(), 24u);
}

TEST_F(RingAllocatorTest, SkipsTheEndInsteadOfWrappingARange)
{
    RingAllocator<uint64_t> ring(100u, 8);

    ASSERT_TRUE(ring.Allocate(60u, 1u, 1u).has_value());
    ASSERT_TRUE(ring.Allocate(30u, 1u, 2u).has_value());

    // 90 + 20 passes the end, and the start is not free yet
    EXPECT_FALSE(ring.Allocate(20u, 1u, 3u).has_value());

    ring.ReleaseThrough(1u);
    const std::optional<RingRange> wrapped = ring.Allocate(20u, 1u, 3u);

    ASSERT_TRUE(wrapped.has_value());
    EXPECT_EQ(wrapped->Offset, 0u);

    // the skipped 10 bytes at the end count as used until the oldest range goes
    EXPECT_EQ(ring.UsedBytes(), 60u);
    EXPECT_EQ(ring.LiveRanges(), 2);
}

TEST_F(RingAllocatorTest, AnEmptyRingRestartsAtTheStart)
{
    RingAllocator<uint64_t> ring(100u, 8);

    ASSERT_TRUE(ring.Allocate(40u, 1u, 1u).has_value());
    ring.ReleaseThrough(1u);
    EXPECT_EQ(ring.UsedBytes(), 0u);
    EXPECT_EQ(ring.LiveRanges(), 0);

    const std::optional<RingRange> whole = ring.Allocate(100u, 1u, 2u);

    ASSERT_TRUE(whole.has_value());
    EXPECT_EQ(whole->Offset, 0u);
    EXPECT_EQ(ring.UsedBytes(), 100u);
    EXPECT_FALSE(ring.Allocate(1u, 1u, 2u).has_value());
}

TEST_F(RingAllocatorTest, ARangeLargerThanTheCapacityNeverFits)
{
    RingAllocator<uint64_t> ring(100u, 8);

    EXPECT_FALSE(ring.Allocate(101u, 1u, 1u).has_value());
    EXPECT_EQ(ring.LiveRanges(), 0);
}

TEST_F(RingAllocatorTest, ARecordArrayThatIsFullRefusesWhileBytesAreFree)
{
    RingAllocator<uint64_t> ring(1024u, 4);

    for (uint64_t tag = 1u; tag <= 4u; ++tag)
    {
        ASSERT_TRUE(ring.Allocate(8u, 1u, tag).has_value());
    }

    EXPECT_EQ(ring.LiveRanges(), 4);
    EXPECT_LT(ring.UsedBytes(), ring.Capacity());
    EXPECT_FALSE(ring.Allocate(8u, 1u, 5u).has_value());

    ring.ReleaseThrough(1u);
    EXPECT_EQ(ring.LiveRanges(), 3);

    // the record array is a ring too, so the freed record is reused after the others
    const std::optional<RingRange> reused = ring.Allocate(8u, 1u, 5u);

    ASSERT_TRUE(reused.has_value());
    EXPECT_EQ(reused->Offset, 32u);
    EXPECT_FALSE(ring.Allocate(8u, 1u, 5u).has_value());
}

TEST_F(RingAllocatorTest, AReleaseFreesEveryRangeWithAnEqualOrLowerTag)
{
    RingAllocator<uint64_t> ring(1024u, 8);

    ASSERT_TRUE(ring.Allocate(8u, 1u, 1u).has_value());
    ASSERT_TRUE(ring.Allocate(8u, 1u, 1u).has_value());
    ASSERT_TRUE(ring.Allocate(8u, 1u, 3u).has_value());

    ring.ReleaseThrough(2u);
    EXPECT_EQ(ring.LiveRanges(), 1);

    ring.ReleaseThrough(2u);
    EXPECT_EQ(ring.LiveRanges(), 1);

    ring.ReleaseThrough(3u);
    EXPECT_EQ(ring.LiveRanges(), 0);
}

namespace
{
    RingModel::RingModel(uint64_t _capacity, int32_t max_live_ranges) :
      capacity{ _capacity },
      maxRanges{ max_live_ranges }
    {}

    std::optional<RingRange> RingModel::Allocate(uint64_t size, uint64_t alignment, uint64_t tag)
    {
        if (static_cast<int32_t>(live.size()) == maxRanges)
        {
            return std::nullopt;
        }

        // with nothing live, the next pass starts here
        if (live.empty())
        {
            virtualTail = NextPassStart(virtualTail, capacity);
        }

        // the bytes after the newest range, padded one byte at a time until the ring offset is aligned
        uint64_t afterNewest = virtualTail;
        while ((afterNewest % capacity) % alignment != 0u)
        {
            afterNewest += 1u;
        }

        const bool staysInPass = afterNewest / capacity == virtualTail / capacity;
        const uint64_t nextPass = NextPassStart(virtualTail, capacity);

        std::optional<uint64_t> chosen;
        if (staysInPass && fits(afterNewest, size))
        {
            chosen = afterNewest;
        }
        else if (fits(nextPass, size))
        {
            chosen = nextPass;
        }

        if (!chosen.has_value())
        {
            return std::nullopt;
        }

        live.push_back(ModelRange{ *chosen, size, tag });
        virtualTail = *chosen + size;
        return RingRange{ *chosen % capacity, size };
    }

    void RingModel::ReleaseThrough(uint64_t tag)
    {
        while (!live.empty() && live.front().Tag <= tag)
        {
            live.pop_front();
        }
    }

    uint64_t RingModel::UsedBytes() const
    {
        return live.empty() ? 0u : virtualTail - live.front().VirtualStart;
    }

    int32_t RingModel::LiveRanges() const
    {
        return static_cast<int32_t>(live.size());
    }

    std::vector<RingRange> RingModel::LiveRingRanges() const
    {
        std::vector<RingRange> ranges;
        for (const ModelRange& range : live)
        {
            ranges.push_back(RingRange{ range.VirtualStart % capacity, range.Size });
        }

        return ranges;
    }

    bool RingModel::fits(uint64_t virtual_start, uint64_t size) const
    {
        const bool oneCapacityAtMost = size <= capacity;
        const bool insideOnePass = virtual_start / capacity == (virtual_start + size - 1u) / capacity;
        const bool windowFits = virtual_start + size - oldestVirtualStart() <= capacity;
        return oneCapacityAtMost && insideOnePass && windowFits;
    }

    uint64_t RingModel::oldestVirtualStart() const
    {
        return live.empty() ? virtualTail : live.front().VirtualStart;
    }

    uint64_t NextPassStart(uint64_t virtual_address, uint64_t capacity)
    {
        return ((virtual_address + capacity - 1u) / capacity) * capacity;
    }

    bool RangesOverlap(const RingRange& first, const RingRange& second)
    {
        return first.Offset < second.Offset + second.Size && second.Offset < first.Offset + first.Size;
    }
}

} // namespace containers
