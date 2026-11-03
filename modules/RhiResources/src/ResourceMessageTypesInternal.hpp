#pragma once
#ifndef RESOURCE_MESSAGE_TYPES_INTERNAL_HPP
#define RESOURCE_MESSAGE_TYPES_INTERNAL_HPP
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>
#include <vulkan/vulkan_core.h>

/** @brief A debug name, copied on the calling thread when the message is built. The create infos carry a
 *  `std::string_view`, and the caller's string may be gone by the time the worker reads the message, so we copy it
 *  into this fixed buffer instead of keeping the view. Longer names are truncated. The text is always
 *  null-terminated, because Vulkan's object naming reads a C string. */
struct DebugName
{
    static constexpr int32_t k_Capacity = 63;

    DebugName() noexcept = default;
    explicit DebugName(std::string_view name) noexcept;

    /** @brief Returns this name followed by `suffix`, truncated to the capacity. Used to name views and samplers
     *  after the resource that owns them. */
    [[nodiscard]] DebugName WithSuffix(std::string_view suffix) const noexcept;
    [[nodiscard]] std::string_view View() const noexcept;
    [[nodiscard]] const char* CString() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;

    std::array<char, k_Capacity> Text{};
    int8_t Length{ 0 };
};

static_assert(sizeof(DebugName) == 64, "DebugName is sized to one cache line");

/** @brief The waits of one transfer call, in a fixed array so that the message needs no allocation. */
struct TransferWaitSet
{
    std::array<rhi::TransferWait, rhi::k_MaxTransferWaits> Items{};
    int32_t Count{ 0 };
};

// Users still call functions with the old API on the surface, but we translate to these internal structures
// so that we can place them in the message queue (type erased by being shoved in a variant, effectively). `Future` is
// the index of the call's slot in the context's future table: the handlers write the result there

// Because users just provide pointers to raw data, we need to take a copy of the data so that exiting
// the function once they finish enqueuing the message doesn't invalidate the data right before we use it.
// We'll free this as soon as it's uploaded to the GPU or a staging buffer
struct InternalResourceDataContainer
{

    struct BufferData
    {
        BufferData() noexcept;
        BufferData(const RhiBufferResourceData& _data);
        ~BufferData() = default;

        BufferData(const BufferData&) = delete;
        BufferData& operator=(const BufferData&) = delete;

        BufferData(BufferData&& other) noexcept;
        BufferData& operator=(BufferData&& other) noexcept;

        std::unique_ptr<std::byte[]> Data{ nullptr };
        size_t Size{ 0u };
        size_t Alignment{ 0u };
    };

    using BufferDataVector = std::vector<BufferData>;

    // Images have a bit more metadata, so we need this struct for them
    struct ImageData
    {
        ImageData() noexcept;
        ImageData(const RhiImageResourceData& _data);
        ~ImageData() = default;

        ImageData(const ImageData&) = delete;
        ImageData& operator=(const ImageData&) = delete;

        ImageData(ImageData&& other) noexcept;
        ImageData& operator=(ImageData&& other) noexcept;

        std::unique_ptr<std::byte[]> Data{ nullptr };
        // size of the actual image data, not width/height info
        size_t Size{ 0u };
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        uint32_t ArrayLayer{ 0u };
        uint32_t NumLayers{ 0u };
        uint32_t MipLevel{ 0u };
    };

    using ImageDataVector = std::vector<ImageData>;

    std::variant<BufferDataVector, ImageDataVector> DataVector;
    std::optional<uint32_t> NumLayers;

    InternalResourceDataContainer(std::span<const RhiBufferResourceData> data);

    InternalResourceDataContainer(std::span<const RhiImageResourceData> data);

    InternalResourceDataContainer() noexcept;
};

// the create infos in these messages always have empty `Name` views: ResourceContext copies the name into `Name`
// on the calling thread, then clears the view, so that no stored create info refers to the caller's string
struct CreateBufferMessage
{
    BufferCreateInfo BufferInfo;
    DebugName Name;
    std::optional<InternalResourceDataContainer> InitialData = std::nullopt;
    int32_t Future{ -1 };
};

// an image with a SamplerInfo becomes a CombinedImageSampler, which replaced
// CreateCombinedImageSamplerMessage
struct CreateImageMessage
{
    ImageCreateInfo ImageInfo;
    DebugName Name;
    std::optional<InternalResourceDataContainer> InitialData = std::nullopt;
    // the usage that the image has after the upload. read only when InitialData has a value
    ImageUsageBits UsageAfterUpload{ ImageUsageBits::Invalid };
    int32_t Future{ -1 };
};

struct CreateSamplerMessage
{
    SamplerCreateInfo SamplerInfo;
    DebugName Name;
    int32_t Future{ -1 };
};

/** @brief Returns the create message with the name copied on the calling thread and the view cleared. Leaves the data
 *  and the usage after upload empty. */
