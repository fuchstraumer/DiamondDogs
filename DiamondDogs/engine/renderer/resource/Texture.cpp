#include "stdafx.h"
#define STB_IMAGE_IMPLEMENTATION
#include "Texture.h"

#include "engine/renderer/core\LogicalDevice.h"
#include "engine/renderer\command\CommandPool.h"
#include "engine/renderer\resource\Buffer.h"

namespace vulpes {

	Texture2D_STB::Texture2D_STB(const char * filename, const Device* _parent) : Image(_parent) {
		int tmp_width, tmp_height, tmp_channels;
		pixels = stbi_load(filename, &tmp_width, &tmp_height, &tmp_channels, STBI_rgb_alpha);
		width = static_cast<uint32_t>(tmp_width);
		height = static_cast<uint32_t>(tmp_height);
		channels = static_cast<uint32_t>(tmp_channels);
		imageDataSize = width * height * 4;
	}

	Texture2D_STB::~Texture2D_STB(){
		if (sampler != VK_NULL_HANDLE) {
			vkDestroySampler(parent->vkHandle(), sampler, allocators);
		}

	}

	void Texture2D_STB::CreateSampler(const VkSamplerCreateInfo & sampler_info){
		VkResult result = vkCreateSampler(parent->vkHandle(), &sampler_info, allocators, &sampler);
		VkAssert(result);
	}

	VkExtent3D Texture2D_STB::GetExtents() const noexcept {
		return VkExtent3D{ width, height, 1 };
	}

