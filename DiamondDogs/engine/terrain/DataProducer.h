#pragma once
#ifndef VULPES_TERRAIN_DATA_PRODUCER_H
#define VULPES_TERRAIN_DATA_PRODUCER_H

#include "stdafx.h"
#include "engine/renderer/ForwardDecl.h"
#include "engine/renderer/NonCopyable.h"
#include "engine/renderer/command/CommandPool.h"
#include "engine/renderer/resource/Buffer.h"
#include "engine/renderer/resource/PipelineCache.h"
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

	namespace terrain {
		class HeightNode;
	}

	enum class RequestStatus : uint8_t {
		Received, // in a producer
		NeedsTransfer, // request data not on device ready, can't dispatch shader.
		Complete, // request complete, data ready.
	};


	/*
		Request to run a pipeline 
		- Shader contains the VkShaderModule unique to this request. I assume the "name" is just "main". Stage flags are just VK_SHADER_STAGE_COMPUTE_BIT
		- Specializations contain (if applicable) specialization constants that are being modified for this invocation
		- Input is the input buffer being operated on (readonly), Output is buffer being written to
	*/
	struct DataRequest {
		
		~DataRequest() {
			delete Input;
			delete Output;
		}

		ShaderModule* Shader;
		Buffer *Input, *Output;
		std::unique_ptr<Buffer> Result;

		RequestStatus Status;

		size_t Width, Height;

		VkComputePipelineCreateInfo pipelineCreateInfo;
		VkPipelineShaderStageCreateInfo shaderInfo;
		VkSpecializationInfo specializationInfo;
		std::vector<VkSpecializationMapEntry> specializations;
		terrain::HeightNode *node, *parent;
		bool Complete() const;
		Buffer* GetData();

		static DataRequest* UpsampleRequest(terrain::HeightNode* node, terrain::HeightNode* parent, const Device* dvc);
	};


	

	class DataProducer : public NonMovable {
	public:

		DataProducer(const Device* parent);

		~DataProducer();

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
		VkDescriptorPool descriptorPool;

		// we iterate through this and attach one request per submit.
		std::list<VkQueue> availQueues;
		VkQueue spareQueue; // general-use queue used for transfers, if needed.
		
		std::forward_list<DataRequest*> submittedRequests;

		std::vector<VkFence> fences;
		std::vector<VkSemaphore> semaphores;
		std::vector<VkPipeline> pipelines;

		std::list<DataRequest*> requests;

		static size_t numProducers;

		const Device* parent;
		std::unique_ptr<CommandPool> computePool;
		std::unique_ptr<CommandPool> transferPool;
		std::unique_ptr<PipelineCache> pipelineCache;

	};


	

}

#endif // !VULPES_TERRAIN_DATA_PRODUCER_H
