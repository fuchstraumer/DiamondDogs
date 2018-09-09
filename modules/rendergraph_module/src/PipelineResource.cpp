#include "PipelineResource.hpp"

PipelineResource::PipelineResource(std::string _name, size_t physical_idx) : name(std::move(_name)), 
    idx(std::move(physical_idx))  {}

PipelineResource::~PipelineResource() {}

bool PipelineResource::IsBuffer() const noexcept {
    return std::holds_alternative<buffer_info_t>(info);
}

bool PipelineResource::IsImage() const noexcept {
    return std::holds_alternative<image_info_t>(info);
}

bool PipelineResource::IsStorage() const noexcept {
    return storage;
}

bool PipelineResource::IsTransient() const noexcept {
    return transient;
}

void PipelineResource::WrittenBySubmission(size_t idx) {
    writtenInPasses.emplace(std::move(idx));
}

void PipelineResource::ReadBySubmission(size_t idx) {
    readInPasses.emplace(std::move(idx));
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

void PipelineResource::SetUsedPipelineStages(const size_t& submission_idx, VkPipelineStageFlags stages) {
    pipelineStagesPerSubmission[submission_idx] = stages;
}

void PipelineResource::AddUsedPipelineStages(const size_t& submission_idx, VkPipelineStageFlags stages) {
    pipelineStagesPerSubmission[submission_idx] |= stages;
}

void PipelineResource::SetInfo(resource_info_variant_t _info) {
    info = std::move(_info);
}

void PipelineResource::SetStorage(const bool& _storage) {
    storage = _storage;
}

void PipelineResource::SetTransient(const bool& _transient) {
    transient = _transient;
}

const size_t& PipelineResource::GetIdx() const noexcept {
    return idx;
}

const std::string& PipelineResource::ParentSetName() const noexcept {
    return parentSetName;
}

const VkDescriptorType& PipelineResource::DescriptorType() const noexcept {
    return intendedType;
}

const std::string& PipelineResource::Name() const noexcept {
    return name;
}

VkPipelineStageFlags PipelineResource::PipelineStages(const size_t& submission_idx) const noexcept {
    auto iter = pipelineStagesPerSubmission.find(submission_idx);
    if (iter != std::end(pipelineStagesPerSubmission)) {
        return iter->second;
    }
    else {
        return VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM;
    }
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
    return (info == other.info) && (readInPasses == other.readInPasses) && (writtenInPasses == other.writtenInPasses) &&
        (pipelineStagesPerSubmission == other.pipelineStagesPerSubmission) && (name == other.name) && (intendedType == other.intendedType) &&
        (parentSetName == other.parentSetName) && (idx == other.idx) && (transient == other.transient) &&
        (storage == other.storage);
}

VkPipelineStageFlags PipelineResource::AllStagesUsedIn() const noexcept {
    VkPipelineStageFlags result = 0;
    for (auto& flags : pipelineStagesPerSubmission) {
        result |= flags.second;
    }
    return result;
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

bool resource_dimensions_t::operator==(const resource_dimensions_t & other) const noexcept {
    return (Format == other.Format) && (BufferInfo == other.BufferInfo) && (Width == other.Width) &&
        (Height == other.Height) && (Depth == other.Depth) && (Layers == other.Layers) && (Levels == other.Levels) &&
        (Samples == other.Samples) && (Transient == other.Transient) && (Persistent == other.Persistent) && (Storage == other.Storage);
}

bool resource_dimensions_t::operator!=(const resource_dimensions_t & other) const noexcept {
    return (Format != other.Format) || (BufferInfo != other.BufferInfo) || (Width != other.Width) ||
        (Height != other.Height) || (Depth != other.Depth) || (Layers != other.Layers) || (Levels != other.Levels) ||
        (Samples != other.Samples) || (Transient != other.Transient) || (Persistent != other.Persistent) || (Storage != other.Storage);
}
