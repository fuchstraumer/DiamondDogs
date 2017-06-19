#include "stdafx.h"
#include "DataProducer.h"
#include "engine/renderer/core/LogicalDevice.h"

namespace vulpes {

	size_t DataProducer::numProducers = 0;

	bool DataRequest::Complete() const {
		if (auto check = Result.lock()) {
			return true;
		}
		else {
			return false;
		}
	}

	Buffer* vulpes::DataRequest::GetData() const {
		if (!Complete()) {
			throw std::runtime_error("Tried to access data before it was generated.");
		}

		auto result = Result.lock();

		return result->Data.release();
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
	}

	void DataProducer::Request(DataRequest * req) {
		// add to back, to preserve priority.
		requests.push_back(req);
	}

	size_t DataProducer::Submit() {
		size_t submitted = 0;

		auto cmd = computePool->GetCmdBuffer(0);

		for (auto iter = availQueues.begin(); iter != availQueues.end(); ++iter) {
			auto req = requests.front();
			requests.pop_front();


		}
		return size_t();
	}

}
