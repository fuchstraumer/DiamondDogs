#pragma once
#ifndef VULPES_VK_RESOURCE_BASE_H
#define VULPES_VK_RESOURCE_BASE_H

#include "stdafx.h"
#include "engine\renderer\ForwardDecl.h"
#include "engine\renderer\core\LogicalDevice.h"
#include "engine\util\ref_counter.h"
namespace vulpes {

	namespace detail {

		class vkResource : public ref_counter {
		public:
			vkResource() = default;
			vkResource(const VkDevice& device) : parent(&device) {}

			VkDevice& Device() const noexcept;
		protected:
			std::shared_ptr<const VkDevice> parent;
		};

	}

}

#endif // !VULPES_VK_RESOURCE_BASE_H
