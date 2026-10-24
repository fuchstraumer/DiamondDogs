#include <gtest/gtest.h>

#include <xxhash.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
    // The test buffer of xxHash's sanity check (cli/xsum_sanity_check.c, XSUM_fillTestBuffer).
    std::array<uint8_t, 5> FillTestBuffer();
}

// The expected values are xxHash's own: XSUM_XXH128_testdata in Lodestone/third_party/xxHash/tests/sanity_test_vectors.h,
// the entries with length 0 and length 5, both with seed 0.
TEST(XxHashTest, Xxh3_128OfEmptyInputMatchesReferenceVector)
{
    const XXH128_hash_t hash = XXH3_128bits(nullptr, 0);

    EXPECT_EQ(hash.low64, 0x6001C324468D497FULL);
    EXPECT_EQ(hash.high64, 0x99AA06D3014798D8ULL);
}

TEST(XxHashTest, Xxh3_128OfFiveByteInputMatchesReferenceVector)
{
    const std::array<uint8_t, 5> input = FillTestBuffer();
    const XXH128_hash_t hash = XXH3_128bits(input.data(), input.size());

    EXPECT_EQ(hash.low64, 0x057C7ED2C01FA1D1ULL);
    EXPECT_EQ(hash.high64, 0x62ED587687606B4EULL);
}

namespace
{
    std::array<uint8_t, 5> FillTestBuffer()
    {
        constexpr uint64_t prime32 = 2654435761U;
        constexpr uint64_t prime64 = 11400714785074694797ULL;

        std::array<uint8_t, 5> buffer{};
        uint64_t byteGenerator = prime32;

        for (uint8_t& byte : buffer)
        {
            byte = static_cast<uint8_t>(byteGenerator >> 56);
            byteGenerator *= prime64;
        }

        return buffer;
    }
}
