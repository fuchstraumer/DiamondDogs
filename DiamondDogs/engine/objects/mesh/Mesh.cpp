#include "stdafx.h"
#include "Mesh.h"

#include "engine\renderer\resource\ShaderModule.h"
#include "engine\renderer\resource\Buffer.h"
#include "engine\renderer\core\LogicalDevice.h"
#include "engine\renderer\command\CommandPool.h"

namespace vulpes {

	namespace mesh {

		Mesh::Mesh(const glm::vec3 & pos, const glm::vec3 & _scale, const glm::vec3 & _angle) : position(pos), scale(_scale), angle(_angle) {
			vbo.fill(nullptr);
			ebo = nullptr;
			glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
			glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
			glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
			glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
			glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), angle.z, glm::vec3(0.0f, 0.0f, 1.0f));
			model = translationMatrix * rotX * rotY * rotZ * scaleMatrix;
		}

		Mesh::Mesh(Mesh && other) {
			vbo = std::move(other.vbo);
			ebo = std::move(other.ebo);
			vertices = std::move(other.vertices);
			indices = std::move(other.indices);
			ready = std::move(other.ready);
			device = std::move(other.device);
			model = std::move(other.model);
			angle = std::move(other.angle);
			position = std::move(other.position);
			scale = std::move(other.scale);
			other.vbo.fill(nullptr);
			other.ebo = nullptr;
		}

		Mesh & Mesh::operator=(Mesh && other) {
			vbo = std::move(other.vbo);
			ebo = std::move(other.ebo);
			vertices = std::move(other.vertices);
			indices = std::move(other.indices);
			ready = std::move(other.ready);
			device = std::move(other.device);
			model = std::move(other.model);
			angle = std::move(other.angle);
			position = std::move(other.position);
			scale = std::move(other.scale);
			other.vbo.fill(nullptr);
			other.ebo = nullptr;
			return *this;
		}

		Mesh::~Mesh() {
			cleanup();
		}

		vertex_t Mesh::get_vertex(const uint32_t& idx) const {
			return vertices.positions[idx];
		}

		uint32_t Mesh::add_vertex(const vertex_t & v) {
			vertices.positions.push_back(v.Position);
			vertices.normals_uvs.push_back(vert_data{ v.Normal, v.UV });
			return static_cast<uint32_t>(vertices.positions.size() - 1);
		}

		uint32_t Mesh::add_vertex(vertex_t && v) {
			vertices.positions.push_back(std::move(v.Position));
			vertices.normals_uvs.push_back(std::move(vert_data{ v.Normal, v.UV }));
			return static_cast<uint32_t>(vertices.positions.size() - 1);
		}

		void Mesh::add_triangle(const uint32_t & i0, const uint32_t & i1, const uint32_t & i2) {
			indices.push_back(i0);
			indices.push_back(i1);
			indices.push_back(i2);
		}

		void Mesh::resize(const uint32_t & sz) {
			vertices.resize(sz);
		}

		glm::mat4 Mesh::get_model_matrix() const {
			return model;
		}

		glm::mat4 Mesh::get_rte_mv(const glm::mat4& view) const {
			glm::mat4 mv = view * model;
			mv[0][3] = mv[1][3] = mv[2][3] = 0.0f;
			return glm::mat4();
		}

		void Mesh::create_vbo(const Device* render_device, CommandPool* cmd_pool, const VkQueue& queue) {

			if (ready) {
				return;
			}

			this->device = render_device;

			VkDeviceSize sz = vertices.size() * sizeof(glm::vec3);

			vbo[0] = new Buffer(device);
			vbo[0]->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sz);

			sz = vertices.normals_uvs.size() * sizeof(vert_data);
			vbo[1] = new Buffer(device);
			vbo[1]->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sz);

			create_ebo(cmd_pool, queue);

			ready = true;
		}

		void Mesh::create_buffers(const Device * _device) {
			if (ready) {
				return;
			}

			this->device = _device;

			VkDeviceSize sz = vertices.size() * sizeof(glm::vec3);

			vbo[0] = new Buffer(device);
			vbo[0]->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sz);

			sz = vertices.normals_uvs.size() * sizeof(vert_data);
			vbo[1] = new Buffer(device);
			vbo[1]->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sz);

			ebo = new Buffer(device);
			ebo->CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indices.size() * sizeof(uint32_t));

			ready = true;
		}

		void Mesh::transfer_to_device(CommandPool * cmd_pool, const VkQueue & queue) {
			VkCommandBuffer transfer_cmd = cmd_pool->StartSingleCmdBuffer();
			record_transfer_commands(transfer_cmd);
			cmd_pool->EndSingleCmdBuffer(transfer_cmd, queue);
		}

		void Mesh::create_ebo(CommandPool * cmd_pool, const VkQueue & queue) {
			ebo = new Buffer(device);
			ebo->CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indices.size() * sizeof(uint32_t));
		}

		void Mesh::record_transfer_commands(VkCommandBuffer & transfer_cmd) {
			vbo[0]->CopyTo(vertices.positions.data(), transfer_cmd, vbo[0]->AllocSize(), 0);
			vbo[1]->CopyTo(vertices.normals_uvs.data(), transfer_cmd, vbo[1]->AllocSize(), 0);
			ebo->CopyTo(indices.data(), transfer_cmd, ebo->AllocSize(), 0);
			//free_cpu_data();
		}

		void Mesh::render(const VkCommandBuffer & cmd) const {
			static const VkDeviceSize offsets[]{ 0 };
			VkBuffer buffers[2]{ vbo[0]->vkHandle(), vbo[1]->vkHandle() };
			vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
			vkCmdBindIndexBuffer(cmd, ebo->vkHandle(), 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
		}

		void Mesh::cleanup() {
			if (!ready) {
				return;
			}
			free_cpu_data();
			destroy_vk_resources();
			ready = false;
		}

		void Mesh::free_cpu_data() {
			vertices.positions.clear();
			vertices.positions.shrink_to_fit();
			vertices.normals_uvs.clear();
			vertices.normals_uvs.shrink_to_fit();
			if (ready) {
				ready = false;
			}
		}

		void Mesh::destroy_vk_resources() {
			if (vbo[0]) {
				vbo[0]->Destroy();
			}
			if (vbo[1]) {
				vbo[1]->Destroy();
			}
			if (ebo) {
				ebo->Destroy();
			}
			ready = false;
		}
	}
}
