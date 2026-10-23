#include "ColorPipe.hpp"
#include "ColorPipeError.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "DisplayTarget.hpp"
#include "Fence.hpp"
#include "FullscreenDrawState.hpp"
#include "GpuTimers.hpp"
#include "ImageDataFormats.hpp"
#include "ManifestDescriptors.hpp"
#include "PlatformTypes.hpp"
#include "Queue.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderProgram.hpp"
#include "Swapchain.hpp"
#include "VariantKey.hpp"
#include <vulkan/vulkan_core.h>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace color
{
    namespace
    {
        constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
        constexpr std::string_view k_ModuleName{ "ColorComposite" };
        constexpr std::array<std::string_view, 2> k_EntryPoints{ "ColorCompositeVS", "ColorCompositeFS" };
        constexpr std::string_view k_TonemapperAxisName{ "TONEMAPPER" };
        constexpr std::string_view k_EncodeAxisName{ "ENCODE" };

        constexpr std::array<TonemapperKind, 2> k_WrittenTonemappers{ TonemapperKind::Passthrough, TonemapperKind::FalseColorEv };
        constexpr size_t k_TonemapperKindCount = 5u;

        // the scene and the UI rest in this layout between composites
        constexpr rhi::ImageLayout k_RestingLayout = rhi::ImageLayout::Attachment;
        constexpr rhi::ImageLayout k_ReadLayout = rhi::ImageLayout::ReadOnly;
        constexpr VkImageLayout k_ReadVkLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        constexpr rhi::ImageSubresourceRange k_ColorRange{ rhi::ImageAspect::Color, 0u, 1u, 0u, 1u };

        // the shader reads element 0. The rest is padding
        constexpr size_t k_ExposureBufferBytes = 16u;

        /** @brief Must match DisplayParameters in ColorCompositeTonemappers.slang */
        struct DisplayParameters
        {
            float PaperWhiteNits{ 0.0f };
            float PeakNits{ 0.0f };
            float BlackNits{ 0.0f };
            float Padding{ 0.0f };
        };
        static_assert(sizeof(DisplayParameters) == 16u);
    }

    struct CompositeProgram
    {
        std::unique_ptr<rhi::ManifestProgram> Program;
        lodestone::VariantKey Variant{};
        VkDescriptorSet Set{ VK_NULL_HANDLE };
    };

    struct ColorPipeImpl
    {
        ~ColorPipeImpl();

        rhi::Device* Device{ nullptr };
        ResourceContext* Resources{ nullptr };
        const rhi::ShaderLibrary* Library{ nullptr };
        Swapchain* TargetSwapchain{ nullptr };
        const lodestone::ManifestIndex* Index{ nullptr };

        DisplayColorCapabilities Capabilities{};
        SurfaceChoice Surface{};
        DisplayOverrides Overrides{};
        DisplayTarget Target{};
        TonemapperKind Tonemapper{ TonemapperKind::Invalid };
        float ExposureEv{ 0.0f };

        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        GraphicsResource Scene{};
        GraphicsResource Ui{};
        GraphicsResource ExposureBuffer{};

        // indexed by TonemapperKind, empty for kinds without a program
        std::array<CompositeProgram, k_TonemapperKindCount> Programs;
        VkDescriptorPool DescriptorPool{ VK_NULL_HANDLE };
        FullscreenDrawState DrawState;
    };

    namespace
    {
        [[nodiscard]] std::string_view TonemapperTypeName(TonemapperKind kind) noexcept;
        [[nodiscard]] std::string_view EncodingCaseName(DisplayEncoding encoding) noexcept;
        [[nodiscard]] GraphicsResource WaitForResource(const std::shared_ptr<GraphicsResourceReply>& reply);
        [[nodiscard]] ImageCreateInfo MakeTargetInfo(ImageFormat format, uint32_t width, uint32_t height);
        [[nodiscard]] ColorTargetView MakeTargetView(const GraphicsResource& resource, ImageFormat format, uint32_t width, uint32_t height) noexcept;
        [[nodiscard]] SwapchainHdrMetadata MakeHdrMetadata(const DisplayTarget& target) noexcept;
        [[nodiscard]] DisplayParameters MakeDisplayParameters(const DisplayTarget& target) noexcept;
        [[nodiscard]] CompositeProgram& ProgramOf(ColorPipeImpl& impl, TonemapperKind kind) noexcept;
        [[nodiscard]] ColorPipeError BuildPrograms(ColorPipeImpl& impl);
        [[nodiscard]] ColorPipeError CreateDescriptorSets(ColorPipeImpl& impl);
        [[nodiscard]] ColorPipeError WriteDescriptors(ColorPipeImpl& impl);
        [[nodiscard]] ColorPipeError CreateExposureBuffer(ColorPipeImpl& impl);
        [[nodiscard]] ColorPipeError CreateTargets(ColorPipeImpl& impl, uint32_t width, uint32_t height);
        [[nodiscard]] bool MoveTargetsToRestingLayout(ColorPipeImpl& impl);
        void DestroyTargets(ColorPipeImpl& impl);
        void DestroyResourceIfCreated(ColorPipeImpl& impl, GraphicsResource& resource);
        void ApplyHdrMetadata(ColorPipeImpl& impl);
    }

    ColorPipeImpl::~ColorPipeImpl()
    {
        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Device->Handle().As<VkDevice>(), DescriptorPool, nullptr);
        }

        DestroyTargets(*this);
        DestroyResourceIfCreated(*this, ExposureBuffer);
    }

    ColorPipeResult<std::unique_ptr<ColorPipe>> ColorPipe::Create(const ColorPipeCreateInfo& create_info)
    {
        if (create_info.Surface.Encoding == DisplayEncoding::Invalid)
        {
            return std::unexpected(ColorPipeError::SurfaceUnusable);
        }

        auto impl = std::make_unique<ColorPipeImpl>();
        impl->Device = create_info.Device;
        impl->Resources = create_info.Resources;
        impl->Library = create_info.Library;
        impl->TargetSwapchain = create_info.TargetSwapchain;
        impl->Capabilities = create_info.Capabilities;
        impl->Surface = create_info.Surface;
        impl->Overrides = create_info.Overrides;
        impl->Tonemapper = create_info.Tonemapper;
        impl->Target = ResolveDisplayTarget(impl->Capabilities, impl->Surface, impl->Overrides);

        uint32_t width = create_info.Width;
        uint32_t height = create_info.Height;

        if (impl->TargetSwapchain != nullptr)
        {
            const math::Float2 extent = impl->TargetSwapchain->GetExtent();
            width = static_cast<uint32_t>(extent.x);
            height = static_cast<uint32_t>(extent.y);
        }

        if (width == 0u || height == 0u)
        {
            return std::unexpected(ColorPipeError::ResourceCreationFailed);
        }

        if (!impl->DrawState.Load(impl->Device->Handle().As<VkDevice>()))
        {
            return std::unexpected(ColorPipeError::ProgramCreationFailed);
        }

        if (const ColorPipeError error = BuildPrograms(*impl); error != ColorPipeError::Success)
        {
            return std::unexpected(error);
        }

        if (ProgramOf(*impl, impl->Tonemapper).Program == nullptr)
        {
            return std::unexpected(ColorPipeError::TonemapperUnavailable);
        }

        if (const ColorPipeError error = CreateDescriptorSets(*impl); error != ColorPipeError::Success)
        {
            return std::unexpected(error);
        }

        if (const ColorPipeError error = CreateExposureBuffer(*impl); error != ColorPipeError::Success)
        {
            return std::unexpected(error);
        }

        if (const ColorPipeError error = CreateTargets(*impl, width, height); error != ColorPipeError::Success)
        {
            return std::unexpected(error);
        }

        if (const ColorPipeError error = WriteDescriptors(*impl); error != ColorPipeError::Success)
        {
            return std::unexpected(error);
        }

        ApplyHdrMetadata(*impl);

        return std::make_unique<ColorPipe>(std::move(impl));
    }

    ColorPipe::ColorPipe(std::unique_ptr<ColorPipeImpl>&& _impl) noexcept : impl{ std::move(_impl) } {}
    ColorPipe::~ColorPipe() = default;

    const DisplayTarget& ColorPipe::Target() const noexcept
    {
        return impl->Target;
    }

    TonemapperKind ColorPipe::Tonemapper() const noexcept
    {
        return impl->Tonemapper;
    }

    ColorTargetView ColorPipe::SceneTarget() const noexcept
    {
        return MakeTargetView(impl->Scene, CommonFormats::RGBA16_Float, impl->Width, impl->Height);
    }

    ColorTargetView ColorPipe::UiTarget() const noexcept
    {
        return MakeTargetView(impl->Ui, CommonFormats::RGBA8_UNorm, impl->Width, impl->Height);
    }

    void ColorPipe::SetExposureEv(float exposure_ev) noexcept
    {
        impl->ExposureEv = exposure_ev;
    }

    ColorPipeError ColorPipe::SetTonemapper(TonemapperKind kind) noexcept
    {
        if (ProgramOf(*impl, kind).Program == nullptr)
        {
            return ColorPipeError::TonemapperUnavailable;
        }

        impl->Tonemapper = kind;

        return ColorPipeError::Success;
    }

    void ColorPipe::SetOverrides(const DisplayOverrides& overrides)
    {
        impl->Overrides = overrides;
        impl->Target = ResolveDisplayTarget(impl->Capabilities, impl->Surface, impl->Overrides);
        ApplyHdrMetadata(*impl);
    }

    void ColorPipe::RecordComposite(rhi::CommandBuffer& command_buffer, rhi::ImageViewHandle output_view, rhi::GpuTimerBlock* timers)
    {
        std::optional<rhi::ScopedGpuTimer> timer;

        if (timers != nullptr)
        {
            timer.emplace(*timers, "Color composite");
        }

        // written in the queue, so that a frame in flight reads its own value
        const rhi::GlobalBarrier readToWrite
        {
            rhi::PipelineStage::FragmentShader, rhi::Access::ShaderStorageRead,
            rhi::PipelineStage::AllTransfer, rhi::Access::TransferWrite
        };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &readToWrite, 1 }, {}, {} });

        command_buffer.FillBuffer(rhi::BufferHandle{ impl->ExposureBuffer.VkHandle }, 0u, sizeof(float), std::bit_cast<uint32_t>(impl->ExposureEv));

        const rhi::GlobalBarrier writeToRead
        {
            rhi::PipelineStage::AllTransfer, rhi::Access::TransferWrite,
            rhi::PipelineStage::FragmentShader, rhi::Access::ShaderStorageRead
        };
        const std::array<rhi::ImageBarrier, 2> toRead
        {
            rhi::ImageBarrier
            {
                rhi::ImageHandle{ impl->Scene.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::PipelineStage::FragmentShader, rhi::Access::ShaderSampledRead,
                k_RestingLayout, k_ReadLayout, k_ColorRange
            },
            rhi::ImageBarrier
            {
                rhi::ImageHandle{ impl->Ui.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::PipelineStage::FragmentShader, rhi::Access::ShaderSampledRead,
                k_RestingLayout, k_ReadLayout, k_ColorRange
            },
        };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &writeToRead, 1 }, {}, toRead });

        rhi::ColorAttachment outputAttachment{};
        outputAttachment.ImageView = output_view;
        outputAttachment.Layout = rhi::ImageLayout::ColorAttachment;
        outputAttachment.LoadOp = rhi::LoadOp::DontCare;
        outputAttachment.StoreOp = rhi::StoreOp::Store;

        rhi::RenderingInfo renderingInfo{};
        renderingInfo.ColorAttachments = std::span{ &outputAttachment, 1 };
        renderingInfo.RenderArea = rhi::Rect2D{ 0, 0, impl->Width, impl->Height };
        command_buffer.BeginRendering(renderingInfo);

        const rhi::Viewport viewport{ 0.0f, 0.0f, static_cast<float>(impl->Width), static_cast<float>(impl->Height), 0.0f, 1.0f };
        command_buffer.SetViewports(std::span{ &viewport, 1 });
        command_buffer.SetScissors(std::span{ &renderingInfo.RenderArea, 1 });

        const CompositeProgram& composite = ProgramOf(*impl, impl->Tonemapper);
        RhiAssert(command_buffer.BindShaders(composite.Program->Program()));
        impl->DrawState.Record(command_buffer.Handle().As<VkCommandBuffer>());

        const rhi::DescriptorSetHandle set{ reinterpret_cast<uint64_t>(composite.Set) };
        command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Graphics, composite.Program->PipelineLayout(), 0u, std::span{ &set, 1 });

        // the range exists: creation checked the layout
        const std::optional<rhi::PushConstantRange> range = composite.Program->PushConstants();
        const DisplayParameters parameters = MakeDisplayParameters(impl->Target);
        command_buffer.PushConstants(composite.Program->PipelineLayout(), range->StageFlags, 0u, std::as_bytes(std::span{ &parameters, 1 }));

        command_buffer.Draw(3u, 1u, 0u, 0u);
        command_buffer.EndRendering();

        const std::array<rhi::ImageBarrier, 2> toResting
        {
            rhi::ImageBarrier
            {
                rhi::ImageHandle{ impl->Scene.VkHandle },
                rhi::PipelineStage::FragmentShader, rhi::Access::ShaderSampledRead,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                k_ReadLayout, k_RestingLayout, k_ColorRange
            },
            rhi::ImageBarrier
            {
                rhi::ImageHandle{ impl->Ui.VkHandle },
                rhi::PipelineStage::FragmentShader, rhi::Access::ShaderSampledRead,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                k_ReadLayout, k_RestingLayout, k_ColorRange
            },
        };
        command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, toResting });
    }

    ColorPipeError ColorPipe::OnSwapchainRecreated()
    {
        if (impl->TargetSwapchain == nullptr)
        {
            return ColorPipeError::SwapchainMissing;
        }

        const math::Float2 extent = impl->TargetSwapchain->GetExtent();

        if (const ColorPipeError error = Resize(static_cast<uint32_t>(extent.x), static_cast<uint32_t>(extent.y)); error != ColorPipeError::Success)
        {
            return error;
        }

        // the new swapchain has no metadata yet
        ApplyHdrMetadata(*impl);

        return ColorPipeError::Success;
    }

    ColorPipeError ColorPipe::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0u || height == 0u)
        {
            return ColorPipeError::ResourceCreationFailed;
        }

        DestroyTargets(*impl);

        if (const ColorPipeError error = CreateTargets(*impl, width, height); error != ColorPipeError::Success)
        {
            return error;
        }

        return WriteDescriptors(*impl);
    }

    namespace
    {
        std::string_view TonemapperTypeName(TonemapperKind kind) noexcept
        {
            switch (kind)
            {
            case TonemapperKind::Passthrough: return "Passthrough";
            case TonemapperKind::FalseColorEv: return "FalseColorEv";
            case TonemapperKind::KhronosNeutral: return "KhronosNeutral";
            case TonemapperKind::AgX: return "AgX";
            default: return "";
            }
        }

        std::string_view EncodingCaseName(DisplayEncoding encoding) noexcept
        {
            switch (encoding)
            {
            case DisplayEncoding::SrgbNonlinear: return "SrgbNonlinear";
            case DisplayEncoding::ScRgbLinear: return "ScRgbLinear";
            case DisplayEncoding::Hdr10Pq: return "Hdr10Pq";
            default: return "";
            }
        }

        GraphicsResource WaitForResource(const std::shared_ptr<GraphicsResourceReply>& reply)
        {
            if (reply == nullptr || reply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                return GraphicsResource::Null();
            }

            return reply->GetResource();
        }

        ImageCreateInfo MakeTargetInfo(ImageFormat format, uint32_t width, uint32_t height)
        {
            ImageCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            // TransferDestination lets a test fill the target
            info.Usage = ImageUsageBits::RenderTargetRead | ImageUsageBits::RenderTargetWrite | ImageUsageBits::SrvFragmentShader | ImageUsageBits::TransferDestination;
            info.Type = ImageType::Type2D;
            info.Format = format;
            info.Width = width;
            info.Height = height;
            info.Depth = 1u;
            info.ViewInfo = ImageViewCreateInfo{};
            info.ViewInfo->ViewType = ImageViewType::Type2D;
            info.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;

            return info;
        }

        ColorTargetView MakeTargetView(const GraphicsResource& resource, ImageFormat format, uint32_t width, uint32_t height) noexcept
        {
            return ColorTargetView{ rhi::ImageHandle{ resource.VkHandle }, rhi::ImageViewHandle{ resource.VkViewHandle }, format, k_RestingLayout, width, height };
        }

        SwapchainHdrMetadata MakeHdrMetadata(const DisplayTarget& target) noexcept
        {
            const Gamut& primaries = target.Primaries;

            SwapchainHdrMetadata metadata{};
            metadata.RedPrimaryX = static_cast<float>(primaries.Red.X);
            metadata.RedPrimaryY = static_cast<float>(primaries.Red.Y);
            metadata.GreenPrimaryX = static_cast<float>(primaries.Green.X);
            metadata.GreenPrimaryY = static_cast<float>(primaries.Green.Y);
            metadata.BluePrimaryX = static_cast<float>(primaries.Blue.X);
            metadata.BluePrimaryY = static_cast<float>(primaries.Blue.Y);
            metadata.WhitePointX = static_cast<float>(primaries.White.X);
            metadata.WhitePointY = static_cast<float>(primaries.White.Y);
            metadata.MaxLuminance = target.PeakNits;
            metadata.MinLuminance = target.BlackNits;
            metadata.MaxContentLightLevel = target.PeakNits;
            metadata.MaxFrameAverageLightLevel = target.PaperWhiteNits;

            return metadata;
        }

        DisplayParameters MakeDisplayParameters(const DisplayTarget& target) noexcept
        {
            return DisplayParameters{ target.PaperWhiteNits, target.PeakNits, target.BlackNits, 0.0f };
        }

        CompositeProgram& ProgramOf(ColorPipeImpl& impl, TonemapperKind kind) noexcept
        {
            return impl.Programs[static_cast<size_t>(kind)];
        }

        ColorPipeError BuildPrograms(ColorPipeImpl& impl)
        {
            rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = impl.Library->ModuleIndex(k_ModuleName);

            if (!index.has_value())
            {
                return ColorPipeError::CompositeModuleMissing;
            }

            impl.Index = *index;
            const std::string_view encodingName = EncodingCaseName(impl.Target.Encoding);

            for (const TonemapperKind kind : k_WrittenTonemappers)
            {
                std::expected<lodestone::VariantKey, lodestone::QueryError> variant = impl.Index->Query()
                    .Where(k_TonemapperAxisName, lodestone::AxisValueDomain::Type, TonemapperTypeName(kind))
                    .Where(k_EncodeAxisName, lodestone::AxisValueDomain::Enum, encodingName)
                    .SingleFor(k_EntryPoints);

                if (!variant.has_value())
                {
                    return ColorPipeError::CompositeVariantMissing;
                }

                rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> program =
                    impl.Library->CreateProgram(impl.Device->Handle(), k_ModuleName, *variant, k_EntryPoints);

                if (!program.has_value())
                {
                    return ColorPipeError::ProgramCreationFailed;
                }

                // the shader and this file declare the block twice, so the size is checked here
                const std::optional<rhi::PushConstantRange> range = (*program)->PushConstants();

                if (!range.has_value() || range->Size != sizeof(DisplayParameters))
                {
                    return ColorPipeError::ShaderLayoutMismatch;
                }

                CompositeProgram& composite = ProgramOf(impl, kind);
                composite.Program = std::move(*program);
                composite.Variant = *variant;
            }

            return ColorPipeError::Success;
        }

        ColorPipeError CreateDescriptorSets(ColorPipeImpl& impl)
        {
            const VkDevice device = impl.Device->Handle().As<VkDevice>();
            const uint32_t setCount = static_cast<uint32_t>(k_WrittenTonemappers.size());

            const std::array<VkDescriptorPoolSize, 2> poolSizes
            {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2u * setCount },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, setCount },
            };
            const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0u,
                setCount, static_cast<uint32_t>(poolSizes.size()), poolSizes.data() };

            if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &impl.DescriptorPool) != VK_SUCCESS)
            {
                return ColorPipeError::DescriptorSetupFailed;
            }

            for (const TonemapperKind kind : k_WrittenTonemappers)
            {
                CompositeProgram& composite = ProgramOf(impl, kind);
                const VkDescriptorSetLayout setLayout = composite.Program->SetLayouts()[0].As<VkDescriptorSetLayout>();
                const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl.DescriptorPool, 1u, &setLayout };

                if (vkAllocateDescriptorSets(device, &setInfo, &composite.Set) != VK_SUCCESS)
                {
                    return ColorPipeError::DescriptorSetupFailed;
                }
            }

            return ColorPipeError::Success;
        }

        ColorPipeError WriteDescriptors(ColorPipeImpl& impl)
        {
            const VkDevice device = impl.Device->Handle().As<VkDevice>();
            const BoundResources bound
            {
                { "SceneTarget", BoundResource{ impl.Scene, k_ReadVkLayout, {} } },
                { "UiTarget", BoundResource{ impl.Ui, k_ReadVkLayout, {} } },
                { "ExposureEv", BoundResource{ impl.ExposureBuffer, VK_IMAGE_LAYOUT_UNDEFINED, {} } },
            };

            for (const TonemapperKind kind : k_WrittenTonemappers)
            {
                const CompositeProgram& composite = ProgramOf(impl, kind);
                const std::optional<std::string> error = WriteManifestDescriptors(device, composite.Set, *impl.Index, composite.Variant, k_EntryPoints, bound);

                if (error.has_value())
                {
                    return ColorPipeError::DescriptorSetupFailed;
                }
            }

            return ColorPipeError::Success;
        }

        ColorPipeError CreateExposureBuffer(ColorPipeImpl& impl)
        {
            BufferCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            info.Usage = BufferUsageBits::FragmentShaderSRV | BufferUsageBits::CopyDestination;
            info.Size = k_ExposureBufferBytes;
            impl.ExposureBuffer = WaitForResource(impl.Resources->CreateBuffer(info, {}));

            return impl.ExposureBuffer ? ColorPipeError::Success : ColorPipeError::ResourceCreationFailed;
        }

        ColorPipeError CreateTargets(ColorPipeImpl& impl, uint32_t width, uint32_t height)
        {
            impl.Scene = WaitForResource(impl.Resources->CreateImage(MakeTargetInfo(CommonFormats::RGBA16_Float, width, height), {}));
            impl.Ui = WaitForResource(impl.Resources->CreateImage(MakeTargetInfo(CommonFormats::RGBA8_UNorm, width, height), {}));

            if (!impl.Scene || !impl.Ui)
            {
                DestroyTargets(impl);

                return ColorPipeError::ResourceCreationFailed;
            }

            impl.Width = width;
            impl.Height = height;

            return MoveTargetsToRestingLayout(impl) ? ColorPipeError::Success : ColorPipeError::ResourceCreationFailed;
        }

        bool MoveTargetsToRestingLayout(ColorPipeImpl& impl)
        {
            rhi::Device& device = *impl.Device;
            rhi::CommandPool commandPool(device.Handle(), rhi::CommandPool::Type::Graphics, device.GetQueueFamilyIndices().Graphics);

            if (!commandPool.AllocateCommandBuffers(1u).IsSuccess())
            {
                return false;
            }

            rhi::CommandBuffer commandBuffer(commandPool.GetCommandBuffer(0u));
            RhiAssert(commandBuffer.Begin());

            // a new image has no contents to keep
            const std::array<rhi::ImageBarrier, 2> toResting
            {
                rhi::ImageBarrier
                {
                    rhi::ImageHandle{ impl.Scene.VkHandle },
                    rhi::PipelineStage::None, rhi::Access::None,
                    rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                    rhi::ImageLayout::Undefined, k_RestingLayout, k_ColorRange
                },
                rhi::ImageBarrier
                {
                    rhi::ImageHandle{ impl.Ui.VkHandle },
                    rhi::PipelineStage::None, rhi::Access::None,
                    rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                    rhi::ImageLayout::Undefined, k_RestingLayout, k_ColorRange
                },
            };
            commandBuffer.Barrier(rhi::DependencyInfo{ {}, {}, toResting });
            RhiAssert(commandBuffer.End());

            const rhi::CommandBufferHandle handle = commandBuffer.Handle();
            rhi::Fence fence(device.Handle());

            if (!rhi::QueueSubmit(device.GetGeneralQueue(), rhi::SubmitInfo{ {}, std::span{ &handle, 1 }, {} }, fence.Handle()).IsSuccess())
            {
                return false;
            }

            return fence.Wait();
        }

        void DestroyTargets(ColorPipeImpl& impl)
        {
            DestroyResourceIfCreated(impl, impl.Scene);
            DestroyResourceIfCreated(impl, impl.Ui);
        }

        void DestroyResourceIfCreated(ColorPipeImpl& impl, GraphicsResource& resource)
        {
            if (!resource)
            {
                return;
            }

            (void)impl.Resources->DestroyResource(resource)->WaitForCompletion(k_TimeoutNs);
            resource = GraphicsResource{};
        }

        void ApplyHdrMetadata(ColorPipeImpl& impl)
        {
            const bool isHdr = impl.Target.Encoding == DisplayEncoding::ScRgbLinear || impl.Target.Encoding == DisplayEncoding::Hdr10Pq;

            if (impl.TargetSwapchain != nullptr && isHdr)
            {
                impl.TargetSwapchain->SetHdrMetadata(MakeHdrMetadata(impl.Target));
            }
        }
    }
}
