#include "content/JsonDecode.hpp"
#include "content/EncodeErased.hpp"
#include "content/JsonSink.hpp"
#include "content/JsonWriter.hpp"
#include "reflect/Describe.hpp"
#include "reflect/TypeInfo.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using nlohmann::json;

    constexpr std::string_view k_TypeMember{ "$type" };
    constexpr std::string_view k_ValueMember{ "value" };
    constexpr std::string_view k_SchemaMember{ "schema" };
    constexpr std::string_view k_VersionMember{ "version" };
    constexpr std::string_view k_DataMember{ "data" };

    void AddError(foundation::content::DecodeResult& result,
                  foundation::content::DecodeIssueKind kind,
                  std::string path,
                  std::string message);

    void AddWarning(foundation::content::DecodeResult& result,
                    foundation::content::DecodeIssueKind kind,
                    std::string path,
                    std::string message);

    [[nodiscard]] std::string JoinFieldPath(std::string_view prefix, std::string_view name);

    [[nodiscard]] std::string JoinElementPath(std::string_view prefix, int64_t index);

    template<typename Integer>
    void WriteStored(void* value, int64_t bits) noexcept;

    void WriteIntegerBits(const foundation::reflect::TypeInfo& type, void* value, int64_t bits) noexcept;

    [[nodiscard]] float NarrowFloatLimit(double limit) noexcept;

    void DecodeBool(void* object,
                    const json& value,
                    std::string_view path,
                    foundation::content::DecodeResult& result);

    void DecodeSignedInt(const foundation::reflect::TypeInfo& type,
                         void* object,
                         const json& value,
                         std::string_view path,
                         const foundation::reflect::FieldMetadata* metadata,
                         const foundation::content::DecodeOptions& options,
                         foundation::content::DecodeResult& result);

    void DecodeUnsignedInt(const foundation::reflect::TypeInfo& type,
                           void* object,
                           const json& value,
                           std::string_view path,
                           const foundation::reflect::FieldMetadata* metadata,
                           const foundation::content::DecodeOptions& options,
                           foundation::content::DecodeResult& result);

    void DecodeInt(const foundation::reflect::TypeInfo& type,
                   void* object,
                   const json& value,
                   std::string_view path,
                   const foundation::reflect::FieldMetadata* metadata,
                   const foundation::content::DecodeOptions& options,
                   foundation::content::DecodeResult& result);

    void DecodeFloat32(void* object,
                       double number,
                       std::string_view path,
                       const foundation::reflect::FieldMetadata* metadata,
                       const foundation::content::DecodeOptions& options,
                       foundation::content::DecodeResult& result);

    void DecodeFloat64(void* object,
                       double number,
                       std::string_view path,
                       const foundation::reflect::FieldMetadata* metadata,
                       const foundation::content::DecodeOptions& options,
                       foundation::content::DecodeResult& result);

    void DecodeFloat(const foundation::reflect::TypeInfo& type,
                     void* object,
                     const json& value,
                     std::string_view path,
                     const foundation::reflect::FieldMetadata* metadata,
                     const foundation::content::DecodeOptions& options,
                     foundation::content::DecodeResult& result);

    void DecodeString(const foundation::reflect::TypeInfo& type,
                      void* object,
                      const json& value,
                      std::string_view path,
                      foundation::content::DecodeResult& result);

    void DecodeEnum(const foundation::reflect::TypeInfo& type,
                    void* object,
                    const json& value,
                    std::string_view path,
                    foundation::content::DecodeResult& result);

    void DecodeStruct(const foundation::reflect::TypeInfo& type,
                      void* object,
                      const json& value,
                      std::string_view path,
                      bool skip_type_member,
                      const foundation::content::DecodeOptions& options,
                      foundation::content::DecodeResult& result);

    void DecodeSequence(const foundation::reflect::TypeInfo& type,
                        void* object,
                        const json& value,
                        std::string_view path,
                        const foundation::reflect::FieldMetadata* metadata,
                        const foundation::content::DecodeOptions& options,
                        foundation::content::DecodeResult& result);

    void DecodeOptional(const foundation::reflect::TypeInfo& type,
                        void* object,
                        const json& value,
                        std::string_view path,
                        const foundation::reflect::FieldMetadata* metadata,
                        const foundation::content::DecodeOptions& options,
                        foundation::content::DecodeResult& result);

    void DecodeEnumVariant(const foundation::reflect::TypeInfo& alt_type,
                           void* active,
                           const json& value,
                           std::string_view path,
                           const foundation::content::DecodeOptions& options,
                           foundation::content::DecodeResult& result);

    void DecodeVariant(const foundation::reflect::TypeInfo& type,
                       void* object,
                       const json& value,
                       std::string_view path,
                       const foundation::reflect::FieldMetadata* metadata,
                       const foundation::content::DecodeOptions& options,
                       foundation::content::DecodeResult& result);

    void DecodeValue(const foundation::reflect::TypeInfo& type,
                     void* object,
                     const json& value,
                     std::string_view path,
                     const foundation::reflect::FieldMetadata* metadata,
                     bool skip_type_member,
                     const foundation::content::DecodeOptions& options,
                     foundation::content::DecodeResult& result);

    [[nodiscard]] const foundation::content::SchemaMigration* FindMigration(
        std::span<const foundation::content::SchemaMigration> migrations,
        std::string_view schema,
        int32_t from_version) noexcept;

    [[nodiscard]] std::optional<int32_t> ReadInt32(const json& value) noexcept;
}

