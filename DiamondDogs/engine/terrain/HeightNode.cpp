#include "stdafx.h"
#include "HeightNode.h"

namespace vulpes {
	namespace terrain {

		HeightNode::HeightNode(const glm::ivec3 & node_grid_coordinates) : gridCoords(node_grid_coordinates) {}

		HeightNode::HeightNode(const glm::ivec3 & node_grid_coordinates, const HeightNode & parent) : gridCoords(node_grid_coordinates), parentGridCoords(parent.GridCoords()) {
			SampleFromParent(parent);
		}

		void HeightNode::SampleResiduals(const Sampler& samplers) {
			// TODO: Scale coordinates used to sample by some set value. 
			// Samplers take size_t coordinates as arguments.
			throw std::runtime_error("Not implemented");
		}

		void HeightNode::SampleFromParent(const HeightNode & node) {
			// See: proland/preprocess/terrain/HeightMipmap.cpp to find original implementation
			size_t tile_size = std::min(rootNodeSize << gridCoords.z, nodeSize);
			// These aren't the parent grid coords: these are the offsets from the parent height samples that we 
			// use to sample from the parents data.
			size_t parent_x, parent_y;
			parent_x = 1 + (gridCoords.x % 2) * tile_size / 2;
			parent_y = 1 + (gridCoords.y % 2) * tile_size / 2;

			// Tile size is the number of vertices we directly use: we add 5 as we sample the borders,
			// which helps with the upsampling process.
			const size_t num_samples = tile_size + 5;
			gridSize = num_samples;

			samples.resize(num_samples * num_samples);

			/*
				Upsampling: Here we check the current i,j pair representing our local grid coordinates.
				Based on where we are on the grid, we either directly sample or perform various interpolations/
				mixings to weight the sample from the parent. This avoids obvious borders between regions
				and lets one heightmap image continue to work for smaller and smaller tiles.

				We change the residual magnitude and functions based on the LOD level of this tile, though, 
				as we will still be losing some detail at high LOD levels.
			*/
			for (size_t j = 0; j <= tile_size + 4; ++j) {
				for (size_t i = 0; i <= tile_size + 4; ++i) {
					float sample;
					if (j % 2 == 0) {
						if (i % 2 == 0) {
							sample = node.Sample(i / 2 + parent_x - 1 + (j / 2 + parent_y) * num_samples);
						}
						else {
							float z0, z1, z2, z3;
							z0 = node.Sample(i / 2 + parent_x - 1 + (j / 2 + parent_y) * num_samples);
							z1 = node.Sample(i / 2 + parent_x + (j / 2 * parent_y) * num_samples);
							z2 = node.Sample(i / 2 + parent_x + 1 + (j / 2 * parent_y) * num_samples);
							z3 = node.Sample(i / 2 + parent_x + 2 + (j / 2 * parent_y) * num_samples);
							sample = ((z1 + z2) * 9.0f - (z0 + z3)) / 16.0f;
						}
					}
					else {
						if (i % 2 == 0) {
							float z0, z1, z2, z3;
							z0 = node.Sample(i / 2 + parent_x + (j / 2 - 1 + parent_y)*num_samples);
							z1 = node.Sample(i / 2 + parent_x + (j / 2 + parent_y)*num_samples);
							z2 = node.Sample(i / 2 + parent_x + (j / 2 + 1 + parent_y)*num_samples);
							z3 = node.Sample(i / 2 + parent_x + (j / 2 + 2 + parent_y)*num_samples);
							sample = ((z1 + z2) * 9.0f - (z0 + z3)) / 16.0f;
						}
						else {
							size_t dI, dJ;
							sample = 0.0f;
							for (dJ = -1; dJ <= 2; ++dJ) {
								float f = (dJ == -1 || dJ == 2) ? (-1.0f / 16.0f) : (9.0f / 16.0f);
								for (dI = -1; dI <= 2; ++dI) {
									float g = (dI == -1 || dI == 2) ? (-1.0f / 16.0f) : (9.0f / 16.0f);
									sample += f * g * node.Sample(i / 2 + dI + parent_x + (j / 2 + dJ + parent_y)*num_samples);
								}
							}
						}
					}
					samples[i + (j * num_samples)].Height() = std::move(sample);
					// Parent height is used to morph between LOD levels, so that we don't notice much pop-in as new mesh tiles are loaded.
					samples[i + (j * num_samples)].ParentHeight() = node.Sample(i / 2 + parent_x + (j / 2 + parent_y)*num_samples);
				}
			}
		}

		float HeightNode::Sample(const size_t & x, const size_t & y) const {
			return samples[x + (y * gridSize)].x;
		}

		float HeightNode::Sample(const size_t & idx) const {
			return samples[idx].x;
		}

		const glm::ivec3 & HeightNode::GridCoords() const noexcept {
			return gridCoords;
		}

		size_t HeightNodeLoader::LoadNodes() {
			size_t nodes_loaded = 0;
			for (auto& node_future : wipNodeCache) {
				std::pair<glm::ivec3, std::shared_ptr<HeightNode>> new_node;
				new_node.first = node_future.first;
				new_node.second = node_future.second.get();
				nodeCache.insert(new_node);
				++nodes_loaded;
			}
			return nodes_loaded;
		}

		bool HeightNodeLoader::LoadNode(const glm::ivec3 & node_pos, const glm::ivec3 & parent_pos) {
			if (HasNode(parent_pos)) {
				// Parent loaded, see if child is loaded.
				if (HasNode(node_pos)) {
					// Desired node already exists, can get it.
					return true;
				}
				// Parent loaded but child isn't. Create child now.
				const std::shared_ptr<HeightNode>& parent_node = nodeCache.at(node_pos);
				wipNodeCache.insert(std::make_pair(node_pos, std::async(std::launch::async, CreateNode, node_pos, *parent_node)));
				return true;
			}
			// Parent isn't loaded: need parent to be loaded before we can create a new node.
			return false;
		}

		bool HeightNodeLoader::HasNode(const glm::ivec3 & node_pos) {
			return (nodeCache.count(node_pos) != 0);
		}

		std::shared_ptr<HeightNode> HeightNodeLoader::CreateNode(const glm::ivec3 & pos, const HeightNode & parent) {
			return std::make_shared<HeightNode>(pos, parent);
		}

	}
}