#include "FoamMaterial.hpp"
#include "ImageDataFormats.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include <gli/gli.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;

    [[nodiscard]] std::expected<gli::texture2d, std::string> LoadKtx(const std::filesystem::path& path, gli::format expected_format);
    [[nodiscard]] std::expected<std::array<float, 3>, std::string> LoadTint(const std::filesystem::path& path);
    [[nodiscard]] std::expected<GraphicsResource, std::string> CreateSampledImage(
        ResourceContext& resources,
        const gli::texture2d& texture,
        ImageFormat format,
        std::string_view name);
}

namespace ocean
{
    std::expected<std::unique_ptr<FoamMaterial>, std::string> FoamMaterial::Load(
        ResourceContext& resources,
        const std::filesystem::path& folder,
        std::string_view name)
    {
        const std::filesystem::path surfacePath = folder / std::format("{}_Surface.ktx", name);
        const std::filesystem::path normalPath = folder / std::format("{}_Normal.ktx", name);
        const std::filesystem::path tintPath = folder / std::format("{}_Material.json", name);

        std::expected<gli::texture2d, std::string> surfaceTexture = LoadKtx(surfacePath, gli::FORMAT_RGBA8_UNORM_PACK8);
        if (!surfaceTexture.has_value())
        {
            return std::unexpected(surfaceTexture.error());
        }

        std::expected<gli::texture2d, std::string> normalTexture = LoadKtx(normalPath, gli::FORMAT_RG8_UNORM_PACK8);
        if (!normalTexture.has_value())
        {
            return std::unexpected(normalTexture.error());
        }

        std::expected<std::array<float, 3>, std::string> tint = LoadTint(tintPath);
        if (!tint.has_value())
        {
            return std::unexpected(tint.error());
        }

        std::expected<GraphicsResource, std::string> surface =
            CreateSampledImage(resources, *surfaceTexture, CommonFormats::RGBA8_UNorm, "Ocean_FoamSurface");
        if (!surface.has_value())
        {
            return std::unexpected(std::format("{}: {}", surfacePath.string(), surface.error()));
        }

        std::expected<GraphicsResource, std::string> normal =
            CreateSampledImage(resources, *normalTexture, CommonFormats::RG8_UNorm, "Ocean_FoamNormal");
        if (!normal.has_value())
        {
            // the surface image exists already, so a failed load must destroy it
            (void)resources.DestroyResource(*surface)->WaitForCompletion(k_TimeoutNs);
            return std::unexpected(std::format("{}: {}", normalPath.string(), normal.error()));
        }

        return std::make_unique<FoamMaterial>(resources, *surface, *normal, *tint);
    }

    FoamMaterial::FoamMaterial(ResourceContext& _resources, const GraphicsResource& _surface, const GraphicsResource& _normal, const std::array<float, 3>& _tint) noexcept :
        resources{ &_resources },
        surface{ _surface },
        normal{ _normal },
        tint{ _tint }
    {}

    FoamMaterial::~FoamMaterial()
    {
        for (const GraphicsResource* image : { &surface, &normal })
        {
            if (*image)
            {
                (void)resources->DestroyResource(*image)->WaitForCompletion(k_TimeoutNs);
            }
        }
    }

    GraphicsResource FoamMaterial::Surface() const noexcept
    {
        return surface;
    }

    GraphicsResource FoamMaterial::Normal() const noexcept
    {
        return normal;
    }

    std::array<float, 3> FoamMaterial::Tint() const noexcept
    {
        return tint;
    }
}

namespace
{
    std::expected<gli::texture2d, std::string> LoadKtx(const std::filesystem::path& path, gli::format expected_format)
    {
        std::error_code error;
        if (!std::filesystem::exists(path, error))
        {
            return std::unexpected(std::format("{} is missing", path.string()));
        }

        // gli returns an empty texture on any failure
        const std::string pathText = path.string();
        gli::texture2d texture{ gli::load(pathText) };

        if (texture.empty())
        {
            return std::unexpected(std::format("{} is not a KTX file that gli can read", pathText));
        }

        if (texture.format() != expected_format)
        {
            return std::unexpected(std::format("{} has gli format {}, and the packer writes {}", pathText,
                static_cast<int32_t>(texture.format()), static_cast<int32_t>(expected_format)));
        }

        const gli::extent2d extent = texture.extent(0u);
        const int32_t fullChain = static_cast<int32_t>(std::bit_width(static_cast<uint32_t>(std::max(extent.x, extent.y))));
        const int32_t levels = static_cast<int32_t>(texture.levels());

        if (levels != fullChain)
        {
            return std::unexpected(std::format("{} has {} mip levels, and a {}x{} image needs {}", pathText, levels, extent.x, extent.y, fullChain));
        }

        return texture;
    }

    std::expected<std::array<float, 3>, std::string> LoadTint(const std::filesystem::path& path)
    {
        std::ifstream file{ path };
        if (!file)
        {
            return std::unexpected(std::format("{} is missing", path.string()));
        }

        // no exceptions: a parse error returns a discarded value
        const nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
        if (json.is_discarded() || !json.is_object())
        {
            return std::unexpected(std::format("{} is not valid JSON", path.string()));
        }

        const auto tintIterator = json.find("albedo_tint");
        if (tintIterator == json.end() || !tintIterator->is_array() || tintIterator->size() != 3u)
        {
            return std::unexpected(std::format("{} needs \"albedo_tint\": [r, g, b]", path.string()));
        }

        std::array<float, 3> tint{};
        for (size_t channel = 0u; channel < 3u; ++channel)
        {
            const nlohmann::json& value = (*tintIterator)[channel];
            if (!value.is_number())
            {
                return std::unexpected(std::format("{}: albedo_tint[{}] is not a number", path.string(), channel));
            }

            tint[channel] = value.get<float>();
        }

        return tint;
    }

    std::expected<GraphicsResource, std::string> CreateSampledImage(
        ResourceContext& resources,
        const gli::texture2d& texture,
        ImageFormat format,
        std::string_view name)
    {
        const uint32_t levels = static_cast<uint32_t>(texture.levels());
        const gli::extent2d extent = texture.extent(0u);

        std::vector<RhiImageResourceData> mipData;
        mipData.reserve(levels);

        for (uint32_t level = 0u; level < levels; ++level)
        {
            const gli::extent2d levelExtent = texture.extent(level);
            const std::byte* levelBytes = static_cast<const std::byte*>(texture.data(0u, 0u, level));

            RhiImageResourceData data{};
            data.Data = std::span<const std::byte>{ levelBytes, texture.size(level) };
            data.Width = static_cast<uint32_t>(levelExtent.x);
            data.Height = static_cast<uint32_t>(levelExtent.y);
            data.MipLevel = level;
            mipData.push_back(data);
        }

        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageBits::SampledImage | ImageUsageBits::SrvFragmentShader;
        info.Type = ImageType::Type2D;
        info.Format = format;
        info.Width = static_cast<uint32_t>(extent.x);
        info.Height = static_cast<uint32_t>(extent.y);
        info.Depth = 1u;
        info.MipLevels = levels;
        info.ViewInfo = ImageViewCreateInfo{};
        info.ViewInfo->ViewType = ImageViewType::Type2D;
        info.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;
        info.ViewInfo->Range.LevelCount = levels;
        info.Name = name;

        std::shared_ptr<GraphicsResourceReply> reply = resources.CreateImage(info, mipData);
        if (reply == nullptr || reply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return std::unexpected(std::string{ "creating the GPU image failed" });
        }

        return reply->GetResource();
    }
}
