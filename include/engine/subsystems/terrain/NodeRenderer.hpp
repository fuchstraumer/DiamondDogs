#pragma once
#ifndef VULPES_TERRAIN_NODE_RENDERER_H
#define VULPES_TERRAIN_NODE_RENDERER_H

#include "stdafx.h"
#include "ForwardDecl.hpp"
#include "engine\util\AABB.hpp"
#include "HeightNode.hpp"
#include "resource/ShaderModule.hpp"
#include "util/TaskPool.hpp"
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

		class NodeRenderer {
		public:
			bool UpdateLOD = true;
			static bool DrawAABBs;
			static float MaxRenderDistance;
			static vulpes::util::TaskPool HeightDataTasks;

			terrain_push_ubo uboData;

			NodeRenderer(const Device* parent_dvc, TransferPool* transfer_pool);

			~NodeRenderer();

			void CreatePipeline(const VkRenderPass& renderpass, const glm::mat4& projection);

			void AddNode(TerrainNode * node);

			void Render(VkCommandBuffer& graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4 & view, const glm::vec3& view_pos, const VkViewport& viewport, const VkRect2D& scissor);

			void RunTasks();

		private:

			void setupDescriptors();

			void setupPipelineLayout();

			void allocateDescriptors();

			void createShaders();

			void updateUBO(const glm::mat4& projection);

			void renderNodes(VkCommandBuffer& cmd_buffer, VkCommandBufferBeginInfo& begin_info);

			void transferNodesToDvc();

			void transferNodeToDvc(std::shared_ptr<TerrainNode> node_to_transfer);

			void updateGUI();

			std::set<std::shared_ptr<TerrainNode>> readyNodes;
			std::forward_list<std::shared_ptr<TerrainNode>> transferNodes;

			VkDescriptorSetLayout descriptorSetLayout;
			VkDescriptorSet descriptorSet;
			VkDescriptorPool descriptorPool;
			VkPipelineLayout pipelineLayout;
			std::unique_ptr<ShaderModule> vert, frag;
			const Device* device;

			std::unique_ptr<GraphicsPipeline> pipeline;
			std::unique_ptr<PipelineCache> pipelineCache;
			TransferPool* transferPool;
			const VkViewport* frameViewport;
			const VkRect2D* frameScissorRect;
			const glm::vec3* frameCameraPos;


		};

	}

}

#endif // !VULPES_TERRAIN_NODE_SUBS_ET_H
