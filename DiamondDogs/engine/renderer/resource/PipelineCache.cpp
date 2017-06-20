#include "stdafx.h"
#include "PipelineCache.h"
#include "../core/LogicalDevice.h"
#include "../core/PhysicalDevice.h"
#include "../../util/easylogging++.h"

namespace vulpes {

	PipelineCache::PipelineCache(const Device* _parent, const uint16_t& hash_id) : parent(_parent) {
		std::string cache_dir = std::string("./shader_cache/");
		std::string fname = cache_dir + std::to_string(hash_id) + std::string(".vkdat");
		filename = fname;
		createInfo = VkPipelineCacheCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, 0, nullptr };

		/*
			check for pre-existing cache file.
		*/
		std::ifstream cache(filename, std::ios::in);
		if (cache) {

			// get header (4 uint32_t, 16 int8_t) = (32 int8_t)
			std::vector<int8_t> header(32);
			cache.get(reinterpret_cast<char*>(header.data()), 32);
			
			// Check to see if header data matches current device.
			if (Verify(header)) {
				LOG(INFO) << "Found valid pipeline cache data with hash_id" << std::to_string(hash_id) << " .";
				std::string cache_str((std::istreambuf_iterator<char>(cache)), std::istreambuf_iterator<char>());
				uint32_t cache_size = static_cast<uint32_t>(cache_str.size() * sizeof(char));
				createInfo.initialDataSize = cache_size;
				createInfo.pInitialData = cache_str.data();
			}
			else {
				LOG(INFO) << "Pre-existing cache file isn't valid: creating new pipeline cache.";
				// header data doesn't match, wouldn't be valid. need to create new/fresh cache.
				createInfo.initialDataSize = 0;
				createInfo.pInitialData = nullptr;
			}
		}

		VkResult result = vkCreatePipelineCache(parent->vkHandle(), &createInfo, nullptr, &handle);
		VkAssert(result);
	}

	PipelineCache::~PipelineCache() {
		VkResult saved = saveToFile();
		VkAssert(saved);
		vkDeviceWaitIdle(parent->vkHandle());
		vkDestroyPipelineCache(parent->vkHandle(), handle, nullptr);
	}

	VkResult PipelineCache::saveToFile() const {
		VkResult result = VK_SUCCESS;
		size_t cache_size;

		if (!parent) {
			LOG(ERROR) << "Attempted to delete/save a non-existent cache!";
			return VK_ERROR_DEVICE_LOST;
		}

		// get data attached to active cache.
		result = vkGetPipelineCacheData(parent->vkHandle(), handle, &cache_size, nullptr);
		VkAssert(result);

		if (cache_size != 0) {
			std::ofstream file(filename, std::ios::out);
			void* cache_data;
			cache_data = malloc(cache_size);
			result = vkGetPipelineCacheData(parent->vkHandle(), handle, &cache_size, cache_data);
			VkAssert(result);
			file.write(reinterpret_cast<const char*>(cache_data), cache_size);
			file.close();
			free(cache_data);
			LOG(INFO) << "Saved pipeline cache data to file successfully";
			return VK_SUCCESS;
		}

		LOG(WARNING) << "Saving of pipeline cache to file failed with unindentified failure.";
		return VK_ERROR_VALIDATION_FAILED_EXT;
	}

	bool PipelineCache::Verify(const std::vector<int8_t>& cache_header) const {
		const uint32_t* header = reinterpret_cast<const uint32_t*>(cache_header.data());
		
		if (header[0] != 32) {
			return false;
		}

		if (header[1] != static_cast<uint32_t>(VK_PIPELINE_CACHE_HEADER_VERSION_ONE)) {
			return false;
		}

		auto& physical_device = parent->GetPhysicalDevice();


		if (header[1] != physical_device.Properties.vendorID) {
			return false;
		}

		if (header[2] != physical_device.Properties.deviceID) {
			return false;
		}

		const uint8_t* uuid = reinterpret_cast<const uint8_t*>(cache_header.data() + 4 * sizeof(uint32_t));

		if (uuid != physical_device.Properties.pipelineCacheUUID) {
			return false;
		}

		return true;
	}

	const VkPipelineCache& PipelineCache::vkHandle() const{
		return handle;
	}

}