#pragma once
#ifndef VULPES_VK_MEMORY_H
#define VULPES_VK_MEMORY_H
#include "stdafx.h"

namespace vulpes {

	// returns aligned offset given input alignment
	template<typename offset_type = VkDeviceSize, typename alignment_type = VkDeviceSize>
	constexpr static auto align(const offset_type& offset, const alignment_type& alignment)->VkDeviceSize {
		return std::ceil(static_cast<double>(offset) / static_cast<double>(alignment)) * static_cast<double>(alignment);
	}

	class MemoryAllocator;

	struct MemoryAllocation {
		VkDeviceSize Size, Offset;
	};

	/*
	
		Single item in the parent memory pool.
		
	*/

	struct MemoryItem {
		
		union {
			MemoryAllocator* Allocator = nullptr;

		};
	
		MemoryAllocation AllocInfo;
	};

}

#endif // !VULPES_VK_MEMORY_H
