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
#include <variant>

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

/** @brief A box of texels in one mip level, for each layer of a range of layers. `X`, `Y` and `Z` are the texel offset
 *  of the box, and `Width`, `Height` and `Depth` are its extent. Every extent and `LayerCount` must be above 0.
 *  An image that is not 3D has `Z` of 0 and `Depth` of 1. A block-compressed format counts texels, and the box must
 *  start on a block edge. It must end on a block edge or on the edge of the mip level. */
struct ImageRegion
{
    int32_t MipLevel{ 0 };
    int32_t BaseArrayLayer{ 0 };
    int32_t LayerCount{ 0 };
    int32_t X{ 0 };
    int32_t Y{ 0 };
    int32_t Z{ 0 };
    int32_t Width{ 0 };
    int32_t Height{ 0 };
    int32_t Depth{ 0 };
};

/** @brief One box of an image, and where its texels are in a buffer. The texels are in rows, and the rows are in
 *  images, and the layers of the box follow each other. `BufferRowTexels` and `BufferImageRows` set the distance between
 *  rows and between images in texels, as `bufferRowLength` and `bufferImageHeight` do in Vulkan. */
struct BufferImageRegion
{
    uint64_t BufferOffset{ 0u };
    // 0 means tightly packed, as in Vulkan
    int32_t BufferRowTexels{ 0 };
    int32_t BufferImageRows{ 0 };
    ImageRegion Image;
};

/** @brief One box of an image to image copy. `Source` gives the extent and the layer count of both sides. Of
 *  `Destination`, the call reads `MipLevel`, `BaseArrayLayer`, `X`, `Y` and `Z`, and ignores the rest. */
struct ImageCopyRegion
{
    ImageRegion Source;
    ImageRegion Destination;
};

/** @brief Holds the raw data of one rectangle of an image, and where the rectangle goes. The texels of each layer are
 *  tightly packed, layer after layer. The context copies the bytes before the call returns.
 *  `X`, `Y` and `Z` are the texel offset of the rectangle in the mip level, and `Width`, `Height` and `Depth` are its
 *  extent. `Depth` of 0 means the depth of the mip level. A piece with no offset and the extent of the mip level
 *  writes the whole mip level. A block-compressed format counts the extent and the offset in texels, as Vulkan does. */
struct RhiImageResourceData
{
    std::span<const std::byte> Data;
    int32_t Width{ 0 };
    int32_t Height{ 0 };
    int32_t ArrayLayer{ 0 };
    int32_t NumLayers{ 1 };
    int32_t MipLevel{ 0 };
    int32_t NumMips{ 0 };
    int32_t X{ 0 };
    int32_t Y{ 0 };
    int32_t Z{ 0 };
    int32_t Depth{ 0 };
};

struct BufferCreateInfo
{
    ResourceDomain Domain{ ResourceDomain::Invalid };
    ResourceCreationFlags Flags{ ResourceCreationFlags::None };
    /** @brief If this member is given, the buffer will be created with a correponding BufferView object. Otherwise, we won't create a view. */
    std::optional<ImageFormat> ViewFormat{ std::nullopt };
    BufferUsageBits Usage{ BufferUsageBits::Invalid };
    size_t Size{ 0u };
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
    // ComponentSwizzle values, not channel indices: None is 0, so identity is Red, Green, Blue, Alpha
    uint16_t R : 4 = static_cast<uint16_t>(ComponentSwizzle::Red);
    uint16_t G : 4 = static_cast<uint16_t>(ComponentSwizzle::Green);
    uint16_t B : 4 = static_cast<uint16_t>(ComponentSwizzle::Blue);
    uint16_t A : 4 = static_cast<uint16_t>(ComponentSwizzle::Alpha);
    [[nodiscard]] constexpr ComponentSwizzle Red() const noexcept
    {
        return static_cast<ComponentSwizzle>(R);
    }
    [[nodiscard]] constexpr ComponentSwizzle Green() const noexcept
    {
        return static_cast<ComponentSwizzle>(G);
    }
    [[nodiscard]] constexpr ComponentSwizzle Blue() const noexcept
    {
        return static_cast<ComponentSwizzle>(B);
    }
    [[nodiscard]] constexpr ComponentSwizzle Alpha() const noexcept
    {
        return static_cast<ComponentSwizzle>(A);
    }
};
static_assert(sizeof(ImageComponentMapping) == 2, "ImageComponentMapping should be packed into two bytes");
static_assert(ImageComponentMapping{}.Red() == ComponentSwizzle::Red && ImageComponentMapping{}.Alpha() == ComponentSwizzle::Alpha,
              "A default ImageComponentMapping is the identity");

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

    // some of these members will seem in a weird order, but this order
    // cuts out a ton of excess padding that is added since we have so 
    // many members that are just 1 byte in size
    ImageFilteringMode MagFilter{ ImageFilteringMode::Linear };
    ImageFilteringMode MinFilter{ ImageFilteringMode::Linear };
    ImageFilteringMode MipFilter{ ImageFilteringMode::Linear };
    AddressMode AddressU{ AddressMode::Repeat };
    AddressMode AddressV{ AddressMode::Repeat };
    AddressMode AddressW{ AddressMode::Repeat };
    bool AnisotropyEnabled{ false };
    bool CompareEnabled{ false };
    ImageCompareOp CompareOp{ ImageCompareOp::None };
    ImageBorderColor BorderColor{ ImageBorderColor::None };
    bool UnnormalizedCoordinates{ false };
    float MipLoadBias{ 0.0f };
    float MaxAnisotropy{ 1.0f };
    float MinLod{ 0.0f };
    float MaxLod{ 1e10f };
    // all values set to -1.0, so that validation checks in the rendergraph
    // can catch when this is not properly set (-1.0f, invalid color, because 0.0f is fine)
    std::optional<math::Float4> CustomBorderColor{ std::nullopt };
    std::string_view Name{};
};

