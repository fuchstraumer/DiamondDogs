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

		enum class NodeStatus {
			Undefined, // Likely not constructed fully or used at all
			OutOfFrustum,
			OutOfRange,
			Active, // Being used in next renderpass
			Subdivided, // Has been subdivided, render children instead of this.
			MeshUnbuilt,
		};

		struct NodeSubset {
			std::unordered_multimap<NodeStatus, const TerrainNode*> nodes;
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

			void Update();

			void UpdateUBO(const glm::mat4& view);
		};

	}

}

#endif // !VULPES_TERRAIN_NODE_SUBS_ET_H