namespace foundation::content
{
    std::string_view ToString(DecodeIssueKind kind) noexcept
    {
        switch (kind)
        {
        case DecodeIssueKind::Invalid:
            return "Invalid";
        case DecodeIssueKind::MalformedJson:
            return "MalformedJson";
        case DecodeIssueKind::SchemaMismatch:
            return "SchemaMismatch";
        case DecodeIssueKind::UnsupportedVersion:
            return "UnsupportedVersion";
        case DecodeIssueKind::UnknownField:
            return "UnknownField";
        case DecodeIssueKind::TypeMismatch:
            return "TypeMismatch";
        case DecodeIssueKind::OutOfRange:
            return "OutOfRange";
        case DecodeIssueKind::UnknownEnumCase:
            return "UnknownEnumCase";
        case DecodeIssueKind::InvalidVariantType:
            return "InvalidVariantType";
        case DecodeIssueKind::SequenceLengthMismatch:
            return "SequenceLengthMismatch";
        }

        return "Invalid";
    }

    bool DecodeResult::Succeeded() const noexcept
    {
        return Errors.empty();
    }

    DecodeResult DecodeJsonErased(const reflect::TypeInfo& type,
                                  void* object,
                                  std::string_view json_text,
                                  const DecodeOptions& options)
    {
        DecodeResult result{};
        const json root = json::parse(json_text, nullptr, false);

        if (root.is_discarded())
        {
            AddError(result, DecodeIssueKind::MalformedJson, {}, "input is not valid JSON");
            return result;
        }

        DecodeValue(type, object, root, {}, nullptr, false, options, result);

        return result;
    }

    DecodeResult DecodeDocumentErased(const reflect::TypeInfo& type,
                                      int32_t expected_version,
                                      void* object,
                                      std::string_view json_text,
                                      std::span<const SchemaMigration> migrations,
                                      const DecodeOptions& options)
    {
        DecodeResult result{};
        const json root = json::parse(json_text, nullptr, false);

        if (root.is_discarded() || !root.is_object())
        {
            AddError(result, DecodeIssueKind::MalformedJson, {}, "document must be a valid JSON object");
            return result;
        }

        const json::const_iterator schemaIt = root.find(k_SchemaMember);

        if (schemaIt == root.end() || !schemaIt->is_string())
        {
            AddError(result,
                     DecodeIssueKind::SchemaMismatch,
                     std::string{ k_SchemaMember },
                     "document is missing a string schema");
            return result;
        }

        const std::string& schemaName = schemaIt->get_ref<const std::string&>();

        if (schemaName != type.Name)
        {
            AddError(result,
                     DecodeIssueKind::SchemaMismatch,
                     std::string{ k_SchemaMember },
                     std::format("expected schema '{}', got '{}'", type.Name, schemaName));
            return result;
        }

        const json::const_iterator versionIt = root.find(k_VersionMember);

        if (versionIt == root.end())
        {
            AddError(result,
                     DecodeIssueKind::UnsupportedVersion,
                     std::string{ k_VersionMember },
                     "document is missing version");
            return result;
        }

        const std::optional<int32_t> parsedVersion = ReadInt32(*versionIt);

        if (!parsedVersion.has_value())
        {
            AddError(result,
                     DecodeIssueKind::UnsupportedVersion,
                     std::string{ k_VersionMember },
                     "document version must fit in int32_t");
            return result;
        }

        const json::const_iterator dataIt = root.find(k_DataMember);

        if (dataIt == root.end())
        {
            AddError(result,
                     DecodeIssueKind::MalformedJson,
                     std::string{ k_DataMember },
                     "document is missing data");
            return result;
        }

        int32_t currentVersion = *parsedVersion;

        if (currentVersion == expected_version)
        {
            DecodeValue(type, object, *dataIt, k_DataMember, nullptr, false, options, result);
            return result;
        }

        std::string dataJson = dataIt->dump(-1, ' ', false, json::error_handler_t::replace);
        std::vector<int32_t> visitedVersions{};
        const int32_t maxSteps = static_cast<int32_t>(migrations.size());
        int32_t stepCount = 0;

        visitedVersions.push_back(currentVersion);

        while (currentVersion != expected_version)
        {
            if (stepCount >= maxSteps)
            {
                AddError(result,
                         DecodeIssueKind::UnsupportedVersion,
                         std::string{ k_VersionMember },
                         "migration exceeded available steps");
                return result;
            }

            const SchemaMigration* migration = FindMigration(migrations, type.Name, currentVersion);

            if (migration == nullptr)
            {
                AddError(result,
                         DecodeIssueKind::UnsupportedVersion,
                         std::string{ k_VersionMember },
                         std::format("no migration from version {} to {}", currentVersion, expected_version));
                return result;
            }

            if (migration->ToVersion == migration->FromVersion ||
                std::ranges::contains(visitedVersions, migration->ToVersion))
            {
                AddError(result,
                         DecodeIssueKind::UnsupportedVersion,
                         std::string{ k_VersionMember },
                         std::format("migration cycle detected at version {}", migration->ToVersion));
                return result;
            }

            std::string errorMessage;

            if (!migration->Migrate(dataJson, errorMessage))
            {
                std::string message = errorMessage.empty()
                                          ? std::format("migration from {} to {} failed",
                                                        migration->FromVersion,
                                                        migration->ToVersion)
                                          : std::move(errorMessage);

                AddError(result,
                         DecodeIssueKind::UnsupportedVersion,
                         std::string{ k_VersionMember },
                         std::move(message));
                return result;
            }

            currentVersion = migration->ToVersion;
            visitedVersions.push_back(currentVersion);
            ++stepCount;
        }

        const json migratedData = json::parse(dataJson, nullptr, false);

        if (migratedData.is_discarded())
        {
            AddError(result,
                     DecodeIssueKind::MalformedJson,
                     std::string{ k_DataMember },
                     "migrated data is not valid JSON");
            return result;
        }

        DecodeValue(type, object, migratedData, k_DataMember, nullptr, false, options, result);

        return result;
    }

