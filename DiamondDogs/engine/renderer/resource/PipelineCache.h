#pragma once
#ifndef VULPES_VK_PIPELINE_CACHE_H
#define VULPES_VK_PIPELINE_CACHE_H

#include "stdafx.h"
#include "../ForwardDecl.h"
#include "../NonCopyable.h"

namespace vulpes {

	class PipelineCache {
		PipelineCache(const PipelineCache& other) = delete;
		PipelineCache& operator=(const PipelineCache& other) = delete;
	public:

		// hash_id used to group pipeline caches by class/type managing the cache.
		PipelineCache(const Device* parent, const uint16_t& hash_id);

		~PipelineCache();

		// reads data from file and verifies that its appropriate for the current 
		// device/system
		bool Verify(const std::vector<int8_t>& cache_header) const;

		const VkPipelineCache& vkHandle() const;

	private:

		VkResult saveToFile() const;

		std::string filename;
		const Device* parent;
		VkPipelineCache handle;
		VkPipelineCacheCreateInfo createInfo;
	};

}

#endif // !VULPES_VK_PIPELINE_CACHE_H
