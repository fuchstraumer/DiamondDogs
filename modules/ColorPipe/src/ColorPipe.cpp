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
#include "RhiSystem.hpp"
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

        constexpr std::array<TonemapperKind, 4> k_WrittenTonemappers{
            TonemapperKind::Passthrough,
            TonemapperKind::FalseColorEv,
            TonemapperKind::KhronosNeutral,
            TonemapperKind::AgX
        };
        constexpr size_t k_TonemapperKindCount = 5u;

        constexpr std::string_view k_BloomModuleName{ "ColorBloom" };
        constexpr std::string_view k_BloomDownsampleEntry{ "ColorBloomDownsampleCS" };
        constexpr std::string_view k_BloomUpsampleEntry{ "ColorBloomUpsampleCS" };
        constexpr size_t k_BloomMipCount = 6u;

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
            float BloomIntensity{ 0.0f };
        };
        static_assert(sizeof(DisplayParameters) == 16u);

        struct BloomDownsamplePushConstants
        {
            float TexelSize[2]{ 0.0f, 0.0f };
            uint32_t UseKarisAverage{ 0u };
            float Padding{ 0.0f };
        };
        static_assert(sizeof(BloomDownsamplePushConstants) == 16u);

        struct BloomUpsamplePushConstants
        {
            float CoarseTexelSize[2]{ 0.0f, 0.0f };
            float Scatter{ 0.0f };
            float Padding{ 0.0f };
        };
        static_assert(sizeof(BloomUpsamplePushConstants) == 16u);
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

        BloomSettings Bloom{};
        std::array<GraphicsResource, k_BloomMipCount> BloomDownsample;
        std::array<GraphicsResource, k_BloomMipCount - 1u> BloomUpsample;
        GraphicsResource LinearSampler;

        const lodestone::ManifestIndex* BloomIndex{ nullptr };
        lodestone::VariantKey BloomDownsampleVariant{};
        lodestone::VariantKey BloomUpsampleVariant{};
        std::unique_ptr<rhi::ManifestProgram> BloomDownsampleProgram;
        std::unique_ptr<rhi::ManifestProgram> BloomUpsampleProgram;

        VkDescriptorPool BloomDescriptorPool{ VK_NULL_HANDLE };
        std::array<VkDescriptorSet, k_BloomMipCount> BloomDownsampleSets{};
        std::array<VkDescriptorSet, k_BloomMipCount - 1u> BloomUpsampleSets{};

        // indexed by TonemapperKind, empty for kinds without a program
        std::array<CompositeProgram, k_TonemapperKindCount> Programs;
        VkDescriptorPool DescriptorPool{ VK_NULL_HANDLE };
        FullscreenDrawState DrawState;
    };

    namespace
    {
        struct Extent2D
        {
            uint32_t Width{ 0u };
            uint32_t Height{ 0u };
        };

        [[nodiscard]] constexpr Extent2D BloomDownsampleExtent(uint32_t base_width, uint32_t base_height, size_t mip_index) noexcept
        {
            uint32_t w = std::max(1u, base_width >> 1);
            uint32_t h = std::max(1u, base_height >> 1);
            for (size_t i = 0; i < mip_index; ++i)
            {
                w = std::max(1u, w >> 1);
                h = std::max(1u, h >> 1);
            }
            return Extent2D{ w, h };
        }

        [[nodiscard]] std::string_view TonemapperTypeName(TonemapperKind kind) noexcept;
        [[nodiscard]] std::string_view EncodingCaseName(DisplayEncoding encoding) noexcept;
        [[nodiscard]] GraphicsResource WaitForResource(const std::shared_ptr<GraphicsResourceReply>& reply);
        [[nodiscard]] ImageCreateInfo MakeTargetInfo(ImageFormat format, uint32_t width, uint32_t height, std::string_view name = {}, std::string_view view_name = {});
        [[nodiscard]] ImageCreateInfo MakeBloomTargetInfo(ImageFormat format, uint32_t width, uint32_t height, std::string_view name = {}, std::string_view view_name = {});
        [[nodiscard]] ColorTargetView MakeTargetView(const GraphicsResource& resource, ImageFormat format, uint32_t width, uint32_t height) noexcept;
        [[nodiscard]] SwapchainHdrMetadata MakeHdrMetadata(const DisplayTarget& target) noexcept;
        [[nodiscard]] DisplayParameters MakeDisplayParameters(const DisplayTarget& target, const BloomSettings& bloom) noexcept;
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
        if (BloomDescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Device->Handle().As<VkDevice>(), BloomDescriptorPool, nullptr);
        }

        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Device->Handle().As<VkDevice>(), DescriptorPool, nullptr);
        }

        DestroyTargets(*this);
        DestroyResourceIfCreated(*this, ExposureBuffer);
        DestroyResourceIfCreated(*this, LinearSampler);
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

        SamplerCreateInfo samplerInfo{};
        samplerInfo.MagFilter = ImageFilteringMode::Linear;
        samplerInfo.MinFilter = ImageFilteringMode::Linear;
        samplerInfo.MipFilter = ImageFilteringMode::Linear;
        samplerInfo.AddressU = SamplerCreateInfo::AddressMode::ClampToEdge;
        samplerInfo.AddressV = SamplerCreateInfo::AddressMode::ClampToEdge;
        samplerInfo.AddressW = SamplerCreateInfo::AddressMode::ClampToEdge;
        samplerInfo.Name = "ColorPipe_LinearSampler";
        impl->LinearSampler = WaitForResource(impl->Resources->CreateSampler(samplerInfo, "ColorPipe_LinearSampler"));

        if (!impl->LinearSampler)
        {
            return std::unexpected(ColorPipeError::ResourceCreationFailed);
        }
        rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_SAMPLER, impl->LinearSampler.VkSamplerHandle, "ColorPipe_LinearSampler");

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

    void ColorPipe::SetBloomSettings(const BloomSettings& settings) noexcept
    {
        impl->Bloom = settings;
    }

    const BloomSettings& ColorPipe::GetBloomSettings() const noexcept
    {
        return impl->Bloom;
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
        // exposure buffer barrier
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

        if (impl->Bloom.Enabled)
        {
            const rhi::ImageBarrier sceneToRead
            {
                rhi::ImageHandle{ impl->Scene.VkHandle },
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                k_RestingLayout, k_ReadLayout, k_ColorRange
            };
            command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &writeToRead, 1 }, {}, std::span{ &sceneToRead, 1 } });

            {
                std::optional<rhi::ScopedGpuTimer> timer;
                if (timers != nullptr)
                {
                    timer.emplace(*timers, "Bloom downsample");
                }

                RhiAssert(command_buffer.BindShaders(impl->BloomDownsampleProgram->Program()));

                for (size_t i = 0; i < k_BloomMipCount; ++i)
                {
                    if (i > 0)
                    {
                        const rhi::ImageBarrier downBarrier
                        {
                            rhi::ImageHandle{ impl->BloomDownsample[i - 1].VkHandle },
                            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                            rhi::ImageLayout::General, rhi::ImageLayout::General, k_ColorRange
                        };
                        command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &downBarrier, 1 } });
                    }

                    const Extent2D srcExtent = (i == 0) ? Extent2D{ impl->Width, impl->Height } : BloomDownsampleExtent(impl->Width, impl->Height, i - 1);
                    const Extent2D dstExtent = BloomDownsampleExtent(impl->Width, impl->Height, i);

                    const BloomDownsamplePushConstants pc
                    {
                        .TexelSize = { 1.0f / static_cast<float>(srcExtent.Width), 1.0f / static_cast<float>(srcExtent.Height) },
                        .UseKarisAverage = (i == 0) ? 1u : 0u,
                        .Padding = 0.0f
                    };

                    const rhi::DescriptorSetHandle set{ reinterpret_cast<uint64_t>(impl->BloomDownsampleSets[i]) };
                    command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, impl->BloomDownsampleProgram->PipelineLayout(), 0u, std::span{ &set, 1 });

                    const std::optional<rhi::PushConstantRange> range = impl->BloomDownsampleProgram->PushConstants();
                    command_buffer.PushConstants(impl->BloomDownsampleProgram->PipelineLayout(), range->StageFlags, 0u, std::as_bytes(std::span{ &pc, 1 }));

                    command_buffer.Dispatch((dstExtent.Width + 7u) / 8u, (dstExtent.Height + 7u) / 8u, 1u);
                }
            }

            {
                std::optional<rhi::ScopedGpuTimer> timer;
                if (timers != nullptr)
                {
                    timer.emplace(*timers, "Bloom upsample");
                }

                RhiAssert(command_buffer.BindShaders(impl->BloomUpsampleProgram->Program()));

                for (int32_t i = static_cast<int32_t>(k_BloomMipCount - 2u); i >= 0; --i)
                {
                    if (i == static_cast<int32_t>(k_BloomMipCount - 2u))
                    {
                        const std::array<rhi::ImageBarrier, 2> upInputBarriers
                        {
                            rhi::ImageBarrier
                            {
                                rhi::ImageHandle{ impl->BloomDownsample[5].VkHandle },
                                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                                rhi::ImageLayout::General, rhi::ImageLayout::General, k_ColorRange
                            },
                            rhi::ImageBarrier
                            {
                                rhi::ImageHandle{ impl->BloomDownsample[4].VkHandle },
                                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                                rhi::ImageLayout::General, rhi::ImageLayout::General, k_ColorRange
                            }
                        };
                        command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, upInputBarriers });
                    }
                    else
                    {
                        const rhi::ImageBarrier upBarrier
                        {
                            rhi::ImageHandle{ impl->BloomUpsample[i + 1].VkHandle },
                            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                            rhi::ImageLayout::General, rhi::ImageLayout::General, k_ColorRange
                        };
                        command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &upBarrier, 1 } });
                    }

                    if (i == 0)
                    {
                        const rhi::ImageBarrier destBarrier
                        {
                            rhi::ImageHandle{ impl->BloomUpsample[0].VkHandle },
                            rhi::PipelineStage::FragmentShader, rhi::Access::ShaderSampledRead,
                            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                            k_ReadLayout, rhi::ImageLayout::General, k_ColorRange
                        };
                        command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &destBarrier, 1 } });
                    }

                    const Extent2D coarseExtent = (i == static_cast<int32_t>(k_BloomMipCount - 2u))
                        ? BloomDownsampleExtent(impl->Width, impl->Height, 5)
                        : BloomDownsampleExtent(impl->Width, impl->Height, i + 1);
                    const Extent2D dstExtent = BloomDownsampleExtent(impl->Width, impl->Height, i);

                    const BloomUpsamplePushConstants pc
                    {
                        .CoarseTexelSize = { 1.0f / static_cast<float>(coarseExtent.Width), 1.0f / static_cast<float>(coarseExtent.Height) },
                        .Scatter = impl->Bloom.Scatter,
                        .Padding = 0.0f
                    };

                    const rhi::DescriptorSetHandle set{ reinterpret_cast<uint64_t>(impl->BloomUpsampleSets[i]) };
                    command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, impl->BloomUpsampleProgram->PipelineLayout(), 0u, std::span{ &set, 1 });

                    const std::optional<rhi::PushConstantRange> range = impl->BloomUpsampleProgram->PushConstants();
                    command_buffer.PushConstants(impl->BloomUpsampleProgram->PipelineLayout(), range->StageFlags, 0u, std::as_bytes(std::span{ &pc, 1 }));

                    command_buffer.Dispatch((dstExtent.Width + 7u) / 8u, (dstExtent.Height + 7u) / 8u, 1u);
                }
            }

            const std::array<rhi::ImageBarrier, 3> toComposite
            {
                rhi::ImageBarrier
                {
                    rhi::ImageHandle{ impl->Scene.VkHandle },
                    rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                    rhi::PipelineStage::FragmentShader, rhi::Access::ShaderSampledRead,
                    k_ReadLayout, k_ReadLayout, k_ColorRange
                },
                rhi::ImageBarrier
                {
                    rhi::ImageHandle{ impl->Ui.VkHandle },
                    rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                    rhi::PipelineStage::FragmentShader, rhi::Access::ShaderSampledRead,
                    k_RestingLayout, k_ReadLayout, k_ColorRange
                },
                rhi::ImageBarrier
                {
                    rhi::ImageHandle{ impl->BloomUpsample[0].VkHandle },
                    rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                    rhi::PipelineStage::FragmentShader, rhi::Access::ShaderSampledRead,
                    rhi::ImageLayout::General, k_ReadLayout, k_ColorRange
                }
            };
            command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, toComposite });
        }
        else
        {
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
        }

        {
            std::optional<rhi::ScopedGpuTimer> timer;
            if (timers != nullptr)
            {
                timer.emplace(*timers, "Color composite");
            }

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

            const std::optional<rhi::PushConstantRange> range = composite.Program->PushConstants();
            const DisplayParameters parameters = MakeDisplayParameters(impl->Target, impl->Bloom);
            command_buffer.PushConstants(composite.Program->PipelineLayout(), range->StageFlags, 0u, std::as_bytes(std::span{ &parameters, 1 }));

            command_buffer.Draw(3u, 1u, 0u, 0u);
            command_buffer.EndRendering();
        }

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

        ImageCreateInfo MakeTargetInfo(ImageFormat format, uint32_t width, uint32_t height, std::string_view name, std::string_view view_name)
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
            info.Name = name;
            info.ViewInfo = ImageViewCreateInfo{};
            info.ViewInfo->ViewType = ImageViewType::Type2D;
            info.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;
            info.ViewInfo->Name = view_name;

            return info;
        }

        ImageCreateInfo MakeBloomTargetInfo(ImageFormat format, uint32_t width, uint32_t height, std::string_view name, std::string_view view_name)
        {
            ImageCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            info.Usage = ImageUsageBits::SampledImage | ImageUsageBits::SrvComputeShader | ImageUsageBits::UavComputeShader | ImageUsageBits::SrvFragmentShader | ImageUsageBits::TransferDestination;
            info.Type = ImageType::Type2D;
            info.Format = format;
            info.Width = width;
            info.Height = height;
            info.Depth = 1u;
            info.Name = name;
            info.ViewInfo = ImageViewCreateInfo{};
            info.ViewInfo->ViewType = ImageViewType::Type2D;
            info.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;
            info.ViewInfo->Name = view_name;

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

        DisplayParameters MakeDisplayParameters(const DisplayTarget& target, const BloomSettings& bloom) noexcept
        {
            return DisplayParameters{ target.PaperWhiteNits, target.PeakNits, target.BlackNits, bloom.Enabled ? bloom.Intensity : 0.0f };
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

                const std::optional<rhi::PushConstantRange> range = (*program)->PushConstants();

                if (!range.has_value() || range->Size != sizeof(DisplayParameters))
                {
                    return ColorPipeError::ShaderLayoutMismatch;
                }

                CompositeProgram& composite = ProgramOf(impl, kind);
                composite.Program = std::move(*program);
                composite.Variant = *variant;

                rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                    composite.Program->PipelineLayout().Get(),
                    std::format("ColorPipe_CompositePipelineLayout_{}", TonemapperTypeName(kind)).c_str());
            }

            rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> bloomIndex = impl.Library->ModuleIndex(k_BloomModuleName);
            if (!bloomIndex.has_value())
            {
                return ColorPipeError::CompositeModuleMissing;
            }
            impl.BloomIndex = *bloomIndex;

            const std::array<std::string_view, 1> downEntry{ k_BloomDownsampleEntry };
            std::expected<lodestone::VariantKey, lodestone::QueryError> downVariant = impl.BloomIndex->Query().SingleFor(downEntry);
            if (!downVariant.has_value())
            {
                return ColorPipeError::CompositeVariantMissing;
            }

            rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> downProgram =
                impl.Library->CreateProgram(impl.Device->Handle(), k_BloomModuleName, *downVariant, downEntry);
            if (!downProgram.has_value())
            {
                return ColorPipeError::ProgramCreationFailed;
            }

            const std::optional<rhi::PushConstantRange> downRange = (*downProgram)->PushConstants();
            if (!downRange.has_value() || downRange->Size != sizeof(BloomDownsamplePushConstants))
            {
                return ColorPipeError::ShaderLayoutMismatch;
            }
            impl.BloomDownsampleProgram = std::move(*downProgram);
            impl.BloomDownsampleVariant = *downVariant;
            rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                impl.BloomDownsampleProgram->PipelineLayout().Get(),
                "ColorPipe_BloomDownsamplePipelineLayout");

            const std::array<std::string_view, 1> upEntry{ k_BloomUpsampleEntry };
            std::expected<lodestone::VariantKey, lodestone::QueryError> upVariant = impl.BloomIndex->Query().SingleFor(upEntry);
            if (!upVariant.has_value())
            {
                return ColorPipeError::CompositeVariantMissing;
            }

            rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> upProgram =
                impl.Library->CreateProgram(impl.Device->Handle(), k_BloomModuleName, *upVariant, upEntry);
            if (!upProgram.has_value())
            {
                return ColorPipeError::ProgramCreationFailed;
            }

            const std::optional<rhi::PushConstantRange> upRange = (*upProgram)->PushConstants();
            if (!upRange.has_value() || upRange->Size != sizeof(BloomUpsamplePushConstants))
            {
                return ColorPipeError::ShaderLayoutMismatch;
            }
            impl.BloomUpsampleProgram = std::move(*upProgram);
            impl.BloomUpsampleVariant = *upVariant;
            rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                impl.BloomUpsampleProgram->PipelineLayout().Get(),
                "ColorPipe_BloomUpsamplePipelineLayout");

            return ColorPipeError::Success;
        }

        ColorPipeError CreateDescriptorSets(ColorPipeImpl& impl)
        {
            const VkDevice device = impl.Device->Handle().As<VkDevice>();
            const uint32_t setCount = static_cast<uint32_t>(k_WrittenTonemappers.size());

            const std::array<VkDescriptorPoolSize, 3> poolSizes
            {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 3u * setCount },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, setCount },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, setCount },
            };
            const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0u,
                setCount, static_cast<uint32_t>(poolSizes.size()), poolSizes.data() };

            if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &impl.DescriptorPool) != VK_SUCCESS)
            {
                return ColorPipeError::DescriptorSetupFailed;
            }
            rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, reinterpret_cast<uint64_t>(impl.DescriptorPool), "ColorPipe_DescriptorPool");

            for (const TonemapperKind kind : k_WrittenTonemappers)
            {
                CompositeProgram& composite = ProgramOf(impl, kind);
                const VkDescriptorSetLayout setLayout = composite.Program->SetLayouts()[0].As<VkDescriptorSetLayout>();
                const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl.DescriptorPool, 1u, &setLayout };

                if (vkAllocateDescriptorSets(device, &setInfo, &composite.Set) != VK_SUCCESS)
                {
                    return ColorPipeError::DescriptorSetupFailed;
                }

                const std::string name = std::format("ColorPipe_CompositeSet_{}", TonemapperTypeName(kind));
                rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(composite.Set), name.c_str());
            }

            constexpr uint32_t bloomSetCount = static_cast<uint32_t>(k_BloomMipCount + (k_BloomMipCount - 1u));
            const std::array<VkDescriptorPoolSize, 3> bloomPoolSizes
            {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16u },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 11u },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 11u },
            };
            const VkDescriptorPoolCreateInfo bloomPoolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0u,
                bloomSetCount, static_cast<uint32_t>(bloomPoolSizes.size()), bloomPoolSizes.data() };

            if (vkCreateDescriptorPool(device, &bloomPoolInfo, nullptr, &impl.BloomDescriptorPool) != VK_SUCCESS)
            {
                return ColorPipeError::DescriptorSetupFailed;
            }
            rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, reinterpret_cast<uint64_t>(impl.BloomDescriptorPool), "ColorPipe_BloomDescriptorPool");

            const VkDescriptorSetLayout downLayout = impl.BloomDownsampleProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
            for (size_t i = 0; i < k_BloomMipCount; ++i)
            {
                const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl.BloomDescriptorPool, 1u, &downLayout };
                if (vkAllocateDescriptorSets(device, &setInfo, &impl.BloomDownsampleSets[i]) != VK_SUCCESS)
                {
                    return ColorPipeError::DescriptorSetupFailed;
                }
                const std::string name = std::format("ColorPipe_BloomDownsampleSet_{}", i);
                rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(impl.BloomDownsampleSets[i]), name.c_str());
            }

            const VkDescriptorSetLayout upLayout = impl.BloomUpsampleProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
            for (size_t i = 0; i < k_BloomMipCount - 1u; ++i)
            {
                const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl.BloomDescriptorPool, 1u, &upLayout };
                if (vkAllocateDescriptorSets(device, &setInfo, &impl.BloomUpsampleSets[i]) != VK_SUCCESS)
                {
                    return ColorPipeError::DescriptorSetupFailed;
                }
                const std::string name = std::format("ColorPipe_BloomUpsampleSet_{}", i);
                rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(impl.BloomUpsampleSets[i]), name.c_str());
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
                { "BloomTarget", BoundResource{ impl.BloomUpsample[0], k_ReadVkLayout, {} } },
                { "BloomSampler", BoundResource{ impl.LinearSampler, VK_IMAGE_LAYOUT_UNDEFINED, {} } },
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

            const std::array<std::string_view, 1> downEntry{ k_BloomDownsampleEntry };
            for (size_t i = 0; i < k_BloomMipCount; ++i)
            {
                const GraphicsResource& source = (i == 0) ? impl.Scene : impl.BloomDownsample[i - 1];
                const VkImageLayout sourceLayout = (i == 0) ? k_ReadVkLayout : VK_IMAGE_LAYOUT_GENERAL;

                const BoundResources downBound
                {
                    { "SourceMip", BoundResource{ source, sourceLayout, {} } },
                    { "LinearSampler", BoundResource{ impl.LinearSampler, VK_IMAGE_LAYOUT_UNDEFINED, {} } },
                    { "DestMip", BoundResource{ impl.BloomDownsample[i], VK_IMAGE_LAYOUT_GENERAL, {} } },
                };

                const std::optional<std::string> error = WriteManifestDescriptors(device, impl.BloomDownsampleSets[i], *impl.BloomIndex, impl.BloomDownsampleVariant, downEntry, downBound);
                if (error.has_value())
                {
                    return ColorPipeError::DescriptorSetupFailed;
                }
            }

            const std::array<std::string_view, 1> upEntry{ k_BloomUpsampleEntry };
            for (size_t i = 0; i < k_BloomMipCount - 1u; ++i)
            {
                const GraphicsResource& coarse = (i == k_BloomMipCount - 2u) ? impl.BloomDownsample[k_BloomMipCount - 1u] : impl.BloomUpsample[i + 1u];
                const GraphicsResource& fine = impl.BloomDownsample[i];
                const GraphicsResource& dest = impl.BloomUpsample[i];

                const BoundResources upBound
                {
                    { "CoarseMip", BoundResource{ coarse, VK_IMAGE_LAYOUT_GENERAL, {} } },
                    { "FineMip", BoundResource{ fine, VK_IMAGE_LAYOUT_GENERAL, {} } },
                    { "LinearSampler", BoundResource{ impl.LinearSampler, VK_IMAGE_LAYOUT_UNDEFINED, {} } },
                    { "DestMip", BoundResource{ dest, VK_IMAGE_LAYOUT_GENERAL, {} } },
                };

                const std::optional<std::string> error = WriteManifestDescriptors(device, impl.BloomUpsampleSets[i], *impl.BloomIndex, impl.BloomUpsampleVariant, upEntry, upBound);
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
            info.Name = "ColorPipe_ExposureBuffer";
            impl.ExposureBuffer = WaitForResource(impl.Resources->CreateBuffer(info, {}));

            if (!impl.ExposureBuffer)
            {
                return ColorPipeError::ResourceCreationFailed;
            }

            rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_BUFFER, impl.ExposureBuffer.VkHandle, "ColorPipe_ExposureBuffer");
            return ColorPipeError::Success;
        }

        ColorPipeError CreateTargets(ColorPipeImpl& impl, uint32_t width, uint32_t height)
        {
            impl.Scene = WaitForResource(impl.Resources->CreateImage(MakeTargetInfo(CommonFormats::RGBA16_Float, width, height, "ColorPipe_Scene", "ColorPipe_Scene_View"), {}));
            impl.Ui = WaitForResource(impl.Resources->CreateImage(MakeTargetInfo(CommonFormats::RGBA8_UNorm, width, height, "ColorPipe_Ui", "ColorPipe_Ui_View"), {}));

            if (!impl.Scene || !impl.Ui)
            {
                DestroyTargets(impl);

                return ColorPipeError::ResourceCreationFailed;
            }

            rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_IMAGE, impl.Scene.VkHandle, "ColorPipe_Scene");
            rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, impl.Scene.VkViewHandle, "ColorPipe_Scene_View");
            rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_IMAGE, impl.Ui.VkHandle, "ColorPipe_Ui");
            rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, impl.Ui.VkViewHandle, "ColorPipe_Ui_View");

            for (size_t i = 0; i < k_BloomMipCount; ++i)
            {
                const Extent2D extent = BloomDownsampleExtent(width, height, i);
                const std::string name = std::format("ColorPipe_BloomDownsample_{}", i);
                const std::string viewName = std::format("ColorPipe_BloomDownsample_{}_View", i);
                impl.BloomDownsample[i] = WaitForResource(impl.Resources->CreateImage(
                    MakeBloomTargetInfo(CommonFormats::RGBA16_Float, extent.Width, extent.Height, name, viewName), {}));

                if (!impl.BloomDownsample[i])
                {
                    DestroyTargets(impl);
                    return ColorPipeError::ResourceCreationFailed;
                }

                rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_IMAGE, impl.BloomDownsample[i].VkHandle, name.c_str());
                rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, impl.BloomDownsample[i].VkViewHandle, viewName.c_str());
            }

            for (size_t i = 0; i < k_BloomMipCount - 1u; ++i)
            {
                const Extent2D extent = BloomDownsampleExtent(width, height, i);
                const std::string name = std::format("ColorPipe_BloomUpsample_{}", i);
                const std::string viewName = std::format("ColorPipe_BloomUpsample_{}_View", i);
                impl.BloomUpsample[i] = WaitForResource(impl.Resources->CreateImage(
                    MakeBloomTargetInfo(CommonFormats::RGBA16_Float, extent.Width, extent.Height, name, viewName), {}));

                if (!impl.BloomUpsample[i])
                {
                    DestroyTargets(impl);
                    return ColorPipeError::ResourceCreationFailed;
                }

                rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_IMAGE, impl.BloomUpsample[i].VkHandle, name.c_str());
                rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, impl.BloomUpsample[i].VkViewHandle, viewName.c_str());
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

            std::vector<rhi::ImageBarrier> barriers;
            barriers.reserve(3 + k_BloomMipCount + (k_BloomMipCount - 1u));

            barriers.push_back(rhi::ImageBarrier
            {
                rhi::ImageHandle{ impl.Scene.VkHandle },
                rhi::PipelineStage::None, rhi::Access::None,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                rhi::ImageLayout::Undefined, k_RestingLayout, k_ColorRange
            });
            barriers.push_back(rhi::ImageBarrier
            {
                rhi::ImageHandle{ impl.Ui.VkHandle },
                rhi::PipelineStage::None, rhi::Access::None,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                rhi::ImageLayout::Undefined, k_RestingLayout, k_ColorRange
            });

            barriers.push_back(rhi::ImageBarrier
            {
                rhi::ImageHandle{ impl.BloomUpsample[0].VkHandle },
                rhi::PipelineStage::None, rhi::Access::None,
                rhi::PipelineStage::FragmentShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::Undefined, k_ReadLayout, k_ColorRange
            });

            for (size_t i = 0; i < k_BloomMipCount; ++i)
            {
                barriers.push_back(rhi::ImageBarrier
                {
                    rhi::ImageHandle{ impl.BloomDownsample[i].VkHandle },
                    rhi::PipelineStage::None, rhi::Access::None,
                    rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite | rhi::Access::ShaderSampledRead,
                    rhi::ImageLayout::Undefined, rhi::ImageLayout::General, k_ColorRange
                });
            }

            for (size_t i = 1; i < k_BloomMipCount - 1u; ++i)
            {
                barriers.push_back(rhi::ImageBarrier
                {
                    rhi::ImageHandle{ impl.BloomUpsample[i].VkHandle },
                    rhi::PipelineStage::None, rhi::Access::None,
                    rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite | rhi::Access::ShaderSampledRead,
                    rhi::ImageLayout::Undefined, rhi::ImageLayout::General, k_ColorRange
                });
            }

            commandBuffer.Barrier(rhi::DependencyInfo{ {}, {}, barriers });
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
            for (size_t i = 0; i < k_BloomMipCount; ++i)
            {
                DestroyResourceIfCreated(impl, impl.BloomDownsample[i]);
            }
            for (size_t i = 0; i < k_BloomMipCount - 1u; ++i)
            {
                DestroyResourceIfCreated(impl, impl.BloomUpsample[i]);
            }
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
