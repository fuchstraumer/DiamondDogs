#include "DeviceConfig.hpp"
#include "BootResult.hpp"
#include "LaunchStage.hpp"
#include "RhiFlags.hpp"
#include "nlohmann/json_fwd.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    using json = nlohmann::json;

    constexpr std::string_view k_SectionKey{ "Device" };
    constexpr std::string_view k_ApiVersionKey{ "ApiVersion" };
    constexpr std::string_view k_ValidationKey{ "Validation" };
    constexpr std::string_view k_ApplicationNameKey{ "ApplicationName" };
    constexpr std::string_view k_DeclinedKey{ "Declined" };

    [[nodiscard]] bootstrap::BootResult ConfigFailure(std::string_view key, std::string_view detail);
    [[nodiscard]] std::optional<rhi::ApiVersion> ParseApiVersion(std::string_view text) noexcept;
    [[nodiscard]] std::optional<rhi::ValidationLayers> ParseValidation(std::string_view text) noexcept;
    [[nodiscard]] bootstrap::BootResult ReadField(const std::string& key, const json& value, bootstrap::DeviceConfig& config);
    [[nodiscard]] bootstrap::BootResult ReadDeclined(const json& value, bootstrap::DeviceConfig& config);
}

namespace bootstrap
{
    BootResult ReadDeviceConfig(const LaunchStage& launch, DeviceConfig& config)
    {
        const ConfigDocument& document = launch.Config();

        if (document.Text.empty())
        {
            return SuccessIn(Stage::Device);
        }

        // LaunchStage checked that the text is a JSON object
        const json root = json::parse(document.Text, nullptr, false);
        const auto section = root.find(k_SectionKey);

        if (section == root.end())
        {
            return SuccessIn(Stage::Device);
        }

        if (!section->is_object())
        {
            return ConfigFailure(k_SectionKey, "the section is not an object");
        }

        for (const auto& [key, value] : section->items())
        {
            BootResult read = ReadField(key, value, config);

            if (!read.Succeeded())
            {
                return read;
            }
        }

        return SuccessIn(Stage::Device);
    }
}

namespace
{
    bootstrap::BootResult ConfigFailure(std::string_view key, std::string_view detail)
    {
        return bootstrap::BootResult
        {
            .Code = bootstrap::BootResultCode::ConfigInvalid,
            .FailedStage = bootstrap::Stage::Device,
            .Subject = std::string{ key },
            .Source = std::string{ k_SectionKey },
            .Detail = std::string{ detail },
        };
    }

    std::optional<rhi::ApiVersion> ParseApiVersion(std::string_view text) noexcept
    {
        struct VersionName
        {
            std::string_view Name;
            rhi::ApiVersion Version;
        };

        constexpr VersionName k_Versions[]
        {
            { "1.0", rhi::ApiVersion::Vulkan10 },
            { "1.1", rhi::ApiVersion::Vulkan11 },
            { "1.2", rhi::ApiVersion::Vulkan12 },
            { "1.3", rhi::ApiVersion::Vulkan13 },
            { "1.4", rhi::ApiVersion::Vulkan14 },
            { "Latest", rhi::ApiVersion::Latest },
        };

        for (const VersionName& version : k_Versions)
        {
            if (version.Name == text)
            {
                return version.Version;
            }
        }

        return std::nullopt;
    }

    std::optional<rhi::ValidationLayers> ParseValidation(std::string_view text) noexcept
    {
        struct ValidationName
        {
            std::string_view Name;
            rhi::ValidationLayers Level;
        };

        constexpr ValidationName k_Levels[]
        {
            { "None", rhi::ValidationLayers::None },
            { "BaseOnly", rhi::ValidationLayers::BaseOnly },
            { "WithSynchronization", rhi::ValidationLayers::WithSynchronization },
            { "Full", rhi::ValidationLayers::Full },
            { "CrashDiagnostic", rhi::ValidationLayers::CrashDiagnostic },
        };

        for (const ValidationName& level : k_Levels)
        {
            if (level.Name == text)
            {
                return level.Level;
            }
        }

        return std::nullopt;
    }

    bootstrap::BootResult ReadField(const std::string& key, const json& value, bootstrap::DeviceConfig& config)
    {
        if (key == k_DeclinedKey)
        {
            return ReadDeclined(value, config);
        }

        if (key != k_ApiVersionKey && key != k_ValidationKey && key != k_ApplicationNameKey)
        {
            return ConfigFailure(key, "unknown key");
        }

        if (!value.is_string())
        {
            return ConfigFailure(key, "the value is not a string");
        }

        const std::string text = value.get<std::string>();

        if (key == k_ApplicationNameKey)
        {
            config.ApplicationName = text;
            return bootstrap::SuccessIn(bootstrap::Stage::Device);
        }

        if (key == k_ApiVersionKey)
        {
            const std::optional<rhi::ApiVersion> version = ParseApiVersion(text);

            if (!version.has_value())
            {
                return ConfigFailure(key, R"(not "1.0" to "1.4" or "Latest")");
            }

            config.ApiVersion = *version;
            return bootstrap::SuccessIn(bootstrap::Stage::Device);
        }

        const std::optional<rhi::ValidationLayers> validation = ParseValidation(text);

        if (!validation.has_value())
        {
            return ConfigFailure(key, "not None, BaseOnly, WithSynchronization, Full or CrashDiagnostic");
        }

        config.Validation = *validation;
        return bootstrap::SuccessIn(bootstrap::Stage::Device);
    }

    bootstrap::BootResult ReadDeclined(const json& value, bootstrap::DeviceConfig& config)
    {
        if (!value.is_array())
        {
            return ConfigFailure(k_DeclinedKey, "the value is not an array");
        }

        for (const json& name : value)
        {
            if (!name.is_string())
            {
                return ConfigFailure(k_DeclinedKey, "an element is not a string");
            }

            config.Declined.push_back(name.get<std::string>());
        }

        return bootstrap::SuccessIn(bootstrap::Stage::Device);
    }
}
