#include "stdafx.h"
#include "imguiWrapper.h"
#include "engine\renderer\core\LogicalDevice.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\renderer\render\GraphicsPipeline.h"
#include "engine\renderer\render\Swapchain.h"
#include "engine\renderer\core\PhysicalDevice.h"
#include "engine\renderer\core\Instance.h"
#include "engine\renderer\render\MSAA.h"
#include "engine/renderer/resource/PipelineCache.h"

namespace vulpes {

	imguiWrapper::~imguiWrapper() {
		vkDestroyDescriptorSetLayout(device->vkHandle(), descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device->vkHandle(), descriptorPool, nullptr);
		vkDestroyPipelineLayout(device->vkHandle(), pipelineLayout, nullptr);
		vkDestroyImage(device->vkHandle(), fontImage, nullptr);
		vkDestroyImageView(device->vkHandle(), fontView, nullptr);
		vkDestroySampler(device->vkHandle(), fontSampler, nullptr);
		delete vbo;
		delete ebo;
		delete vert;
		delete frag;
		delete pipeline;
		cache.reset();
	}

	void imguiWrapper::Init(const Device * dvc, std::shared_ptr<PipelineCache> _cache, const VkRenderPass & renderpass, const Swapchain * swapchain) {
		device = dvc;
		cache = _cache;
		vbo = new Buffer(device);
		ebo = new Buffer(device);
		vert = new ShaderModule(device, "shaders/gui/ui.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		frag = new ShaderModule(device, "shaders/gui/ui.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		{
			std::array<VkDescriptorPoolSize, 11> pool_sizes{
				VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
			};
			VkDescriptorPoolCreateInfo pool_info = vk_descriptor_pool_create_info_base;
			pool_info.pPoolSizes = pool_sizes.data();
			pool_info.poolSizeCount = 1;
			pool_info.maxSets = 2;
			pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			VkResult result = vkCreateDescriptorPool(device->vkHandle(), &pool_info, nullptr, &descriptorPool);
			VkAssert(result);
		}

		// Load texture.
		ImGuiIO& io = ImGui::GetIO();

		unsigned char* pixels;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &imgWidth, &imgHeight);
		size_t data_size = imgWidth * imgHeight * 4 * sizeof(char);

		// Upload pixel data to staging/transfer buffer object
		Buffer::CreateStagingBuffer(device, data_size, transferBuffer, transferMemory);
		void* mapped;
		VkResult result = vkMapMemory(device->vkHandle(), transferMemory, 0, data_size, 0, &mapped);
		VkAssert(result);
		memcpy(mapped, pixels, data_size);
		vkUnmapMemory(device->vkHandle(), transferMemory);

		// Create image
		VkImageCreateInfo image_info = vk_image_create_info_base;
		image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
		image_info.extent = VkExtent3D{ static_cast<uint32_t>(imgWidth), static_cast<uint32_t>(imgHeight), 1 };
		image_info.mipLevels = 1;
		image_info.arrayLayers = 1;
		image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		result = vkCreateImage(device->vkHandle(), &image_info, nullptr, &fontImage);
		VkAssert(result);
		VkMemoryRequirements memreqs;
		vkGetImageMemoryRequirements(device->vkHandle(), fontImage, &memreqs);
		VkMemoryAllocateInfo alloc = vk_allocation_info_base;
		alloc.allocationSize = memreqs.size;
		alloc.memoryTypeIndex = device->GetMemoryTypeIdx(memreqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		result = vkAllocateMemory(device->vkHandle(), &alloc, nullptr, &fontMemory);
		VkAssert(result);
		result = vkBindImageMemory(device->vkHandle(), fontImage, fontMemory, 0);
		VkAssert(result);


		VkImageViewCreateInfo view_info = vk_image_view_create_info_base;
		view_info.image = fontImage;
		view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
		view_info.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		result = vkCreateImageView(device->vkHandle(), &view_info, nullptr, &fontView);
		VkAssert(result);


		VkSamplerCreateInfo sampler_info = vk_sampler_create_info_base;
		result = vkCreateSampler(device->vkHandle(), &sampler_info, nullptr, &fontSampler);
		VkAssert(result);

		
		VkDescriptorSetLayoutBinding layout_binding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &fontSampler };
		VkDescriptorSetLayoutCreateInfo layout_info = vk_descriptor_set_layout_create_info_base;
		layout_info.bindingCount = 1;
		layout_info.pBindings = &layout_binding;
		result = vkCreateDescriptorSetLayout(device->vkHandle(), &layout_info, nullptr, &descriptorSetLayout);
		VkAssert(result);
		

		VkDescriptorSetAllocateInfo set_alloc = vk_descriptor_set_alloc_info_base;
		set_alloc.descriptorPool = descriptorPool;
		set_alloc.descriptorSetCount = 1;
		set_alloc.pSetLayouts = &descriptorSetLayout;
		result = vkAllocateDescriptorSets(device->vkHandle(), &set_alloc, &descriptorSet);
		VkAssert(result);
		

		VkPushConstantRange push_constant{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4 };
		VkPipelineLayoutCreateInfo pipeline_info = vk_pipeline_layout_create_info_base;
		pipeline_info.pushConstantRangeCount = 1;
		pipeline_info.pPushConstantRanges = &push_constant;
		pipeline_info.pSetLayouts = &descriptorSetLayout;
		pipeline_info.setLayoutCount = 1;
		result = vkCreatePipelineLayout(device->vkHandle(), &pipeline_info, nullptr, &pipelineLayout);
		VkAssert(result);
		

		
		VkDescriptorImageInfo font_info;
		font_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		font_info.imageView = fontView;
		font_info.sampler = fontSampler;
		VkWriteDescriptorSet write_set{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &font_info, nullptr, nullptr };
		vkUpdateDescriptorSets(device->vkHandle(), 1, &write_set, 0, nullptr);
	
		std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
			vert->PipelineInfo(),
			frag->PipelineInfo()
		};

