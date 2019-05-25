#include "PipelineResource.hpp"
#include <stdexcept>

PipelineResource::PipelineResource(std::string _name, size_t physical_idx) : name(std::move(_name)), 
    idx(std::move(physical_idx))  {}

PipelineResource::~PipelineResource() {}

bool PipelineResource::IsBuffer() const noexcept {
    return std::holds_alternative<buffer_info_t>(info);
}

bool PipelineResource::IsImage() const noexcept {
    return std::holds_alternative<image_info_t>(info);
}

bool PipelineResource::IsTransient() const noexcept {
    return transient;
}

void PipelineResource::SetWrittenBySubmission(size_t idx) {
    writtenInPasses.emplace(std::move(idx));
}

void PipelineResource::SetReadBySubmission(size_t idx) {
    readInPasses.emplace(std::move(idx));
}

void PipelineResource::SetPipelineStageFlagsForSubmission(size_t idx, VkPipelineStageFlagBits stage_flags)
{
    submissionStages.emplace(idx, stage_flags);
}

void PipelineResource::SetIdx(size_t _idx) {
    idx = std::move(_idx);
}

void PipelineResource::SetParentSetName(std::string _name) {
    parentSetName = std::move(_name);
}

void PipelineResource::SetName(std::string _name) {
    name = std::move(_name);
}

void PipelineResource::SetDescriptorType(VkDescriptorType type) {
    intendedType = std::move(type);
}

void PipelineResource::SetInfo(resource_info_variant_t _info) {
    info = std::move(_info);
}

void PipelineResource::SetTransient(const bool& _transient) {
    transient = _transient;
}

void PipelineResource::AddQueue(size_t submission_idx, uint32_t queue_idx)
{
    usedQueues.emplace(submission_idx, queue_idx);
}

size_t PipelineResource::GetIdx() const noexcept {
    return idx;
}

const std::string& PipelineResource::ParentSetName() const noexcept {
    return parentSetName;
}

VkDescriptorType PipelineResource::DescriptorType() const noexcept {
    return intendedType;
}

const std::string& PipelineResource::Name() const noexcept {
    return name;
}

const std::unordered_set<size_t>& PipelineResource::SubmissionsWrittenIn() const noexcept {
    return writtenInPasses;
}

const std::unordered_set<size_t>& PipelineResource::SubmissionsReadIn() const noexcept {
    return readInPasses;
}

const resource_info_variant_t& PipelineResource::GetInfo() const noexcept {
    return info;
}

const image_info_t& PipelineResource::GetImageInfo() const {
    if (IsImage()) {
        return std::get<image_info_t>(info);
    }
    else {
        throw std::logic_error("Attempted to retrieve ImageInfo for object that is not an image.");
    }
}

const buffer_info_t& PipelineResource::GetBufferInfo() const {
    if (IsBuffer()) {
        return std::get<buffer_info_t>(info);
    }
    else {
        throw std::logic_error("Attempted to retrieve buffer info for object that is not a buffer.");
    }
}

bool PipelineResource::operator==(const PipelineResource & other) const noexcept {
    return (readInPasses == other.readInPasses) && (writtenInPasses == other.writtenInPasses) &&
        (name == other.name) && (intendedType == other.intendedType) &&
        (parentSetName == other.parentSetName) && (idx == other.idx) && (transient == other.transient);
}

const std::unordered_map<size_t, uint32_t>& PipelineResource::UsedQueues() const noexcept {
    return usedQueues;
}

VkPipelineStageFlagBits PipelineResource::SubmissionStageFlags(const size_t idx) const noexcept
{
    return submissionStages.at(idx);
}

bool buffer_info_t::operator==(const buffer_info_t& other) const noexcept {
    return (Size == other.Size) && (Usage == other.Usage) && (Format == other.Format);
}

bool buffer_info_t::operator!=(const buffer_info_t & other) const noexcept {
    return (Size != other.Size) || (Usage != other.Usage) || (Format != other.Format);
}

bool image_info_t::operator==(const image_info_t& other) const noexcept {
    return (SizeClass == other.SizeClass) && (SizeX == other.SizeX) && (SizeY == other.SizeY) &&
        (Samples == other.Samples) && (MipLevels == other.MipLevels) && (ArrayLayers == other.ArrayLayers) &&
        (Anisotropy == other.Anisotropy) && (SizeRelativeName == other.SizeRelativeName) && (Usage == other.Usage) && 
        (Format == other.Format);
}

bool image_info_t::operator!=(const image_info_t & other) const noexcept{
    return (SizeClass != other.SizeClass) || (SizeX != other.SizeX) || (SizeY != other.SizeY) ||
        (Samples != other.Samples) || (MipLevels != other.MipLevels) || (ArrayLayers != other.ArrayLayers) ||
        (Anisotropy != other.Anisotropy) || (SizeRelativeName != other.SizeRelativeName) || (Usage != other.Usage) ||
        (Format != other.Format);
}

bool resource_dimensions_t::is_storage_image() const noexcept {
    return (ImageUsage & VK_IMAGE_USAGE_STORAGE_BIT);
}

bool resource_dimensions_t::requires_semaphore() const {
    throw std::runtime_error("Better implement this.");
}

bool resource_dimensions_t::is_buffer_like() const noexcept {
    return is_storage_image() && (BufferInfo.Size != 0);
}

bool resource_dimensions_t::operator==(const resource_dimensions_t & other) const noexcept {
    return (Format == other.Format) && (BufferInfo == other.BufferInfo) && (Width == other.Width) &&
        (Height == other.Height) && (Depth == other.Depth) && (Layers == other.Layers) && (Levels == other.Levels) &&
        (Samples == other.Samples) && (Transient == other.Transient) && (Persistent == other.Persistent);
}

bool resource_dimensions_t::operator!=(const resource_dimensions_t & other) const noexcept {
    return (Format != other.Format) || (BufferInfo != other.BufferInfo) || (Width != other.Width) ||
        (Height != other.Height) || (Depth != other.Depth) || (Layers != other.Layers) || (Levels != other.Levels) ||
        (Samples != other.Samples) || (Transient != other.Transient) || (Persistent != other.Persistent);
}
