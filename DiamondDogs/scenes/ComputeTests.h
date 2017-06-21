#pragma once
#ifndef VULPES_COMPUTE_TESTS_H
#define VULPES_COMPUTE_TESTS_H

#include "stdafx.h"
#include "BaseScene.h"
#include "engine\terrain\TerrainQuadtree.h"
#include "engine\renderer\render\GraphicsPipeline.h"
#include "engine/renderer/resource/PipelineCache.h"
#include "engine/terrain/DataProducer.h"
namespace compute_tests {

	using namespace vulpes;

	class ComputeTests : public BaseScene {
	public:

		ComputeTests() : BaseScene(1) {
			const std::type_info& id = typeid(ComputeTests);
			uint16_t hash = static_cast<uint16_t>(id.hash_code());
			pipelineCache = std::make_shared<PipelineCache>(device, hash);

			VkQueue transfer;
			transfer = device->GraphicsQueue(0);
			instance->SetCamPos(glm::vec3(0.0f, 400.0f, 0.0f));
			object = new terrain::TerrainQuadtree(device, 1.30f, 1, 10000.0, glm::vec3(0.0f));
			object->SetupNodePipeline(renderPass->vkHandle(), swapchain, pipelineCache, instance->GetProjectionMatrix());
		}

		void ExecuteTests(const size_t& num_levels, const double& root_node_length = 10000.0, const size_t& root_node_size = 261, const size_t& child_node_size = 37) {
			DataProducer producer(device);
			using namespace terrain;
			TerrainNode root_node(glm::ivec3(0, 0, 0), glm::ivec3(0, 0, 0), glm::vec3(0.0f), root_node_length);
			HeightmapNoise root_noise(HeightNode::RootSampleGridSize, glm::vec3(0.0f), HeightNode::RootSampleGridSize / (HeightNode::RootNodeLength * 2.0));
			auto root_height = std::make_shared<HeightNode>(glm::ivec3(0, 0, 0), root_noise.samples);
			root_node.SetHeightData(root_height);

			// Subdivide first four nodes
			std::array<DataRequest*, 8> lod_1_requests;
			std::array<HeightNode*, 8> lod_1_nodes;
			glm::ivec3 grid_pos = glm::ivec3(root_node.GridCoordinates.x * 2, root_node.GridCoordinates.y * 2, root_node.Depth() + 1);
			lod_1_nodes[0] = new HeightNode(grid_pos, *root_node.HeightData, false);
			lod_1_nodes[1] = new HeightNode(grid_pos + glm::ivec3(1, 0, 0), *root_node.HeightData, false);
			lod_1_nodes[2] = new HeightNode(grid_pos + glm::ivec3(0, 1, 0), *root_node.HeightData, false);
			lod_1_nodes[3] = new HeightNode(grid_pos + glm::ivec3(1, 1, 0), *root_node.HeightData, false);
			for (size_t i = 0; i < 4; ++i) {
				lod_1_requests[i] = DataRequest::UpsampleRequest(lod_1_nodes[i], root_node.HeightData.get(), device);
				producer.Request(lod_1_requests[i]);
			}

			for (size_t i = 4; i < 8; ++i) {
				lod_1_requests[i] = lod_1_requests[i - 4];
				producer.Request(lod_1_requests[i]);
			}

			producer.RecordCommands();
			producer.Submit();

			for (size_t i = 0; i < 8; ++i) {
				auto& result = lod_1_requests[i]->Result;

				size_t num_samples = lod_1_requests[i]->node->NumSamples();
				
				glm::vec2* mapped;
				vkMapMemory(device->vkHandle(), result->DvcMemory(), 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&mapped));
				
				for (size_t i = 0; i < num_samples; ++i) {
					std::cout << glm::to_string(mapped[i]) << "\n";
				}

				/*auto min_max = std::minmax_element(result_heights.cbegin(), result_heights.cend());
				float min_z, max_z;
				min_z = result_heights.at(min_max.first - result_heights.cbegin());
				max_z = result_heights.at(min_max.second - result_heights.cbegin());*/
				std::string fname("compute_test_node_");
				fname += std::to_string(i);
				fname += std::string(".png");
				//save_hm_to_file(result_heights, min_z, max_z, fname.c_str(), num_samples / num_samples, num_samples / num_samples);
				result->Unmap();
			}
		}

		virtual void WindowResized() override {}
		virtual void RecreateObjects() override {}
		virtual void RecordCommands() override {}

	private:

		std::shared_ptr<PipelineCache> pipelineCache;
		terrain::TerrainQuadtree* object;
	};

}

#endif // !VULPES_COMPUTE_TESTS_H