[[nodiscard]] CreateBufferMessage MakeCreateBufferMessage(const BufferCreateInfo& create_info, int32_t future_slot);
/** @brief As MakeCreateBufferMessage, for an image. The sampler takes its name from the image. */
[[nodiscard]] CreateImageMessage MakeCreateImageMessage(const ImageCreateInfo& create_info, int32_t future_slot);

/** @brief Asks for a block of device-local memory. The worker checks both fields. */
struct CreateMemoryHeapMessage
{
    uint64_t Size{ 0u };
    uint32_t MemoryTypeBits{ 0u };
    DebugName Name;
    int32_t Future{ -1 };
};

/** @brief A buffer or image that is bound to memory in a heap at an offset, instead of allocating its own. The create
 *  message in it never has initial data. */
struct CreatePlacedBufferMessage
{
    CreateBufferMessage Create;
    GraphicsResource Heap{ GraphicsResource::Null() };
    uint64_t Offset{ 0u };
};

struct CreatePlacedImageMessage
{
    CreateImageMessage Create;
    GraphicsResource Heap{ GraphicsResource::Null() };
    uint64_t Offset{ 0u };
};

struct SetBufferDataMessage
{
    SetBufferDataMessage() noexcept = default;
    SetBufferDataMessage(GraphicsResource _destBuffer, std::span<const RhiBufferResourceData> data) noexcept;
    SetBufferDataMessage(GraphicsResource _destBuffer,
                         InternalResourceDataContainer&& _data,
                         int32_t future_slot) noexcept;
    SetBufferDataMessage(const SetBufferDataMessage&) = delete;
    SetBufferDataMessage& operator=(const SetBufferDataMessage&) = delete;
    SetBufferDataMessage(SetBufferDataMessage&& other) noexcept;
    SetBufferDataMessage& operator=(SetBufferDataMessage&& other) noexcept;

    GraphicsResource DestBuffer{ GraphicsResource::Null() };
    InternalResourceDataContainer Data;
    TransferWaitSet Waits;
    int32_t Future{ -1 };
};

struct SetImageDataMessage
{
    SetImageDataMessage() noexcept = default;
    SetImageDataMessage(GraphicsResource _destImage, std::span<const RhiImageResourceData> data) noexcept;
    // for use when transferring rest of work to the transfer system
    SetImageDataMessage(GraphicsResource _destImage,
                        InternalResourceDataContainer&& _data,
                        int32_t future_slot) noexcept;
    SetImageDataMessage(const SetImageDataMessage&) = delete;
    SetImageDataMessage& operator=(const SetImageDataMessage&) = delete;
    SetImageDataMessage(SetImageDataMessage&& other) noexcept;
    SetImageDataMessage& operator=(SetImageDataMessage&& other) noexcept;

    GraphicsResource DestImage{ GraphicsResource::Null() };
    InternalResourceDataContainer Data;
    rhi::ImageUsageTransition Usage;
    TransferWaitSet Waits;
    int32_t Future{ -1 };
};

struct FillResourceMessage
{
    GraphicsResource Resource{ GraphicsResource::Null() };
    uint32_t Value{ 0 };
    size_t Offset{ 0 };
    size_t Size{ 0 };
    TransferWaitSet Waits;
    int32_t Future{ -1 };
};

struct MapResourceMessage
{
    GraphicsResource Resource{ GraphicsResource::Null() };
    size_t Size{ 0 };
    size_t Offset{ 0 };
    int32_t Future{ -1 };
};

struct UnmapResourceMessage
{
    GraphicsResource Resource{ GraphicsResource::Null() };
    size_t Size{ 0 };
    size_t Offset{ 0 };
    int32_t Future{ -1 };
};

struct CopyResourceMessage
{
    GraphicsResource SourceResource{ GraphicsResource::Null() };
    // CopyBuffer sends false and CopyImage sends true. a source of the other kind fails with WrongResourceKind
    bool SourceIsImage{ false };
    // not copying contents just means using the source like a template I guess
    bool CopyContents = false;
    // read only for an image source with contents
    rhi::ImageUsageTransition SourceUsage;
    ImageUsageBits CopyUsageAfter{ ImageUsageBits::Invalid };
    TransferWaitSet Waits;
    int32_t Future{ -1 };
};

// the public call decides the kind. the worker fails with WrongResourceKind when a resource is not of the kind
enum class CopyKind : uint8_t
{
    Invalid = 0,
    BufferToBuffer = 1,
    ImageToBuffer = 2,
    BufferToImage = 3,
    ImageToImage = 4,
};

