#pragma once
#ifndef DIAMOND_DOGS_FRAME_CAPTURE_HPP
#define DIAMOND_DOGS_FRAME_CAPTURE_HPP
#include "ImageDataFormats.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "containers/mwsrQueue.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class ResourceContext;

namespace rhi
{
    struct CommandBuffer;
}

namespace capture
{
    /** @brief How the stored values encode color, which decides the conversion to 8-bit sRGB */
    enum class CaptureEncoding : uint8_t
    {
        /** @brief Already sRGB encoded: an sRGB format, or a UNorm format on an sRGB-nonlinear swapchain */
        sRGB,
        /** @brief Linear extended sRGB (scRGB), 1.0 = 80 nits: values are clamped to [0, 1], then sRGB encoded */
        LinearExtendedSRGB,
        /** @brief HDR10: BT.2020 primaries with the ST 2084 (PQ) curve. 80 nits maps to 1.0, then it is clamped */
        HDR10_PQ,
    };

    /** @brief What a tracked capture produces */
    enum class CaptureOutput : uint8_t
    {
        Invalid = 0,
        /** @brief A PNG file, as RecordCopy writes */
        Png,
        /** @brief The values of one texel, with no file */
        PixelValues,
        /** @brief The copied bytes, unchanged, in a .bin file: texels in rows, top row first, with no padding */
        Raw,
    };

    /** @brief The result of a tracked capture, which TakeResults returns */
    struct CaptureResult
    {
        uint64_t Ticket{ 0u };
        bool Succeeded{ false };
        /** @brief The file that was written. Empty for PixelValues */
        std::filesystem::path Path;
        /** @brief Raw only: the size of the written file */
        uint64_t Bytes{ 0u };
        /** @brief The size of the written image, after the downsample */
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        /** @brief PixelValues only: the decoded linear BT.709 value, after LinearScale and before any clamp. 1.0 is paper
         *  white on an HDR swapchain */
        std::array<float, 3> LinearRgb{};
        /** @brief PixelValues only: the 8-bit sRGB value that a PNG of this texel would contain */
        std::array<uint8_t, 3> Srgb8{};
        /** @brief PixelValues only: the stored bytes of the texel in hexadecimal, in memory order */
        std::string RawHex;
    };

    /** @brief Which frames to capture: Count frames, the first at First, then one every Every frames */
    struct CaptureSchedule
    {
        uint64_t First{ 0u };
        uint64_t Every{ 1u };
        uint32_t Count{ 0u };

        [[nodiscard]] bool Due(uint64_t frame) const noexcept;
        [[nodiscard]] uint64_t LastFrame() const noexcept;
    };

    struct CaptureSource
    {
        rhi::ImageHandle Image;
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        ImageFormat Format;
        CaptureEncoding Encoding{ CaptureEncoding::sRGB };
        /** @brief The layout the image is in when the copy is recorded, and the work that last wrote it */
        rhi::ImageLayout CurrentLayout{ rhi::ImageLayout::ColorAttachment };
        rhi::PipelineStage SrcStages{ rhi::PipelineStage::ColorAttachmentOutput };
        rhi::Access SrcAccess{ rhi::Access::ColorAttachmentWrite };
        /** @brief Multiplies the linear values before the clamp and the sRGB encode. Used for LinearExtendedSRGB, and
         *  for HDR10_PQ after the PQ decode (the values are linear scRGB there). Ignored for sRGB, which is already
         *  encoded */
        float LinearScale{ 1.0f };
        /** @brief The layout the image is left in, for example PresentSource for a swapchain image */
        rhi::ImageLayout FinalLayout{ rhi::ImageLayout::PresentSource };
        /** @brief The work after the capture that uses the image. The barrier back to FinalLayout blocks it. A swapchain
         *  image needs nothing: the present waits for its semaphore. An image that later commands use needs their
         *  stages and accesses, or AllCommands with MemoryRead and MemoryWrite */
        rhi::PipelineStage DstStages{ rhi::PipelineStage::Copy };
        rhi::Access DstAccess{ rhi::Access::None };
        /** @brief Color, or Depth for a depth image. Only Raw copies Depth */
        rhi::ImageAspect Aspect{ rhi::ImageAspect::Color };
        /** @brief The top-left texel of the copy. Width and Height are the size of the copied area. The caller checks
         *  that the area is inside the image */
        int32_t OffsetX{ 0 };
        int32_t OffsetY{ 0 };
        /** @brief Averages each Downsample x Downsample block in linear light before the encode. An edge block that is
         *  cut short averages the texels it has. 1 keeps every texel, and writes the same bytes as before this option */
        uint32_t Downsample{ 1u };
    };

    /** @brief A range of a buffer to copy unchanged, the work that last wrote it, and the work after the capture that
     *  writes it again */
    struct BufferCaptureSource
    {
        rhi::BufferHandle Buffer;
        uint64_t Offset{ 0u };
        uint64_t Bytes{ 0u };
        rhi::PipelineStage SrcStages{ rhi::PipelineStage::AllCommands };
        rhi::Access SrcAccess{ rhi::Access::MemoryWrite };
        rhi::PipelineStage DstStages{ rhi::PipelineStage::AllCommands };
    };

