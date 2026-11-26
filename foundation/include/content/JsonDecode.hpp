#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_JSON_DECODE_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_JSON_DECODE_HPP
#include "content/JsonWriter.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace foundation::reflect
{
    struct TypeInfo;
}

namespace foundation::content
{
    enum class DecodeIssueKind : uint8_t
    {
        Invalid = 0,
        MalformedJson,
        SchemaMismatch,
        UnsupportedVersion,
        UnknownField,
        TypeMismatch,
        OutOfRange,
        UnknownEnumCase,
        InvalidVariantType,
        SequenceLengthMismatch,
    };

    [[nodiscard]] std::string_view ToString(DecodeIssueKind kind) noexcept;

    struct DecodeIssue
    {
        DecodeIssueKind Kind{ DecodeIssueKind::Invalid };
        std::string Path;
        std::string Message;
    };

    struct DecodeOptions
    {
        bool IgnoreUnknownFields{ true };
        bool ClampOutOfRange{ false };
    };

    inline constexpr DecodeOptions k_LenientDecodeOptions
    {
        .IgnoreUnknownFields = true,
        .ClampOutOfRange = true,
    };

    inline constexpr DecodeOptions k_StrictDecodeOptions
    {
        .IgnoreUnknownFields = false,
        .ClampOutOfRange = false,
    };

    struct DecodeResult
    {
        std::vector<DecodeIssue> Warnings;
        std::vector<DecodeIssue> Errors;

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    struct DocumentHeader
    {
        std::string Schema;
        int32_t Version{ 0 };
    };

    // Migrates a serialized JSON "data" object from FromVersion to ToVersion before typed decoding runs.
    using JsonMigrationFn = bool (*)(std::string& json_data, std::string& error_message);

    struct SchemaMigration
    {
        std::string_view Schema;
        int32_t FromVersion{ 0 };
        int32_t ToVersion{ 0 };
        JsonMigrationFn Migrate{ nullptr };
    };

    [[nodiscard]] DecodeResult DecodeJsonErased(const reflect::TypeInfo& type,
                                                void* object,
                                                std::string_view json_text,
                                                const DecodeOptions& options);

    [[nodiscard]] DecodeResult DecodeDocumentErased(const reflect::TypeInfo& type,
                                                    int32_t expected_version,
                                                    void* object,
                                                    std::string_view json_text,
                                                    std::span<const SchemaMigration> migrations,
                                                    const DecodeOptions& options);

    [[nodiscard]] JsonResult<std::string> EncodeDocumentErased(const reflect::TypeInfo& type,
                                                               int32_t version,
                                                               const void* object,
                                                               JsonStyle style);
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_JSON_DECODE_HPP
