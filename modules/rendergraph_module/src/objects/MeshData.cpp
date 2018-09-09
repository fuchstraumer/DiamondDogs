#include "resources/MeshData.hpp"
#include "resource/Buffer.hpp"
namespace vpsk {

    void MeshData::CreateBuffers(const vpr::Device * device) {
        
        VBO0 = std::make_unique<vpr::Buffer>(device);
        VBO1 = std::make_unique<vpr::Buffer>(device);
        EBO = std::make_unique<vpr::Buffer>(device);

        VBO0->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, static_cast<VkDeviceSize>(sizeof(glm::vec3) * Positions.size()));
        VBO1->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, static_cast<VkDeviceSize>(sizeof(vertex_data_t) * Vertices.size()));
        EBO->CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, static_cast<VkDeviceSize>(sizeof(uint32_t) * Indices.size()));

        vboStaging0 = vpr::Buffer::CreateStagingBuffer(device, Positions.data(), Positions.size() * sizeof(glm::vec3));
        vboStaging1 = vpr::Buffer::CreateStagingBuffer(device, Vertices.data(), Vertices.size() * sizeof(vertex_data_t));
        eboStaging = vpr::Buffer::CreateStagingBuffer(device, Indices.data(), Indices.size() * sizeof(uint32_t));

    }

    void MeshData::TransferToDevice(VkCommandBuffer cmd) {
        VBO0->CopyTo(vboStaging0.get(), cmd, 0);
        VBO1->CopyTo(vboStaging1.get(), cmd, 0);
        EBO->CopyTo(eboStaging.get(), cmd, 0);
    }

    void MeshData::FreeStagingBuffers() {
        vboStaging0.reset();
        vboStaging1.reset();
        eboStaging.reset();
        Positions.clear(); Positions.shrink_to_fit();
        Vertices.clear(); Vertices.shrink_to_fit();
        Indices.clear(); Indices.shrink_to_fit();
    }

    std::vector<VkBuffer> MeshData::GetVertexBuffers() const {
        return std::vector<VkBuffer>{ VBO0->vkHandle(), VBO1->vkHandle() };
    }

    MeshData::MeshData() noexcept : VBO0{ nullptr }, VBO1{ nullptr }, EBO{ nullptr }, vboStaging0{ nullptr }, vboStaging1{ nullptr }, eboStaging{ nullptr } {}

    MeshData::~MeshData() {}

    MeshData::MeshData(MeshData && other) noexcept : VBO0(std::move(other.VBO0)), VBO1(std::move(other.VBO1)), EBO(std::move(other.EBO)), Positions(std::move(other.Positions)),
        Vertices(std::move(other.Vertices)), Indices(std::move(other.Indices)), vboStaging0(std::move(other.vboStaging0)), vboStaging1(std::move(other.vboStaging1)), eboStaging(std::move(other.eboStaging)) { }

    MeshData& MeshData::operator=(MeshData && other) noexcept {
        VBO0 = std::move(other.VBO0);
        VBO1 = std::move(other.VBO1);
        EBO = std::move(other.EBO);
        Positions = std::move(other.Positions);
        Vertices = std::move(other.Vertices);
        Indices = std::move(other.Indices);
        vboStaging0 = std::move(other.vboStaging0);
        vboStaging1 = std::move(other.vboStaging1);
        eboStaging = std::move(other.eboStaging);
        return *this;
    }

    MeshData::operator VertexBufferComponent() const {
        return VertexBufferComponent(GetVertexBuffers());
    }

    MeshData::operator IndexBufferComponent() const {
        return IndexBufferComponent(EBO->vkHandle());
    }

    AnimatedMeshData::AnimatedMeshData() noexcept : MeshData(), VBO2{nullptr}, vboStaging2{nullptr} {}

    AnimatedMeshData::~AnimatedMeshData() {}

    AnimatedMeshData::AnimatedMeshData(AnimatedMeshData&& other) noexcept : MeshData(std::move(other)), VBO2(std::move(other.VBO2)),
        vboStaging2(std::move(other.vboStaging2)), AnimationData(std::move(AnimationData)) {}

    AnimatedMeshData& AnimatedMeshData::operator=(AnimatedMeshData&& other) noexcept {
        MeshData::operator=(std::move(other));
        VBO2 = std::move(other.VBO2);
        vboStaging2 = std::move(other.vboStaging2);
        AnimationData = std::move(other.AnimationData);
        return *this;
    }

    void AnimatedMeshData::CreateBuffers(const vpr::Device * device) {
        MeshData::CreateBuffers(device);

        VBO2 = std::make_unique<vpr::Buffer>(device);
        VBO2->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
            sizeof(vertex_animation_data_t) * AnimationData.size());
        
        vboStaging2 = vpr::Buffer::CreateStagingBuffer(device, AnimationData.data(), sizeof(vertex_animation_data_t) * AnimationData.size());

    }

    void AnimatedMeshData::TransferToDevice(VkCommandBuffer cmd) {
        MeshData::TransferToDevice(cmd);
        VBO2->CopyTo(vboStaging2.get(), cmd, 0);
    }

    void AnimatedMeshData::FreeStagingBuffers() {
        MeshData::FreeStagingBuffers();
        vboStaging2.reset();
        AnimationData.clear(); AnimationData.shrink_to_fit();
    }

    std::vector<VkBuffer> AnimatedMeshData::GetVertexBuffers() const {
        return std::vector<VkBuffer>{ VBO0->vkHandle(), VBO1->vkHandle(), VBO2->vkHandle() };
    }
    
}
