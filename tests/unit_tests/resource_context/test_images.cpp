#include "ResourceContextFixture.hpp"
#include <array>

namespace
{
    ImageCreateInfo SampledTexture(uint32_t width, uint32_t height)
    {
        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageBits::SampledImage | ImageUsageBits::SrvComputeShader;
        info.Type = ImageType::Type2D;
        info.Format = ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };
        info.Width = width;
        info.Height = height;
        info.Depth = 1u;
        return info;
    }
}

TEST_F(ResourceContextTest, ImageWithViewUploadsInitialData)
{
    constexpr uint32_t k_Size = 64u;
    const std::vector<uint32_t> texels = MakeWordPattern(k_Size * k_Size, 5u);
    RhiImageResourceData data{};
    data.Data = std::as_bytes(std::span{ texels });
    data.Width = k_Size;
    data.Height = k_Size;

    ImageCreateInfo info = SampledTexture(k_Size, k_Size);
    info.ViewInfo = ImageViewCreateInfo{};

    std::shared_ptr<GraphicsResourceReply> reply = context->CreateImage(info, std::span{ &data, 1u });
    ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    const GraphicsResource image = reply->GetResource();
    EXPECT_EQ(image.Type, ResourceType::Image);
    EXPECT_NE(image.VkHandle, 0u);
    EXPECT_NE(image.VkViewHandle, 0u);
    EXPECT_EQ(image.VkSamplerHandle, 0u);
    DestroyAndWait(image);
}

TEST_F(ResourceContextTest, ImageWithSamplerIsACombinedImageSampler)
{
    ImageCreateInfo info = SampledTexture(16u, 16u);
    info.ViewInfo = ImageViewCreateInfo{};
    info.SamplerInfo = SamplerCreateInfo{};

    std::shared_ptr<GraphicsResourceReply> reply = context->CreateImage(info, {});
    ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    const GraphicsResource image = reply->GetResource();
    EXPECT_EQ(image.Type, ResourceType::CombinedImageSampler);
    EXPECT_NE(image.VkSamplerHandle, 0u);
    DestroyAndWait(image);
}

TEST_F(ResourceContextTest, SetImageDataAfterAnEarlierUpload)
{
    // the second upload starts from the layout the first one left, which validation checks on submit
    constexpr uint32_t k_Size = 8u;
    const std::vector<uint32_t> texels = MakeWordPattern(k_Size * k_Size, 11u);
    RhiImageResourceData data{};
    data.Data = std::as_bytes(std::span{ texels });
    data.Width = k_Size;
    data.Height = k_Size;

    std::shared_ptr<GraphicsResourceReply> reply = context->CreateImage(SampledTexture(k_Size, k_Size), std::span{ &data, 1u });
    ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    std::shared_ptr<ResourceTransferReply> setReply = context->SetImageData(reply->GetResource(), std::span{ &data, 1u });
    EXPECT_EQ(setReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
}

TEST_F(ResourceContextTest, MipLevelsAndLayersUploadPerRegion)
{
    constexpr uint32_t k_Size = 16u;
    ImageCreateInfo info = SampledTexture(k_Size, k_Size);
    info.MipLevels = 2u;
    info.ArrayLayers = 2u;
    info.ViewInfo = ImageViewCreateInfo{};
    info.ViewInfo->ViewType = ImageViewType::Type2DArray;
    info.ViewInfo->Range.LevelCount = 2u;
    info.ViewInfo->Range.LayerCount = 2u;

    const std::vector<uint32_t> mip0 = MakeWordPattern(k_Size * k_Size * 2u, 1u);
    const std::vector<uint32_t> mip1 = MakeWordPattern((k_Size / 2u) * (k_Size / 2u) * 2u, 2u);
    std::array<RhiImageResourceData, 2> data{};
    data[0] = RhiImageResourceData{ std::as_bytes(std::span{ mip0 }), k_Size, k_Size, 0u, 2u, 0u, 1u };
    data[1] = RhiImageResourceData{ std::as_bytes(std::span{ mip1 }), k_Size / 2u, k_Size / 2u, 0u, 2u, 1u, 1u };

    std::shared_ptr<GraphicsResourceReply> reply = context->CreateImage(info, data);
    EXPECT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
}

TEST_F(ResourceContextTest, DepthImageWithoutData)
{
    ImageCreateInfo info{};
    info.Domain = ResourceDomain::GPUOnly;
    info.Usage = ImageUsageBits::DepthWrite | ImageUsageBits::DepthRead;
    info.Type = ImageType::Type2D;
    info.Format = ImageFormat{ ImageComponentFormats::D32, ImageDataType::Float };
    info.Width = 128u;
    info.Height = 128u;
    info.ViewInfo = ImageViewCreateInfo{};

    std::shared_ptr<GraphicsResourceReply> reply = context->CreateImage(info, {});
    ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    EXPECT_NE(reply->GetResource().VkViewHandle, 0u);
}

TEST_F(ResourceContextTest, CopyImageDuplicatesTheImage)
{
    constexpr uint32_t k_Size = 32u;
    const std::vector<uint32_t> texels = MakeWordPattern(k_Size * k_Size, 3u);
    RhiImageResourceData data{};
    data.Data = std::as_bytes(std::span{ texels });
    data.Width = k_Size;
    data.Height = k_Size;

    ImageCreateInfo info = SampledTexture(k_Size, k_Size);
    info.Usage |= ImageUsageBits::TransferSource;
    std::shared_ptr<GraphicsResourceReply> reply = context->CreateImage(info, std::span{ &data, 1u });
    ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);

    std::shared_ptr<GraphicsResourceReply> copyReply = context->CopyImage(reply->GetResource());
    ASSERT_EQ(copyReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    EXPECT_NE(copyReply->GetResource().VkHandle, reply->GetResource().VkHandle);
}

TEST_F(ResourceContextTest, ImageReadsBackIntoABuffer)
{
    constexpr uint32_t k_Size = 32u;
    const std::vector<uint32_t> texels = MakeWordPattern(k_Size * k_Size, 7u);
    RhiImageResourceData data{};
    data.Data = std::as_bytes(std::span{ texels });
    data.Width = k_Size;
    data.Height = k_Size;

    ImageCreateInfo info = SampledTexture(k_Size, k_Size);
    info.Usage |= ImageUsageBits::TransferSource;
    std::shared_ptr<GraphicsResourceReply> reply = context->CreateImage(info, std::span{ &data, 1u });
    ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);

    // twice, so the second copy starts from the layout the first one left
    for (int pass = 0; pass < 2; ++pass)
    {
        const std::vector<uint32_t> words = ReadBackWords(reply->GetResource(), texels.size() * sizeof(uint32_t));
        EXPECT_EQ(words, texels);
    }
}

TEST_F(ResourceContextTest, StandaloneSamplerCreateAndDestroy)
{
    SamplerCreateInfo info{};
    info.MagFilter = ImageFilteringMode::Nearest;
    info.AddressU = SamplerCreateInfo::AddressMode::ClampToEdge;
    std::shared_ptr<GraphicsResourceReply> reply = context->CreateSampler(info);
    ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    const GraphicsResource sampler = reply->GetResource();
    EXPECT_EQ(sampler.Type, ResourceType::Sampler);
    EXPECT_NE(sampler.VkSamplerHandle, 0u);
    DestroyAndWait(sampler);
}

TEST_F(ResourceContextTest, ImageWithoutFormatFails)
{
    ImageCreateInfo info = SampledTexture(16u, 16u);
    info.Format = ImageFormat{};
    std::shared_ptr<GraphicsResourceReply> reply = context->CreateImage(info, {});
    EXPECT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
}
