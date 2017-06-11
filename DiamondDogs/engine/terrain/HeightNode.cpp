#include "stdafx.h"
#include "HeightNode.h"
#include "engine\util\noise.h"

namespace vulpes {
	namespace terrain {

		size_t HeightNode::RootNodeSize = 24;
		double HeightNode::RootNodeLength = 1000;

		HeightNode::HeightNode(const glm::ivec3 & node_grid_coordinates, std::vector<HeightSample>& init_samples) : gridCoords(node_grid_coordinates), nodeSize(RootNodeSize + 5) {
			samples = std::move(init_samples);			
		}

		HeightNode::HeightNode(const glm::ivec3 & node_grid_coordinates, const HeightNode & parent) : gridCoords(node_grid_coordinates), parentGridCoords(parent.GridCoords()), nodeSize(RootNodeSize + 5) {
			SampleFromParent(parent);
		}

		
		void HeightNode::SampleFromParent(const HeightNode & node) {
			// See: proland/preprocess/terrain/HeightMipmap.cpp to find original implementation
			size_t tile_size = std::min(RootNodeSize << gridCoords.z, nodeSize);

			// Tile size is the number of vertices we directly use: we add 5 as we sample the borders,
			// which helps with the upsampling process.
			gridSize = tile_size - 5;

			// These aren't the parent grid coords: these are the offsets from the parent height samples that we 
			// use to sample from the parents data.
			size_t parent_x, parent_y;
			parent_x = 1 + (gridCoords.x % 2) * gridSize / 2;
			parent_y = 1 + (gridCoords.y % 2) * gridSize / 2;

			samples.resize(nodeSize * nodeSize);

			/*
				Upsampling: Here we check the current i,j pair representing our local grid coordinates.
				Based on where we are on the grid, we either directly sample or perform various interpolations/
				mixings to weight the sample from the parent. This avoids obvious borders between regions
				and lets one heightmap image continue to work for smaller and smaller tiles.

				We change the residual magnitude and functions based on the LOD level of this tile, though, 
				as we will still be losing some detail at high LOD levels.
			*/

			for (size_t j = 0; j < nodeSize; ++j) {
				for (size_t i = 0; i < nodeSize; ++i) {
					float sample;
					if (j % 2 == 0) {
						if (i % 2 == 0) {
							sample = node.Sample(i / 2 + parent_x - 1 + (j / 2 + parent_y) * nodeSize);
						}
						else {
							float z0, z1, z2, z3;
							z0 = node.Sample(i / 2 + parent_x - 1 + (j / 2 + parent_y) * nodeSize);
							z1 = node.Sample(i / 2 + parent_x + (j / 2 + parent_y) * nodeSize);
							z2 = node.Sample(i / 2 + parent_x + 1 + (j / 2 + parent_y) * nodeSize);
							z3 = node.Sample(i / 2 + parent_x + 2 + (j / 2 + parent_y) * nodeSize);
							sample = ((z1 + z2) * 9.0f - (z0 + z3)) / 16.0f;
						}
					}
					else {
						if (i % 2 == 0) {
							float z0, z1, z2, z3;
							z0 = node.Sample(i / 2 + parent_x + (j / 2 - 1 + parent_y)*nodeSize);
							z1 = node.Sample(i / 2 + parent_x + (j / 2 + parent_y)*nodeSize);
							z2 = node.Sample(i / 2 + parent_x + (j / 2 + 1 + parent_y)*nodeSize);
							z3 = node.Sample(i / 2 + parent_x + (j / 2 + 2 + parent_y)*nodeSize);
							sample = ((z1 + z2) * 9.0f - (z0 + z3)) / 16.0f;
						}
						else {
							size_t dI, dJ;
							sample = 0.0f;
							for (dJ = -1; dJ <= 2; ++dJ) {
								float f = (dJ == -1 || dJ == 2) ? (-1.0f / 16.0f) : (9.0f / 16.0f);
								for (dI = -1; dI <= 2; ++dI) {
									float g = (dI == -1 || dI == 2) ? (-1.0f / 16.0f) : (9.0f / 16.0f);
									sample += f * g * node.Sample(i / 2 + dI + parent_x + (j / 2 + dJ + parent_y)*nodeSize);
								}
							}
						}
					}
					samples[i + (j * nodeSize)].Sample.x = std::move(sample);
					// Parent height is used to morph between LOD levels, so that we don't notice much pop-in as new mesh tiles are loaded.
					// samples[i + (j * num_samples)].ParentHeight() = node.Sample(i / 2 + parent_x + (j / 2 + parent_y)*num_samples);
				}
			}
		}

		float HeightNode::Sample(const size_t & x, const size_t & y) const {
			return samples[x + (y * nodeSize)].Sample.x;
		}

		glm::vec3 HeightNode::Normal(const size_t & x, const size_t & y) const{
			return samples[x + (y * nodeSize)].Normal;
		}

		float HeightNode::Sample(const size_t & idx) const {
			return samples[idx].Sample.x;
		}

		const glm::ivec3 & HeightNode::GridCoords() const noexcept {
			return gridCoords;
		}

		size_t HeightNode::GridSize() const noexcept {
			return gridSize;
		}

		void HeightNode::SetRootNodeSize(const size_t & new_size) {
			RootNodeSize = new_size;
		}

