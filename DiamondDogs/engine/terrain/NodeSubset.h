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

		class NodeSubset {
			
			std::list<TerrainNode*> readyNodes;
			std::forward_list<TerrainNode*> transferNodes;

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
			Buffer* ubo;

			TransferPool* transferPool;
			VkQueue* transferQueue;

		public:

			vsUBO uboData;

			NodeSubset(const Device* parent_dvc);

			void CreatePipeline(const VkRenderPass& renderpass, const Swapchain* swapchain, std::shared_ptr<PipelineCache>& cache, const glm::mat4& projection);

			void CreateUBO(const glm::mat4& projection);

			void AddNode(TerrainNode * node, bool ready);

			void Update(VkCommandBuffer& graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4 & view, const VkViewport& viewport, const VkRect2D& scissor);

		};

	}

}

#endif // !VULPES_TERRAIN_NODE_SUBS_ET_H
