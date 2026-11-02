#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_HASH_SINK_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_HASH_SINK_HPP

#include "content/CanonicalSink.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace foundation::content
{
    struct Hash128
    {
        uint64_t Low{ 0 };
        uint64_t High{ 0 };
    };

    // Feeds XXH3-128 in streaming mode, so no buffer holds the encoded bytes.
    class HashSink final : public CanonicalSink
    {
    public:
        HashSink() noexcept;
        ~HashSink() override;

        void BeginStruct(std::string_view type_name) override;
        void EndStruct() override;
        void BeginField(std::string_view field_name) override;
        void WriteBool(bool value) override;
        void WriteInt(int64_t value, int32_t width_bytes, bool is_signed) override;
        void WriteFloat32(float value) override;
        void WriteFloat64(double value) override;
        void WriteString(std::string_view value) override;
        void WriteEnum(std::string_view type_name, std::string_view case_name) override;
        void BeginArray(uint64_t count) override;
        void EndArray() override;
        void WriteOptional(bool present) override;
        void WriteVariant(std::string_view alternative_name) override;
        void WriteKeyBytes(uint64_t low, uint64_t high) override;

        // bytes of a blob, with no tag and no length. The caller writes the length first.
        void WriteRawBytes(std::span<const std::byte> bytes) noexcept;

        [[nodiscard]] Hash128 Finish() const noexcept;

    private:
        // The state is a fixed-size aligned buffer, so that xxhash.h stays out of this header and a key costs no
        // allocation. HashSink.cpp static_asserts that the buffer fits XXH3_state_t.
        static constexpr size_t k_StateSize = 640;
        static constexpr size_t k_StateAlignment = 64;

        alignas(k_StateAlignment) std::byte state[k_StateSize];
    };
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_HASH_SINK_HPP
