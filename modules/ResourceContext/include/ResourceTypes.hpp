#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
#include <cstdint>
#include "ResourceFlags.hpp"
#include "ResourceDataFormats.hpp"
#include <optional>

/**
 * @brief Contains definitions for user-facing resource data structures for use with the resource context and RHI.
 */

/** @brief Holds the raw data and some metadata that will be used to set the contents of a buffer resource (especially during initial creation). */
struct RhiBufferResourceData
{
    const void* Data{ 0u };
    size_t DataSize{ 0u };
    size_t DataAlignment{ 0u };
};

/** @brief Holds the raw data and some dimensional metadata that will be made the contents of an image resource. */
struct RhiImageResourceData
{
    const void* Data{ nullptr };
    size_t DataSize{ 0u };
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
    std::optional<ResourceFormat> ViewFormat{ std::nullopt };
    /** @brief User-defined data pointer that can be associated with the buffer for application-specific purposes. */
    void* UserData{ nullptr };
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
    std::optional<ResourceFormat> FormatOverride{ std::nullopt };
    /** @brief User-defined data pointer that can be associated with the image view for application-specific purposes. */
    void* UserData{ nullptr };
};

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
    float MaxLod{ std::numeric_limits<float>::max() };

    ImageBorderColor BorderColor{ ImageBorderColor::None };
    // leaving undefined for now since we don't have RGBA color object yet
    // std::optional<RGBAColor> CustomBorderColor;

    bool UnnormalizedCoordinates{ false };
};

struct ImageCreateInfo
{
    ResourceDomain Domain{ ResourceDomain::Invalid };
    ResourceCreationFlags Flags{ ResourceCreationFlags::None };
    ImageUsageBits Usage{ ImageUsageBits::Invalid };
    ImageType Type{ ImageType::Invalid };
    ResourceFormat Format{};
    uint32_t Width{ 0u };
    uint32_t Height{ 0u };
    uint32_t Depth{ 0u };
    uint32_t MipLevels{ 1u };
    uint32_t ArrayLayers{ 1u };
    /** @brief Optional view info - if present we will create a view with this image, if not no view will be created. */
    std::optional<ImageViewCreateInfo> ViewInfo{ std::nullopt };
    /** @brief Optional sampler info - if present we will create a sampler with this image, if not no sampler will be created. */
    std::optional<SamplerCreateInfo> SamplerInfo{ std::nullopt };
    /** @brief User-defined data pointer that can be associated with the image for application-specific purposes. */
    void* UserData{ nullptr };
};

struct GraphicsResource
{
    static GraphicsResource Null() noexcept;
    
    constexpr bool operator==(const GraphicsResource& other) const noexcept;
    constexpr bool operator!=(const GraphicsResource& other) const noexcept;
    constexpr explicit operator bool() const noexcept;

    ResourceDomain Domain{ ResourceDomain::Invalid };
    uint32_t EntityHandle{ 0u };
    uint64_t VkHandle{ 0u };
    uint64_t VkViewHandle{ 0u };
    uint64_t VkSamplerHandle{ 0u };
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