struct CopyResourceContentsMessage
{
    CopyKind Kind{ CopyKind::Invalid };
    GraphicsResource SourceResource{ GraphicsResource::Null() };
    GraphicsResource DestinationResource{ GraphicsResource::Null() };
    // each is read only when that side is an image
    rhi::ImageUsageTransition SourceUsage;
    rhi::ImageUsageTransition DestinationUsage;
    // true for the new image of CopyImage: it has no layout yet, so its transfer starts from UNDEFINED
    bool DestinationStartsUndefined{ false };
    TransferWaitSet Waits;
    int32_t Future{ -1 };
};

struct DestroyResourceMessage
{
    GraphicsResource Resource{ GraphicsResource::Null() };
    int32_t Future{ -1 };
};

/** @brief Tells the worker thread to return. Shutdown is a message so that the worker can block on the queue instead
 *  of polling a flag. */
struct ExitWorkerMessage
{
};

/** @brief One timeline's pending value, as the owner reported it in Tick. Semaphore is a VkSemaphore cast to uint64_t. */
struct ConsumerTimelinePoint
{
    uint64_t Semaphore{ 0u };
    uint64_t PendingValue{ 0u };
};

/** @brief The owner's per-frame call. Fixed size, so that the message needs no allocation. */
struct TickMessage
{
    std::array<ConsumerTimelinePoint, k_MaxConsumerTimelines> Points{};
    int32_t Count{ 0 };
};

struct RegisterConsumerTimelineMessage
{
    uint64_t Semaphore{ 0u };
    int32_t Future{ -1 };
};

struct UnregisterConsumerTimelineMessage
{
    uint64_t Semaphore{ 0u };
    int32_t Future{ -1 };
};

/** @brief Sent by the transfer thread when its completed ticket moves, so that a worker blocked on an empty queue checks
 *  its retirements. It carries nothing: the worker reads the published ticket itself. */
struct TransferProgressMessage
{
};

using ResourceMessagePayloadType = std::variant<CreateBufferMessage,
                                                CreateImageMessage,
                                                CreateSamplerMessage,
                                                CreateMemoryHeapMessage,
                                                CreatePlacedBufferMessage,
                                                CreatePlacedImageMessage,
                                                SetBufferDataMessage,
                                                SetImageDataMessage,
                                                FillResourceMessage,
                                                MapResourceMessage,
                                                UnmapResourceMessage,
                                                CopyResourceMessage,
                                                CopyResourceContentsMessage,
                                                DestroyResourceMessage,
                                                ExitWorkerMessage,
                                                TickMessage,
                                                RegisterConsumerTimelineMessage,
                                                UnregisterConsumerTimelineMessage,
                                                TransferProgressMessage>;

// following message types are broadly the same, but feature extra info to reduce need for thread sync or
// safety concerns with transfer system.

struct TransferSystemReqBufferInfo
{
    VkBuffer BufferHandle{ VK_NULL_HANDLE };
    BufferCreateInfo CreateInfo;
};

struct TransferSystemReqImageInfo
{
    VkImage ImageHandle{ VK_NULL_HANDLE };
    ImageCreateInfo CreateInfo;
    // the layout the image is in when the transfer starts, and the layout the transfer leaves it in. the worker
    // derives both from the caller's usages, and the transfer thread uses them as they come
    VkImageLayout BeforeLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageLayout AfterLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
};

struct TransferSystemSetBufferDataMessage
{
    TransferSystemReqBufferInfo BufferInfo;
    InternalResourceDataContainer Data;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemSetImageDataMessage
{
    TransferSystemReqImageInfo ImageInfo;
    InternalResourceDataContainer Data;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemFillBufferMessage
{
    TransferSystemReqBufferInfo BufferInfo;
    uint32_t Value{ 0u };
    size_t Offset{ 0u };
    size_t Size{ 0u };
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemCopyBufferToBufferMessage
{
    TransferSystemReqBufferInfo SrcBufferInfo;
    TransferSystemReqBufferInfo DstBufferInfo;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemCopyImageToImageMessage
{
    TransferSystemReqImageInfo SrcImageInfo;
    TransferSystemReqImageInfo DstImageInfo;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemCopyImageToBufferMessage
{
    TransferSystemReqImageInfo ImageInfo;
    TransferSystemReqBufferInfo BufferInfo;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemCopyBufferToImageMessage
{
    TransferSystemReqBufferInfo BufferInfo;
    TransferSystemReqImageInfo ImageInfo;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

/** @brief Tells the transfer thread to return, so that it can block on an empty queue instead of polling a flag. */
struct TransferSystemExitMessage
{
};

using TransferPayloadType = std::variant<TransferSystemSetBufferDataMessage,
                                         TransferSystemSetImageDataMessage,
                                         TransferSystemFillBufferMessage,
                                         TransferSystemCopyBufferToBufferMessage,
                                         TransferSystemCopyImageToImageMessage,
                                         TransferSystemCopyImageToBufferMessage,
                                         TransferSystemCopyBufferToImageMessage,
                                         TransferSystemExitMessage>;

#endif // RESOURCE_MESSAGE_TYPES_INTERNAL_HPP
