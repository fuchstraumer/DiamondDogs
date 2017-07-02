#pragma once
#ifndef VULPES_VK_TEXTURE_H
#define VULPES_VK_TEXTURE_H
#include "stdafx.h"
#include "engine/renderer/ForwardDecl.h"
#include "engine/renderer/NonCopyable.h"
#include "engine/renderer/resource/Allocator.h"
#include "Image.h"

namespace vulpes {

	template<typename gli_texture_type>
	class Texture : public Image {
	public:

		Texture(const Device* _parent, const VkImageUsageFlags& flags);

		~Texture() = default;

		void CreateFromFile(const char* filename);

		// Avoid using this, when possible: Using a single-shot command buffer for a single transfer is a waste of device resources and time.
		void TransferUsingPool(CommandPool* transfer_pool, VkQueue& transfer_queue) const;

		// Preferred method: Create several textures, then transfer them all using one command buffer and queue submission
		void TransferUsingCmdBuffer(VkCommandBuffer& transfer_cmd_buffer) const;

		VkDescriptorImageInfo GetDescriptor() const noexcept;
		const VkSampler& Sampler() const noexcept;

		uint32_t Width = 0, Height = 0, Depth = 0;

	private:

		// creates backing handle and gets memory for the texture proper
		void createTexture();
		void createView();
		void createSampler();

		static void GetFormatFromGLI(const gli_texture_type& texture_data);

		gli_texture_type loadTextureDataFromFile(const char* filename);
		void updateTextureParameters(const gli_texture_type& texture_data);
		void createCopyInformation(const gli_texture_type& texture_data);
		void copyFromFileToStaging(const char* filename);

		VkSampler sampler;

		VkBuffer stagingBuffer;
		VkMappedMemoryRange stagingMemory;

		uint32_t mipLevels = 0, layerCount = 0;

		std::vector<VkBufferImageCopy> copyInfo;
	};

	template<typename gli_texture_type>
	inline Texture<gli_texture_type>::Texture(const Device * _parent, const VkImageUsageFlags & flags) : Image(parent) {
		createInfo = vk_image_create_info_base;
		createInfo.usage = flags;
	}

	template<typename gli_texture_type>
	inline void Texture<gli_texture_type>::CreateFromFile(const char * filename) {
		copyFromFileToStaging(filename);
		createTexture(); 
		createView();
		createSampler();
	}

	template<typename gli_texture_type>
	inline void Texture<gli_texture_type>::TransferUsingCmdBuffer(VkCommandBuffer & transfer_cmd_buffer) const {

		// Need barriers to transition layout from initial undefined/uninitialized layout to what we'll use in the shader this is for.
		auto barrier0 = Image::GetMemoryBarrier(image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		barrier0.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, layerCount };
		auto barrier1 = Image::GetMemoryBarrier(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		barrier1.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, layerCount };

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier0);
		assert(!copyInfo.empty());
		vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(copyInfo.size()), copyInfo.data());
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier1);

		// can now destroy staging object
		parent->vkAllocator->DestroyBuffer(stagingBuffer);
	}

	template<typename gli_texture_type>
	inline void Texture<gli_texture_type>::createSampler() {
		throw std::runtime_error("Attempted to create VkSampler for an unsupported texture type.");
	}

	template<typename gli_texture_type>
	inline void Texture<gli_texture_type>::createView() {
		throw std::runtime_error("Attempted to create VkImageView for an unsupported texture type.");
	}

	template<typename gli_texture_type>
	inline gli_texture_type Texture<gli_texture_type>::loadTextureDataFromFile(const char * filename) {
		throw std::runtime_error("Attempt to load data for an unsupported texture type: add specialized method for this type, or use a supported type");
	}

	template<typename gli_texture_type>
	inline void Texture<gli_texture_type>::updateTextureParameters(const gli_texture_type& texture_data) {
		throw std::runtime_error("Attempted to update parameters of an unsupported texture type.");
	}

	template<typename gli_texture_type>
	inline void Texture<gli_texture_type>::createCopyInformation(const gli_texture_type & texture_data) {
		throw std::runtime_error("Attempted to create buffer-image copy information for an unsupported texture type.");
	}

	template<typename gli_texture_type>
	inline void Texture<gli_texture_type>::copyFromFileToStaging(const char* filename) {
		throw std::runtime_error("Attempted to copy unsupported texture type to staging - add support for texture type or use a supported type");
	}

	template<>
	inline void Texture<gli::texture2d>::CreateFromFile(const char* filename) {
			
		copyFromFileToStaging(filename);

		format = texture_format;
		createInfo.imageType = VK_IMAGE_TYPE_2D;
		createInfo.format = texture_format;
		createInfo.mipLevels = mipLevels;
		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.extent = VkExtent3D{ Width, Height, 1 };
		createInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		createInfo.arrayLayers = layerCount;

		Image::CreateImage(handle, memory, parent, createInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		createView();
		createSampler();
			/*result = vkCreateImage(parent->vkHandle(), &createInfo, allocators, &image);
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
			VkAssert(result);*/
	}

	template<>
	inline gli::texture2d Texture<gli::texture2d>::loadTextureDataFromFile(const char* filename) {
		gli::texture2d result = gli::texture2d(gli::load(filename));
		updateTextureParameters(result);
		createCopyInformation(result);
		return std::move(result);
	}

	template<> 
	inline void Texture<gli::texture2d>::updateTextureParameters(const gli::texture2d& texture_data) {
		Width = static_cast<uint32_t>(texture_data.extent().x);
		Height = static_cast<uint32_t>(texture_data.extent().y);
		Depth = 1;
		mipLevels = static_cast<uint32_t>(texture_data.levels());
		layerCount = static_cast<uint32_t>(texture_data.layers());
	}

	template<>
	inline void vulpes::Texture<gli::texture2d>::createCopyInformation(const gli::texture2d& texture_data) {
		assert(mipLevels != 0);
		copyInfo.resize(mipLevels);
		uint32_t offset = 0;
		for (uint32_t i = 0; i < mipLevels; ++i) {
			copyInfo[i] = VkBufferImageCopy{
				offset,
				0,
				0,
				VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, i, 0, layerCount },
				VkOffset3D{ 0, 0, 0 },
				VkExtent3D{ static_cast<uint32_t>(texture_data[i].extent().x), static_cast<uint32_t>(texture_data[i].extent().y), 1 }
			};
			offset += static_cast<uint32_t>(texture_data[i].size());
		}
	}

	template<>
	inline void Texture<gli::texture2d>::copyFromFileToStaging(const char* filename) {

		gli::texture2d texture_data = loadTextureDataFromFile(filename);

		Buffer::CreateStagingBuffer(parent, texture_data.size(), stagingBuffer, stagingMemory);

		VkResult result = VK_SUCCESS;
		void* mapped;
		result = vkMapMemory(parent->vkHandle(), stagingMemory.memory, stagingMemory.offset, stagingMemory.size, 0, &mapped);
		VkAssert(result);
		memcpy(mapped, texture_data.data(), texture_data.size());
		vkUnmapMemory(parent->vkHandle(), stagingMemory.memory);

	}

}

#endif // !VULPES_VK_TEXTURE_H