    JsonResult<std::string> EncodeDocumentErased(const reflect::TypeInfo& type,
                                                 int32_t version,
                                                 const void* object,
                                                 JsonStyle style)
    {
        JsonSink sink{ style };

        sink.BeginStruct("Document");
        sink.BeginField(k_SchemaMember);
        sink.WriteString(type.Name);
        sink.BeginField(k_VersionMember);
        sink.WriteInt(static_cast<int64_t>(version), static_cast<int32_t>(sizeof(int32_t)), true);
        sink.BeginField(k_DataMember);
        EncodeErased(type, object, sink);
        sink.EndStruct();

        return sink.Finish();
    }
}

namespace
{
    void AddError(foundation::content::DecodeResult& result,
                  foundation::content::DecodeIssueKind kind,
                  std::string path,
                  std::string message)
    {
        result.Errors.push_back(foundation::content::DecodeIssue
                                {
                                    .Kind = kind,
                                    .Path = std::move(path),
                                    .Message = std::move(message),
                                });
    }

    void AddWarning(foundation::content::DecodeResult& result,
                    foundation::content::DecodeIssueKind kind,
                    std::string path,
                    std::string message)
    {
        result.Warnings.push_back(foundation::content::DecodeIssue
                                  {
                                      .Kind = kind,
                                      .Path = std::move(path),
                                      .Message = std::move(message),
                                  });
    }

    std::string JoinFieldPath(std::string_view prefix, std::string_view name)
    {
        if (prefix.empty())
        {
            return std::string{ name };
        }

        return std::format("{}.{}", prefix, name);
    }

    std::string JoinElementPath(std::string_view prefix, int64_t index)
    {
        return std::format("{}[{}]", prefix, index);
    }

    template<typename Integer>
    void WriteStored(void* value, int64_t bits) noexcept
    {
        const Integer stored = static_cast<Integer>(bits);

        std::memcpy(value, &stored, sizeof(Integer));
    }

    void WriteIntegerBits(const foundation::reflect::TypeInfo& type, void* value, int64_t bits) noexcept
    {
        switch (type.IntegerWidthBytes)
        {
        case 1:
            WriteStored<uint8_t>(value, bits);
            break;
        case 2:
            WriteStored<uint16_t>(value, bits);
            break;
        case 4:
            WriteStored<uint32_t>(value, bits);
            break;
        default:
            WriteStored<uint64_t>(value, bits);
            break;
        }
    }

    float NarrowFloatLimit(double limit) noexcept
    {
        constexpr double k_FloatMax = std::numeric_limits<float>::max();

        return static_cast<float>(std::clamp(limit, -k_FloatMax, k_FloatMax));
    }

