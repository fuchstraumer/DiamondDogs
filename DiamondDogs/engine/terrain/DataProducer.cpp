#include "stdafx.h"
#include "DataProducer.h"
#include "engine/renderer/core/LogicalDevice.h"
#include "engine/renderer/resource/Texture.h"
#include "HeightNode.h"
#include "engine/renderer/resource/ShaderModule.h"
#include "engine/renderer/render/GraphicsPipeline.h"

namespace vulpes {

	size_t DataProducer::numProducers = 0;

	bool DataRequest::Complete() const {
		return Status == RequestStatus::Complete;
	}

	Buffer* vulpes::DataRequest::GetData() {
		if (!Complete()) {
			throw std::runtime_error("Tried to access data before it was generated.");
		}

		return Result.release();
	}

	bool DataRequest::operator<(const DataRequest & other) const {
		return node->GridCoords().z < other.node->GridCoords().z;
	}

	DataProducer::DataProducer(const Device * _parent) : parent(_parent) {
		
		pipelineCache = std::make_unique<PipelineCache>(parent, typeid(DataProducer).hash_code() + numProducers);

		// we offset the hash used for pipeline cache by this: that way,
		// each producer instance gets its own cache (so, split instances among distinct task types to leverage cache)
		++numProducers; 

		// set up queues
		if (parent->HasDedicatedComputeQueues()) {
			for (uint32_t i = 0; i < parent->numComputeQueues; ++i) {
				availQueues.push_front(parent->ComputeQueue(i));
			}
		}
		else {

			assert(parent->numGraphicsQueues >= 2);
			// Check if we have more than one graphics queue
			for (uint32_t i = parent->numGraphicsQueues / 2; i < parent->numGraphicsQueues; ++i) {
				availQueues.push_front(parent->GraphicsQueue(i));
			}
			
		}

		VkCommandPoolCreateInfo pool_info = vk_command_pool_info_base;
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pool_info.queueFamilyIndex = parent->QueueFamilyIndices.Compute;

		computePool = std::make_unique<CommandPool>(parent, pool_info);
		computePool->CreateCommandBuffers(availQueues.size());

		spareQueue = parent->GetGeneralQueue();
		pool_info.queueFamilyIndex = parent->QueueFamilyIndices.Graphics;

		transferPool = std::make_unique<CommandPool>(parent, pool_info);
		transferPool->CreateCommandBuffers(1);

		fences.resize(availQueues.size());

		pipelines.resize(availQueues.size());
		VkResult result = VK_SUCCESS;
		for (size_t i = 0; i < availQueues.size(); ++i) {
			result = vkCreateFence(parent->vkHandle(), &vk_fence_create_info_base, nullptr, &fences[i]);
			VkAssert(result);
			
		}

		semaphores.resize(availQueues.size());
		VkSemaphoreCreateInfo semaphore_info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
		for (size_t i = 0; i < semaphores.size(); ++i) {
			result = vkCreateSemaphore(parent->vkHandle(), &semaphore_info, nullptr, &semaphores[i]);
			VkAssert(result);
		}
		/*
			setup descriptors, layouts, bindings, pools, etc
		*/

		static const std::array<VkDescriptorPoolSize, 2> pool_sizes{
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 },
		};

		VkDescriptorPoolCreateInfo dsc_pool_info = vk_descriptor_pool_create_info_base;
		dsc_pool_info.poolSizeCount = 2;
		dsc_pool_info.pPoolSizes = pool_sizes.data();
		dsc_pool_info.maxSets = 1;
		
		result = vkCreateDescriptorPool(parent->vkHandle(), &dsc_pool_info, nullptr, &descriptorPool);
		VkAssert(result);

