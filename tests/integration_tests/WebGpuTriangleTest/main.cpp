#include "BootResult.hpp"
#include "DeviceRequirements.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextWebGpu.hpp"
#include "RhiContext.hpp"
#include "RhiContextWebGpu.hpp"
#include "WebGpuDeviceStage.hpp"

#include <emscripten/emscripten.h>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <print>
#include <span>

namespace
{

struct TriangleVertex
{
    float Position[2];
    float Color[3];
};

constexpr TriangleVertex k_TriangleVertices[] =
{
    { {  0.00f,  0.60f }, { 1.0f, 0.15f, 0.20f } },
    { { -0.60f, -0.55f }, { 0.15f, 0.95f, 0.30f } },
    { {  0.60f, -0.55f }, { 0.20f, 0.45f, 1.00f } },
};

constexpr const char* k_TriangleShaderWgsl = R"(
struct VertexInput
{
    @location(0) position : vec2f,
    @location(1) color : vec3f,
};

struct VertexOutput
{
    @builtin(position) clipPosition : vec4f,
    @location(0) color : vec3f,
};

@vertex
fn vsMain(input : VertexInput) -> VertexOutput
{
    var output : VertexOutput;
    output.clipPosition = vec4f(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}

@fragment
fn fsMain(input : VertexOutput) -> @location(0) vec4f
{
    return vec4f(input.color, 1.0);
}
)";

struct AppState
{
    bootstrap::WebGpuDeviceStage DeviceStage;
    std::unique_ptr<ResourceContext> Resources;
    GraphicsResource VertexBuffer;
    wgpu::Buffer WgpuVertexBuffer;
    wgpu::RenderPipeline Pipeline;
    wgpu::Surface Surface;
    wgpu::Device Device;
    wgpu::Queue Queue;
    bool InitializedPipeline{ false };
    bool Failed{ false };
    int32_t FrameCount{ 0 };
};

[[nodiscard]] bool InitializeTriangleResources(AppState& state);
void RenderTriangleFrame(AppState& state);
void MainLoopTick(void* user_data);

} // namespace

int main()
{
    EM_ASM(
        window['__ddFrameCount'] = 0;
        window['__ddWebGpuError'] = false;
    );

    AppState state{};
    bootstrap::DeviceConfig deviceConfig{};
    deviceConfig.Validation = rhi::ValidationLayers::BaseOnly;
    deviceConfig.ApplicationName = "WebGpuTriangleTest";

    bootstrap::WebGpuSurfaceConfig surfaceConfig{};
    surfaceConfig.CanvasSelector = "#canvas";
    surfaceConfig.Width = 800u;
    surfaceConfig.Height = 600u;

    const bootstrap::DeviceRequirements requirements{};

    state.DeviceStage.Start(requirements, deviceConfig, surfaceConfig);
    emscripten_set_main_loop_arg(&MainLoopTick, &state, 0, 1);
    return 0;
}