	void Texture2D_STB::SendToDevice(CommandPool* cmd_pool, VkQueue & transfer_queue) {

		CopyToStaging(pixels, transfer_queue);

		// Specify layout and copy information.
		VkImageSubresourceLayers sr_layers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		VkImageCopy copy_region{ sr_layers, { 0, 0, 0 }, sr_layers, { 0, 0, 0 }, { width, height, 1} };

		VkCommandBuffer copy_cmd = cmd_pool->StartSingleCmdBuffer();
		{
			// Change staging layout
			VkImageMemoryBarrier staging_barrier = GetMemoryBarrier(staging_image, format, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			vkCmdPipelineBarrier(copy_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &staging_barrier);
			// Change Texture2D layout
			VkImageMemoryBarrier image_barrier = GetMemoryBarrier(image, format, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			vkCmdPipelineBarrier(copy_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

			// Copy image data
			vkCmdCopyImage(copy_cmd, staging_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

			// transition layout from transfer destination to shader read layout
			image_barrier = GetMemoryBarrier(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			vkCmdPipelineBarrier(copy_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

			// Perform commands we just specified.
			// CommandPool::SubmitTransferCommand(copy_cmd, transfer_queue);
		}

		cmd_pool->EndSingleCmdBuffer(copy_cmd, transfer_queue);
		
		vkFreeMemory(parent->vkHandle(), staging_memory, allocators);
		vkDestroyImage(parent->vkHandle(), staging_image, allocators);

		on_device = true;
	}

	void Texture2D_STB::CopyToStaging(void * data, const VkQueue & transfer_queue, const VkDeviceSize & copy_size, const VkDeviceSize & offset){
		
		// Create staging image
		VkDeviceSize dummy_size = 0;
		CreateImage(staging_image, staging_memory, dummy_size, this->parent, extents, format, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_TILING_LINEAR);

		// prepare to copy from host->staging and then staging->device
		void* mapped;
		VkResult result = vkMapMemory(this->parent->vkHandle(), staging_memory, 0, imageDataSize, 0, &mapped);
		VkAssert(result);

		// Need to get image memory layout details before just copying data over
		VkImageSubresource sub_rsrc{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
		VkSubresourceLayout sub_rsrc_layout;
		vkGetImageSubresourceLayout(parent->vkHandle(), staging_image, &sub_rsrc, &sub_rsrc_layout);
		// Usually the case when image has dimensions that are a power-of-two
		if (sub_rsrc_layout.rowPitch == width * 4) {
			memcpy(mapped, pixels, imageDataSize);
		}
		else {
			// Memory is oddly aligned, copy data row-by-row
			uint8_t* mapped_bytes = reinterpret_cast<uint8_t*>(mapped);
			for (uint32_t j = 0; j < width; ++j) {
				memcpy(&mapped_bytes[j * sub_rsrc_layout.rowPitch], &pixels[j * width * 4], width * 4);
			}
		}

		vkUnmapMemory(parent->vkHandle(), staging_memory);

		// Cleanup stb pixel data
		stbi_image_free(pixels);
	}

	const VkSampler & Texture2D_STB::Sampler() const noexcept{
		return sampler;
	}

	TextureCubemap::TextureCubemap(const char * filename, const Device * parent) : Image(parent), createInfo(vk_image_create_info_base) {
		textureData = std::move(gli::texture_cube(gli::load(filename)));
		width = static_cast<uint32_t>(textureData.extent().x);
		height = static_cast<uint32_t>(textureData.extent().y);
		mipLevels = static_cast<uint32_t>(textureData.levels());
	}

	TextureCubemap::~TextureCubemap() {
		if (sampler != VK_NULL_HANDLE) {
			vkDestroySampler(parent->vkHandle(), sampler, allocators);
		}
	}

	void TextureCubemap::Create(CommandPool * pool, const VkQueue & queue) {

		VkMemoryAllocateInfo alloc = vk_allocation_info_base;
		VkMemoryRequirements reqs;

		VkBufferCreateInfo staging_info = vk_buffer_create_info_base;
		staging_info.size =	static_cast<uint32_t>(textureData.size());
		staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		staging_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkResult result = VK_SUCCESS;
		result = vkCreateBuffer(parent->vkHandle(), &staging_info, allocators, &stagingBuffer);
		VkAssert(result);

		vkGetBufferMemoryRequirements(parent->vkHandle(), stagingBuffer, &reqs);
		alloc.allocationSize = reqs.size;
		alloc.memoryTypeIndex = parent->GetMemoryTypeIdx(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		result = vkAllocateMemory(parent->vkHandle(), &alloc, allocators, &stagingMemory);
		VkAssert(result);
		result = vkBindBufferMemory(parent->vkHandle(), stagingBuffer, stagingMemory, 0);
		VkAssert(result);

		// Copy texture data to staging buffer.
		uint8_t* mapped;
		result = vkMapMemory(parent->vkHandle(), stagingMemory, 0, reqs.size, 0, reinterpret_cast<void**>(&mapped));
		VkAssert(result);
		memcpy(mapped, textureData.data(), textureData.size());
		vkUnmapMemory(parent->vkHandle(), stagingMemory);

		// Now set up buffer copy regions for each face and all of its mip levels.
		std::vector<VkBufferImageCopy> stagingCopyRegions;
		size_t offset = 0;

		for (uint32_t face_idx = 0; face_idx < 6; ++face_idx) {
			for (uint32_t mip_level = 0; mip_level < mipLevels; ++mip_level) {
				VkBufferImageCopy copy{};
				copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copy.imageSubresource.mipLevel = mip_level;
				copy.imageSubresource.baseArrayLayer = face_idx;
				copy.imageSubresource.layerCount = 1;
				copy.imageExtent.width = static_cast<uint32_t>(textureData[face_idx][mip_level].extent().x);
				copy.imageExtent.height = static_cast<uint32_t>(textureData[face_idx][mip_level].extent().y);
				copy.imageExtent.depth = 1;
				copy.bufferOffset = static_cast<uint32_t>(offset);
				stagingCopyRegions.push_back(std::move(copy));
				// Increment offset by datasize of last specified copy region
				offset += textureData[face_idx][mip_level].size();
			}
		}

		// Create the target image we'll be copying to.
		createInfo.imageType = VK_IMAGE_TYPE_2D;
		createInfo.format = format;
		createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		createInfo.mipLevels = mipLevels;
		createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.extent = VkExtent3D{ width, height, 1 };
		createInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		createInfo.arrayLayers = 6;
		createInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		result = vkCreateImage(parent->vkHandle(), &createInfo, allocators, &image);
		VkAssert(result);

		vkGetImageMemoryRequirements(parent->vkHandle(), image, &reqs);
		alloc.allocationSize = reqs.size;
		alloc.memoryTypeIndex = parent->GetMemoryTypeIdx(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		result = vkAllocateMemory(parent->vkHandle(), &alloc, allocators, &memory);
		VkAssert(result);
		
		result = vkBindImageMemory(parent->vkHandle(), image, memory, 0);
		VkAssert(result);

		// Specify layout and copy information.
		VkImageSubresourceRange image_range_info{};
		image_range_info.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_range_info.baseArrayLayer = 0;
		image_range_info.baseMipLevel = 0;
		image_range_info.levelCount = mipLevels;
		image_range_info.layerCount = 6;

		VkFence transfer_fence;
		result = vkCreateFence(parent->vkHandle(), &vk_fence_create_info_base, nullptr, &transfer_fence);

		VkCommandBuffer copy_cmd = pool->StartSingleCmdBuffer();

			// Change Texture2D layout
			VkImageMemoryBarrier image_barrier = GetMemoryBarrier(image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			image_barrier.subresourceRange = image_range_info;
			vkCmdPipelineBarrier(copy_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

			// Copy image data
			vkCmdCopyBufferToImage(copy_cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(stagingCopyRegions.size()), stagingCopyRegions.data());

			// transition layout from transfer destination to shader read layout
			image_barrier = GetMemoryBarrier(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			vkCmdPipelineBarrier(copy_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

		pool->EndSingleCmdBuffer(copy_cmd, queue);

		// Create sampler
		VkSamplerCreateInfo sampler_info = vk_sampler_create_info_base;
		sampler_info.maxLod = static_cast<float>(mipLevels);
		result = vkCreateSampler(parent->vkHandle(), &sampler_info, allocators, &sampler);
		VkAssert(result);

		VkImageViewCreateInfo view_info = vk_image_view_create_info_base;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		view_info.subresourceRange.layerCount = 6;
		view_info.subresourceRange.levelCount = mipLevels;
		view_info.image = image;
		view_info.format = format;

		result = vkCreateImageView(parent->vkHandle(), &view_info, allocators, &view);
		VkAssert(result);

		finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		
		vkFreeMemory(parent->vkHandle(), stagingMemory, allocators);
		vkDestroyBuffer(parent->vkHandle(), stagingBuffer, allocators);
		textureData.clear();
	}

	VkDescriptorImageInfo TextureCubemap::GetDescriptor() const noexcept {
		return VkDescriptorImageInfo{ sampler, view, finalLayout };
	}

	const VkSampler & TextureCubemap::Sampler() const noexcept {
		return sampler;
	}

	void Texture2D::CreateFromBuffer(void * data, const VkDeviceSize& size, CommandPool * pool, VkQueue & queue){
		imageDataSize = size;

		Buffer::CreateStagingBuffer(parent, imageDataSize, stagingBuffer, stagingMemory);

		void* mapped;
		VkResult err = VK_SUCCESS;
		err = vkMapMemory(parent->vkHandle(), stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
		VkAssert(err);
		
		// Copy data to staging.
		memcpy(mapped, data, imageDataSize);

		vkUnmapMemory(parent->vkHandle(), stagingMemory);

		// Setup copy command from buffer to image
		VkBufferImageCopy buffer_to_image{
			0,
			0,
			0,
			VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },

		};

		VkCommandBuffer cpy_buffer = pool->StartSingleCmdBuffer();
	}

	void Texture2D::CreateFromFile(const char * filename, const VkFormat& texture_format, CommandPool * pool, VkQueue & queue) {

		// Load data from file and get parameters from said file.
		textureData = std::move(gli::texture2d(gli::load(filename)));
		width = static_cast<uint32_t>(textureData.extent().x);
		height = static_cast<uint32_t>(textureData.extent().y);
		mipLevels = static_cast<uint32_t>(textureData.levels());
		layerCount = static_cast<uint32_t>(textureData.layers());
		format = texture_format;

		Buffer::CreateStagingBuffer(parent, textureData.size(), stagingBuffer, stagingMemory);

		VkResult result = VK_SUCCESS;
		void* mapped;
		result = vkMapMemory(parent->vkHandle(), stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
		VkAssert(result);

		memcpy(mapped, textureData.data(), textureData.size());

		vkUnmapMemory(parent->vkHandle(), stagingMemory);

		std::vector<VkBufferImageCopy> copy_data(mipLevels);
		uint32_t offset = 0;
		for (uint32_t i = 0; i < mipLevels; ++i) {
			copy_data[i] = VkBufferImageCopy{
				offset,
				0,
				0,
				VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, i, 0, layerCount},
				VkOffset3D{ 0, 0, 0},
				VkExtent3D{ static_cast<uint32_t>(textureData[i].extent().x), static_cast<uint32_t>(textureData[i].extent().y), 1 }
			};
			offset += static_cast<uint32_t>(textureData[i].size());
		}

		createInfo.imageType = VK_IMAGE_TYPE_2D;
		createInfo.format = texture_format;
		createInfo.mipLevels = mipLevels;
		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.extent = VkExtent3D{ width, height, 1 };
		createInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		createInfo.arrayLayers = layerCount;

		result = vkCreateImage(parent->vkHandle(), &createInfo, allocators, &image);
		VkAssert(result);

		VkMemoryAllocateInfo alloc = vk_allocation_info_base;
		VkMemoryRequirements reqs;

		vkGetImageMemoryRequirements(parent->vkHandle(), image, &reqs);
		alloc.allocationSize = reqs.size;
		alloc.memoryTypeIndex = parent->GetMemoryTypeIdx(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		
		result = vkAllocateMemory(parent->vkHandle(), &alloc, nullptr, &memory);
		VkAssert(result);
		result = vkBindImageMemory(parent->vkHandle(), image, memory, 0);
		VkAssert(result);

		auto barrier0 = Image::GetMemoryBarrier(image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		barrier0.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, layerCount };
		auto barrier1 = Image::GetMemoryBarrier(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		barrier1.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, layerCount };

		auto cmd = pool->StartSingleCmdBuffer();

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier0);
		
		vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(copy_data.size()), copy_data.data());

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier1);

		pool->EndSingleCmdBuffer(cmd, queue);

		vkFreeMemory(parent->vkHandle(), stagingMemory, allocators);
		vkDestroyBuffer(parent->vkHandle(), stagingBuffer, allocators);

		VkImageViewCreateInfo view_info = vk_image_view_create_info_base;
		view_info.subresourceRange.levelCount = mipLevels;
		view_info.subresourceRange.layerCount = layerCount;
		view_info.image = image;
		view_info.format = format;

		result = vkCreateImageView(parent->vkHandle(), &view_info, allocators, &view);
		VkAssert(result);
	}

	void Texture2D::CreateSampler(const VkSamplerCreateInfo & info){
		VkResult result = VK_SUCCESS;
		result = vkCreateSampler(parent->vkHandle(), &info, allocators, &sampler);
		VkAssert(result);
	}

	VkDescriptorImageInfo Texture2D::GetDescriptor() const noexcept{
		return VkDescriptorImageInfo{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	}

	const VkSampler & Texture2D::Sampler() const noexcept{
		return sampler;
	}

}
