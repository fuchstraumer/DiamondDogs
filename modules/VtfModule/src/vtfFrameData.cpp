#include "vtfFrameData.hpp"
#include "GraphicsPipeline.hpp"
#include "Renderpass.hpp"
#include "PipelineCache.hpp"
#include "Semaphore.hpp"
#include "ShaderModule.hpp"
#include "Fence.hpp"
#include "Framebuffer.hpp"
#include "CommandPool.hpp"

std::unordered_map<std::string, std::vector<st::ShaderStage>> vtf_frame_data_t::groupStages{};
std::unordered_map<std::string, std::unique_ptr<vpr::PipelineCache>> vtf_frame_data_t::pipelineCaches{};
std::unordered_map<st::ShaderStage, std::unique_ptr<vpr::ShaderModule>> vtf_frame_data_t::shaderModules{};

vtf_frame_data_t::vtf_frame_data_t() {}

vtf_frame_data_t::~vtf_frame_data_t() {}
