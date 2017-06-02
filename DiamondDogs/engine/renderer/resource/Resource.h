#pragma once
#ifndef VULPES_VK_RESOURCE_H
#define VULPES_VK_RESOURCE_H
#include "stdafx.h"

namespace vulpes {

	namespace detail {

		/*
			Base class representing a resource that belongs to a parent device.
		*/
		
		class Device;

		class vkResource {
			vkResource(const vkResource& other) = delete;
			vkResource& operator=(const vkResource& other) = delete;
		public:
			vkResource(const Device* dvc) : device(dvc) {};
		protected:
			const Device* device;
		};

		template<typename handle_type>
		class vk_handle {
			vk_handle(const vk_handle& other) = delete;
			vk_handle(vk_handle&& other) : handle(std::move(other.handle)) { other.reset(); }
			vk_handle& operator=(const vk_handle& other) = delete;
			vk_handle& operator=(vk_handle&& other) { handle = std::move(other.handle); other.reset(); return *this; }
		public:
			const handle_type& vkHandle() const noexcept {
				return handle;
			}
			
			void reset() {
				handle = VK_NULL_HANDLE;
			}

		protected:
			handle_type handle;
		};


	}

}
#endif // !VULPES_VK_RESOURCE_H
