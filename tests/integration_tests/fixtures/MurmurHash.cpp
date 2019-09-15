#include "MurmurHash.hpp"
#include <type_traits>
/*
    Implemented based on https://github.com/aappleby/smhasher/blob/master/src/MurmurHash3.cpp
*/
#if defined(_MSC_VER)

#define FORCE_INLINE __forceinline
#include <stdlib.h>

FORCE_INLINE uint32_t rotl32(uint32_t x, int r)
{
    return _rotl(x, r);
}

FORCE_INLINE uint64_t rotl64(uint64_t x, int r)
{
    return _rotl64(x, r);
}

#else

#define FORCE_INLINE __attribute__((always_inline))

FORCE_INLINE uint32_t rotl32(uint32_t x, int r)
{
    return (x << r) | (x >> (32 - r));
}

FORCE_INLINE uint64_t rotl64(uint64_t x, int r)
{
    return (x << r) | (x >> (64 - r));
}

#endif

FORCE_INLINE constexpr uint64_t fmix64(uint64_t k) noexcept
{
    k ^= k >> 33LLU;
    k *= 0xff51afd7ed558ccdLLU;
    k ^= k >> 33LLU;
    k *= 0xc4ceb9fe1a85ec53LLU;
    k ^= k >> 33LLU;
    return k;
}

//-----------------------------------------------------------------------------
// MurmurHash2 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.
// constexpr and noexcept hash is a fun thing
uint64_t MurmurHash2(const void* key, const size_t len, const uint64_t seed) noexcept
{
    static_assert(std::is_same_v<size_t, uint64_t>, "uint64_t and size_t need to be the same for this code to work!");
    static_assert(sizeof(void*) == 8u, "This hash only works on 64-bit platforms!");
    constexpr uint64_t hashConstantM = 0xc6a4a7935bd1e995LLU;
    constexpr uint64_t hashConstantR = 47LLU;

    uint64_t hashResult = seed ^ (static_cast<uint64_t>(len) * hashConstantM);

    const uint64_t* data = reinterpret_cast<const uint64_t*>(key);
    const uint64_t* end = data + (len / 8u);

    while (data != end)
    {
        uint64_t k = *data++;

        k *= hashConstantM;
        k ^= k >> hashConstantR;
        k *= hashConstantM;

        hashResult ^= k;
        hashResult *= hashConstantM;
    }

    const uint8_t* data2 = reinterpret_cast<const uint8_t*>(data);

    switch (len & 7)
    {
    case 7:
        hashResult ^= static_cast<uint64_t>(data2[6]) << 48LLU; [[fallthrough]] ;
    case 6:
        hashResult ^= static_cast<uint64_t>(data2[5]) << 40LLU; [[fallthrough]] ;
    case 5:
        hashResult ^= static_cast<uint64_t>(data2[4]) << 32LLU; [[fallthrough]] ;
    case 4:
        hashResult ^= static_cast<uint64_t>(data2[3]) << 24LLU; [[fallthrough]] ;
    case 3:
        hashResult ^= static_cast<uint64_t>(data2[2]) << 16LLU; [[fallthrough]] ;
    case 2:
        hashResult ^= static_cast<uint64_t>(data2[1]) <<  8LLU; [[fallthrough]] ;
    case 1:
        hashResult ^= static_cast<uint64_t>(data2[0]) <<  0LLU;
        hashResult *= hashConstantM; [[fallthrough]] ;
    case 0:
        break;
    default:
#ifdef _MSC_VER
        __assume(0);
        // since we can never reach any of the other cases above, this is safe
        // and can increase perf by generating less code
#endif
    }

    hashResult ^= hashResult >> hashConstantR;
    hashResult *= hashConstantM;
    hashResult ^= hashResult >> hashConstantR;

    return hashResult;
}