		static VkVertexInputBindingDescription bind_descr{ 0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX };

		static std::array<VkVertexInputAttributeDescription, 3> attr_descr{
			VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
			VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32_SFLOAT,  sizeof(float) * 2 },
			VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * 4 }
		};

		GraphicsPipelineInfo pipelineInfo;

		pipelineInfo.VertexInfo.vertexBindingDescriptionCount = 1;
		pipelineInfo.VertexInfo.pVertexBindingDescriptions = &bind_descr;
		pipelineInfo.VertexInfo.vertexAttributeDescriptionCount = 3;
		pipelineInfo.VertexInfo.pVertexAttributeDescriptions = attr_descr.data();

		pipelineInfo.RasterizationInfo.cullMode = VK_CULL_MODE_NONE;

		VkPipelineColorBlendAttachmentState color_blend{
			VK_TRUE,
			VK_BLEND_FACTOR_SRC_ALPHA,
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			VK_BLEND_OP_ADD,
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			VK_BLEND_FACTOR_ZERO,
			VK_BLEND_OP_ADD,
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
		};

		pipelineInfo.ColorBlendInfo.attachmentCount = 1;
		pipelineInfo.ColorBlendInfo.pAttachments = &color_blend;

		// Set this through dynamic state so we can do it when rendering.
		pipelineInfo.DynamicStateInfo.dynamicStateCount = 2;
		static const VkDynamicState states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		pipelineInfo.DynamicStateInfo.pDynamicStates = states;

		//pipelineInfo.DepthStencilInfo.depthTestEnable = VK_FALSE;
		pipelineInfo.DepthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		pipelineInfo.MultisampleInfo.rasterizationSamples = Multisampling::SampleCount;

		VkGraphicsPipelineCreateInfo pipeline_create_info = vk_graphics_pipeline_create_info_base;
		pipeline_create_info.flags = 0;
		pipeline_create_info.stageCount = 2;
		pipeline_create_info.pStages = shader_stages.data();
		pipeline_create_info.pInputAssemblyState = &pipelineInfo.AssemblyInfo;
		pipeline_create_info.pTessellationState = nullptr;
		pipeline_create_info.pViewportState = &pipelineInfo.ViewportInfo;
		pipeline_create_info.pRasterizationState = &pipelineInfo.RasterizationInfo;
		pipeline_create_info.pMultisampleState = &pipelineInfo.MultisampleInfo;
		pipeline_create_info.pVertexInputState = &pipelineInfo.VertexInfo;
		pipeline_create_info.pDepthStencilState = &pipelineInfo.DepthStencilInfo;
		pipeline_create_info.pColorBlendState = &pipelineInfo.ColorBlendInfo;
		pipeline_create_info.pDynamicState = &pipelineInfo.DynamicStateInfo;
		pipeline_create_info.layout = pipelineLayout;
		pipeline_create_info.renderPass = renderpass;
		pipeline_create_info.subpass = 0;
		pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
		pipeline_create_info.basePipelineIndex = -1;

		pipeline = new GraphicsPipeline(device, swapchain);
		pipeline->Init(pipeline_create_info, cache->vkHandle());

	}

	void imguiWrapper::UploadTextureData(CommandPool * transfer_pool) {
		// Transfer image data from transfer buffer onto the device.
		auto cmd = transfer_pool->StartSingleCmdBuffer();
		VkQueue queue = device->GraphicsQueue(0);
		VkImageMemoryBarrier image_barrier = vk_image_memory_barrier_base;
		image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier.srcAccessMask = 0;
		image_barrier.image = fontImage;
		image_barrier.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

		VkBufferImageCopy buff_copy{};
		buff_copy.imageSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		buff_copy.imageExtent = VkExtent3D{ static_cast<uint32_t>(imgWidth), static_cast<uint32_t>(imgHeight), 1 };
		vkCmdCopyBufferToImage(cmd, transferBuffer, fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buff_copy);

		image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);

		transfer_pool->EndSingleCmdBuffer(cmd, queue);
	}

	void imguiWrapper::NewFrame(Instance* instance, bool update_framegraph) {
		ImGui::NewFrame();
		auto* winptr = dynamic_cast<InstanceGLFW*>(instance)->Window;
		auto& io = ImGui::GetIO();

		for (auto i = 0; i < 3; i++){
			io.MouseDown[i] = mouseClick[i] || glfwGetMouseButton(winptr, i) != 0;
			mouseClick[i] = false;
		}

		if (instance->keys[GLFW_KEY_LEFT_ALT]) {
			double mouse_x, mouse_y;
			glfwGetCursorPos(winptr, &mouse_x, &mouse_y);
			io.MousePos = ImVec2(float(mouse_x), float(mouse_y));
			io.MouseDrawCursor = true;
			Instance::cameraLock = true;
			glfwSetInputMode(winptr, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
			io.WantCaptureMouse = true;
		}
		else {
			io.MousePos = ImVec2(-1, -1);
			Instance::cameraLock = false;
			io.WantCaptureMouse = false;
			io.MouseDrawCursor = false;
			glfwSetInputMode(winptr, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}

		ImVec4 clear_color = ImColor(114, 144, 154);

		ImGui::Begin("Debug");
		static float f = 0.0f;
		ImGui::Text("DiamondDogs");
		ImGui::Text(device->GetPhysicalDevice().Properties.deviceName);

		if (update_framegraph) {
			std::rotate(settings.frameTimes.begin(), settings.frameTimes.begin() + 1, settings.frameTimes.end());
			float frame_time = 1000.0f / (instance->frameTime);
			settings.frameTimes.back() = frame_time;
			if (frame_time < settings.frameTimeMin) {
				settings.frameTimeMin = frame_time;
			}
			if (frame_time > settings.frameTimeMax) {
				settings.frameTimeMax = frame_time;
			}
		}

		ImGui::PlotLines("Frame Timer", &settings.frameTimes[0], static_cast<int>(settings.frameTimes.size()), 0, "", settings.frameTimeMin, settings.frameTimeMax, ImVec2(0, 80));
		ImGui::InputFloat("Min frame time", &settings.frameTimeMin);
		ImGui::InputFloat("Max frame time", &settings.frameTimeMax);
		ImGui::Text("Camera");
		glm::vec3 pos = instance->GetCamPos();
		ImGui::InputFloat3("Position", glm::value_ptr(pos), 2);
		
		ImGui::SetNextWindowPos(ImVec2(50, 20), ImGuiSetCond_FirstUseEver);
		ImGui::End();
		
	}

	void imguiWrapper::UpdateBuffers() {
		ImDrawData* draw_data = ImGui::GetDrawData();

		VkDeviceSize vtx_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
		VkDeviceSize idx_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

		if (vbo->Size() != vtx_size) {
			if (vbo) {
				delete vbo;
			}
			
			vbo = new Buffer(device);
			vbo->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vtx_size);
		}

		if (ebo->Size() != idx_size) {
			if (ebo) {
				delete ebo;
			}
			ebo = new Buffer(device);
			ebo->CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, idx_size);
		}
		
		vbo->Map();
		ebo->Map();
		VkDeviceSize vtx_offset = 0, idx_offset = 0;
		for (int i = 0; i < draw_data->CmdListsCount; ++i) {
			const ImDrawList* list = draw_data->CmdLists[i];
			vbo->CopyTo(list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert), vtx_offset);
			vtx_offset += list->VtxBuffer.Size * sizeof(ImDrawVert);
			ebo->CopyTo(list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx), idx_offset);
			idx_offset += list->IdxBuffer.Size * sizeof(ImDrawIdx);
		}
		vbo->Unmap();
		ebo->Unmap();
	}

	void imguiWrapper::DrawFrame(VkCommandBuffer & cmd) {
		auto& io = ImGui::GetIO();

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());

		static const VkDeviceSize offsets[1]{ 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo->vkHandle(), offsets);
		vkCmdBindIndexBuffer(cmd, ebo->vkHandle(), 0, VK_INDEX_TYPE_UINT16);

		glm::vec4 push_constants = glm::vec4(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y, -1.0f, -1.0f);
		vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4, &push_constants);

		VkViewport viewport{ 0, 0, io.DisplaySize.x, io.DisplaySize.y, 0.0f, 1.0f };
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		ImDrawData* draw_data = ImGui::GetDrawData();
		int32_t vtx_offset = 0, idx_offset = 0;
		for (int32_t i = 0; i < draw_data->CmdListsCount; ++i) {
			const auto* cmd_list = draw_data->CmdLists[i];
			for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; ++j) {
				const auto* draw_cmd = &cmd_list->CmdBuffer[j];
				VkRect2D scissor;
				scissor.offset.x = std::max(static_cast<int32_t>(draw_cmd->ClipRect.x), 0);
				scissor.offset.y = std::max(static_cast<int32_t>(draw_cmd->ClipRect.y), 0);
				scissor.extent.width = static_cast<uint32_t>(draw_cmd->ClipRect.z - draw_cmd->ClipRect.x);
				scissor.extent.height = static_cast<uint32_t>(draw_cmd->ClipRect.w - draw_cmd->ClipRect.y);
				vkCmdSetScissor(cmd, 0, 1, &scissor);
				vkCmdDrawIndexed(cmd, draw_cmd->ElemCount, 1, idx_offset, vtx_offset, 0);
				idx_offset += draw_cmd->ElemCount;
			}
			vtx_offset += cmd_list->VtxBuffer.Size;
		}
	}

}
