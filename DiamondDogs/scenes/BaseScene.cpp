#include "stdafx.h"
#include "BaseScene.h"

#include "engine\renderer\core\Instance.h"
#include "engine\renderer\core\LogicalDevice.h"
#include "engine\renderer\core\PhysicalDevice.h"
#include "engine\renderer\render\Swapchain.h"
#include "engine\renderer\render\Renderpass.h"
#include "engine\renderer\render\Framebuffer.h"
#include "engine\renderer\command\CommandPool.h"
#include "engine\renderer\render\DepthStencil.h"
#include "engine\util\AABB.h"

vulpes::BaseScene::BaseScene(const size_t& num_secondary_buffers, const uint32_t& _width, const uint32_t& _height) : width(_width), height(_height) {

	VkInstanceCreateInfo create_info = vk_base_instance_info;
	instance = new InstanceGLFW(create_info, false);
	glfwSetWindowUserPointer(instance->Window, this);
	instance->SetupPhysicalDevices();
	instance->SetupSurface();

	device = new Device(instance, instance->physicalDevice);

	swapchain = new Swapchain();
	swapchain->Init(instance, instance->physicalDevice, device);

	CreateCommandPools(num_secondary_buffers * swapchain->ImageCount);
	SetupRenderpass();
	SetupDepthStencil();

	util::AABB::cache = std::make_unique<PipelineCache>(device, static_cast<uint16_t>(typeid(util::AABB).hash_code()));
	util::AABB::SetupRenderData(device, renderPass->vkHandle(), swapchain, instance->GetProjectionMatrix());

	VkSemaphoreCreateInfo semaphore_info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
	VkResult result = vkCreateSemaphore(device->vkHandle(), &semaphore_info, nullptr, &semaphores[0]);
	VkAssert(result);
	result = vkCreateSemaphore(device->vkHandle(), &semaphore_info, nullptr, &semaphores[1]);
	VkAssert(result);
}

vulpes::BaseScene::~BaseScene() {
	util::AABB::CleanupVkResources();
	for (const auto& fbuf : framebuffers) {
		vkDestroyFramebuffer(device->vkHandle(), fbuf, nullptr);
	}
	delete gui;
	delete swapchain;
	delete renderPass;
	delete secondaryPool;
	delete transferPool;
	delete graphicsPool;
	vkDestroySemaphore(device->vkHandle(), semaphores[1], nullptr);
	vkDestroySemaphore(device->vkHandle(), semaphores[0], nullptr);
	delete device;
	delete instance;
}

void vulpes::BaseScene::CreateCommandPools(const size_t& num_secondary_buffers) {
	VkCommandPoolCreateInfo pool_info = vk_command_pool_info_base;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = device->QueueFamilyIndices.Graphics;
	graphicsPool = new CommandPool(device, pool_info);

	VkCommandBufferAllocateInfo alloc_info = vk_command_buffer_allocate_info_base;
	graphicsPool->CreateCommandBuffers(swapchain->ImageCount, alloc_info);

	pool_info.queueFamilyIndex = device->QueueFamilyIndices.Graphics;
	transferPool = new CommandPool(device, pool_info);
	transferPool->CreateCommandBuffers(1);

	pool_info.queueFamilyIndex = device->QueueFamilyIndices.Graphics;
	secondaryPool = new CommandPool(device, pool_info);
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
	secondaryPool->CreateCommandBuffers(swapchain->ImageCount * num_secondary_buffers, alloc_info);
}

