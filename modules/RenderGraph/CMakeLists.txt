
set(LOOM_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/Compile.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/CompiledGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/DecideRuns.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/FramePlan.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/LoomDeviceLimits.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ParamsStore.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PlanBarriers.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/Declarations.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/GraphBuilder.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/LoomDiagnostic.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/LoomError.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/LoomFormats.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/LoomKeys.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PassBuilders.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PassStatements.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SequenceBuilder.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/freeze/FrozenGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/freeze/Freeze.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/freeze/Publish.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/freeze/ResolveNames.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/freeze/ResolveDraw.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/freeze/ResolveKey.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/freeze/Validate.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Records.hpp"
)

# Each source sits beside its own header; a *Context.hpp holds a stage's shared state and has no source.
set(LOOM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Declarations.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/freeze/FrozenGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/GraphBuilder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/LoomDiagnostic.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/LoomError.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/LoomFormats.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PassBuilders.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PassStatements.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SequenceBuilder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/OrderContext.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/OrderedGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/OrderedGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/Accesses.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/Accesses.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/Edges.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/Edges.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/Explain.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/Explain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/Sort.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/Sort.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/DataChecks.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/DataChecks.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/Liveness.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/order/Liveness.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resolve/ResolveContext.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resolve/ResolvedGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resolve/ResolvedGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resolve/Shaders.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resolve/Shaders.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resolve/Pins.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resolve/Pins.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resolve/Variants.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resolve/Variants.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resolve/Helpers.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resolve/Helpers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/BindContext.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/BoundGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/BoundGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/Layout.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/Layout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/Slots.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/Slots.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/Params.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/Params.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/Helpers.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/Helpers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/size/SizedGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/size/Size.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/size/Size.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/size/ImageSize.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/size/ImageSize.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/LayoutContext.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/LaidOutGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Layout.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Layout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Dispatch.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Dispatch.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Attachments.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Attachments.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Feedback.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Feedback.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Limits.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Limits.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Diagnostics.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/Diagnostics.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compile/CompiledData.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compile/Compile.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compile/CompiledGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compile/Pipelines.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compile/Pipelines.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compile/Uses.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compile/Uses.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/FramePlan.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/DecideRuns.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/PlanBarriers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/ParamsStore.cpp"
)

add_module(loom
    ${LOOM_HEADERS}
    ${LOOM_SOURCES}
)

# The core links no RHI: each backend (loom_vulkan below, loom_webgpu later) does. The core reads two plain headers
# of RhiResources by path only, for SamplerCreateInfo: ResourceTypes.hpp and RhiResourceFlags.hpp include nothing but
# foundation. It does not link rhi_resources, which links the Vulkan RHI.
target_include_directories(loom PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../RhiResources/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")

# The freeze/ headers include the stage headers in src/ by name.
target_include_directories(loom PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(loom PUBLIC foundation shader_bundle)

bootstrap_assert_no_rhi(loom)

# The Vulkan backend: Realize, recording and submit. It reads the core's compiled tables (src/compile), so it sees the
# core's src folder; the core never sees this one.
add_module(loom_vulkan
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/include/LoomGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/src/LoomGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/src/GraphImpl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/src/GraphImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/src/Realize.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/src/Realize.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/src/Tick.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/src/Tick.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/src/VulkanMapping.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/src/VulkanMapping.cpp"
)

target_include_directories(loom_vulkan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/include")
target_include_directories(loom_vulkan PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/src")
target_link_libraries(loom_vulkan PUBLIC loom rhi_system rhi_resources)