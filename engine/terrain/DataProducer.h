#pragma once
#ifndef VULPES_TERRAIN_DATA_PRODUCER_H
#define VULPES_TERRAIN_DATA_PRODUCER_H

#include "stdafx.h"
#include "VulpesRender/include/ForwardDecl.h"
#include "VulpesRender/include/NonCopyable.h"
#include "VulpesRender/include/command/CommandPool.h"
#include "VulpesRender/include/resource/Buffer.h"
#include "VulpesRender/include/resource/PipelineCache.h"
#include <queue>

namespace vulpes {

	namespace terrain {
		class HeightNode;
	}

	enum class RequestStatus : uint8_t {
		Received, // in a producer
		NeedsTransfer, // request data not on device ready, can't dispatch shader.
		Complete, // request complete, data ready.
	};

	struct DataRequest {

		ShaderModule* Shader;
		std::unique_ptr<Buffer> Result, Input, Output;
		glm::ivec4 specData;
		RequestStatus Status;

		size_t Width, Height;

		VkComputePipelineCreateInfo pipelineCreateInfo;
		VkPipelineShaderStageCreateInfo shaderInfo;
		VkSpecializationInfo specializationInfo;
		std::vector<VkSpecializationMapEntry> specializations;
		terrain::HeightNode *node;
		const terrain::HeightNode* parent;
		bool Complete() const;
		Buffer* GetData();

		bool operator<(const DataRequest& other) const;

		static std::shared_ptr<DataRequest> UpsampleRequest(terrain::HeightNode* node, const terrain::HeightNode* parent, const Device* dvc);
	};


	

	class DataProducer : public NonMovable {
	public:

		DataProducer(const Device* parent);

		~DataProducer();

		void Request(DataRequest* req);

		void PrepareSubmissions();

		void Submit();

		bool Complete() const;

	private:

		void createQueues();
		void createCommandPool();
		void createFences();
		void createSemaphores();
		void setupDescriptors();
		void createPipelineLayout();
		void allocateDescriptors();
		void createBarriers();

		void updateWriteDescriptors(const DataRequest * request);
		void updatePipelineInfo(const DataRequest* request, size_t curr_req_idx);
		void createPipelines();
		void updateBarriers(const DataRequest* request);
		void recordCommands(const DataRequest * request, const size_t& curr_idx);
		void uploadRequestData(std::forward_list<DataRequest*>& transfer_list);

		void submitTransfers();
		void submitCompute();
		void resetObjects();
		void resetPipelines();

		// each request could have a unique layout or descriptor set.
		VkPipelineLayout pipelineLayout;
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorSet descriptorSet;
		VkDescriptorPool descriptorPool;
		std::array<VkWriteDescriptorSet, 2> writeDescriptors;

		VkBufferMemoryBarrier computeCompleteBarrier, hostTransitionBarrier;

		// we iterate through this and attach one request per submit.
		std::list<VkQueue> availQueues;
		VkQueue spareQueue; // general-use queue used for transfers, if needed.
		
		std::list<DataRequest*> submittedRequests;

		std::vector<VkFence> fences;
		std::vector<VkSemaphore> semaphores;
		std::vector<VkPipeline> pipelines;
		std::vector<VkComputePipelineCreateInfo> pipelineInfos;
		std::list<DataRequest*> requests;

		static size_t numProducers;

		const Device* parent;
		std::unique_ptr<CommandPool> computePool;
		std::unique_ptr<CommandPool> transferPool;
		std::unique_ptr<PipelineCache> pipelineCache;

	};


	

}

#endif // !VULPES_TERRAIN_DATA_PRODUCER_H
