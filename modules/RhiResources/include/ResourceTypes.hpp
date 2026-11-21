#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
#include "RhiResourceFlags.hpp"
#include "ImageDataFormats.hpp"
#include "math/Math.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

/**
 * @brief Contains definitions for user-facing resource data structures for use with the resource context and RHI.
 */

/** @brief Holds the raw data and the place in the buffer where it goes (especially during initial creation).
 *  Build Data with std::as_bytes(std::span{ values }). The context copies the bytes before the call returns.
 *  `DestinationOffset` is the byte offset in the destination buffer where this piece starts. Each piece has its own
 *  offset: the context does not pack the pieces. */
struct RhiBufferResourceData
{
    std::span<const std::byte> Data;
    uint64_t DestinationOffset{ 0u };
};

/** @brief One byte range of a buffer to buffer copy. */
struct BufferCopyRegion
{
    uint64_t SourceOffset{ 0u };
    uint64_t DestinationOffset{ 0u };
    uint64_t Size{ 0u };
};

/** @brief The most regions that one transfer call takes. */
constexpr int32_t k_MaxTransferRegions = 256;

/** @brief Holds the raw data and some dimensional metadata that will be made the contents of an image resource. */
struct RhiImageResourceData
{
    std::span<const std::byte> Data;
    uint32_t Width{ 0u };
    uint32_t Height{ 0u };
    uint32_t ArrayLayer{ 0u };
    uint32_t NumLayers{ 1u };
    uint32_t MipLevel{ 0u };
    uint32_t NumMips{ 0u };
};

struct BufferCreateInfo
{
    ResourceDomain Domain{ ResourceDomain::Invalid };
    ResourceCreationFlags Flags{ ResourceCreationFlags::None };
    BufferUsageBits Usage{ BufferUsageBits::Invalid };
    size_t Size{ 0u };
    /** @brief If this member is given, the buffer will be created with a correponding BufferView object. Otherwise, we won't create a view. */
    std::optional<ImageFormat> ViewFormat{ std::nullopt };
    /** @brief Optional debug name. ResourceContext copies it when the create call is made, so the string only has to
     *  live until the call returns. Names longer than 62 characters are truncated. */
    std::string_view Name;
};

/** @brief Describes a specific range within an image, including mip levels and array layers, used for creating image views or specifying subresource ranges in image operations. */
struct ImageRange
{
    ImageAspectFlags AspectMask{ ImageAspectFlags::None };
    uint32_t BaseMipLevel{ 0u };
    uint32_t LevelCount{ 1u };
    uint32_t BaseArrayLayer{ 0u };
    uint32_t LayerCount{ 1u };
};

/** @brief Describes the mapping of image components for a view, mapping components of the image in memory to what is returned by shader load instructions.
 *  @note Can be left to identity in most cases, where each component maps directly to itself.
 */
struct ImageComponentMapping
{
    ComponentSwizzle R{ ComponentSwizzle::Red };
    ComponentSwizzle G{ ComponentSwizzle::Green };
    ComponentSwizzle B{ ComponentSwizzle::Blue };
    ComponentSwizzle A{ ComponentSwizzle::Alpha };
};

/** @brief Default component mapping where each channel maps to itself (RGBA -> RGBA) */
constexpr static inline ImageComponentMapping IdentityImageSwizzle{};

struct ImageViewCreateInfo
{
    /** @brief Handle to the image resource this view is created from. Can be null when attached to an ImageCreateInfo struct, at which point it's considered the view for that image. */
    uint64_t ImageHandle{ 0u };
    ImageViewType ViewType{ ImageViewType::Invalid };
    ImageComponentMapping ComponentMapping{ IdentityImageSwizzle };
    ImageRange Range{};
    /** @brief If provided, will reinterpret the format of the base image to the given image for this view */
    std::optional<ImageFormat> FormatOverride{ std::nullopt };
};

// the device feature that SamplerCreateInfo::AnisotropyEnabled needs. A caller asks for it as a need, and checks
// rhi::Device::IsFeatureEnabled before it sets AnisotropyEnabled
inline constexpr std::string_view k_SamplerAnisotropyFeature{ "VkPhysicalDeviceFeatures.samplerAnisotropy" };

struct SamplerCreateInfo
{
    enum class AddressMode : uint8_t
    {
        None = 0,
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder,
        /** @note Provided by VK_KHR_sampler_mirror_clamp_to_edge or Vk1.2. */
        MirrorClampToEdge
    };

    enum class ImageBorderColor : uint8_t
    {
        None = 0,
        FloatTransparentBlack,
        IntTransparentBlack,
        FloatOpaqueBlack,
        IntOpaqueBlack,
        FloatOpaqueWhite,
        IntOpaqueWhite,
        /** @note Requires optional member of sampler create info to be specified */
        FloatCustom,
        /** @note Requires optional member of sampler create info to be specified */
        IntCustom
    };


    ImageFilteringMode MagFilter{ ImageFilteringMode::Linear };
    ImageFilteringMode MinFilter{ ImageFilteringMode::Linear };
    ImageFilteringMode MipFilter{ ImageFilteringMode::Linear };

