#pragma once
#ifndef DIAMOND_DOGS_FRAME_CAPTURE_HPP
#define DIAMOND_DOGS_FRAME_CAPTURE_HPP
#include "ImageDataFormats.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "containers/mwsrQueue.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
        /** @brief The layout the image is left in, for example PresentSource for a swapchain image */
        rhi::ImageLayout FinalLayout{ rhi::ImageLayout::PresentSource };
    };

    /** Copies rendered images to PNG files (8-bit sRGB, RGB) without stalling the frame.
     *
     *  There is one readback buffer per slot (use one slot per frame in flight). RecordCopy records the image to buffer
     *  copy into a frame's command buffer. The copy is read the next time that slot is used, when the frame's fence has
     *  already been waited, or in Flush. A worker thread converts the pixels and writes the file.
     *
     *  The image needs transfer source usage (see Swapchain::SupportsTransferSource). */
    class FrameCapture
    {
    public:
        FrameCapture(ResourceContext& resources, uint32_t slot_count, std::filesystem::path directory);
        ~FrameCapture();
        FrameCapture(const FrameCapture&) = delete;
        FrameCapture& operator=(const FrameCapture&) = delete;

        [[nodiscard]] static bool IsSupported(ImageFormat format, CaptureEncoding encoding) noexcept;

        /** @brief Records the copy of source into slot's buffer, and leaves the image in source.FinalLayout.
         *
         *  Call it only after the GPU is done with the slot's previous frame (after its fence wait): it first hands
         *  the slot's earlier capture to the writer. The file is directory / (name + ".png"). Returns false, and records
         *  nothing, when the format is not supported or the buffer could not be created. */
        bool RecordCopy(rhi::CommandBuffer& command_buffer, uint32_t slot, const CaptureSource& source, std::string name);

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
            uint64_t Bytes{ 0u };
            bool Pending{ false };
            uint32_t Width{ 0u };
            uint32_t Height{ 0u };
            ImageFormat Format;
            CaptureEncoding Encoding{ CaptureEncoding::sRGB };
            std::string Name;
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
            std::filesystem::path Path;
        };

        void drainSlot(Slot& slot);
        bool ensureBuffer(Slot& slot, uint64_t bytes);
        void waitForOutstandingBelow(uint32_t limit);
        void writerLoop();

        ResourceContext& resources;
        std::filesystem::path directory;
        std::vector<Slot> slots;

        mwsrQueue<Job> jobs;
        std::atomic<uint32_t> outstanding{ 0u };
        std::atomic<uint32_t> written{ 0u };
        std::atomic<uint32_t> failed{ 0u };
        std::thread writer;
    };
}

#endif //!DIAMOND_DOGS_FRAME_CAPTURE_HPP
