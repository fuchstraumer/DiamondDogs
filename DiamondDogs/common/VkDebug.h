#pragma once
#ifndef VULPES_VK_DEBUG_H
#define VULPES_VK_DEBUG_H

#include "stdafx.h"

namespace vulpes {

	static VKAPI_ATTR VkBool32 VKAPI_CALL VkBaseDebugCallbackPFN(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char* layerPrefix, const char* msg, void* userData) {

		std::string prefix("");

		if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
			prefix += "ERROR:";
		}
		if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
			prefix += "WARNING:";
		}
		if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
			prefix += "INFORMATION:";
		}
		if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
			prefix += "PERFORMANCE_WARNING:";
		}
		// These come from the loader and validation layers: prefixed with VK_DEBUG to represent state as debug info from within vulkan itself
		if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
			prefix += "VK_DEBUG:";
		}

		std::stringstream debug_msg;
		debug_msg << prefix << " [" << layerPrefix << " ] Code: " << code << " : " << msg;
		if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
			std::cerr << debug_msg.str() << std::endl;
		}
		else {
			std::cout << debug_msg.str() << std::endl;
		}


		return VK_FALSE;
	}


	void DestroyDebugCallback(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* allocation_callbacks = nullptr);

	void CreateDebugCallback(const VkInstance& instance, const VkDebugReportFlagsEXT& flags, VkDebugReportCallbackEXT* callback, const VkAllocationCallbacks* allocators = nullptr);

}

#endif // !VULPES_VK_DEBUG_H