    AddressMode AddressU{ AddressMode::Repeat };
    AddressMode AddressV{ AddressMode::Repeat };
    AddressMode AddressW{ AddressMode::Repeat };

    float MipLoadBias{ 0.0f };

    bool AnisotropyEnabled{ false };
    float MaxAnisotropy{ 1.0f };

    bool CompareEnabled{ false };
    ImageCompareOp CompareOp{ ImageCompareOp::None };

    float MinLod{ 0.0f };
    float MaxLod{ 1e10f };

    ImageBorderColor BorderColor{ ImageBorderColor::None };
    // all values set to -1.0, so that validation checks in the rendergraph
    // can catch when this is not properly set (-1.0f, invalid color, because 0.0f is fine)
    std::optional<math::Float4> CustomBorderColor{ std::nullopt };

    bool UnnormalizedCoordinates{ false };
    std::string_view Name{};
};

struct ImageCreateInfo
{
    ResourceDomain Domain{ ResourceDomain::Invalid };
    ResourceCreationFlags Flags{ ResourceCreationFlags::None };
    ImageUsageBits Usage{ ImageUsageBits::Invalid };
    ImageType Type{ ImageType::Invalid };
    ImageFormat Format;
    uint32_t Width{ 0u };
    uint32_t Height{ 0u };
    uint32_t Depth{ 0u };
    uint32_t MipLevels{ 1u };
    uint32_t ArrayLayers{ 1u };
    /** @brief Samples per texel: 1, or a power of two the device supports for this format and usage (multisampled
     *  render targets). A multisampled image has one mip level and cannot take initial data */
    uint32_t SampleCount{ 1u };
    /** @brief Optional view info - if present we will create a view with this image, if not no view will be created. */
    std::optional<ImageViewCreateInfo> ViewInfo{ std::nullopt };
    /** @brief Optional sampler info - if present we will create a sampler with this image, if not no sampler will be created. */
    std::optional<SamplerCreateInfo> SamplerInfo{ std::nullopt };
    /** @brief Optional debug name, copied as `BufferCreateInfo::Name` is. The view and the sampler are named after it. */
    std::string_view Name;
};

namespace rhi
{
    /** @brief The image's usage on the GPU just before the transfer runs, and the usage it must have after the transfer.
     *  Both must be non-zero, and each must be a subset of the usage the image was created with. `Before` can also be
     *  `ImageUsageBits::Discard` alone: the old contents do not matter, and the transfer starts from UNDEFINED. */
    struct ImageUsageTransition
    {
        ImageUsageBits Before{ ImageUsageBits::Invalid };
        ImageUsageBits After{ ImageUsageBits::Invalid };
    };
}

/** @brief The most consumer timelines that a ResourceContext checks before it frees a destroyed resource. */
constexpr inline int32_t k_MaxConsumerTimelines = 4;

struct GraphicsResource
{
    static GraphicsResource Null() noexcept;

    bool operator==(const GraphicsResource& other) const noexcept;
    bool operator!=(const GraphicsResource& other) const noexcept;
    explicit operator bool() const noexcept;

    ResourceDomain Domain{ ResourceDomain::Invalid };
    ResourceType Type{ ResourceType::Invalid };
    uint32_t EntityHandle{ 0u };
    uint64_t VkHandle{ 0u };
    uint64_t VkViewHandle{ 0u };
    uint64_t VkSamplerHandle{ 0u };
    uint64_t DeviceAddress{ 0u };
};

/**
 * @brief Facts about a resource that never change after it is created. `ResourceFuture<GraphicsResource>::Description()`
 * returns it. A field that does not apply to the resource type has its zero value.
 *
 * The context is stateless. It does not track the usage of a resource after the creating call. Whoever hands the
 * resource on states its usage at that time.
 */
struct ResourceDescription
{
    ResourceType Type{ ResourceType::Invalid };
    ResourceDomain Domain{ ResourceDomain::Invalid };
    /** @brief Buffers: the usage that the context created the buffer with. */
    BufferUsageBits BufferUsage{ BufferUsageBits::Invalid };
    /** @brief Buffers: the size that the caller asked for. It is not the size of the allocation. */
    uint64_t ByteSize{ 0u };
    /** @brief Images: the usage that the context created the image with. It includes `TransferDestination` when the
     *  creating call had initial data. */
    ImageUsageBits ImageUsage{ ImageUsageBits::Invalid };
    ImageType Dimensions{ ImageType::Invalid };
    ImageFormat Format{};
    int32_t Width{ 0 };
    int32_t Height{ 0 };
    int32_t Depth{ 0 };
    int32_t MipLevels{ 0 };
    int32_t ArrayLayers{ 0 };
    int32_t SampleCount{ 0 };
    /** @brief The usage that the creating call left the image in. Invalid when that call wrote no data: the image
     *  has no defined contents yet. */
    ImageUsageBits InitialUsage{ ImageUsageBits::Invalid };
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
