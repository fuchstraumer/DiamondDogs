#include "content/HashSink.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

#include <cassert>

namespace
{
    XXH3_state_t* AsState(std::byte* storage) noexcept;
    const XXH3_state_t* AsState(const std::byte* storage) noexcept;
}

namespace foundation::content
{
    static_assert(sizeof(XXH3_state_t) <= 640, "HashSink::k_StateSize is too small for XXH3_state_t");
    static_assert(alignof(XXH3_state_t) <= 64, "HashSink::k_StateAlignment is too small for XXH3_state_t");

    HashSink::HashSink() noexcept
    {
        XXH3_state_t* xxh_state = AsState(state);
        XXH3_INITSTATE(xxh_state);

        const XXH_errorcode result = XXH3_128bits_reset(xxh_state);
        assert(result == XXH_OK);
        (void)result;
    }

    HashSink::~HashSink() = default;

    void HashSink::WriteRawBytes(std::span<const std::byte> bytes) noexcept
    {
        HashSink::Append(bytes.data(), bytes.size());
    }

    Hash128 HashSink::Finish() const noexcept
    {
        const XXH3_state_t* xxh_state = AsState(state);
        const XXH128_hash_t digest = XXH3_128bits_digest(xxh_state);

        return Hash128{ .Low = digest.low64, .High = digest.high64 };
    }

    void HashSink::Append(const std::byte* data, size_t length) noexcept
    {
        const XXH_errorcode result = XXH3_128bits_update(AsState(state), data, length);
        assert(result == XXH_OK);
        (void)result;
    }
}

namespace
{
    XXH3_state_t* AsState(std::byte* storage) noexcept
    {
        return reinterpret_cast<XXH3_state_t*>(storage);
    }

    const XXH3_state_t* AsState(const std::byte* storage) noexcept
    {
        return reinterpret_cast<const XXH3_state_t*>(storage);
    }
}
