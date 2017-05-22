#pragma once
#ifndef VULPES_MESH_H
#define VULPES_MESH_H
#include "stdafx.h"
#include "MeshComponents.h"
#include "engine/renderer/objects\ForwardDecl.h"


namespace vulpes {

	struct vert_data {
		glm::vec3 normal;
		glm::vec2 uv;
	};

	struct Vertices {
		std::vector<glm::vec3> positions;
		std::vector<vert_data> normals_uvs;

		size_t size() const noexcept {
			return positions.size();
		}

		void resize(const size_t& amt) noexcept {
			positions.resize(amt);
			normals_uvs.resize(amt);
		}

		static const std::array<VkVertexInputBindingDescription, 2> BindDescr() noexcept {
			return std::array<VkVertexInputBindingDescription, 2> {
				VkVertexInputBindingDescription{ 0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX },
				VkVertexInputBindingDescription{ 1, sizeof(vert_data), VK_VERTEX_INPUT_RATE_VERTEX },
			};
		};

		static const std::array<VkVertexInputAttributeDescription, 3> AttrDescr() noexcept {
			return std::array<VkVertexInputAttributeDescription, 3> {
				VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
				VkVertexInputAttributeDescription{ 1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0 },
				VkVertexInputAttributeDescription{ 2, 1, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec3) },
			};
		};

		static const VkPipelineVertexInputStateCreateInfo PipelineInfo() noexcept {
			auto bind = BindDescr();
			auto attr = AttrDescr();
			return VkPipelineVertexInputStateCreateInfo{
				VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
				nullptr,
				0,
				2,
				bind.data(),
				3, 
				attr.data(),
			};
		}
	};



	class Mesh {
	public:
		Mesh() : position(0.0f), scale(1.0f, 1.0f, 1.0f), angle(0.0f) {
			model = get_model_matrix();
		}

		~Mesh();

		vertex_t get_vertex(const uint32_t& idx) const;

		uint32_t add_vertex(const vertex_t& v);
		uint32_t add_vertex(vertex_t&& v);

		void add_triangle(const uint32_t& i0, const uint32_t& i1, const uint32_t& i2);

		glm::mat4 model;
		glm::vec3 position, scale, angle;

		glm::mat4 get_model_matrix() const;

		glm::mat4 get_rte_mv(const glm::mat4& view) const;

		void create_vbo(const Device* render_device, CommandPool* cmd_pool, const VkQueue& queue);

		void create_ebo(CommandPool* cmd_pool, const VkQueue& queue);

		void render(const VkCommandBuffer& cmd);

	protected:
		Vertices vertices;
		std::vector<uint32_t> indices;
	private:
		// Position, normal, UV
		std::array<Buffer*, 2> vbo;

		// Indices.
		Buffer* ebo;

		const Device* device;
	};

	/*
		Specialized mesh instance for rendering relative-to-eye
	*/

	struct SOA_vertices_rte {

		std::vector<glm::vec3> positions_low;
		std::vector<glm::vec3> positions_high;
		std::vector<glm::vec3> normals;
		std::vector<glm::vec2> uvs;

		rte_vertex_t operator[](const uint32_t& idx) const {
			return rte_vertex_t{ positions_low[idx], positions_high[idx], normals[idx], uvs[idx] };
		}

		size_t size() const {
			return positions_low.size();
		}

		uint32_t add_vertex(const glm::dvec3& double_position, const glm::vec3& normal = glm::vec3(0.0f), const glm::vec2& uv = glm::vec2(0.0f)) {
			auto double_to_floats = [](const double& value)->std::pair<float, float> {
				std::pair<float, float> result;
				if (value >= 0.0) {
					double high = floorf(value / 65536.0) * 65536.0;
					result.first = (float)high;
					result.second = (float)(value - high);
				}
				else {
					double high = floorf(-value / 65536.0) * 65536.0;
					result.first = (float)-high;
					result.second = (float)(value + high);
				}
				return result;
			};
			auto xx = double_to_floats(double_position.x);
			auto yy = double_to_floats(double_position.y);
			auto zz = double_to_floats(double_position.z);
			positions_high.push_back(glm::vec3(xx.first, yy.first, zz.first));
			positions_low.push_back(glm::vec3(xx.second, yy.second, zz.second));
			normals.push_back(normal);
			uvs.push_back(uv);
			return static_cast<uint32_t>(positions_high.size() - 1);
		}

		void resize(const size_t& amt) {
			positions_low.resize(amt);
			positions_high.resize(amt);
			normals.resize(amt);
			uvs.resize(amt);
		}
	};

}
#endif //!VULPES_MESH_H