		void HeightNode::SetRootNodeLength(const double & new_length) {
			RootNodeLength = new_length;
		}

		std::shared_ptr<HeightNode> HeightNodeLoader::GetNode(const glm::ivec3 & node){
			return nodeCache.at(node);
		}

		size_t HeightNodeLoader::LoadNodes() {
			size_t nodes_loaded = 0;
			auto iter = wipNodeCache.begin();
			while (iter != wipNodeCache.end()) {
				std::pair<glm::ivec3, std::shared_ptr<HeightNode>> new_node;
				new_node.first = iter->first;
				new_node.second = iter->second.get();
				auto inserted = nodeCache.insert(new_node);
				if (!inserted.second) {
					throw std::runtime_error("Failed to insert. Possible hash collision?");
				}
				++nodes_loaded;
				wipNodeCache.erase(iter++);
			}
			return nodes_loaded;
		}

		height_node_iterator_t HeightNodeLoader::begin(){
			return nodeCache.begin();
		}

		height_node_iterator_t HeightNodeLoader::end() {
			return nodeCache.end();
		}

		const_height_node_iterator_t HeightNodeLoader::begin() const {
			return nodeCache.begin();
		}

		const_height_node_iterator_t HeightNodeLoader::end() const{
			return nodeCache.end();
		}

		const_height_node_iterator_t HeightNodeLoader::cbegin() const{
			return nodeCache.cbegin();
		}

		const_height_node_iterator_t HeightNodeLoader::cend() const {
			return nodeCache.cend();
		}

		size_t HeightNodeLoader::size() const
		{
			return size_t();
		}

		bool HeightNodeLoader::SubdivideNode(const glm::ivec3 & node_pos) {
			bool loaded;
			loaded = LoadNode(glm::ivec3{ node_pos.x, node_pos.y, node_pos.z + 1 }, node_pos);
			loaded |= LoadNode(glm::ivec3{ node_pos.x + 1, node_pos.y, node_pos.z + 1 }, node_pos);
			loaded |= LoadNode(glm::ivec3{ node_pos.x, node_pos.y + 1, node_pos.z + 1 }, node_pos);
			loaded |= LoadNode(glm::ivec3{ node_pos.x + 1, node_pos.y + 1, node_pos.z + 1 }, node_pos);
			return loaded;
		}

		bool HeightNodeLoader::LoadNode(const glm::ivec3 & node_pos, const glm::ivec3 & parent_pos) {
			if (HasNode(parent_pos)) {
				// Parent loaded, see if child is loaded.
				if (HasNode(node_pos)) {
					// Desired node already exists, can get it.
					return true;
				}
				// Parent loaded but child isn't. Create child now.
				const std::shared_ptr<HeightNode>& parent_node = nodeCache.at(parent_pos);
				auto inserted = wipNodeCache.insert(std::make_pair(node_pos, std::async(std::launch::async, CreateNode, node_pos, *parent_node)));
				if (!inserted.second) {
					throw std::runtime_error("Failed to insert. Possible hash collision?");
				}
				return true;
			}
			// Parent isn't loaded: need parent to be loaded before we can create a new node.
			return false;
		}

		bool HeightNodeLoader::HasNode(const glm::ivec3 & node_pos) {
			if (nodeCache.count(node_pos) != 0) {
				return true;
			}
			else if (wipNodeCache.count(node_pos) != 0) {
				return LoadNodes() >= 1;
			}
			return false;
		}

		float HeightNodeLoader::GetHeight(const glm::ivec3& node_pos, const glm::vec2 world_pos) {

			double curr_size = RootNodeLength / (1 << node_pos.z);

			// Make sure query is in range of current node.
			if (abs(world_pos.x) >= curr_size + 1.0 || abs(world_pos.y) >= curr_size + 1.0) {
				throw std::out_of_range("Attempted to sample out of range of heightnode");
			}

			float x, y;
			x = world_pos.x;
			y = world_pos.y;

			if (!HasNode(node_pos)) {
				throw std::out_of_range("Desired node doesn't exist.");
			}

			auto& node = nodeCache.at(node_pos);
			size_t curr_grid_size = node->GridSize();
			
			x = 2.0f + (fmod(x, curr_size) / curr_size) * curr_grid_size;
			y = 2.0f + (fmod(abs(y), curr_size) / curr_size) * curr_grid_size;

			// Sample coords
			size_t sx = floorf(x), sy = floorf(y);

			return node->Sample(sx, sy);
		}


		std::shared_ptr<HeightNode> HeightNodeLoader::CreateNode(const glm::ivec3 & pos, const HeightNode & parent) {
			return std::make_shared<HeightNode>(pos, parent);
		}

		HeightmapNoise::HeightmapNoise(const size_t & num_samples, const glm::vec3& starting_pos, const float& step_size) {
			noise::GeneratorBase gen(step_size);
			samples.resize(num_samples * num_samples);
			glm::vec2 xy = starting_pos.xz;
			for (size_t j = 0; j < num_samples; ++j) {
				for (size_t i = 0; i < num_samples; ++i) {

					glm::vec3 pos = glm::vec3(xy.x + (i * step_size), 0.0f, xy.y + (j * step_size));
					samples[i + (j * num_samples)].Sample.x = gen.SimplexBillow(pos.x, pos.y, 0.05, 10, 1.6f, 1.8f) / 50.0f;

				}
			}
		}

}
}