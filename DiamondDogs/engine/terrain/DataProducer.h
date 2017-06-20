#pragma once
#ifndef VULPES_TERRAIN_DATA_PRODUCER_H
#define VULPES_TERRAIN_DATA_PRODUCER_H

#include "stdafx.h"
#include "engine/renderer/ForwardDecl.h"
#include "engine/renderer/NonCopyable.h"
#include "engine/renderer/command/CommandPool.h"
#include "engine/renderer/resource/Buffer.h"
#include <queue>

/*
	
	Defines a general-use data producer that acts as a worker object.
	Runs a compute shader invocation that takes input data and upsamples
	it given a few special parameters.

	If flag of a terrain or height node is set to NEEDS_DATA, this
	object is still working on it. Once this flag changes to NEEDS_TRANFER
	or READY, the object will be retaken by the node renderer.

*/

namespace vulpes {

	enum class RequestStatus : uint8_t {
		Received, // in a producer
		Queued, // commands recording/recorded
		Submitted, // commands submitted
		Complete, // request complete, data ready.
	};


	/*
		Request to run a pipeline 
		- Shader contains the VkShaderModule unique to this request. I assume the "name" is just "main". Stage flags are just VK_SHADER_STAGE_COMPUTE_BIT
		- Specializations contain (if applicable) specialization constants that are being modified for this invocation
		- Input is the input texture being operated on (readonly), Output is texture being written to
		- Result is a weak pointer, so that we can check if it's ready or not.
	*/
	struct DataRequest {
		
		VkShaderModule Shader;
		std::vector<VkSpecializationMapEntry> Specializations;
		Buffer *Input, *Output;
		std::unique_ptr<Buffer> Result;

		bool Complete() const;

		Buffer* GetData();

		RequestStatus Status;

		size_t Width, Height;

	};

	inline static DataRequest UpsampleRequest() {

	}

	

	class DataProducer : public NonMovable {
	public:

		DataProducer(const Device* parent);

		void Request(DataRequest* req);

		// submit availQueues.size() requests, return num submitted.
		size_t RecordCommands();

		size_t Submit();

		// returns true when requests.empty()
		bool Complete() const;

	private:

		// each request could have a unique layout or descriptor set.
		VkPipelineLayout pipelineLayout;
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorSet descriptorSet;
		VkPipeline pipeline;
		// we iterate through this and attach one request per submit.
		std::forward_list<VkQueue> availQueues;
		std::forward_list<DataRequest*> submittedRequests;
		std::vector<VkFence> fences;

		std::list<DataRequest*> requests;

		static size_t numProducers;

		const Device* parent;
		const VkAllocationCallbacks* allocators;
		VkFence computeFence;
		std::unique_ptr<CommandPool> computePool;
		std::unique_ptr<PipelineCache> pipelineCache;
		
		uint32_t computeFamilyIndex;

		// single descriptor, updated each invocation with current input/outputs.
		VkDescriptorImageInfo imageInfo;
	};

}

#endif // !VULPES_TERRAIN_DATA_PRODUCER_H
