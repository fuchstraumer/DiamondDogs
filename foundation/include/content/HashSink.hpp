#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_HASH_SINK_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_HASH_SINK_HPP

#include "content/ByteStreamSink.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace foundation::content
{
    struct Hash128
    {
        uint64_t Low{ 0 };
        uint64_t High{ 0 };
    };

    // Feeds XXH3-128 in streaming mode, so no buffer holds the encoded bytes. The layout of the bytes is written
    // by ByteStreamSink.
    class HashSink final : public ByteStreamSink
    {
    public:
        HashSink() noexcept;
        ~HashSink() override;

        // bytes of a blob, with no tag and no length. The caller writes the length first.
        void WriteRawBytes(std::span<const std::byte> bytes) noexcept;

        [[nodiscard]] Hash128 Finish() const noexcept;

    protected:
        void Append(const std::byte* data, size_t length) noexcept override;

    private:
        // The state is a fixed-size aligned buffer, so that xxhash.h stays out of this header and a key costs no
        // allocation. HashSink.cpp static_asserts that the buffer fits XXH3_state_t.
        static constexpr size_t k_StateSize = 640;
        static constexpr size_t k_StateAlignment = 64;

        alignas(k_StateAlignment) std::byte state[k_StateSize];
    };
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_HASH_SINK_HPP