struct ImageCreateInfo
{
    ResourceDomain Domain{ ResourceDomain::Invalid };
    ResourceCreationFlags Flags{ ResourceCreationFlags::None };
    ImageType Type{ ImageType::Invalid };
    ImageFormat Format;
    ImageUsageBits Usage{ ImageUsageBits::Invalid };
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
    // todo-ship: these two fields could be overlapped: DeviceAddress is only for buffers!
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

/** @brief What one op of a transfer set does. Each kind reads the fields of `TransferSetOp` that its row lists, and
 *  `Region` must hold the alternative that its row names. A set with an op whose `Region` holds another alternative
 *  fails with `InvalidTransferSet`. */
enum class TransferOpKind : uint8_t
{
    Invalid = 0,
    /** @brief Copies bytes of `TransferSetDesc::Data` into the `Destination` buffer. `Region` holds a
     *  `BufferCopyRegion`: `SourceOffset` is an offset into `Data`, and `DestinationOffset` is an offset into the
     *  buffer. */
    UploadBuffer = 1,
    /** @brief Copies texels of `Data` into the `Destination` image. `Region` holds a `BufferImageRegion`, which is
     *  the box, and its `BufferOffset` is an offset into `Data`. */
    UploadImage = 2,
    /** @brief Fills a range of the `Destination` buffer with `FillValue`. `Region` holds a `BufferCopyRegion`, with
     *  `DestinationOffset` and `Size`. */
    FillBuffer = 3,
    /** @brief Copies one range of the `Source` buffer to the `Destination` buffer. `Region` holds a
     *  `BufferCopyRegion`. */
    CopyBuffer = 4,
    /** @brief Copies one box from the `Source` buffer to the `Destination` image. `Region` holds a
     *  `BufferImageRegion`, which is the box. */
    CopyBufferToImage = 5,
    /** @brief Copies one box from the `Source` image to the `Destination` buffer. `Region` holds a
     *  `BufferImageRegion`, which is the box. */
    CopyImageToBuffer = 6,
    /** @brief Copies one box from the `Source` image to the `Destination` image. `Region` holds an
     *  `ImageCopyRegion`. */
    CopyImage = 7,
};

/**
 * @brief One region of one transfer in a transfer set. A caller that wants several regions of one resource uses
 * several ops. `SourceUsage` and `DestinationUsage` are read only for an image on that side, and mean what they mean in
 * the single calls (`Discard` as `Before` of a destination needs the region to cover its whole mip level).
 *
 * `Region` holds one alternative, and `Kind` decides which one (the rows of `TransferOpKind` list them). A variant
 * stores one region and not three, so an op is smaller, and the worker checks the alternative before it reads it.
 */
struct TransferSetOp
{
    TransferOpKind Kind{ TransferOpKind::Invalid };
    GraphicsResource Source;
    GraphicsResource Destination;
    rhi::ImageUsageTransition SourceUsage;
    rhi::ImageUsageTransition DestinationUsage;
    std::variant<BufferCopyRegion, BufferImageRegion, ImageCopyRegion> Region;
    uint32_t FillValue{ 0u };
};

/**
 * @brief The ops of a transfer set, in ranges, and the bytes that its uploads read.
 *
 * `RangeEnds` lists the end index of each range, and the values rise. The last one equals `Ops.size()`, and each range
 * has at least one op. The ops of one range are independent: the transfer thread records them with no barrier between
 * them, so no two ops of a range may write one resource, and no resource may be both read and written. Debug builds
 * check this and fail the set with `RangeHazard`. The ranges run in order, and each range sees the writes of the ranges
 * before it.
 *
 * The context is stateless, and it does not check that the usages of an image agree from range to range. When an image
 * is in two ranges, the `Before` usage of the later op must be the `After` usage that the earlier op gave.
 */
struct TransferSetDesc
{
    std::span<const TransferSetOp> Ops;
    std::span<const int32_t> RangeEnds;
    std::span<const std::byte> Data;
};

/** @brief The most ops that one transfer set takes. A fixed cap, so that one set cannot block the transfer thread for long. */
constexpr int32_t k_MaxTransferSetOps = 256;

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_TYPES_HPP
