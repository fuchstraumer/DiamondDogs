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

		};

		struct NodeSubset {
			std::forward_list<const TerrainNode*> nodes;
			static const bool sortNodesByDistance = false;
		public:

			NodeSubset() = default;

			void AddNode(const TerrainNode* node);

			void BuildCommandBuffers(VkCommandBuffer& cmd);
		};

	}

}

#endif // !VULPES_TERRAIN_NODE_SUBS_ET_H