    /** Copies rendered images to PNG files (8-bit sRGB, RGB) without stalling the frame.
     *
     *  There is one readback buffer per slot (use one slot per frame in flight). RecordCopy records the image to buffer
     *  copy into a frame's command buffer. The copy is read the next time that slot is used, when the frame's fence has
     *  already been waited, or in Flush. A worker thread converts the pixels and writes the file.
     *
     *  The image needs transfer source usage (see Swapchain::SupportsTransferSource).
     *
     *  Raw copies keep the stored bytes of an image or a buffer, for data that a PNG cannot hold: normals, depth,
     *  HDR values. They share the slots, so one slot holds one copy of any kind. */
    class FrameCapture
    {
    public:
        FrameCapture(ResourceContext& resources, uint32_t slot_count, std::filesystem::path directory);
        ~FrameCapture();
        FrameCapture(const FrameCapture&) = delete;
        FrameCapture& operator=(const FrameCapture&) = delete;

        [[nodiscard]] static bool IsSupported(ImageFormat format, CaptureEncoding encoding) noexcept;
        /** @brief Returns the size of one texel that Raw copies, or 0 when Raw cannot copy the format */
        [[nodiscard]] static uint32_t RawTexelBytes(ImageFormat format) noexcept;

        /** @brief Records the copy of source into slot's buffer, and leaves the image in source.FinalLayout.
         *
         *  Call it only after the GPU is done with the slot's previous frame (after its fence wait): it first hands
         *  the slot's earlier capture to the writer. The file is directory / (name + ".png"). Returns false, and records
         *  nothing, when the format is not supported or the buffer could not be created. */
        bool RecordCopy(rhi::CommandBuffer& command_buffer, uint32_t slot, const CaptureSource& source, std::string name);

        /** @brief Records a copy like RecordCopy, and reports the result through TakeResults. Returns the ticket, or
         *  nothing when nothing was recorded */
        [[nodiscard]] std::optional<uint64_t> RecordTrackedCopy(rhi::CommandBuffer& command_buffer,
                                                                uint32_t slot,
                                                                const CaptureSource& source,
                                                                std::string name,
                                                                CaptureOutput output);

        /** @brief Records the copy of a buffer range, and reports a Raw result through TakeResults. Returns the ticket,
         *  or nothing when nothing was recorded */
        [[nodiscard]] std::optional<uint64_t> RecordTrackedBufferCopy(rhi::CommandBuffer& command_buffer,
                                                                      uint32_t slot,
                                                                      const BufferCaptureSource& source,
                                                                      std::string name);

        /** @brief Hands the slot's earlier capture to the writer. Call it once per frame after the slot's fence wait,
         *  so that a capture is written without waiting for the next capture or a Flush */
        void Collect(uint32_t slot);

        /** @brief Returns the results of tracked captures that finished since the last call. Call it on one thread */
        [[nodiscard]] std::vector<CaptureResult> TakeResults();

        /** @brief Writes every pending capture and waits for the writer. The device must be idle */
        void Flush();

        /** @brief How many files were written so far, and how many failed */
        [[nodiscard]] uint32_t WrittenCount() const noexcept;
        [[nodiscard]] uint32_t FailedCount() const noexcept;
        [[nodiscard]] const std::filesystem::path& Directory() const noexcept;

    private:
        struct Slot
        {
            GraphicsResource Buffer{};
            // the size of Buffer, and the bytes that the pending copy wrote
            uint64_t Bytes{ 0u };
            uint64_t CopiedBytes{ 0u };
            bool Pending{ false };
            uint32_t Width{ 0u };
            uint32_t Height{ 0u };
            ImageFormat Format;
            CaptureEncoding Encoding{ CaptureEncoding::sRGB };
            float LinearScale{ 1.0f };
            std::string Name;
            uint32_t Downsample{ 1u };
            // 0 for a capture that reports nothing
            uint64_t Ticket{ 0u };
            CaptureOutput Output{ CaptureOutput::Png };
        };

        // no default member initializers: clang checks the queue's default constructor before this class is complete,
        // and it rejects a nested class whose initializers are not parsed yet. Job{} still zeroes every member.
        struct Job
        {
            std::vector<std::byte> Pixels;
            uint32_t Width;
            uint32_t Height;
            ImageFormat Format;
            CaptureEncoding Encoding;
            float LinearScale;
            std::filesystem::path Path;
            uint32_t Downsample;
            uint64_t Ticket;
            CaptureOutput Output;
        };

        void drainSlot(Slot& slot);
        bool ensureBuffer(Slot& slot, uint64_t bytes);
        [[nodiscard]] bool recordCopy(rhi::CommandBuffer& command_buffer, Slot& slot, const CaptureSource& source, CaptureOutput output);
        void reportFailure(const Job& job);
        void writeJob(const Job& job);
        void writeRaw(const Job& job);
        void waitForOutstandingBelow(uint32_t limit);
        void writerLoop();

        ResourceContext& resources;
        std::filesystem::path directory;
        std::vector<Slot> slots;

        mwsrQueue<Job> jobs;
        // the writer and the frame thread push, and the caller of TakeResults pops. Only tracked captures push, and the
        // caller takes them each frame, so the blocking push never waits in practice
        mwsrQueue<CaptureResult> results;
        uint64_t nextTicket{ 1u };
        std::atomic<uint32_t> outstanding{ 0u };
        std::atomic<uint32_t> written{ 0u };
        std::atomic<uint32_t> failed{ 0u };
        std::thread writer;
    };
}

#endif //!DIAMOND_DOGS_FRAME_CAPTURE_HPP
