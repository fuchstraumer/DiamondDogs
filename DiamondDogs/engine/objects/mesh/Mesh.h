#pragma once
#ifndef VULPES_MESH_H
#define VULPES_MESH_H
#include "stdafx.h"
#include "MeshComponents.h"
#include "engine/renderer//core//LogicalDevice.h"
#include "engine/renderer/resource/Buffer.h"

namespace vulpes {

	namespace mesh {

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

			void reserve(const size_t& amt) noexcept {
				positions.reserve(amt);
				normals_uvs.reserve(amt);
			}

			void shrink_to_fit() noexcept {
				positions.shrink_to_fit();
				normals_uvs.shrink_to_fit();
			}

			void push_back(vertex_t vert) {
				positions.push_back(std::move(vert.Position));
				normals_uvs.push_back(std::move(vert_data{ std::move(vert.Normal), std::move(vert.UV) }));
			}

			size_t PositionsSize() const noexcept {
				return positions.size() * sizeof(glm::vec3);
			}

			size_t DataSize() const noexcept {
				return normals_uvs.size() * sizeof(vert_data);
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


		struct TerrainVertices {
			std::vector<glm::vec4> positions;
			std::vector<vert_data> normals_uvs;

			size_t size() const noexcept {
				return positions.size();
			}

			void resize(const size_t& amt) noexcept {
				positions.resize(amt);
				normals_uvs.resize(amt);
			}

			void reserve(const size_t& amt) noexcept {
				positions.reserve(amt);
				normals_uvs.reserve(amt);
			}

			void shrink_to_fit() noexcept {
				positions.shrink_to_fit();
				normals_uvs.shrink_to_fit();
			}

			size_t PositionsSize() const noexcept {
				return positions.size() * sizeof(glm::vec4);
			}

			size_t DataSize() const noexcept {
				return normals_uvs.size() * sizeof(vert_data);
			}

			static const std::array<VkVertexInputBindingDescription, 2> BindDescr() noexcept {
				return std::array<VkVertexInputBindingDescription, 2> {
					VkVertexInputBindingDescription{ 0, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX },
					VkVertexInputBindingDescription{ 1, sizeof(vert_data), VK_VERTEX_INPUT_RATE_VERTEX },
				};
			};

			static const std::array<VkVertexInputAttributeDescription, 3> AttrDescr() noexcept {
				return std::array<VkVertexInputAttributeDescription, 3> {
					VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
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


		template<typename vertices_type = Vertices, typename vertex_type = vertex_t, typename index_type = uint32_t>
		class Mesh {
			Mesh(const Mesh& other) = delete;
			Mesh& operator=(const Mesh& other) = delete;

		public:

			Mesh(const glm::vec3& pos = glm::vec3(0.0f), const glm::vec3& _scale = glm::vec3(1.0f), const glm::vec3& _angle = glm::vec3(0.0f));

			Mesh(Mesh&& other);
			Mesh& operator=(Mesh&& other);

			virtual ~Mesh();

			vertex_t get_vertex(const index_type& idx) const;

			index_type add_vertex(const vertex_type& v);
			index_type add_vertex(vertex_type&& v);
			void add_triangle(const index_type& i0, const index_type& i1, const index_type& i2);

			void resize(const index_type& sz);

			glm::mat4 get_model_matrix() const;
			glm::mat4 update_model_matrix();

			void create_buffers(const Device* render_device);

			void transfer_to_device(CommandPool* cmd_pool, const VkQueue& queue);
			void record_transfer_commands(VkCommandBuffer& transfer_cmd);

			void render(const VkCommandBuffer& cmd) const;

			void cleanup();
			void free_cpu_data();
			void destroy_vk_resources();

			bool Ready() const noexcept { return ready; }

			vertices_type vertices;
			std::vector<index_type> indices;

			glm::mat4 model;
			glm::vec3 position, scale, angle;

		protected:

			// Position, normal, UV
			std::array<Buffer*, 2> vbo;

			// Indices.
			Buffer* ebo;
			bool ready = false;
			const Device* device;
		};


		template<typename vertices_type, typename vertex_type, typename index_type>
		inline Mesh<vertices_type, vertex_type, index_type>::Mesh(const glm::vec3 & pos, const glm::vec3 & _scale, const glm::vec3 & _angle) : position(pos), scale(_scale), angle(_angle) {
			vbo.fill(nullptr);
			ebo = nullptr;
			glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
			glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
			glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
			glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
			glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), angle.z, glm::vec3(0.0f, 0.0f, 1.0f));
			model = translationMatrix * rotX * rotY * rotZ * scaleMatrix;
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline Mesh<vertices_type, vertex_type, index_type>::Mesh(Mesh&& other) {
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

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline Mesh<vertices_type, vertex_type, index_type>& Mesh<vertices_type, vertex_type, index_type>::operator=(Mesh && other) {
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
		
		template<typename vertices_type, typename vertex_type, typename index_type>
		inline Mesh<vertices_type, vertex_type, index_type>::~Mesh() {
			cleanup();
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline vertex_t Mesh<vertices_type, vertex_type, index_type>::get_vertex(const index_type & idx) const{
			return vertices[idx];
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline index_type Mesh<vertices_type, vertex_type, index_type>::add_vertex(const vertex_type & v){
			vertices.push_back(v);
			return vertices.size() - 1;
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline index_type Mesh<vertices_type, vertex_type, index_type>::add_vertex(vertex_type && v) {
			vertices.push_back(std::move(v));
			return vertices.size() - 1;
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline void Mesh<vertices_type, vertex_type, index_type>::add_triangle(const index_type & i0, const index_type & i1, const index_type & i2) {
			indices.insert(indices.end(), { i0, i1, i2 });
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline void Mesh<vertices_type, vertex_type, index_type>::resize(const index_type & sz) {
			vertices.resize(sz);
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline glm::mat4 Mesh<vertices_type, vertex_type, index_type>::get_model_matrix() const{
			return model;
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline glm::mat4 Mesh<vertices_type, vertex_type, index_type>::update_model_matrix(){
			glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
			glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
			glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
			glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
			glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), angle.z, glm::vec3(0.0f, 0.0f, 1.0f));
			model = translationMatrix * rotX * rotY * rotZ * scaleMatrix;
			return model;
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline void Mesh<vertices_type, vertex_type, index_type>::create_buffers(const Device * render_device) {
			if (ready) {
				return;
			}

			this->device = render_device;

			VkDeviceSize sz = vertices.PositionsSize();

			vbo[0] = new Buffer(device);
			vbo[0]->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sz);

			sz = vertices.DataSize();
			vbo[1] = new Buffer(device);
			vbo[1]->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sz);

			ebo = new Buffer(device);
			ebo->CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indices.size() * sizeof(index_type));

			ready = true;
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline void Mesh<vertices_type, vertex_type, index_type>::transfer_to_device(CommandPool * cmd_pool, const VkQueue & queue) {
			VkCommandBuffer transfer_cmd = cmd_pool->StartSingleCmdBuffer();
			record_transfer_commands(transfer_cmd);
			cmd_pool->EndSingleCmdBuffer(transfer_cmd, queue);
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline void Mesh<vertices_type, vertex_type, index_type>::record_transfer_commands(VkCommandBuffer & transfer_cmd) {
			vbo[0]->CopyTo(vertices.positions.data(), transfer_cmd, vbo[0]->AllocSize(), 0);
			vbo[1]->CopyTo(vertices.normals_uvs.data(), transfer_cmd, vbo[1]->AllocSize(), 0);
			ebo->CopyTo(indices.data(), transfer_cmd, ebo->AllocSize(), 0);
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline void Mesh<vertices_type, vertex_type, index_type>::render(const VkCommandBuffer & cmd) const {
			static const VkDeviceSize offsets[]{ 0 };
			VkBuffer buffers[2]{ vbo[0]->vkHandle(), vbo[1]->vkHandle() };
			vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
			if (typeid(index_type) == typeid(uint16_t)) {
				vkCmdBindIndexBuffer(cmd, ebo->vkHandle(), 0, VK_INDEX_TYPE_UINT16);
			}
			// Default to 32-bit uint
			else {
				vkCmdBindIndexBuffer(cmd, ebo->vkHandle(), 0, VK_INDEX_TYPE_UINT32);
			}
			vkCmdDrawIndexed(cmd, static_cast<index_type>(indices.size()), 1, 0, 0, 0);
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline void Mesh<vertices_type, vertex_type, index_type>::cleanup() {
			free_cpu_data();
			if (!ready) {
				return;
			}
			destroy_vk_resources();
			ready = false;
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline void Mesh<vertices_type, vertex_type, index_type>::free_cpu_data() {
			vertices.positions.clear();
			vertices.positions.shrink_to_fit();
			vertices.normals_uvs.clear();
			vertices.normals_uvs.shrink_to_fit();
			ready = false;
		}

		template<typename vertices_type, typename vertex_type, typename index_type>
		inline void Mesh<vertices_type, vertex_type, index_type>::destroy_vk_resources() {
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


} // !NAMESPACE_MESH

} // !NAMESPACE_VULPES
#endif //!VULPES_MESH_H