    void DecodeBool(void* object,
                    const json& value,
                    std::string_view path,
                    foundation::content::DecodeResult& result)
    {
        if (!value.is_boolean())
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "expected bool");
            return;
        }

        *static_cast<bool*>(object) = value.get<bool>();
    }

    void DecodeSignedInt(const foundation::reflect::TypeInfo& type,
                         void* object,
                         const json& value,
                         std::string_view path,
                         const foundation::reflect::FieldMetadata* metadata,
                         const foundation::content::DecodeOptions& options,
                         foundation::content::DecodeResult& result)
    {
        constexpr double k_TwoPow63 = 9223372036854775808.0;
        const uint64_t unsignedMax = type.IntegerWidthBytes == 8
                                         ? std::numeric_limits<uint64_t>::max()
                                         : (uint64_t{ 1 } << (type.IntegerWidthBytes * 8)) - 1;
        const int64_t typeMax = static_cast<int64_t>(unsignedMax >> 1);
        const int64_t typeMin = -typeMax - 1;
        int64_t effectiveMin = typeMin;
        int64_t effectiveMax = typeMax;

        if (metadata != nullptr && metadata->Range.has_value())
        {
            const double rangeMin = metadata->Range->Min;
            const double rangeMax = metadata->Range->Max;

            if (std::isnan(rangeMin) || std::isnan(rangeMax) || rangeMin > rangeMax)
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::OutOfRange,
                         std::string{ path },
                         "invalid integer field range");
                return;
            }

            if (rangeMin >= k_TwoPow63 || rangeMax < static_cast<double>(typeMin))
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::OutOfRange,
                         std::string{ path },
                         "integer value is outside field range");
                return;
            }

            if (rangeMin > static_cast<double>(typeMin))
            {
                const double ceiledMin = std::ceil(rangeMin);

                if (ceiledMin >= k_TwoPow63)
                {
                    AddError(result,
                             foundation::content::DecodeIssueKind::OutOfRange,
                             std::string{ path },
                             "integer value is outside field range");
                    return;
                }

                effectiveMin = std::max(typeMin, static_cast<int64_t>(ceiledMin));
            }

            if (rangeMax < k_TwoPow63)
            {
                effectiveMax = std::min(typeMax, static_cast<int64_t>(std::floor(rangeMax)));
            }

            if (effectiveMin > effectiveMax)
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::OutOfRange,
                         std::string{ path },
                         "integer value is outside field range");
                return;
            }
        }

        bool outOfRange = false;
        int64_t decoded = 0;

        if (value.is_number_unsigned())
        {
            const uint64_t raw = value.get<uint64_t>();

            if (effectiveMax < 0 || raw > static_cast<uint64_t>(effectiveMax))
            {
                outOfRange = true;
                decoded = effectiveMax;
            }
            else
            {
                decoded = static_cast<int64_t>(raw);

                if (decoded < effectiveMin)
                {
                    outOfRange = true;
                    decoded = effectiveMin;
                }
            }
        }
        else
        {
            const int64_t raw = value.get<int64_t>();

            if (raw < effectiveMin)
            {
                outOfRange = true;
                decoded = effectiveMin;
            }
            else if (raw > effectiveMax)
            {
                outOfRange = true;
                decoded = effectiveMax;
            }
            else
            {
                decoded = raw;
            }
        }

        if (outOfRange)
        {
            if (options.ClampOutOfRange)
            {
                WriteIntegerBits(type, object, decoded);
                AddWarning(result,
                           foundation::content::DecodeIssueKind::OutOfRange,
                           std::string{ path },
                           "integer value clamped to valid range");
                return;
            }

            AddError(result,
                     foundation::content::DecodeIssueKind::OutOfRange,
                     std::string{ path },
                     "integer value is out of range");
            return;
        }

        WriteIntegerBits(type, object, decoded);
    }

    void DecodeUnsignedInt(const foundation::reflect::TypeInfo& type,
                           void* object,
                           const json& value,
                           std::string_view path,
                           const foundation::reflect::FieldMetadata* metadata,
                           const foundation::content::DecodeOptions& options,
                           foundation::content::DecodeResult& result)
    {
        constexpr double k_TwoPow64 = 18446744073709551616.0;
        const uint64_t typeMax = type.IntegerWidthBytes == 8
                                     ? std::numeric_limits<uint64_t>::max()
                                     : (uint64_t{ 1 } << (type.IntegerWidthBytes * 8)) - 1;
        uint64_t effectiveMin = 0u;
        uint64_t effectiveMax = typeMax;

        if (metadata != nullptr && metadata->Range.has_value())
        {
            const double rangeMin = metadata->Range->Min;
            const double rangeMax = metadata->Range->Max;

            if (std::isnan(rangeMin) || std::isnan(rangeMax) || rangeMin > rangeMax)
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::OutOfRange,
                         std::string{ path },
                         "invalid unsigned integer field range");
                return;
            }

            if (rangeMin >= k_TwoPow64 || rangeMax < 0.0)
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::OutOfRange,
                         std::string{ path },
                         "unsigned integer value is outside field range");
                return;
            }

            if (rangeMin > 0.0)
            {
                const double ceiledMin = std::ceil(rangeMin);

                if (ceiledMin >= k_TwoPow64)
                {
                    AddError(result,
                             foundation::content::DecodeIssueKind::OutOfRange,
                             std::string{ path },
                             "unsigned integer value is outside field range");
                    return;
                }

                effectiveMin = static_cast<uint64_t>(ceiledMin);
            }

            if (rangeMax < k_TwoPow64)
            {
                effectiveMax = std::min(typeMax, static_cast<uint64_t>(std::floor(rangeMax)));
            }

            if (effectiveMin > effectiveMax)
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::OutOfRange,
                         std::string{ path },
                         "unsigned integer value is outside field range");
                return;
            }
        }

        bool outOfRange = false;
        uint64_t decoded = 0u;

        if (value.is_number_unsigned())
        {
            const uint64_t raw = value.get<uint64_t>();

            if (raw < effectiveMin)
            {
                outOfRange = true;
                decoded = effectiveMin;
            }
            else if (raw > effectiveMax)
            {
                outOfRange = true;
                decoded = effectiveMax;
            }
            else
            {
                decoded = raw;
            }
        }
        else
        {
            const int64_t raw = value.get<int64_t>();

            if (raw < 0)
            {
                outOfRange = true;
                decoded = effectiveMin;
            }
            else
            {
                const uint64_t unsignedRaw = static_cast<uint64_t>(raw);

                if (unsignedRaw < effectiveMin)
                {
                    outOfRange = true;
                    decoded = effectiveMin;
                }
                else if (unsignedRaw > effectiveMax)
                {
                    outOfRange = true;
                    decoded = effectiveMax;
                }
                else
                {
                    decoded = unsignedRaw;
                }
            }
        }

        if (outOfRange)
        {
            if (options.ClampOutOfRange)
            {
                WriteIntegerBits(type, object, static_cast<int64_t>(decoded));
                AddWarning(result,
                           foundation::content::DecodeIssueKind::OutOfRange,
                           std::string{ path },
                           "unsigned integer value clamped to valid range");
                return;
            }

            AddError(result,
                     foundation::content::DecodeIssueKind::OutOfRange,
                     std::string{ path },
                     "unsigned integer value is out of range");
            return;
        }

        WriteIntegerBits(type, object, static_cast<int64_t>(decoded));
    }

    void DecodeInt(const foundation::reflect::TypeInfo& type,
                   void* object,
                   const json& value,
                   std::string_view path,
                   const foundation::reflect::FieldMetadata* metadata,
                   const foundation::content::DecodeOptions& options,
                   foundation::content::DecodeResult& result)
    {
        if (!value.is_number_integer())
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "expected integer");
            return;
        }

        if (type.IntegerIsSigned)
        {
            DecodeSignedInt(type, object, value, path, metadata, options, result);
            return;
        }

        DecodeUnsignedInt(type, object, value, path, metadata, options, result);
    }

    void DecodeFloat32(void* object,
                       double number,
                       std::string_view path,
                       const foundation::reflect::FieldMetadata* metadata,
                       const foundation::content::DecodeOptions& options,
                       foundation::content::DecodeResult& result)
    {
        constexpr double k_FloatMax = std::numeric_limits<float>::max();
        float minBound = -std::numeric_limits<float>::max();
        float maxBound = std::numeric_limits<float>::max();

        if (metadata != nullptr && metadata->Range.has_value())
        {
            const double rangeMin = metadata->Range->Min;
            const double rangeMax = metadata->Range->Max;

            if (std::isnan(rangeMin) || std::isnan(rangeMax) || rangeMin > rangeMax ||
                rangeMin > k_FloatMax || rangeMax < -k_FloatMax)
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::OutOfRange,
                         std::string{ path },
                         "float32 value is outside field range");
                return;
            }

            minBound = NarrowFloatLimit(rangeMin);
            maxBound = NarrowFloatLimit(rangeMax);
        }

        if (std::abs(number) > k_FloatMax)
        {
            if (options.ClampOutOfRange)
            {
                *static_cast<float*>(object) = number < 0.0 ? minBound : maxBound;
                AddWarning(result,
                           foundation::content::DecodeIssueKind::OutOfRange,
                           std::string{ path },
                           "float32 value clamped to valid range");
                return;
            }

            AddError(result,
                     foundation::content::DecodeIssueKind::OutOfRange,
                     std::string{ path },
                     "float32 value exceeds representable range");
            return;
        }

        const float narrowed = static_cast<float>(number);

        if (narrowed < minBound || narrowed > maxBound)
        {
            if (options.ClampOutOfRange)
            {
                *static_cast<float*>(object) = std::clamp(narrowed, minBound, maxBound);
                AddWarning(result,
                           foundation::content::DecodeIssueKind::OutOfRange,
                           std::string{ path },
                           "float32 value clamped to field range");
                return;
            }

            AddError(result,
                     foundation::content::DecodeIssueKind::OutOfRange,
                     std::string{ path },
                     "float32 value is outside field range");
            return;
        }

        *static_cast<float*>(object) = narrowed;
    }

    void DecodeFloat64(void* object,
                       double number,
                       std::string_view path,
                       const foundation::reflect::FieldMetadata* metadata,
                       const foundation::content::DecodeOptions& options,
                       foundation::content::DecodeResult& result)
    {
        if (metadata != nullptr && metadata->Range.has_value())
        {
            const double rangeMin = metadata->Range->Min;
            const double rangeMax = metadata->Range->Max;

            if (std::isnan(rangeMin) || std::isnan(rangeMax) || rangeMin > rangeMax)
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::OutOfRange,
                         std::string{ path },
                         "invalid float64 field range");
                return;
            }

            if (number < rangeMin || number > rangeMax)
            {
                if (options.ClampOutOfRange)
                {
                    const double clamped = std::clamp(number, rangeMin, rangeMax);

                    if (!std::isfinite(clamped))
                    {
                        AddError(result,
                                 foundation::content::DecodeIssueKind::OutOfRange,
                                 std::string{ path },
                                 "float64 range has no finite bound");
                        return;
                    }

                    *static_cast<double*>(object) = clamped;
                    AddWarning(result,
                               foundation::content::DecodeIssueKind::OutOfRange,
                               std::string{ path },
                               "float64 value clamped to field range");
                    return;
                }

                AddError(result,
                         foundation::content::DecodeIssueKind::OutOfRange,
                         std::string{ path },
                         "float64 value is outside field range");
                return;
            }
        }

        *static_cast<double*>(object) = number;
    }

    void DecodeFloat(const foundation::reflect::TypeInfo& type,
                     void* object,
                     const json& value,
                     std::string_view path,
                     const foundation::reflect::FieldMetadata* metadata,
                     const foundation::content::DecodeOptions& options,
                     foundation::content::DecodeResult& result)
    {
        if (value.is_string())
        {
            const std::string& text = value.get_ref<const std::string&>();

            if (text == "NaN" || text == "Infinity" || text == "-Infinity")
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::OutOfRange,
                         std::string{ path },
                         std::format("non-finite float string '{}'", text));
                return;
            }

            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "expected number");
            return;
        }

        if (!value.is_number())
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "expected number");
            return;
        }

        double number = 0.0;

        if (value.is_number_unsigned())
        {
            number = static_cast<double>(value.get<uint64_t>());
        }
        else if (value.is_number_integer())
        {
            number = static_cast<double>(value.get<int64_t>());
        }
        else
        {
            number = value.get<double>();
        }

        if (!std::isfinite(number))
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::OutOfRange,
                     std::string{ path },
                     "float value is not finite");
            return;
        }

        if (type.Kind == foundation::reflect::TypeKind::Float32)
        {
            DecodeFloat32(object, number, path, metadata, options, result);
            return;
        }

        DecodeFloat64(object, number, path, metadata, options, result);
    }

    void DecodeString(const foundation::reflect::TypeInfo& type,
                      void* object,
                      const json& value,
                      std::string_view path,
                      foundation::content::DecodeResult& result)
    {
        if (!value.is_string())
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "expected string");
            return;
        }

        if (type.WriteString == nullptr)
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "string field is not writable");
            return;
        }

        type.WriteString(object, value.get_ref<const std::string&>());
    }

    void DecodeEnum(const foundation::reflect::TypeInfo& type,
                    void* object,
                    const json& value,
                    std::string_view path,
                    foundation::content::DecodeResult& result)
    {
        if (!value.is_string())
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "expected enum case string");
            return;
        }

        const std::string& caseName = value.get_ref<const std::string&>();
        const foundation::reflect::EnumCaseInfo* enumCase = foundation::reflect::FindEnumCase(type, caseName);

        if (enumCase == nullptr)
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::UnknownEnumCase,
                     std::string{ path },
                     std::format("unknown enum case '{}'", caseName));
            return;
        }

        WriteIntegerBits(type, object, enumCase->Value);
    }

    void DecodeStruct(const foundation::reflect::TypeInfo& type,
                      void* object,
                      const json& value,
                      std::string_view path,
                      bool skip_type_member,
                      const foundation::content::DecodeOptions& options,
                      foundation::content::DecodeResult& result)
    {
        if (!value.is_object())
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "expected object for struct");
            return;
        }

        for (const auto& [key, memberValue] : value.items())
        {
            if (skip_type_member && key == k_TypeMember)
            {
                continue;
            }

            const std::string childPath = JoinFieldPath(path, key);
            const foundation::reflect::FieldInfo* field = foundation::reflect::FindField(type, key);

            if (field == nullptr)
            {
                std::string message = std::format("unknown field '{}'", key);

                if (options.IgnoreUnknownFields)
                {
                    AddWarning(result,
                               foundation::content::DecodeIssueKind::UnknownField,
                               childPath,
                               std::move(message));
                }
                else
                {
                    AddError(result,
                             foundation::content::DecodeIssueKind::UnknownField,
                             childPath,
                             std::move(message));
                }

                continue;
            }

            DecodeValue(field->Type(),
                        field->Address(object),
                        memberValue,
                        childPath,
                        &field->Metadata,
                        false,
                        options,
                        result);
        }
    }

    void DecodeSequence(const foundation::reflect::TypeInfo& type,
                        void* object,
                        const json& value,
                        std::string_view path,
                        const foundation::reflect::FieldMetadata* metadata,
                        const foundation::content::DecodeOptions& options,
                        foundation::content::DecodeResult& result)
    {
        if (!value.is_array())
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "expected array for sequence");
            return;
        }

        if (!type.ElementsAreWritable)
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "sequence elements are not writable");
            return;
        }

        const int64_t arrayCount = static_cast<int64_t>(value.size());

        if (type.ResizeSequence != nullptr)
        {
            if (!type.ResizeSequence(object, arrayCount))
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::SequenceLengthMismatch,
                         std::string{ path },
                         std::format("failed to resize sequence to {}", arrayCount));
                return;
            }
        }
        else
        {
            const int64_t expectedCount = type.FixedElementCount >= 0
                                              ? type.FixedElementCount
                                              : type.ElementCount(object);

            if (arrayCount != expectedCount)
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::SequenceLengthMismatch,
                         std::string{ path },
                         std::format("expected {} elements, got {}", expectedCount, arrayCount));
                return;
            }
        }

        const foundation::reflect::TypeInfo& elementType = type.Element();

        for (int64_t index = 0; index < arrayCount; ++index)
        {
            void* elementAddress = const_cast<void*>(type.ElementAt(object, index));
            const std::string elementPath = JoinElementPath(path, index);

            DecodeValue(elementType,
                        elementAddress,
                        value[static_cast<size_t>(index)],
                        elementPath,
                        metadata,
                        false,
                        options,
                        result);
        }
    }

    void DecodeOptional(const foundation::reflect::TypeInfo& type,
                        void* object,
                        const json& value,
                        std::string_view path,
                        const foundation::reflect::FieldMetadata* metadata,
                        const foundation::content::DecodeOptions& options,
                        foundation::content::DecodeResult& result)
    {
        if (value.is_null())
        {
            type.ResetOptional(object);
            return;
        }

        void* inner = type.EmplaceOptional(object);

        DecodeValue(type.Element(), inner, value, path, metadata, false, options, result);
    }

    void DecodeEnumVariant(const foundation::reflect::TypeInfo& alt_type,
                           void* active,
                           const json& value,
                           std::string_view path,
                           const foundation::content::DecodeOptions& options,
                           foundation::content::DecodeResult& result)
    {
        for (const auto& [key, memberValue] : value.items())
        {
            static_cast<void>(memberValue);

            if (key == k_TypeMember || key == k_ValueMember)
            {
                continue;
            }

            const std::string childPath = JoinFieldPath(path, key);
            std::string message = std::format("unknown field '{}'", key);

            if (options.IgnoreUnknownFields)
            {
                AddWarning(result,
                           foundation::content::DecodeIssueKind::UnknownField,
                           childPath,
                           std::move(message));
            }
            else
            {
                AddError(result,
                         foundation::content::DecodeIssueKind::UnknownField,
                         childPath,
                         std::move(message));
            }
        }

        const json::const_iterator valueIt = value.find(k_ValueMember);
        const std::string valuePath = JoinFieldPath(path, k_ValueMember);

        if (valueIt == value.end())
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     valuePath,
                     "enum variant is missing 'value' member");
            return;
        }

        DecodeEnum(alt_type, active, *valueIt, valuePath, result);
    }

    void DecodeVariant(const foundation::reflect::TypeInfo& type,
                       void* object,
                       const json& value,
                       std::string_view path,
                       const foundation::reflect::FieldMetadata* metadata,
                       const foundation::content::DecodeOptions& options,
                       foundation::content::DecodeResult& result)
    {
        static_cast<void>(metadata);

        if (!value.is_object())
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "expected object for variant");
            return;
        }

        const json::const_iterator typeIt = value.find(k_TypeMember);

        if (typeIt == value.end() || !typeIt->is_string())
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::InvalidVariantType,
                     JoinFieldPath(path, k_TypeMember),
                     "variant object is missing string '$type'");
            return;
        }

        const std::string& typeName = typeIt->get_ref<const std::string&>();
        int32_t matchedIndex = -1;

        for (int32_t index = 0; index < static_cast<int32_t>(type.Alternatives.size()); ++index)
        {
            const foundation::reflect::TypeInfo& alternative =
                type.Alternatives[static_cast<size_t>(index)]();

            if (alternative.Name == typeName)
            {
                matchedIndex = index;
                break;
            }
        }

        if (matchedIndex < 0)
        {
            AddError(result,
                     foundation::content::DecodeIssueKind::InvalidVariantType,
                     JoinFieldPath(path, k_TypeMember),
                     std::format("unknown variant type '{}'", typeName));
            return;
        }

        void* active = type.EmplaceVariant(object, matchedIndex);
        const foundation::reflect::TypeInfo& alternative =
            type.Alternatives[static_cast<size_t>(matchedIndex)]();

        if (alternative.Kind == foundation::reflect::TypeKind::Struct)
        {
            DecodeStruct(alternative, active, value, path, true, options, result);
            return;
        }

        if (alternative.Kind == foundation::reflect::TypeKind::Enum)
        {
            DecodeEnumVariant(alternative, active, value, path, options, result);
            return;
        }

        DecodeValue(alternative, active, value, path, nullptr, false, options, result);
    }

    void DecodeValue(const foundation::reflect::TypeInfo& type,
                     void* object,
                     const json& value,
                     std::string_view path,
                     const foundation::reflect::FieldMetadata* metadata,
                     bool skip_type_member,
                     const foundation::content::DecodeOptions& options,
                     foundation::content::DecodeResult& result)
    {
        switch (type.Kind)
        {
        case foundation::reflect::TypeKind::Bool:
            DecodeBool(object, value, path, result);
            break;
        case foundation::reflect::TypeKind::Int:
            DecodeInt(type, object, value, path, metadata, options, result);
            break;
        case foundation::reflect::TypeKind::Float32:
            [[fallthrough]];
        case foundation::reflect::TypeKind::Float64:
            DecodeFloat(type, object, value, path, metadata, options, result);
            break;
        case foundation::reflect::TypeKind::String:
            DecodeString(type, object, value, path, result);
            break;
        case foundation::reflect::TypeKind::Enum:
            DecodeEnum(type, object, value, path, result);
            break;
        case foundation::reflect::TypeKind::Struct:
            DecodeStruct(type, object, value, path, skip_type_member, options, result);
            break;
        case foundation::reflect::TypeKind::Sequence:
            DecodeSequence(type, object, value, path, metadata, options, result);
            break;
        case foundation::reflect::TypeKind::Optional:
            DecodeOptional(type, object, value, path, metadata, options, result);
            break;
        case foundation::reflect::TypeKind::Variant:
            DecodeVariant(type, object, value, path, metadata, options, result);
            break;
        case foundation::reflect::TypeKind::Invalid:
            AddError(result,
                     foundation::content::DecodeIssueKind::TypeMismatch,
                     std::string{ path },
                     "invalid reflected type");
            break;
        }
    }

    const foundation::content::SchemaMigration* FindMigration(
        std::span<const foundation::content::SchemaMigration> migrations,
        std::string_view schema,
        int32_t from_version) noexcept
    {
        for (const foundation::content::SchemaMigration& migration : migrations)
        {
            if (migration.Schema == schema &&
                migration.FromVersion == from_version &&
                migration.Migrate != nullptr)
            {
                return &migration;
            }
        }

        return nullptr;
    }

    std::optional<int32_t> ReadInt32(const json& value) noexcept
    {
        if (!value.is_number_integer())
        {
            return std::nullopt;
        }

        if (value.is_number_unsigned())
        {
            const uint64_t raw = value.get<uint64_t>();

            if (raw > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
            {
                return std::nullopt;
            }

            return static_cast<int32_t>(raw);
        }

        const int64_t raw = value.get<int64_t>();

        if (raw < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
            raw > static_cast<int64_t>(std::numeric_limits<int32_t>::max()))
        {
            return std::nullopt;
        }

        return static_cast<int32_t>(raw);
    }
}
