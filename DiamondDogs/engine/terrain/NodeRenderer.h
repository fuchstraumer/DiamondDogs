#pragma once
#ifndef VULPES_TERRAIN_NODE_RENDERER_H
#define VULPES_TERRAIN_NODE_RENDERER_H

#include "stdafx.h"
#include "engine\renderer\ForwardDecl.h"
#include "engine\util\AABB.h"
#include <set>
#include "HeightNode.h"
#include "DataProducer.h"
#include "engine/util/ThreadWrapper.h"

namespace vulpes {

	namespace terrain {

		/*
			
			Node Renderer - 

			represents a group of nodes that will be rendered using the same
			pipeline and most of the same rendering parameters. Individual model
			matrices will be used, and nodes can have unique mesh construction methods.
		
		*/

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

		class NodeRenderer {
			
			std::set<TerrainNode*> readyNodes;
			std::forward_list<TerrainNode*> transferNodes;

			VkDescriptorSetLayout descriptorSetLayout;
			VkDescriptorSet descriptorSet;
			VkDescriptorPool descriptorPool;
			VkPipelineLayout pipelineLayout;
			VkPipelineLayout aabbPipelineLayout;
			ShaderModule *vert, *frag;
			const Device* device;

			GraphicsPipeline* pipeline;
			std::shared_ptr<PipelineCache> pipelineCache;
			std::unique_ptr<DataProducer> dataProducer;

			struct vsUBO {
				glm::mat4 model;
				glm::mat4 view, projection;
			};

			
			Buffer* ubo;
			TransferPool* transferPool;
			ThreadWrapped upsampleThread;
			
		public:
			bool UpdateLOD = true;
			static bool DrawAABBs;
			static float MaxRenderDistance;

			vsUBO uboData;

			NodeRenderer(const Device* parent_dvc);

			~NodeRenderer();

			void CreatePipeline(const VkRenderPass& renderpass, const Swapchain* swapchain, std::shared_ptr<PipelineCache>& cache, const glm::mat4& projection);

			void CreateUBO(const glm::mat4& projection);

			void AddNode(TerrainNode * node);

			void AddRequest(TerrainNode* node);

			void RemoveNode(TerrainNode * node);

			void Render(VkCommandBuffer& graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4 & view, const glm::vec3& view_pos, const VkViewport& viewport, const VkRect2D& scissor);

			void SendRequests();

		};

	}

}

#endif // !VULPES_TERRAIN_NODE_SUBS_ET_H
