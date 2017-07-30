#include "stdafx.h"
#include "DataProducer.h"
#include "core/LogicalDevice.h"
#include "resource/Texture.h"
#include "resource/ShaderModule.h"
#include "render/GraphicsPipeline.h"
#include "HeightNode.h"

namespace vulpes {


	size_t DataProducer::numProducers = 0;

#ifndef NDEBUG
	constexpr bool save_requests_to_file = true;
#else
	constexpr bool save_requests_to_file = false;
#endif // DEBUG


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
		
		pipelineCache = std::make_unique<PipelineCache>(parent, static_cast<int16_t>(typeid(DataProducer).hash_code() + numProducers));

		// we offset the hash used for pipeline cache by this: that way,
		// each producer instance gets its own cache (so, split instances among distinct task types to leverage cache)
		++numProducers; 
		
		createQueues();
		pipelines.resize(availQueues.size());
		pipelineInfos.resize(availQueues.size());
		createCommandPool();
		createFences();
		createSemaphores();
		setupDescriptors();
		createPipelineLayout();
		allocateDescriptors();
		createBarriers();

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

	void DataProducer::createQueues() {

		if (parent->HasDedicatedComputeQueues()) {
			for (uint32_t i = 0; i < parent->NumComputeQueues; ++i) {
				availQueues.push_front(parent->ComputeQueue(i));
			}
		}
		else {
			// Worst-case scenario, also occures when using a debugger like RenderDoc.
			if (parent->NumGraphicsQueues <= 2) {
				availQueues.push_back(parent->GraphicsQueue(0));
			}
			else {
				for (uint32_t i = parent->NumGraphicsQueues / 2; i < parent->NumGraphicsQueues; ++i) {
					availQueues.push_front(parent->GraphicsQueue(i));
				}
			}
		}

	}

	void DataProducer::createCommandPool() {
		
		VkCommandPoolCreateInfo pool_info = vk_command_pool_info_base;
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pool_info.queueFamilyIndex = parent->QueueFamilyIndices.Compute;

		computePool = std::make_unique<CommandPool>(parent, pool_info, true);
		computePool->AllocateCmdBuffers(static_cast<uint32_t>(availQueues.size()));

		spareQueue = parent->GeneralQueue(0);
		pool_info.queueFamilyIndex = parent->QueueFamilyIndices.Graphics;

		transferPool = std::make_unique<CommandPool>(parent, pool_info, true);
		transferPool->AllocateCmdBuffers(1);

		fences.resize(availQueues.size());

	}

	void DataProducer::createFences() {
		
		for (size_t i = 0; i < availQueues.size(); ++i) {
			VkResult result = vkCreateFence(parent->vkHandle(), &vk_fence_create_info_base, nullptr, &fences[i]);
			VkAssert(result);
		}

	}

	void DataProducer::createSemaphores() {
		
		semaphores.resize(availQueues.size());
		VkSemaphoreCreateInfo semaphore_info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };

