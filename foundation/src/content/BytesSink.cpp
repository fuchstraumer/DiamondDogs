#include "content/BytesSink.hpp"
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace foundation::content
{
    void BytesSink::WriteRawBytes(std::span<const std::byte> raw_bytes)
    {
        BytesSink::Append(raw_bytes.data(), raw_bytes.size());
    }

    std::span<const std::byte> BytesSink::Bytes() const noexcept
    {
        return bytes;
    }

    std::vector<std::byte> BytesSink::TakeBytes() noexcept
    {
        std::vector<std::byte> taken = std::move(bytes);
        bytes.clear();

        return taken;
    }

    void BytesSink::Append(const std::byte* data, size_t length)
    {
        bytes.insert(bytes.end(), data, data + length);
    }
}
