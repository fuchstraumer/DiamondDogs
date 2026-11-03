#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_BYTES_SINK_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_BYTES_SINK_HPP

#include "content/ByteStreamSink.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace foundation::content
{
    // Appends the byte stream that HashSink feeds to XXH3-128 into a vector. The layout comes from the same
    // ByteStreamSink code. A recipe table keeps these bytes, so that two recipes with equal keys can be compared
    // byte for byte.
    class BytesSink final : public ByteStreamSink
    {
    public:
        BytesSink() = default;

        // bytes of a blob, with no tag and no length, as in HashSink
        void WriteRawBytes(std::span<const std::byte> bytes);

        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;

        // moves the bytes out and leaves the sink empty
        [[nodiscard]] std::vector<std::byte> TakeBytes() noexcept;

    protected:
        void Append(const std::byte* data, size_t length) override;

    private:
        std::vector<std::byte> bytes;
    };
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_BYTES_SINK_HPP
