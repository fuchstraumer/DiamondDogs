#include "content/HashSink.hpp"
#include "content/CanonicalSink.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

#include <bit>
#include <cassert>

namespace
{
    constexpr uint8_t TagByte(foundation::content::CanonicalTag tag) noexcept;
    XXH3_state_t* AsState(std::byte* storage) noexcept;
    const XXH3_state_t* AsState(const std::byte* storage) noexcept;
    void Update(std::byte* storage, const void* data, size_t length) noexcept;
    void UpdateTag(std::byte* storage, foundation::content::CanonicalTag tag) noexcept;
    void UpdateLittleEndian(std::byte* storage, uint64_t value, int32_t width_bytes) noexcept;
    void UpdateString(std::byte* storage, std::string_view text) noexcept;
}

namespace foundation::content
{
    static_assert(sizeof(XXH3_state_t) <= 640, "HashSink::k_StateSize is too small for XXH3_state_t");
    static_assert(alignof(XXH3_state_t) <= 64, "HashSink::k_StateAlignment is too small for XXH3_state_t");
    static_assert(std::endian::native == std::endian::little, "the byte order of the encoding is little-endian");

    HashSink::HashSink() noexcept
    {
        XXH3_state_t* xxh_state = AsState(state);
        XXH3_INITSTATE(xxh_state);

        const XXH_errorcode result = XXH3_128bits_reset(xxh_state);
        assert(result == XXH_OK);
        (void)result;
    }

    HashSink::~HashSink() = default;

    void HashSink::BeginStruct(std::string_view type_name)
    {
        UpdateTag(state, CanonicalTag::Struct);
        UpdateString(state, type_name);
    }

    void HashSink::EndStruct()
    {
        UpdateTag(state, CanonicalTag::EndStruct);
    }

    void HashSink::BeginField(std::string_view field_name)
    {
        UpdateTag(state, CanonicalTag::Field);
        UpdateString(state, field_name);
    }

    void HashSink::WriteBool(bool value)
    {
        UpdateTag(state, CanonicalTag::Bool);
        UpdateLittleEndian(state, value ? 1 : 0, 1);
    }

    void HashSink::WriteInt(int64_t value, int32_t width_bytes, bool is_signed)
    {
        UpdateTag(state, CanonicalTag::Int);
        UpdateLittleEndian(state, static_cast<uint64_t>(width_bytes), 1);
        UpdateLittleEndian(state, is_signed ? 1 : 0, 1);
        UpdateLittleEndian(state, static_cast<uint64_t>(value), width_bytes);
    }

    void HashSink::WriteFloat32(float value)
    {
        UpdateTag(state, CanonicalTag::Float32);
        UpdateLittleEndian(state, std::bit_cast<uint32_t>(value), 4);
    }

    void HashSink::WriteFloat64(double value)
    {
        UpdateTag(state, CanonicalTag::Float64);
        UpdateLittleEndian(state, std::bit_cast<uint64_t>(value), 8);
    }

    void HashSink::WriteString(std::string_view value)
    {
        UpdateTag(state, CanonicalTag::String);
        UpdateString(state, value);
    }

    void HashSink::WriteEnum(std::string_view type_name, std::string_view case_name)
    {
        UpdateTag(state, CanonicalTag::Enum);
        UpdateString(state, type_name);
        UpdateString(state, case_name);
    }

    void HashSink::BeginArray(uint64_t count)
    {
        UpdateTag(state, CanonicalTag::Array);
        UpdateLittleEndian(state, count, 8);
    }

    void HashSink::EndArray()
    {
        UpdateTag(state, CanonicalTag::EndArray);
    }

    void HashSink::WriteOptional(bool present)
    {
        UpdateTag(state, CanonicalTag::Optional);
        UpdateLittleEndian(state, present ? 1 : 0, 1);
    }

    void HashSink::WriteVariant(std::string_view alternative_name)
    {
        UpdateTag(state, CanonicalTag::Variant);
        UpdateString(state, alternative_name);
    }

    void HashSink::WriteKeyBytes(uint64_t low, uint64_t high)
    {
        UpdateTag(state, CanonicalTag::Key);
        UpdateLittleEndian(state, low, 8);
        UpdateLittleEndian(state, high, 8);
    }

    void HashSink::WriteRawBytes(std::span<const std::byte> bytes) noexcept
    {
        Update(state, bytes.data(), bytes.size());
    }

    Hash128 HashSink::Finish() const noexcept
    {
        const XXH3_state_t* xxh_state = AsState(state);
        const XXH128_hash_t digest = XXH3_128bits_digest(xxh_state);

        return Hash128{ .Low = digest.low64, .High = digest.high64 };
    }
}

namespace
{
    constexpr uint8_t TagByte(foundation::content::CanonicalTag tag) noexcept
    {
        return static_cast<uint8_t>(tag);
    }

    XXH3_state_t* AsState(std::byte* storage) noexcept
    {
        return reinterpret_cast<XXH3_state_t*>(storage);
    }

    const XXH3_state_t* AsState(const std::byte* storage) noexcept
    {
        return reinterpret_cast<const XXH3_state_t*>(storage);
    }

    void Update(std::byte* storage, const void* data, size_t length) noexcept
    {
        const XXH_errorcode result = XXH3_128bits_update(AsState(storage), data, length);
        assert(result == XXH_OK);
        (void)result;
    }

    void UpdateTag(std::byte* storage, foundation::content::CanonicalTag tag) noexcept
    {
        const uint8_t byte = TagByte(tag);
        Update(storage, &byte, 1);
    }

    void UpdateLittleEndian(std::byte* storage, uint64_t value, int32_t width_bytes) noexcept
    {
        uint8_t bytes[8]{};

        for (int32_t index = 0; index < width_bytes; ++index)
        {
            bytes[index] = static_cast<uint8_t>(value >> (8 * index));
        }

        Update(storage, bytes, static_cast<size_t>(width_bytes));
    }

    void UpdateString(std::byte* storage, std::string_view text) noexcept
    {
        UpdateLittleEndian(storage, text.size(), 8);
        Update(storage, text.data(), text.size());
    }
}
