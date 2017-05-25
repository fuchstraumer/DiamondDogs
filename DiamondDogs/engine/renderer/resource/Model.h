#pragma once
#ifndef VULPES_VK_MODEL_H
#define VULPES_VK_MODEL_H
#include "stdafx.h"

#include "objects\ForwardDecl.h"
#include "objects\NonCopyable.h"

#include "VertexTypes.h"
#include "glm/gtx/hash.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tinyobj/tiny_obj_loader.h"

namespace std {
	template<> struct hash<vulpes::Vertex3D> {
		size_t operator()(vulpes::Vertex3D const& vertex) const {
			size_t h0 = std::hash<glm::vec3>()(vertex.Position);
			size_t h1 = std::hash<glm::vec2>()(vertex.UV);
			return h0 ^ (h1 << 1);
		}
	};
}

namespace vulpes {

	struct ModelInfo {
		glm::vec3 Position = glm::vec3(0.0f);
		glm::vec3 Scale = glm::vec3(1.0f);
		glm::vec3 Rotation = glm::vec3(0.0f);
		glm::vec2 UV_Scale = glm::vec2(1.0f);
	};

	template<typename vertex_type>
	class Model : public NonCopyable {
	public:
		Model(const Device* parent);

		~Model();

		/*
		Creates and loads the VBO + EBO
		*/
		void Create(CommandPool* cmd, VkQueue& queue);

		void SetVertices(const std::vector<vertex_type>& vertices);
		void SetIndices(const std::vector<uint32_t>& indices);

		const VkBuffer& vboHandle() const noexcept;
		const VkBuffer& eboHandle() const noexcept;

		const std::vector<vertex_type>& VerticesRef() const noexcept;

		size_t NumIndices() const noexcept;

		virtual size_t NumVertices() const noexcept;

		ModelInfo Info;

	protected:

		const Device* parent;
		const VkAllocationCallbacks* allocators = nullptr;
		Buffer *vbo, *ebo;
		std::vector<uint32_t> indices;
		std::vector<vertex_type> vertices;

	};

	/*
		Imports obj using tinyobj to keep things light.
	*/
	template<typename vertex_type>
	class objModel : public Model<vertex_type> {
	public:

		// Loads obj model at filename.
		objModel(const char* filename, const Device* parent);

		~objModel();

		// Creates vulkan resources
		void Create(const VkCommandBuffer& cmd, const VkQueue& queue, const char* texture_filename);

		Texture2D_STB* modelTexture;

	private:
		
	};


	class aiModel : public Model<float> {
	public:

		aiModel(const char* filename, const std::vector<VertexComponents> desired_components, const Device* parent);

		~aiModel() {
			if (modelTexture != nullptr) {
				delete modelTexture;
			}
		}

		VertexLayout Layout;

		void Create(CommandPool* cmd, VkQueue& queue, const char* texture_filename);

		Texture2D_STB* modelTexture;

		virtual size_t NumVertices() const noexcept override;

		struct Part {
			uint32_t VertexIdxStart;
			uint32_t VertexCount;
			uint32_t IndexStart;
			uint32_t IndexCount;
		};
		std::vector<Part> Parts;

	private:
		static constexpr int defaultAiFlags = aiProcess_FlipWindingOrder | aiProcess_Triangulate | aiProcess_PreTransformVertices | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals;
		
	
	};


