#pragma once
#ifndef GENERATED_PIPELINE_HPP
#define GENERATED_PIPELINE_HPP
#include "ForwardDecl.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace st {
    class Shader;
    struct ShaderStage;
}

class ShaderResourcePack;

class GeneratedPipeline {
    GeneratedPipeline(const GeneratedPipeline&) = delete;
    GeneratedPipeline& operator=(const GeneratedPipeline&) = delete;
public:

    GeneratedPipeline(const st::Shader* _shader);

    void Bake();

    VkPipeline Handle() const;

private:
    friend class ShaderResourcePack;

    const st::Shader* shader{ nullptr };
    std::unique_ptr<vpr::GraphicsPipelineInfo> pipelineInfo;
    std::unique_ptr<vpr::PipelineCache> cache;

};

#endif // !GENERATED_PIPELINE_HPP
