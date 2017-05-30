#pragma once
#ifndef VULPES_VK_TEXTURE_H
#define VULPES_VK_TEXTURE_H

#include "stdafx.h"

#include "stb\stb_image.h"
#include "engine/renderer\ForwardDecl.h"
#include "engine/renderer\NonCopyable.h"
#include "Image.h"

namespace vulpes {


	class Texture2D_STB : public Image {
	public:

		Texture2D_STB(const char* filename, const Device* parent);

		~Texture2D_STB();

		void CreateSampler(const VkSamplerCreateInfo& sampler_info);

		void SendToDevice(CommandPool* copy_cmd, VkQueue& transfer_queue);

		void CopyToStaging(void* data, const VkQueue& transfer_queue, const VkDeviceSize& copy_size = 0, const VkDeviceSize& offset = 0);

		const VkSampler& Sampler() const noexcept;

		VkDeviceSize GetImageSize() const noexcept;

		virtual VkExtent3D GetExtents() const noexcept override;

	private:

		// Set to true when data transferred fully to device.
		bool on_device = false;

		VkSampler sampler;

		// Handles for staging objects.
		VkImage staging_image;
		VkDeviceMemory staging_memory;

		const VkAllocationCallbacks* allocators = nullptr;

		VkImageCreateInfo createInfo;
		
		// Imported STB image data
		stbi_uc* pixels;

		// dimensions read from file
		uint32_t width, height, channels;
	};

	class Texture2D : public Image {
	public:

		Texture2D(const Device* _parent, const VkImageUsageFlags& flags = VK_IMAGE_USAGE_SAMPLED_BIT) : Image(_parent) {
			createInfo = vk_image_create_info_base;
			createInfo.usage = flags;
		}

		~Texture2D() {}

		void CreateFromBuffer(void* data, const VkDeviceSize& size, CommandPool* pool, VkQueue& queue);

		void CreateFromFile(const char* filename, const VkFormat& texture_format, CommandPool* pool, VkQueue& queue);

		// Does not implicitly transfer the object.
		void CreateFromFile(const char* filename, const VkFormat& texture_format);

		void RecordTransferCmd(VkCommandBuffer& transfer_cmd);

		void CreateSampler(const VkSamplerCreateInfo& info);

		VkDescriptorImageInfo GetDescriptor() const noexcept;
		const VkSampler& Sampler() const noexcept;

	private:

		gli::texture2d textureData;

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkSampler sampler;

		uint32_t width, height;
		uint32_t mipLevels, layerCount;

	};

	class TextureCubemap : public Image {
	public:
		
		TextureCubemap(const char* filename, const Device* parent);

		virtual ~TextureCubemap();

		void Create(CommandPool* pool, const VkQueue& queue);

		VkDescriptorImageInfo GetDescriptor() const noexcept;

		const VkSampler& Sampler() const noexcept;

	private:

		gli::texture_cube textureData;

		// using buffer for staging avoids need to transition layouts.
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkSampler sampler;
		VkImageCreateInfo createInfo;

		uint32_t width, height;
		uint32_t mipLevels, layerCount;
	};

	class TextureArray : public Image {
	public:

		TextureArray(const char* filename, const Device* parent);

		~TextureArray();

		void Create(CommandPool* pool, VkQueue& queue);

		VkDescriptorImageInfo GetDescriptor() const noexcept;

		const VkSampler& Sampler() const noexcept;

	private:

		 gli::texture2d_array textureData;

		 VkBuffer stagingBuffer;
		 VkDeviceMemory stagingMemory;

		 VkSampler sampler;
		 VkImageCreateInfo createInfo;

		 const VkAllocationCallbacks* allocators = nullptr;

		 uint32_t width, height;
		 uint32_t mipLevels, layerCount;
	};

}

#endif // !VULPES_VK_TEXTURE_H