namespace
{

bool InitializeTriangleResources(AppState& state)
{
    const rhi::WebGpuRhiContextView view = rhi::UnwrapWebGpuRhiContext(state.DeviceStage.Context());
    const wgpu::Surface surface = state.DeviceStage.Surface();
    const wgpu::TextureFormat surfaceFormat = state.DeviceStage.SurfaceFormat();

    if (!view.Device || !view.Queue || !surface || surfaceFormat == wgpu::TextureFormat::Undefined)
    {
        std::println(stderr, "[WebGpuTriangleTest] WebGPU context or surface handles are missing");
        return false;
    }

    state.Resources = std::make_unique<ResourceContext>();
    const ResourceContextCreateInfo resourceInfo
    {
        .Context = &state.DeviceStage.Context(),
    };

    if (state.Resources->Initialize(resourceInfo) != ResourceContextError::Success)
    {
        std::println(stderr, "[WebGpuTriangleTest] ResourceContext::Initialize failed");
        return false;
    }

    const BufferCreateInfo bufferInfo
    {
        .Domain = ResourceDomain::GPUOnly,
        .Flags = ResourceCreationFlags::None,
        .Usage = BufferUsageBits::VertexInput,
        .Size = sizeof(k_TriangleVertices),
        .Name = "TriangleVertexBuffer",
    };

    const RhiBufferResourceData initialData[] =
    {
        RhiBufferResourceData
        {
            .Data = std::as_bytes(std::span{ k_TriangleVertices }),
            .DestinationOffset = 0u,
        },
    };

    const rhi::ResourceFuture<GraphicsResource> vertexFuture = state.Resources->CreateBuffer(bufferInfo, initialData);

    if (vertexFuture.State() != rhi::FutureState::Complete)
    {
        std::println(stderr,
                     "[WebGpuTriangleTest] CreateBuffer failed: {}",
                     rhi::ToString(vertexFuture.Error()));
        return false;
    }

    state.VertexBuffer = vertexFuture.Get();
    state.WgpuVertexBuffer = rhi::UnwrapWebGpuBuffer(state.VertexBuffer);

    if (!state.WgpuVertexBuffer)
    {
        std::println(stderr, "[WebGpuTriangleTest] UnwrapWebGpuBuffer returned null");
        return false;
    }

    wgpu::ShaderSourceWGSL wgslSource{};
    wgslSource.code = k_TriangleShaderWgsl;

    wgpu::ShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = &wgslSource;
    shaderDesc.label = "TriangleShader";

    const wgpu::ShaderModule shaderModule = view.Device.CreateShaderModule(&shaderDesc);

    if (!shaderModule)
    {
        std::println(stderr, "[WebGpuTriangleTest] CreateShaderModule failed");
        return false;
    }

    const wgpu::VertexAttribute attributes[] =
    {
        wgpu::VertexAttribute
        {
            .format = wgpu::VertexFormat::Float32x2,
            .offset = 0u,
            .shaderLocation = 0u,
        },
        wgpu::VertexAttribute
        {
            .format = wgpu::VertexFormat::Float32x3,
            .offset = sizeof(float) * 2u,
            .shaderLocation = 1u,
        },
    };

    wgpu::VertexBufferLayout vertexLayout{};
    vertexLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexLayout.arrayStride = sizeof(TriangleVertex);
    vertexLayout.attributeCount = 2u;
    vertexLayout.attributes = attributes;

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = surfaceFormat;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fragmentState{};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "fsMain";
    fragmentState.targetCount = 1u;
    fragmentState.targets = &colorTarget;

    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.label = "TrianglePipeline";
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vsMain";
    pipelineDesc.vertex.bufferCount = 1u;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    pipelineDesc.fragment = &fragmentState;

    state.Pipeline = view.Device.CreateRenderPipeline(&pipelineDesc);

    if (!state.Pipeline)
    {
        std::println(stderr, "[WebGpuTriangleTest] CreateRenderPipeline failed");
        return false;
    }

    state.Surface = surface;
    state.Device = view.Device;
    state.Queue = view.Queue;
    state.InitializedPipeline = true;
    return true;
}

void RenderTriangleFrame(AppState& state)
{
    wgpu::SurfaceTexture surfaceTexture{};
    state.Surface.GetCurrentTexture(&surfaceTexture);

    const bool textureUsable =
        surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal ||
        surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal;

    if (!textureUsable || !surfaceTexture.texture)
    {
        return;
    }

    const wgpu::TextureView backbufferView = surfaceTexture.texture.CreateView();

    if (!backbufferView)
    {
        return;
    }

    wgpu::RenderPassColorAttachment colorAttachment{};
    colorAttachment.view = backbufferView;
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.clearValue = wgpu::Color{ 0.08, 0.09, 0.14, 1.0 };

    wgpu::RenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1u;
    passDesc.colorAttachments = &colorAttachment;

    const wgpu::CommandEncoder encoder = state.Device.CreateCommandEncoder();
    const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
    pass.SetPipeline(state.Pipeline);
    pass.SetVertexBuffer(0u, state.WgpuVertexBuffer, 0u, sizeof(k_TriangleVertices));
    pass.Draw(3u, 1u, 0u, 0u);
    pass.End();

    const wgpu::CommandBuffer commands = encoder.Finish();
    state.Queue.Submit(1u, &commands);

    ++state.FrameCount;
    EM_ASM(
        {
            window['__ddFrameCount'] = $0;
        },
        state.FrameCount);

    if (state.FrameCount == 10)
    {
        std::println("[WebGpuTriangleTest] rendered 10 frames");
    }
}

void MainLoopTick(void* user_data)
{
    AppState& state = *static_cast<AppState*>(user_data);

    if (state.Failed)
    {
        return;
    }

    if (!state.DeviceStage.IsReady())
    {
        state.DeviceStage.Step();

        if (state.DeviceStage.HasFailed())
        {
            std::println(stderr,
                         "[WebGpuTriangleTest] boot failed: {}",
                         bootstrap::FormatFailure(state.DeviceStage.Failure()));
            state.Failed = true;
            EM_ASM(
                window['__ddWebGpuError'] = true;
            );
            emscripten_cancel_main_loop();
            return;
        }

        if (!state.DeviceStage.IsReady())
        {
            return;
        }

        // the device gets the adapter's own limits: WebGPU's defaults are 16384 bytes and 256 invocations
        const bootstrap::DeviceFilter filter = state.DeviceStage.Filter();

        std::println("[WebGpuTriangleTest] device limits: workgroup storage {} bytes, {} invocations per workgroup",
                     filter.Limits.MaxWorkgroupMemoryBytes,
                     filter.Limits.MaxInvocationsPerWorkgroup);
    }

    if (!state.InitializedPipeline)
    {
        if (!InitializeTriangleResources(state))
        {
            state.Failed = true;
            EM_ASM(
                window['__ddWebGpuError'] = true;
            );
            emscripten_cancel_main_loop();
            return;
        }
    }

    RenderTriangleFrame(state);
}

} // namespace
