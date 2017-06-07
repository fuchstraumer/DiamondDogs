#include "stdafx.h"
#include "HeightNode.h"

vulpes::terrain::HeightNode::HeightNode(const glm::ivec3 & node_grid_coordinates) : gridCoords(node_grid_coordinates) {}

void vulpes::terrain::HeightNode::SampleResiduals(const Sampler& samplers){
	// TODO: Scale coordinates used to sample by some set value. 
	// Samplers take size_t coordinates as arguments.
	throw std::runtime_error("Not implemented");
}

void vulpes::terrain::HeightNode::SampleFromParent(const HeightNode & node) {
	// See: proland/preprocess/terrain/HeightMipmap.cpp to find original implementation
	size_t tile_size = std::min(rootNodeSize << gridCoords.z, nodeSize);
	size_t parent_x, parent_y;
	parent_x = 1 + (gridCoords.x % 2) * tile_size / 2;
	parent_y = 1 + (gridCoords.y % 2) * tile_size / 2;
	const int num_samples = tile_size + 5;
	finalSamples.resize(num_samples * num_samples);
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
			finalSamples[i + (j * num_samples)] = std::move(sample);
		}
	}
}


float vulpes::terrain::HeightNode::Sample(const size_t & x, const size_t & y) const {
	return finalSamples[x + (y * gridSize)];
}

float vulpes::terrain::HeightNode::Sample(const size_t & idx) const {
	return finalSamples[idx];
}
