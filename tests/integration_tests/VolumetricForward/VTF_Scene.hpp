#pragma once
#ifndef VTF_SCENE_HPP
#define VTF_SCENE_HPP
#include "VulkanScene.hpp"
#include "ForwardDecl.hpp"
#include "vtfFrameData.hpp"
#include <memory>
#include <vector>
#include <future>
#include <atomic>
#include <unordered_map>
#include "vtfStructs.hpp"
#include "common/ShaderStage.hpp"
#include "glm/vec4.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include <array>
#include <list>

struct VulkanResource;
struct ComputePipelineState;
class Descriptor;
class DescriptorPack;
class vtf_frame_data_t;
class DescriptorBinder;

namespace st {
    class ShaderPack;
}

class VTF_Scene final : public VulkanScene {
public:

    static VTF_Scene& Get();

    void AddObjectRenderFn(const vtf_frame_data_t::obj_render_fn_t& function);
    void Construct(RequiredVprObjects objects, void* user_data) final;
    void Destroy() final;

    std::future<bool> LoadingTask;
    std::atomic<bool> isLoading{ true };

private:

    void updateGlobalUBOs();
    void update() final;
    void recordCommands() final;
    void draw() final;
    void endFrame() final;
	void acquireImage() final;
    void present() final;


    VulkanResource* loadTexture(const char * file_path_str, const char* img_name);

    void createIcosphereTester();

    std::vector<std::unique_ptr<vtf_frame_data_t>> frames;
	std::vector<bool> frameSingleExecComputeWorkDone;
    std::unique_ptr<struct TestIcosphereMesh> icosphereTester;
	uint32_t activeFrame{ 0u };

};

#endif // !VTF_SCENE_HPP
