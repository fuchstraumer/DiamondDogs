#pragma once
#ifndef VULPES_MESH_H
#define VULPES_MESH_H
#include "stdafx.h"
#include "MeshComponents.h"
#include "engine/renderer/ForwardDecl.h"


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
		Mesh(const Mesh& other) = delete;
		Mesh& operator=(const Mesh& other) = delete;

	public:
		Mesh(const glm::vec3& pos = glm::vec3(0.0f), const glm::vec3& _scale = glm::vec3(1.0f), const glm::vec3& _angle = glm::vec3(0.0f)) : position(pos), scale(_scale), angle(_angle) {
			vbo.fill(nullptr);
			ebo = nullptr;
			glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
			glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
			glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
			glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
			glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), angle.z, glm::vec3(0.0f, 0.0f, 1.0f));
			model = translationMatrix * rotX * rotY * rotZ * scaleMatrix;
		}

		Mesh(Mesh&& other);
		Mesh& operator=(Mesh&& other);

		~Mesh();

		vertex_t get_vertex(const uint32_t& idx) const;

		uint32_t add_vertex(const vertex_t& v);
		uint32_t add_vertex(vertex_t&& v);

		void add_triangle(const uint32_t& i0, const uint32_t& i1, const uint32_t& i2);

		void resize(const uint32_t& sz);

		glm::mat4 model;
		glm::vec3 position, scale, angle;

		glm::mat4 get_model_matrix() const;

		glm::mat4 get_rte_mv(const glm::mat4& view) const;

		void create_vbo(const Device* render_device, CommandPool* cmd_pool, const VkQueue& queue);

		void create_buffers(const Device* device);

		void transfer_to_device(CommandPool* cmd_pool, const VkQueue& queue);

		void record_transfer_commands(VkCommandBuffer& transfer_cmd);

		void render(const VkCommandBuffer& cmd) const;

		void cleanup();

		void free_cpu_data();

		void destroy_vk_resources();

		bool Ready() const noexcept { return ready; }

		Vertices vertices;
		std::vector<uint32_t> indices;

	protected:
		void create_ebo(CommandPool* cmd_pool, const VkQueue& queue);
		
	private:
		// Position, normal, UV
		std::array<Buffer*, 2> vbo;

		// Indices.
		Buffer* ebo;
		bool ready = false;
		const Device* device;
	};

}
#endif //!VULPES_MESH_H