		for (size_t i = 0; i < semaphores.size(); ++i) {
			VkResult result = vkCreateSemaphore(parent->vkHandle(), &semaphore_info, nullptr, &semaphores[i]);
			VkAssert(result);
		}
	}

	void DataProducer::setupDescriptors() {
		
		static const std::array<VkDescriptorPoolSize, 2> pool_sizes{
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 },
		};

		VkDescriptorPoolCreateInfo dsc_pool_info = vk_descriptor_pool_create_info_base;
		dsc_pool_info.poolSizeCount = 2;
		dsc_pool_info.pPoolSizes = pool_sizes.data();
		dsc_pool_info.maxSets = 1;

		VkResult result = vkCreateDescriptorPool(parent->vkHandle(), &dsc_pool_info, nullptr, &descriptorPool);
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

		writeDescriptors = 
		{
			VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, nullptr, nullptr },
			VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, nullptr, nullptr },
		};
	}

	void DataProducer::createPipelineLayout() {
		
		VkPipelineLayoutCreateInfo pipeline_layout_info = vk_pipeline_layout_create_info_base;
		pipeline_layout_info.pSetLayouts = &descriptorSetLayout;
		pipeline_layout_info.setLayoutCount = 1;

		VkResult result = vkCreatePipelineLayout(parent->vkHandle(), &pipeline_layout_info, nullptr, &pipelineLayout);
		VkAssert(result);
	}

	void DataProducer::allocateDescriptors() {

		VkDescriptorSetAllocateInfo dscr_alloc_info = vk_descriptor_set_alloc_info_base;
		dscr_alloc_info.descriptorPool = descriptorPool;
		dscr_alloc_info.descriptorSetCount = 1;
		dscr_alloc_info.pSetLayouts = &descriptorSetLayout;

		VkResult result = vkAllocateDescriptorSets(parent->vkHandle(), &dscr_alloc_info, &descriptorSet);
		VkAssert(result);

		// Setting this upon creation of these objects won't update them when descriptorSet is valid: has to be done after descriptorSet is made valid.
		writeDescriptors[0].dstSet = descriptorSet;
		writeDescriptors[1].dstSet = descriptorSet;
	}

	void DataProducer::createBarriers() {
		
		computeCompleteBarrier = VkBufferMemoryBarrier{
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

		hostTransitionBarrier = VkBufferMemoryBarrier{
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
	}

	void DataProducer::Request(DataRequest * req) {
		requests.push_back(req);
	}

	void DataProducer::PrepareSubmissions() {	
		if (requests.empty()) {
			return;
		}

		size_t curr_request_idx = 0;
		std::forward_list<DataRequest*> transfer_list;

		// First step sets things up so we can create the pipelines all in one go 
		for (auto iter = availQueues.begin(); iter != availQueues.end(); ++iter) {

			// Happens if we have fewer requests than currently available queues.
			if (requests.empty() || (submittedRequests.size() == availQueues.size())) {
				break;
			}

			auto req = requests.front();
			requests.pop_front();

			if (req->Status == RequestStatus::NeedsTransfer) {
				transfer_list.push_front(req);
			}

			updateWriteDescriptors(req);
			updatePipelineInfo(req, curr_request_idx);
			submittedRequests.push_back(req);
			++curr_request_idx;
		}

		assert(submittedRequests.size() <= availQueues.size());

		createPipelines();

		// Pipelines created, record commands.
		curr_request_idx = 0;
		for (auto iter = submittedRequests.begin(); iter != submittedRequests.end(); ++iter) {
			auto req = *iter;
			updateBarriers(req);
			recordCommands(req, curr_request_idx);
			++curr_request_idx;
		}

		uploadRequestData(transfer_list);

	}

	void DataProducer::updateWriteDescriptors(const DataRequest* request) {
		VkDescriptorBufferInfo input_info = request->Input->GetDescriptor();
		VkDescriptorBufferInfo output_info = request->Output->GetDescriptor();
		writeDescriptors[0].pBufferInfo = &input_info;
		writeDescriptors[1].pBufferInfo = &output_info;
		vkUpdateDescriptorSets(parent->vkHandle(), 2, writeDescriptors.data(), 0, nullptr);
	}

	void DataProducer::updatePipelineInfo(const DataRequest * request, size_t curr_req_idx) {

		pipelineInfos[curr_req_idx] = request->pipelineCreateInfo;
		// Layout overwritten by copy, update again.
		pipelineInfos[curr_req_idx].layout = pipelineLayout;

		if (curr_req_idx == 0) {
			pipelineInfos[curr_req_idx].flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
		}
		else {
			pipelineInfos[curr_req_idx].flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
			pipelineInfos[curr_req_idx].basePipelineIndex = 0;
		}

	}

	void DataProducer::createPipelines() {

		VkResult result = vkCreateComputePipelines(parent->vkHandle(), pipelineCache->vkHandle(), static_cast<uint32_t>(pipelineInfos.size()), pipelineInfos.data(), nullptr, pipelines.data());
		VkAssert(result);

	}

	void DataProducer::updateBarriers(const DataRequest * request) {
		
		// Compute complete barrier checks for shader write complete: thus, it is attached to output buffer.
		computeCompleteBarrier.buffer = request->Output->vkHandle();
		computeCompleteBarrier.size = request->Output->Size();

		// Host transition barrier checks that our transfer write has complete: attached to result (host visible) buffer
		hostTransitionBarrier.buffer = request->Result->vkHandle();
		hostTransitionBarrier.size = request->Output->Size();

	}

	void DataProducer::recordCommands(const DataRequest * request, const size_t& curr_idx) {

		VkCommandBufferBeginInfo begin_info = vk_command_buffer_begin_info_base;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VkBufferCopy output_to_result_copy{ 0, 0, request->Output->Size() };
		VkCommandBuffer cmd_buffer = computePool->GetCmdBuffer(curr_idx);

		VkResult result = vkBeginCommandBuffer(cmd_buffer, &begin_info);
		VkAssert(result);

		vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[curr_idx]);
		vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
		vkCmdDispatch(cmd_buffer, static_cast<uint32_t>(ceil(request->Width / 16)), static_cast<uint32_t>(ceil(request->Height / 16)), 1);
		vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &computeCompleteBarrier, 0, nullptr);
		vkCmdCopyBuffer(cmd_buffer, request->Output->vkHandle(), request->Result->vkHandle(), 1, &output_to_result_copy);
		vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hostTransitionBarrier, 0, nullptr);
		
		result = vkEndCommandBuffer(cmd_buffer);
		VkAssert(result);

	}

	void DataProducer::uploadRequestData(std::forward_list<DataRequest*>& transfer_list) {

		VkCommandBufferBeginInfo begin_info = vk_command_buffer_begin_info_base;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		VkResult result = vkBeginCommandBuffer(transferPool->GetCmdBuffer(0), &begin_info);
		VkAssert(result);

		while (!transfer_list.empty()) {
			auto req = transfer_list.front();
			transfer_list.pop_front();
			auto input_heights = req->parent->GetHeights();
			assert(!input_heights.empty());
			req->Input->CopyTo(input_heights.data(), transferPool->GetCmdBuffer(0), sizeof(glm::vec2) * input_heights.size(), 0);
		}

		result = vkEndCommandBuffer(transferPool->GetCmdBuffer(0));
		VkAssert(result);

	}

	void DataProducer::Submit() {
		if (requests.empty()) {
			return;
		}

		submitTransfers();
		submitCompute();
		resetObjects();
		resetPipelines();

		// cleanup any staging resources used.
		Buffer::DestroyStagingResources(parent);
	
		return;
	}

	void DataProducer::submitTransfers() {
		
		VkSubmitInfo submit_info = vk_submit_info_base;
		submit_info.commandBufferCount = 1;

		submit_info.signalSemaphoreCount = static_cast<uint32_t>(semaphores.size());
		submit_info.pSignalSemaphores = semaphores.data();
		submit_info.pCommandBuffers = &transferPool->GetCmdBuffer(0);
		VkResult result = vkQueueSubmit(spareQueue, 1, &submit_info, VK_NULL_HANDLE);
		VkAssert(result);

	}

	void DataProducer::submitCompute() {

		VkPipelineStageFlags stage_flag = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkSubmitInfo submit_info = vk_submit_info_base;
		submit_info.pWaitDstStageMask = &stage_flag;
		size_t curr_buffer_idx = 0;

		for (auto iter = availQueues.begin(); iter != availQueues.end(); ++iter) {
			if (submittedRequests.empty()) {
				break;
			}

			submit_info.pCommandBuffers = &computePool->GetCmdBuffer(curr_buffer_idx);
			submit_info.waitSemaphoreCount = 1;
			submit_info.pWaitSemaphores = &semaphores[curr_buffer_idx];

			VkResult result = vkQueueSubmit(*iter, 1, &submit_info, fences[curr_buffer_idx]);
			VkAssert(result);

			++curr_buffer_idx;
			submittedRequests.front()->Status = RequestStatus::Complete;
			submittedRequests.pop_front();

		}

		VkResult submit_result = vkWaitForFences(parent->vkHandle(), static_cast<uint32_t>(fences.size()), fences.data(), VK_TRUE, vk_default_fence_timeout);
		VkAssert(submit_result);
		assert(submittedRequests.empty());
	}

	void DataProducer::resetObjects() {
		VkResult result = vkResetCommandPool(parent->vkHandle(), computePool->vkHandle(), VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
		VkAssert(result);
		result = vkResetCommandBuffer(transferPool->GetCmdBuffer(0), VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
		VkAssert(result);
		result = vkResetFences(parent->vkHandle(), static_cast<uint32_t>(fences.size()), fences.data());
		VkAssert(result);
	}

	void DataProducer::resetPipelines() {
		for (auto& pipeline : pipelines) {
			vkDestroyPipeline(parent->vkHandle(), pipeline, nullptr);
			pipeline = VK_NULL_HANDLE;
		}
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
		request->Width = request->Height = static_cast<size_t>(sqrt(node->NumSamples()));

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
