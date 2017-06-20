#include "stdafx.h"
#include "DataProducer.h"
#include "engine/renderer/core/LogicalDevice.h"
#include "engine/renderer/resource/Texture.h"

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

		VkCommandPoolCreateInfo pool_info = vk_command_pool_info_base;
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pool_info.queueFamilyIndex = parent->QueueFamilyIndices.Compute;

		computePool = std::make_unique<CommandPool>(parent, pool_info);
		computePool->CreateCommandBuffers(availQueues.max_size());

		fences.resize(availQueues.max_size());
		VkResult result = VK_SUCCESS;
		for (auto& fence : fences) {
			result = vkCreateFence(parent->vkHandle(), &vk_fence_create_info_base, nullptr, &fence);
			VkAssert(result);
		}
	}

	void DataProducer::Request(DataRequest * req) {
		// add to back, to preserve priority.
		requests.push_back(req);
		req->Status = RequestStatus::Received;
	}

	size_t DataProducer::RecordCommands() {
		size_t submitted = 0;

		VkCommandBufferBeginInfo begin_info = vk_command_buffer_begin_info_base;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

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

		for (auto iter = availQueues.begin(); iter != availQueues.end(); ++iter) {
			auto req = requests.front();
			requests.pop_front();
			req->Status = RequestStatus::Queued;
			auto cmd = computePool->GetCmdBuffer(submitted);
			vkBeginCommandBuffer(cmd, &begin_info);
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
			vkCmdDispatch(cmd, req->Width / 16, req->Height / 16, 1);
			compute_complete_barrier.buffer = req->Output->vkHandle();
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &compute_complete_barrier, 0, nullptr);
			output_result_copy.size = req->Output->AllocSize();
			vkCmdCopyBuffer(cmd, req->Output->vkHandle(), req->Result->vkHandle(), 1, &output_result_copy);
			host_transition_barrier.buffer = req->Result->vkHandle();
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host_transition_barrier, 0, nullptr);
			vkEndCommandBuffer(cmd);
			req->Status = RequestStatus::Submitted;
			submittedRequests.push_front(req);
			++submitted;
		}
		
		return submitted;
	}

	size_t DataProducer::Submit() {
		size_t submitted = 0;

		VkSubmitInfo submit_info = vk_submit_info_base;
		submit_info.commandBufferCount = 1;

		for (auto iter = availQueues.begin(); iter != availQueues.end(); ++iter) {
			submit_info.pCommandBuffers = &computePool->GetCmdBuffer(submitted);
			vkQueueSubmit(*iter, 1, &submit_info, fences[submitted]);
			++submitted;
			submittedRequests.front()->Status = RequestStatus::Complete;
			submittedRequests.pop_front();
		}

		VkResult submit_result = VK_SUCCESS;
		submit_result = vkWaitForFences(parent->vkHandle(), static_cast<uint32_t>(fences.size()), fences.data(), VK_TRUE, vk_default_fence_timeout);
		VkAssert(submit_result);

		submit_result = vkResetCommandPool(parent->vkHandle(), computePool->vkHandle(), VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
		VkAssert(submit_result);
		submit_result = vkResetFences(parent->vkHandle(), static_cast<uint32_t>(fences.size()), fences.data());
		VkAssert(submit_result);

		return submitted;
	}

}