	template<typename vertex_type>
	inline objModel<vertex_type>::objModel(const char * filename, const Device * _parent) : Model<vertex_type>(_parent) {
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filename)) {
			throw std::runtime_error("Failed to import .obj file");
		}

		std::unordered_map<Vertex3D, int> uniqueVertices{};

		for (const auto& shape : shapes) {
			for (const auto& index : shape.mesh.indices) {
				Vertex3D vertex = {};

				vertex.Position = {
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2]
				};

				vertex.UV = {
					attrib.texcoords[2 * index.texcoord_index + 0],
					1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
				};

				if (uniqueVertices.count(vertex) == 0) {
					uniqueVertices[vertex] = vertices.size();
					vertices.push_back(vertex);
				}

				indices.push_back(uniqueVertices[vertex]);
			}
		}
	}

	template<typename vertex_type>
	inline objModel<vertex_type>::~objModel(){
		delete modelTexture;
	}

	template<typename vertex_type>
	inline void objModel<vertex_type>::Create(const VkCommandBuffer & cmd, const VkQueue & queue, const char * texture_filename) {

		// Call inherited function to setup the VBO+EBO
		Model<vertex_type>::Create(cmd, queue);

		// Set up the texture objects 

		modelTexture = new Texture2D_STB(texture_filename, parent);
		modelTexture->Create(modelTexture->GetExtents(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		modelTexture->SendToDevice(cmd, queue);

		VkImageViewCreateInfo view_info = vk_image_view_create_info_base;
		view_info.image = modelTexture->vkHandle();
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
		modelTexture->CreateView(view_info);

		VkSamplerCreateInfo sampler_info = vk_sampler_create_info_base;
		sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		modelTexture->CreateSampler(sampler_info);
	}

	template<typename vertex_type>
	inline Model<vertex_type>::Model(const Device * _parent) : parent(_parent) {}

	template<typename vertex_type>
	inline Model<vertex_type>::~Model(){
		delete ebo; 
		delete vbo;
	}

	template<typename vertex_type>
	inline void Model<vertex_type>::Create(CommandPool* cmd, VkQueue & queue){
		// Set up the buffer objects

		vbo = new Buffer(parent);
		vbo->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(vertex_type) * vertices.size());

		vbo->CopyTo((void*)vertices.data(), cmd, queue, sizeof(vertex_type) * vertices.size());

		ebo = new Buffer(parent);
		ebo->CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(uint32_t) * indices.size());
		ebo->CopyTo((void*)indices.data(), cmd, queue, indices.size() * sizeof(uint32_t));
	}

	template<typename vertex_type>
	inline void Model<vertex_type>::SetVertices(const std::vector<vertex_type>& _vertices){
		vertices = _vertices;
	}

	template<typename vertex_type>
	inline void Model<vertex_type>::SetIndices(const std::vector<uint32_t>& _indices) {
		indices = _indices;
	}

	template<typename vertex_type>
	inline const VkBuffer & Model<vertex_type>::vboHandle() const noexcept{
		return vbo->vkHandle();
	}

	template<typename vertex_type>
	inline const VkBuffer & Model<vertex_type>::eboHandle() const noexcept{
		return ebo->vkHandle();
	}

	template<typename vertex_type>
	inline const std::vector<vertex_type>& Model<vertex_type>::VerticesRef() const noexcept{
		return vertices;
	}

	template<typename vertex_type>
	inline size_t Model<vertex_type>::NumIndices() const noexcept {
		return indices.size();
	}

	template<typename vertex_type>
	inline size_t Model<vertex_type>::NumVertices() const noexcept{
		return vertices.size();
	}

	inline aiModel::aiModel(const char * filename, const std::vector<VertexComponents> desired_components, const Device * parent) : Model<float>(parent), modelTexture(nullptr) {
		const aiScene* scene;
		Assimp::Importer importer;
		scene = importer.ReadFile(filename, defaultAiFlags);
		Layout.Components = desired_components;
		if (!scene) {
			throw std::runtime_error("Failed to read model file");
		}

		Parts.resize(scene->mNumMeshes);

		uint32_t num_verts = 0, num_indices = 0;

		for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
			const aiMesh* curr = scene->mMeshes[i];
			Parts[i] = {};
			Parts[i].VertexIdxStart = num_verts;
			Parts[i].IndexStart = num_indices;

			num_verts += curr->mNumVertices;
			Parts[i].VertexCount = curr->mNumVertices;
			vertices.reserve(vertices.capacity() + curr->mNumVertices);

			const aiVector3D zero_vec(0.0f, 0.0f, 0.0f);
			aiColor3D color(0.0f, 0.0f, 0.0f);
			scene->mMaterials[curr->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE, color);

			for (unsigned int j = 0; j < curr->mNumVertices; ++j) {
				const aiVector3D& pos = curr->mVertices[j];
				const aiVector3D& norm = curr->mNormals[j];
				const aiVector3D& uv = curr->HasTextureCoords(0) ? curr->mTextureCoords[0][j] : zero_vec;
				const aiVector3D& tangent = curr->HasTangentsAndBitangents() ? curr->mTangents[j] : zero_vec;
				const aiVector3D& bitangent = curr->HasTangentsAndBitangents() ? curr->mBitangents[j] : zero_vec;
				for (const auto& cmp : Layout.Components) {
					switch (cmp) {
					case VertexComponents::POS:
						vertices.push_back(pos.x * Info.Scale.x + Info.Position.x);
						vertices.push_back(pos.y * Info.Scale.y + Info.Position.y);
						vertices.push_back(pos.z * Info.Scale.z + Info.Position.z);
						break;
					case VertexComponents::NORM:
						vertices.push_back(norm.x);
						vertices.push_back(norm.y);
						vertices.push_back(norm.z);
						break;
					case VertexComponents::UV:
						vertices.push_back(uv.x * Info.UV_Scale.x);
						vertices.push_back((1.0f - uv.y) * Info.UV_Scale.y);
						break;
					case VertexComponents::COLOR:
						vertices.push_back(color.r);
						vertices.push_back(color.g);
						vertices.push_back(color.b);
						break;
					case VertexComponents::TANGENT:
						vertices.push_back(tangent.x);
						vertices.push_back(tangent.y);
						vertices.push_back(tangent.z);
						break;
					case VertexComponents::BITANGENT:
						vertices.push_back(bitangent.x);
						vertices.push_back(bitangent.y);
						vertices.push_back(bitangent.z);
						break;
					case VertexComponents::PADDING_FLOAT:
						vertices.push_back(0.0f);
						break;
					case VertexComponents::PADDING_VEC4:
						vertices.push_back(0.0f);
						vertices.push_back(0.0f);
						vertices.push_back(0.0f);
						vertices.push_back(0.0f);
						break;
					}
				}
			}

			uint32_t idx_start = static_cast<uint32_t>(indices.size());
			for (unsigned int j = 0; j < curr->mNumFaces; ++j) {
				const aiFace& face = curr->mFaces[j];
				if (face.mNumIndices != 3) {
					continue;
				}
				indices.push_back(idx_start + static_cast<uint32_t>(face.mIndices[0]));
				indices.push_back(idx_start + static_cast<uint32_t>(face.mIndices[1]));
				indices.push_back(idx_start + static_cast<uint32_t>(face.mIndices[2]));
				Parts[i].IndexCount += 3;
				num_indices += 3;
			}
		}
		indices.shrink_to_fit();
		vertices.shrink_to_fit();
	}

	inline void aiModel::Create(CommandPool* cmd, VkQueue & queue, const char * texture_filename) {
		// Call inherited function to setup the VBO+EBO
		Model<float>::Create(cmd, queue);

		// Set up the texture objects 
		if (texture_filename != nullptr) {
			modelTexture = new Texture2D_STB(texture_filename, parent);
			modelTexture->Create(modelTexture->GetExtents(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			modelTexture->SendToDevice(cmd, queue);

			VkImageViewCreateInfo view_info = vk_image_view_create_info_base;
			view_info.image = modelTexture->vkHandle();
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_info.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
			modelTexture->CreateView(view_info);

			VkSamplerCreateInfo sampler_info = vk_sampler_create_info_base;
			sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			modelTexture->CreateSampler(sampler_info);
		}
		else {
			modelTexture = nullptr;
		}
	}

	inline size_t aiModel::NumVertices() const noexcept {
		uint32_t stride = Layout.Stride();
		stride /= sizeof(float);
		return vertices.size() / stride;
	}

}

#endif // !VULPES_VK_MODEL_H
