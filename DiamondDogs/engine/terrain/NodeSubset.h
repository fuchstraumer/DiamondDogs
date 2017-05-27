#pragma once
#ifndef VULPES_TERRAIN_NODE_SUBSET_H
#define VULPES_TERRAIN_NODE_SUBSET_H

#include "stdafx.h"
#include "engine\renderer\ForwardDecl.h"
#include "engine\util\AABB.h"


namespace vulpes {

	namespace terrain {

		class TerrainNode;
		
		struct terrain_push_ubo {
			glm::mat4 model;
			glm::mat4 view;
			glm::mat4 projection;
		};

		struct thread_data {
			CommandPool* pool;
			VkSemaphore semaphore;
			VkFence fence;
		};

		struct NodeSubset {
			std::forward_list<const TerrainNode*> nodes;
			static const bool sortNodesByDistance = false;

			VkDescriptorSetLayout descriptorSetLayout;
			VkDescriptorSet descriptorSet;
			VkDescriptorPool descriptorPool;
			VkPipelineLayout pipelineLayout;
			ShaderModule *vert, *frag;
			const Device* device;

			GraphicsPipeline* pipeline;
			std::shared_ptr<PipelineCache> pipelineCache;

			struct vsUBO {
				glm::mat4 model;
				glm::mat4 view, projection;
			};

			vsUBO uboData;
			Buffer* ubo;

			TransferPool* transferPool;
			VkQueue* transferQueue;

		public:

			NodeSubset(const Device* parent_dvc);

			void CreatePipeline(const VkRenderPass& renderpass, const Swapchain* swapchain, std::shared_ptr<PipelineCache>& cache);

			void CreateUBO(const glm::mat4& projection);

			void AddNode(const TerrainNode* node);

			void BuildCommandBuffers(VkCommandBuffer& cmd);

			void SetTransferObjects(CommandPool* transfer_pool, VkQueue* transfer_queue);

			void UpdateUBO(const glm::mat4& view);
		};

	}

}

#endif // !VULPES_TERRAIN_NODE_SUBS_ET_H