		static const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
			VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		};

		VkDescriptorSetLayoutCreateInfo set_layout_info = vk_descriptor_set_layout_create_info_base;
		set_layout_info.bindingCount = 2;
		set_layout_info.pBindings = bindings.data();

		result = vkCreateDescriptorSetLayout(parent->vkHandle(), &set_layout_info, nullptr, &descriptorSetLayout);
		VkAssert(result);

		VkPipelineLayoutCreateInfo pipeline_layout_info = vk_pipeline_layout_create_info_base;
		pipeline_layout_info.pSetLayouts = &descriptorSetLayout;
		pipeline_layout_info.setLayoutCount = 1;

		result = vkCreatePipelineLayout(parent->vkHandle(), &pipeline_layout_info, nullptr, &pipelineLayout);
		VkAssert(result);

		VkDescriptorSetAllocateInfo dscr_alloc_info = vk_descriptor_set_alloc_info_base;
		dscr_alloc_info.descriptorPool = descriptorPool;
		dscr_alloc_info.descriptorSetCount = 1;
		dscr_alloc_info.pSetLayouts = &descriptorSetLayout;

		result = vkAllocateDescriptorSets(parent->vkHandle(), &dscr_alloc_info, &descriptorSet);
		VkAssert(result);
	}

	DataProducer::~DataProducer() {

		vkDestroyPipelineLayout(parent->vkHandle(), pipelineLayout, nullptr);
		vkFreeDescriptorSets(parent->vkHandle(), descriptorPool, 1, &descriptorSet);
		vkDestroyDescriptorSetLayout(parent->vkHandle(), descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(parent->vkHandle(), descriptorPool, nullptr);
		

		for (auto&& fence : fences) {
			vkDestroyFence(parent->vkHandle(), fence, nullptr);
		}

		for (auto&& semaphore : semaphores) {
			vkDestroySemaphore(parent->vkHandle(), semaphore, nullptr);
		}

		pipelineCache.reset();
	}

	void DataProducer::Request(DataRequest * req) {
		// add to back, to preserve priority.
		requests.push_back(req);
		
	}

	size_t DataProducer::RecordCommands() {	

		if (requests.empty()) {
			return 0;
		}

		requests.sort([&](const DataRequest* r0, const DataRequest* r1) { return *r0 < *r1; });

		size_t submitted = 0;

		VkCommandBufferBeginInfo begin_info = vk_command_buffer_begin_info_base;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		VkBufferMemoryBarrier host_transition_barrier{
			VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			nullptr,
			VK_ACCESS_TRANSFER_WRITE_BIT, // Result buffer is written to by transfer
			VK_ACCESS_HOST_READ_BIT, 
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			VK_NULL_HANDLE,
			0,
			VK_WHOLE_SIZE,
		};

		VkBufferMemoryBarrier compute_complete_barrier{
			VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			nullptr,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT, // Output buffer is read for transfer
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			VK_NULL_HANDLE,
			0,
			VK_WHOLE_SIZE
		};

		VkBufferCopy output_result_copy{
			0,
			0,
			VK_WHOLE_SIZE,
		};

		std::array<VkWriteDescriptorSet, 2> write_dscr{
			VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, nullptr, nullptr },
			VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, nullptr, nullptr },
		};

		std::forward_list<DataRequest*> transfer_list;

		for (auto iter = availQueues.begin(); iter != availQueues.end(); ++iter) {
			if (requests.empty()) {
				break;
			}

			// Pop current request off of list
			auto req = requests.front();
			requests.pop_front();

			/*
				Update write descriptors
			*/
			auto input_info = req->Input->GetDescriptor();
			auto output_info = req->Output->GetDescriptor();
			write_dscr[0].pBufferInfo = &input_info;
			write_dscr[1].pBufferInfo = &output_info;
			vkUpdateDescriptorSets(parent->vkHandle(), 2, write_dscr.data(), 0, nullptr);

			auto& pipeline_info = req->pipelineCreateInfo;
			pipeline_info.layout = pipelineLayout;
			VkResult result = vkCreateComputePipelines(parent->vkHandle(), pipelineCache->vkHandle(), 1, &req->pipelineCreateInfo, nullptr, &pipelines[submitted]);
			VkAssert(result);

			// should be transferred before we get here, if not we have one spare general-use queue.
			if (req->Status == RequestStatus::NeedsTransfer) {
				transfer_list.push_front(req);
			}


			/*
				Record commands
			*/
			auto cmd = computePool->GetCmdBuffer(submitted);
			result = vkBeginCommandBuffer(cmd, &begin_info);
			VkAssert(result);
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[submitted]);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
			vkCmdDispatch(cmd, ceil(req->Width / 16), ceil(req->Height / 16), 1);
			compute_complete_barrier.buffer = req->Input->vkHandle();
			compute_complete_barrier.size = req->Input->Size();
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &compute_complete_barrier, 0, nullptr);
			output_result_copy.size = req->Output->Size();
			vkCmdCopyBuffer(cmd, req->Output->vkHandle(), req->Result->vkHandle(), 1, &output_result_copy);
			host_transition_barrier.buffer = req->Result->vkHandle();
			host_transition_barrier.size = req->Result->Size();
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host_transition_barrier, 0, nullptr);
			result = vkEndCommandBuffer(cmd);
			VkAssert(result);
			submittedRequests.push_front(req);
			++submitted;
		}

		VkResult result = vkBeginCommandBuffer(transferPool->GetCmdBuffer(0), &begin_info);
		VkAssert(result);
		while (!transfer_list.empty()) {
			auto req = transfer_list.front();
			transfer_list.pop_front();
			if (req->parent->samples.empty()) {
				continue;
			}
			auto input_heights = req->parent->GetHeights();
			req->Input->CopyTo(input_heights.data(), transferPool->GetCmdBuffer(0), sizeof(glm::vec2) * input_heights.size(), 0);
		}
		result = vkEndCommandBuffer(transferPool->GetCmdBuffer(0));
		VkAssert(result);
	
		
		return submitted;
	}

	size_t DataProducer::Submit() {

		if (requests.empty()) {
			return 0;
		}

		size_t submitted = 0;

		VkSubmitInfo submit_info = vk_submit_info_base;
		submit_info.commandBufferCount = 1;
		VkResult result = VK_SUCCESS;

		{
			// will signal all semaphores when complete
			submit_info.signalSemaphoreCount = static_cast<uint32_t>(semaphores.size());
			submit_info.pSignalSemaphores = semaphores.data();
			submit_info.pCommandBuffers = &transferPool->GetCmdBuffer(0);
			result = vkQueueSubmit(spareQueue, 1, &submit_info, VK_NULL_HANDLE);
			VkAssert(result);
		}

		VkPipelineStageFlags stage_flag = VK_PIPELINE_STAGE_TRANSFER_BIT;
		submit_info.pWaitDstStageMask = &stage_flag;

		for (auto iter = availQueues.begin(); iter != availQueues.end(); ++iter) {
			if (submittedRequests.empty()) {
				break;
			}
			// queues execute independently when their corresponding semaphore is signaled,
			// indicated resources have been transferred and work can continue.
			submit_info.pCommandBuffers = &computePool->GetCmdBuffer(submitted);
			submit_info.waitSemaphoreCount = 1;
			submit_info.pWaitSemaphores = &semaphores[submitted];
			result = vkQueueSubmit(*iter, 1, &submit_info, fences[submitted]);
			VkAssert(result);
			++submitted;
			submittedRequests.front()->Status = RequestStatus::Complete;
			submittedRequests.pop_front();
		}

		VkResult submit_result = VK_SUCCESS;
		submit_result = vkWaitForFences(parent->vkHandle(), static_cast<uint32_t>(fences.size()), fences.data(), VK_TRUE, vk_default_fence_timeout);
		VkAssert(submit_result);

		submit_result = vkResetCommandPool(parent->vkHandle(), computePool->vkHandle(), VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
		VkAssert(submit_result);
		submit_result = vkResetCommandBuffer(transferPool->GetCmdBuffer(0), VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
		VkAssert(submit_result);
		submit_result = vkResetFences(parent->vkHandle(), static_cast<uint32_t>(fences.size()), fences.data());
		VkAssert(submit_result);

		// destroy pipelines
		for (auto& pipeline : pipelines) {
			vkDestroyPipeline(parent->vkHandle(), pipeline, nullptr);
			pipeline = VK_NULL_HANDLE;
		}

		// cleanup any staging resources used.
		Buffer::DestroyStagingResources(parent);
	
		return submitted;
	}

	bool DataProducer::Complete() const {
		return requests.empty();
	}

	
	std::shared_ptr<DataRequest> DataRequest::UpsampleRequest(terrain::HeightNode * node, const terrain::HeightNode* parent, const Device * dvc) {

		auto request = std::make_shared<DataRequest>();

		request->Input = std::make_unique<Buffer>(dvc);
		request->Input->CreateBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(glm::vec2) * parent->NumSamples());
		request->Output = std::make_unique<Buffer>(dvc);
		request->Output->CreateBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(glm::vec2) * node->NumSamples());
		request->Result = std::make_unique<Buffer>(dvc);
		request->Result->CreateBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(glm::vec2) * node->NumSamples());
		request->node = node;
		request->parent = parent;
		request->Width = request->Height = sqrt(node->NumSamples());

		request->specializations = {
			VkSpecializationMapEntry{ 0, 0, sizeof(int) },
			VkSpecializationMapEntry{ 1, sizeof(int), sizeof(int) },
			VkSpecializationMapEntry{ 2, 2 * sizeof(int), sizeof(int) },
			VkSpecializationMapEntry{ 3, 3 * sizeof(int), sizeof(int) },
		};

		request->specData = glm::ivec4(node->MeshGridSize() + 5, node->MeshGridSize(), node->GridCoords().x, node->GridCoords().y);

		request->specializationInfo = VkSpecializationInfo{
			static_cast<uint32_t>(request->specializations.size()),
			request->specializations.data(),
			sizeof(glm::ivec4),
			&request->specData
		};

		request->Shader = new ShaderModule(dvc, "shaders/terrain/compute/upsample.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT, "main");
		request->shaderInfo = vk_pipeline_shader_stage_create_info_base;
		request->shaderInfo.module = request->Shader->vkHandle();
		request->shaderInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		request->shaderInfo.pName = "main";
		request->shaderInfo.pSpecializationInfo = &request->specializationInfo;

		VkComputePipelineCreateInfo compute_info{
			VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			nullptr,
			0,
			request->shaderInfo,
			VK_NULL_HANDLE,
			VK_NULL_HANDLE,
			-1,
		};

		request->pipelineCreateInfo = compute_info;
		request->Status = RequestStatus::NeedsTransfer;
		return request;
	}

}
