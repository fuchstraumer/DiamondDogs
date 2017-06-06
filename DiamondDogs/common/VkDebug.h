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


	void DestroyDebugCallback(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* allocation_callbacks = nullptr) {
		auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
		if (func != nullptr) {
			func(instance, callback, allocation_callbacks);
		}
	}

	void CreateDebugCallback(const VkInstance& instance, const VkDebugReportFlagsEXT& flags, VkDebugReportCallbackEXT* callback, const VkAllocationCallbacks* allocators = nullptr) {

		auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
		if (func == nullptr) {
			std::cerr << "VKDEBUG::CREATE_DEBUG_CALLBACK: Debug layer and/or extension not enabled for VkInstance given in CreateDebugCallback." << std::endl;
			throw(std::runtime_error("Debug layer and/or extension not enabled for VkInstance given in CreateDebugCallback"));
		}

		VkDebugReportCallbackCreateInfoEXT create_info = vk_debug_callback_create_info_base;
		create_info.flags = flags;
		create_info.pfnCallback = VkBaseDebugCallbackPFN;

		VkResult err = func(instance, &create_info, allocators, callback);
		VkAssert(err);
	}

}

#endif // !VULPES_VK_DEBUG_H