//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.
murmur_hash_result_t MurmurHash3(const void* key, size_t len, const uint32_t seed)
{
    static_assert(std::is_same_v<size_t, uint64_t>, "uint64_t and size_t need to be the same for this code to work!");
    const uint8_t* data = reinterpret_cast<const uint8_t*>(key);
    const size_t num_blocks = len / 16u;

    uint64_t hash0{ static_cast<uint64_t>(seed) };
    uint64_t hash1{ static_cast<uint64_t>(seed) };

    constexpr uint64_t hashConstant0 = 0x87c37b91114253d5LLU;
    constexpr uint64_t hashConstant1 = 0x4cf5ad432745937fLLU;

    const uint64_t* blocks = reinterpret_cast<const uint64_t*>(data);

    for (size_t i = 0; i < num_blocks; ++i)
    {
        uint64_t k0 = blocks[i * 2u + 0u];

        k0 *= hashConstant0;
        k0 = rotl64(k0, 31);
        k0 *= hashConstant1;
        hash0 ^= k0;
        
        hash0 = rotl64(hash0, 27);
        hash0 += hash1;
        hash0 = hash0 * 5LLU + static_cast<uint64_t>(0x52dce729);

        uint64_t k1 = blocks[i * 2u + 1u];

        k1 *= hashConstant1;
        k1 = rotl64(k1, 33);
        k1 *= hashConstant0;
        hash1 ^= k1;

        hash1 = rotl64(hash1, 31);
        hash1 += hash0;
        hash1 = hash1 * 5LLU + static_cast<uint64_t>(0x38495ab5);

    }

    const uint8_t* tail = reinterpret_cast<const uint8_t*>(data + num_blocks * 16u);

    uint64_t k0{ 0u };
    uint64_t k1{ 0u };

    switch (len & 15u)
    {
    case 15:
        k1 ^= static_cast<uint64_t>(tail[14]) << 48LLU; [[fallthrough]] ;
    case 14:
        k1 ^= static_cast<uint64_t>(tail[13]) << 40LLU; [[fallthrough]] ;
    case 13:
        k1 ^= static_cast<uint64_t>(tail[12]) << 32LLU; [[fallthrough]] ;
    case 12:
        k1 ^= static_cast<uint64_t>(tail[11]) << 24LLU; [[fallthrough]] ;
    case 11:
        k1 ^= static_cast<uint64_t>(tail[10]) << 16LLU; [[fallthrough]] ;
    case 10:
        k1 ^= static_cast<uint64_t>(tail[ 9]) <<  8LLU; [[fallthrough]] ;
    case 9:
        k1 ^= static_cast<uint64_t>(tail[ 8]) <<  0LLU;
        k1 *= hashConstant1;
        k1 = rotl64(k1, 33);
        k1 *= hashConstant0;
        hash1 ^= k1; 
        [[fallthrough]] ;
    case 8:
        k0 ^= static_cast<uint64_t>(tail[7]) << 56LLU; [[fallthrough]] ;
    case 7:
        k0 ^= static_cast<uint64_t>(tail[6]) << 48LLU; [[fallthrough]] ;
    case 6:
        k0 ^= static_cast<uint64_t>(tail[5]) << 40LLU; [[fallthrough]] ;
    case 5:
        k0 ^= static_cast<uint64_t>(tail[4]) << 32LLU; [[fallthrough]] ;
    case 4:
        k0 ^= static_cast<uint64_t>(tail[3]) << 24LLU; [[fallthrough]] ;
    case 3:
        k0 ^= static_cast<uint64_t>(tail[2]) << 16LLU; [[fallthrough]] ;
    case 2:
        k0 ^= static_cast<uint64_t>(tail[1]) <<  8LLU; [[fallthrough]] ;
    case 1:
        k0 ^= static_cast<uint64_t>(tail[0]) <<  0LLU;
        k0 *= hashConstant0;
        k0 = rotl64(k0, 31);
        k0 *= hashConstant1;
        hash0 ^= k0;
        [[fallthrough]] ;
    case 0:
        break;
    default:
#ifdef _MSC_VER
        __assume(0); // as above, len & 15u will ONLY ever create the above cases, so this is safe
#endif
    }

    hash0 ^= static_cast<uint64_t>(len);
    hash1 ^= static_cast<uint64_t>(len);

    hash0 += hash1;
    hash1 += hash0;

    hash0 = fmix64(hash0);
    hash1 = fmix64(hash1);

    hash0 += hash1;
    hash1 += hash0;

    return murmur_hash_result_t{ hash0, hash1 };
}