void vulpes::BaseScene::SetupRenderpass() {
	VkAttachmentDescription attach_descr = vk_attachment_description_base;
	attach_descr.format = swapchain->ColorFormat;
	attach_descr.samples = VK_SAMPLE_COUNT_1_BIT;
	attach_descr.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attach_descr.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attach_descr.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attach_descr.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attach_descr.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attach_descr.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentDescription depth_descr = {
		0,
		device->FindDepthFormat(),
		VK_SAMPLE_COUNT_1_BIT,
		VK_ATTACHMENT_LOAD_OP_CLEAR,
		VK_ATTACHMENT_STORE_OP_DONT_CARE,
		VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		VK_ATTACHMENT_STORE_OP_DONT_CARE,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	VkAttachmentReference attach_ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depth_ref{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkSubpassDescription su_descr = vk_subpass_description_base;
	su_descr.flags = 0;
	su_descr.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	su_descr.inputAttachmentCount = 0;
	su_descr.pInputAttachments = nullptr;
	su_descr.colorAttachmentCount = 1;
	su_descr.pColorAttachments = &attach_ref;
	su_descr.pResolveAttachments = nullptr;
	su_descr.pDepthStencilAttachment = &depth_ref;
	su_descr.preserveAttachmentCount = 0;
	su_descr.pPreserveAttachments = nullptr;

	VkSubpassDependency su_depend;
	su_depend.srcSubpass = VK_SUBPASS_EXTERNAL;
	su_depend.dstSubpass = 0;
	su_depend.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	su_depend.srcAccessMask = 0;
	su_depend.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	su_depend.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	su_depend.dependencyFlags = 0;

	std::array<VkAttachmentDescription, 2> decriptions{ attach_descr, depth_descr };
	VkRenderPassCreateInfo rp_info = vk_render_pass_create_info_base;
	rp_info.attachmentCount = static_cast<uint32_t>(decriptions.size());
	rp_info.pAttachments = decriptions.data();
	rp_info.subpassCount = 1;
	rp_info.pSubpasses = &su_descr;
	rp_info.dependencyCount = 1;
	rp_info.pDependencies = &su_depend;

	renderPass = new Renderpass(device, rp_info);
}

void vulpes::BaseScene::SetupDepthStencil(){
	VkQueue depth_queue = device->GraphicsQueue(0);
	depthStencil = new DepthStencil(device, VkExtent3D{ swapchain->Extent.width, swapchain->Extent.height, 1 }, transferPool, depth_queue);
}

void vulpes::BaseScene::SetupFramebuffers(){
	for (uint32_t i = 0; i < swapchain->ImageCount; ++i) {
		VkFramebufferCreateInfo f_info = vk_framebuffer_create_info_base;
		f_info.renderPass = *renderPass;
		std::array<VkImageView, 2> attachments{ swapchain->ImageViews[i], depthStencil->View() };
		f_info.attachmentCount = static_cast<uint32_t>(attachments.size());
		f_info.pAttachments = attachments.data();
		f_info.width = swapchain->Extent.width;
		f_info.height = swapchain->Extent.height;
		f_info.layers = 1;
		framebuffers.push_back(std::move(Framebuffer(device, f_info)));
	}
}

void vulpes::BaseScene::RecreateSwapchain(const bool& windowed_fullscreen){
	// First wait to make sure nothing is in use.
	vkDeviceWaitIdle(device->vkHandle());

	depthStencil->Destroy();

	framebuffers.clear();
	framebuffers.shrink_to_fit();

	transferPool->FreeCommandBuffers();
	graphicsPool->FreeCommandBuffers();
	size_t num_secondary_buffers = secondaryPool->size();
	secondaryPool->FreeCommandBuffers();

	WindowResized();

	renderPass->Destroy();

	swapchain->Destroy();

	/*
		Done destroying resources, recreate resources and objects now
	*/

	swapchain->Recreate();

	graphicsPool->CreateCommandBuffers(swapchain->ImageCount);
	transferPool->CreateCommandBuffers(1);
	secondaryPool->CreateCommandBuffers(num_secondary_buffers);
	SetupRenderpass();
	RecreateObjects();
	SetupDepthStencil();
	SetupFramebuffers();
	RecordCommands();

	vkDeviceWaitIdle(device->vkHandle());
}

float vulpes::BaseScene::GetFrameTime(){
	return frameTime;
}
