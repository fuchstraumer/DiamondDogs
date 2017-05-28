#include "stdafx.h"
#include "GraphicsPipeline.h"
#include "engine/renderer/core/LogicalDevice.h"
#include "engine/renderer/render/Swapchain.h"
#include "engine/renderer/resource/ShaderModule.h"

namespace vulpes {
	GraphicsPipeline::GraphicsPipeline(const Device * _parent, const GraphicsPipelineInfo & _info) : parent(_parent), info(_info) {}

	GraphicsPipeline::GraphicsPipeline(const Device * _parent, const Swapchain* swapchain) : parent(_parent), createInfo(vk_graphics_pipeline_create_info_base) {}

	GraphicsPipeline::~GraphicsPipeline(){
		shaders.clear();
		vkDestroyPipeline(parent->vkHandle(), handle, allocators);
	}

	void GraphicsPipeline::AddShaderModule(const char * filename, const VkShaderStageFlagBits & stages, const char * shader_name){
		shaders.push_back(std::move(ShaderModule(parent, filename, stages, shader_name)));
		shaderPipelineInfos.push_back(shaders.back().PipelineInfo());
	}

	void GraphicsPipeline::Init(VkGraphicsPipelineCreateInfo & create_info, const VkPipelineCache& cache){
		createInfo = create_info;
		VkResult result = vkCreateGraphicsPipelines(parent->vkHandle(), cache, 1, &create_info, nullptr, &handle);
		VkAssert(result);
	}

	void GraphicsPipeline::Init() {
		assert(shaders.size() != 0);
		createInfo.stageCount = static_cast<uint32_t>(shaders.size());
		createInfo.pStages = shaderPipelineInfos.data();
		VkResult result = vkCreateGraphicsPipelines(parent->vkHandle(), VK_NULL_HANDLE, 1, &createInfo, nullptr, &handle);
		VkAssert(result);
	}

	void GraphicsPipeline::SetRenderpass(const VkRenderPass & renderpass){
		createInfo.renderPass = renderpass;
	}

	const VkPipeline & GraphicsPipeline::vkHandle() const noexcept {
		return handle;
	}

	PipelineCache::PipelineCache(const Device * _parent) : parent(_parent) {
		std::string cache_dir = std::string("./shader_cache/");
		uint16_t hash_id = static_cast<uint16_t>(std::hash<unsigned int>{}(static_cast<unsigned int>(parent->GetPhysicalDeviceID())));
		std::string fname = cache_dir + std::to_string(hash_id) + std::string(".vkdat");
		filename = fname;
		createInfo = VkPipelineCacheCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, 0, nullptr };
		std::ifstream cache(filename, std::ios::in);
		if(cache){
#ifndef NDEBUG
			std::cerr << "Pre-existing cache file" << fname << " found, loading... " << std::endl;
#endif // !NDEBUG
			std::string cache_str((std::istreambuf_iterator<char>(cache)), std::istreambuf_iterator<char>());
			uint32_t cache_size = static_cast<uint32_t>(cache_str.size() * sizeof(char));
			createInfo.initialDataSize = cache_size;
			createInfo.pInitialData = cache_str.data();
		}
		
		VkResult result = vkCreatePipelineCache(parent->vkHandle(), &createInfo, nullptr, &handle);
		VkAssert(result);
	}

	PipelineCache::PipelineCache(const Device * _parent, const uint16_t& scene_id_hash) : parent(_parent) {
		std::string cache_dir = std::string("./shader_cache/");
		std::string fname = cache_dir + std::to_string(scene_id_hash) + std::string(".vkdat");
		filename = fname;
		createInfo = VkPipelineCacheCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, 0, nullptr };
		std::ifstream cache(filename, std::ios::in);
		if (cache) {
#ifndef NDEBUG
			std::cerr << "Pre-existing cache file" << fname << " found, loading... " << std::endl;
#endif // !NDEBUG
			std::string cache_str((std::istreambuf_iterator<char>(cache)), std::istreambuf_iterator<char>());
			uint32_t cache_size = static_cast<uint32_t>(cache_str.size() * sizeof(char));
			createInfo.initialDataSize = cache_size;
			createInfo.pInitialData = cache_str.data();
		}

		VkResult result = vkCreatePipelineCache(parent->vkHandle(), &createInfo, nullptr, &handle);
		VkAssert(result);
	}

	PipelineCache::~PipelineCache(){
		save_to_file();
		vkDeviceWaitIdle(parent->vkHandle());
		vkDestroyPipelineCache(parent->vkHandle(), handle, nullptr);
	}

	VkResult PipelineCache::save_to_file() {
		VkResult result = VK_SUCCESS;
		size_t cache_size;

		result = vkGetPipelineCacheData(parent->vkHandle(), handle, &cache_size, nullptr);
		VkAssert(result);
		if (cache_size != 0) {
#ifndef NDEBUG
			std::cerr << "Saving cache to file " << filename << " now..." << std::endl;
#endif // !NDEBUG
			std::ofstream file(filename, std::ios::out);
			void* cache_data;
			cache_data = malloc(cache_size);
			result = vkGetPipelineCacheData(parent->vkHandle(), handle, &cache_size, cache_data);
			VkAssert(result);
			file.write(reinterpret_cast<const char*>(cache_data), cache_size);
			file.close();
			free(cache_data);
			return VK_SUCCESS;
		}
		return VK_ERROR_VALIDATION_FAILED_EXT;
	}

	const VkPipelineCache & PipelineCache::vkHandle() const noexcept
	{
		return handle;
	}

}
