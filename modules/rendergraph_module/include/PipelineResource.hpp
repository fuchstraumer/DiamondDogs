#pragma once
#ifndef DIAMOND_DOGS_PIPELINE_RESOURCE_HPP
#define DIAMOND_DOGS_PIPELINE_RESOURCE_HPP
#include <vulkan/vulkan.h>
#include <string>
#include <variant>
#include <unordered_set>
#include <unordered_map>

struct buffer_info_t {
    VkDeviceSize Size{ 0 };
    VkBufferUsageFlags Usage{ 0 };
    // Used for texel buffers
    VkFormat Format{ VK_FORMAT_UNDEFINED };
    bool operator==(const buffer_info_t& other) const noexcept;
    bool operator!=(const buffer_info_t& other) const noexcept;
};

struct image_info_t {
    enum class size_class : uint8_t {
        Absolute,
        SwapchainRelative,
        InputRelative
    } SizeClass{ size_class::Absolute };
    float SizeX{ 1.0f };
    float SizeY{ 1.0f };
    VkSampleCountFlags Samples{ VK_SAMPLE_COUNT_1_BIT };
    uint32_t MipLevels{ 1 };
    uint32_t ArrayLayers{ 1 };
    float Anisotropy{ 1.0f };
    std::string SizeRelativeName{};
    VkImageUsageFlags Usage{ VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM };
    VkFormat Format{ VK_FORMAT_UNDEFINED };
    bool operator==(const image_info_t& other) const noexcept;
    bool operator!=(const image_info_t& other) const noexcept;
};

using resource_info_variant_t = std::variant<
    buffer_info_t,
    image_info_t
>;

struct resource_dimensions_t {
    VkFormat Format{ VK_FORMAT_UNDEFINED };
    buffer_info_t BufferInfo;
    uint32_t Width{ 0 };
    uint32_t Height{ 0 };
    uint32_t Depth{ 1 };
    uint32_t Layers{ 1 };
    uint32_t Levels{ 1 };
    uint32_t Samples{ 1 };
    bool Transient{ false };
    bool Persistent{ true };
    bool sRGB{ false };
    uint32_t Queues[4]{ 0, 0, 0, 0 };
    VkImageUsageFlags ImageUsage{ 0 };
    bool is_storage_image() const noexcept;
    bool requires_semaphore() const;
    bool is_buffer_like() const noexcept;
    bool operator==(const resource_dimensions_t& other) const noexcept;
    bool operator!=(const resource_dimensions_t& other) const noexcept;
};

class PipelineResource {
public:
        
    PipelineResource(std::string name, size_t physical_idx);
    ~PipelineResource();

    bool IsBuffer() const noexcept;
    bool IsImage() const noexcept;
    bool IsTransient() const noexcept;

    void SetWrittenBySubmission(size_t idx);
    void SetReadBySubmission(size_t idx);
    void SetPipelineStageFlagsForSubmission(size_t idx, VkPipelineStageFlagBits stage_flags);

    void SetIdx(size_t idx);
    void SetParentSetName(std::string _name);
    void SetName(std::string _name);
    void SetDescriptorType(VkDescriptorType type);
    void SetInfo(resource_info_variant_t _info);
    void SetTransient(const bool& _transient);
    void AddQueue(size_t submission_idx, uint32_t queue_idx);
    void AddBufferUsage(VkBufferUsageFlags flags);
    void AddImageUsage(VkImageUsageFlags flags);

    size_t GetIdx() const noexcept;
    const std::string& ParentSetName() const noexcept;
    VkDescriptorType DescriptorType() const noexcept;
    const std::string& Name() const noexcept;
    const std::unordered_set<size_t>& SubmissionsReadIn() const noexcept;
    const std::unordered_set<size_t>& SubmissionsWrittenIn() const noexcept;
    const resource_info_variant_t& GetInfo() const noexcept;
    const image_info_t& GetImageInfo() const;
    const buffer_info_t& GetBufferInfo() const;
    const std::unordered_map<size_t, uint32_t>& UsedQueues() const noexcept;
    VkPipelineStageFlagBits SubmissionStageFlags(const size_t idx) const noexcept;

    bool operator==(const PipelineResource& other) const noexcept;

private:

    size_t idx;
    // We don't care about the unique binding index of a resource relative to a set: just what
    // set it belongs to, as that helps us group things in a more important manner
    std::string parentSetName{};
    // Usually the name read from the shader
    std::string name{};
    // Extracted from the shader
    VkDescriptorType intendedType{ VK_DESCRIPTOR_TYPE_MAX_ENUM };
    // Resources that don't need to persist throughout the frame
    bool transient{ false };
    resource_info_variant_t info;
    std::unordered_set<size_t> readInPasses;
    std::unordered_set<size_t> writtenInPasses;
    std::unordered_map<size_t, uint32_t> usedQueues;
    std::unordered_map<size_t, VkPipelineStageFlagBits> submissionStages;
};

#endif //!DIAMOND_DOGS_PIPELINE_RESOURCE_HPP
