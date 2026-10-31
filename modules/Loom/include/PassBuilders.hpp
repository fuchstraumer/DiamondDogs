#pragma once
#ifndef DIAMOND_DOGS_LOOM_PASS_BUILDERS_HPP
#define DIAMOND_DOGS_LOOM_PASS_BUILDERS_HPP
#include "Declarations.hpp"
#include "LoomKeys.hpp"
#include "PassStatements.hpp"
#include "utility/delegate.hpp"
#include <cstdint>
#include <memory>
#include <source_location>
#include <string_view>

namespace loom
{

    struct PassRecord;
    class GraphBuilder;
    class SequenceBuilder;

    // What every pass builder has: the record, and its place among the anchors. Every call records; none checks or
    // fails (Freeze judges). Only GraphBuilder and SequenceBuilder can make a builder, so a builder cannot exist
    // outside an Add* call.
    class PassBuilder
    {
    public:
        PassBuilder(const PassBuilder&) = delete;
        PassBuilder& operator=(const PassBuilder&) = delete;
        PassBuilder(PassBuilder&&) = delete;
        PassBuilder& operator=(PassBuilder&&) = delete;

        void After(AnchorKey anchor, std::source_location where = std::source_location::current());
        void Before(AnchorKey anchor, std::source_location where = std::source_location::current());

    protected:
        // Each final builder sets the record's PassKind in its own constructor.
        PassBuilder(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, std::source_location where);
        // Not virtual: nothing destroys a builder through a base pointer. Defined in the .cpp, where PassRecord is complete.
        ~PassBuilder();

        std::unique_ptr<PassRecord> record;

    private:
        friend class GraphBuilder;
        friend class SequenceBuilder;

        // Hands the finished record to the committer. The builder is empty afterwards.
        [[nodiscard]] std::unique_ptr<PassRecord> takeRecord() noexcept;
    };

    // What a pass with a shader adds: pins, binds and params. Dispatch and draw passes share these.
    class ShaderPassBuilder : public PassBuilder
    {
    public:
        // A pass pin refines a graph pin on the same axis (S9, G6).
        void Pin(std::string_view axis_name, AxisValue axis_value,
                 std::source_location where = std::source_location::current());

        void Bind(std::string_view binding_name, ResourceKey<Buffer> resource,
                  std::source_location where = std::source_location::current());
        void Bind(std::string_view binding_name, ResourceKey<Buffer> resource, AccessModifier access,
                  std::source_location where = std::source_location::current());
        void Bind(std::string_view binding_name, ResourceKey<Image> resource,
                  std::source_location where = std::source_location::current());
        void Bind(std::string_view binding_name, ResourceKey<Image> resource, AccessModifier access,
                  std::source_location where = std::source_location::current());
        void Bind(std::string_view binding_name, ResourceKey<Sampler> resource,
                  std::source_location where = std::source_location::current());

        // Reads the history slot: last frame's contents (R4).
        void BindPrevious(std::string_view binding_name, ResourceKey<Buffer> resource,
                          std::source_location where = std::source_location::current());
        void BindPrevious(std::string_view binding_name, ResourceKey<Image> resource,
                          std::source_location where = std::source_location::current());

        // The entry point constants (G8). T's size and alignment go into the record for the LM305 and LM704 checks.
        template<typename T>
        void Params(ParamsKey<T> params, std::source_location where = std::source_location::current())
        {
            addParams(params.ToRef(), static_cast<uint32_t>(sizeof(T)), static_cast<uint32_t>(alignof(T)), where);
        }

    protected:
        using PassBuilder::PassBuilder;
        ~ShaderPassBuilder() = default;

    private:
        void addParams(KeyRef params, uint32_t byte_size, uint32_t alignment, std::source_location where);
    };

    // The builder an AddDispatch lambda receives.
    class DispatchBuilder final : public ShaderPassBuilder
    {
    public:
        ~DispatchBuilder() = default;

        void Shader(std::string_view module_name, std::string_view entry_point,
                    std::source_location where = std::source_location::current());

        void DispatchThreadsOver(ResourceKey<Image> resource,
                                 std::source_location where = std::source_location::current());
        void DispatchThreads(int32_t x, int32_t y, int32_t z,
                             std::source_location where = std::source_location::current());
        void DispatchIndirect(ResourceKey<Buffer> arguments,
                              std::source_location where = std::source_location::current());

    private:
        friend class GraphBuilder;
        friend class SequenceBuilder;

        DispatchBuilder(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, std::source_location where);
    };

    // The builder an AddDraw lambda receives. Color target formats come from the images; Layout checks them against
    // the fragment shader's outputs (LM602).
    class DrawBuilder final : public ShaderPassBuilder
    {
    public:
        ~DrawBuilder() = default;

        // An empty fragment entry point is a depth-only draw.
        void Shaders(std::string_view module_name, std::string_view vertex_entry_point,
                     std::string_view fragment_entry_point,
                     std::source_location where = std::source_location::current());

        void Color(int32_t slot, ResourceKey<Image> target, ColorLoad load, AttachmentStore store, BlendMode blend,
                   std::source_location where = std::source_location::current());
        void Color(int32_t slot, FrameTargetKey target, ColorLoad load, AttachmentStore store, BlendMode blend,
                   std::source_location where = std::source_location::current());
        // Compare Always with DepthWrite::Disabled turns the depth test off.
        void Depth(ResourceKey<Image> target, DepthLoad load, AttachmentStore store, rhi::CompareOp compare,
                   DepthWrite write, std::source_location where = std::source_location::current());
        // Optional: without it, the draw uses RasterState's defaults.
        void Raster(const RasterState& state, std::source_location where = std::source_location::current());

        void VertexBuffer(int32_t slot, ResourceKey<Buffer> buffer,
                          std::source_location where = std::source_location::current());
        void IndexBuffer(ResourceKey<Buffer> buffer, IndexType type,
                         std::source_location where = std::source_location::current());

        void Draw(int32_t vertex_count, int32_t instance_count, int32_t first_vertex, int32_t first_instance,
                  std::source_location where = std::source_location::current());
        void DrawIndexed(int32_t index_count, int32_t instance_count, int32_t first_index, int32_t vertex_offset,
                         int32_t first_instance, std::source_location where = std::source_location::current());
        // Indexed when the pass has an IndexBuffer statement.
        void DrawIndirect(ResourceKey<Buffer> arguments,
                          std::source_location where = std::source_location::current());

    private:
        friend class GraphBuilder;
        friend class SequenceBuilder;

        DrawBuilder(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, std::source_location where);
    };

    // The builder an AddRecord lambda receives. Loom cannot see into the callback, so every resource it touches must be
    // a Uses statement: Order computes the pass's hazards from those alone (Q1).
    class RecordBuilder final : public PassBuilder
    {
    public:
        ~RecordBuilder() = default;

        void Uses(ResourceKey<Buffer> resource, ResourceAccess access, BufferUse use,
                  std::source_location where = std::source_location::current());
        void Uses(ResourceKey<Image> resource, ResourceAccess access, ImageUse use,
                  std::source_location where = std::source_location::current());
        void Uses(FrameTargetKey target, ResourceAccess access, ImageUse use,
                  std::source_location where = std::source_location::current());

        // The delegate does not own its object, so the object must outlive the compiled graph.
        void Record(Delegate<void(RecordContext&)> callback,
                    std::source_location where = std::source_location::current());

    private:
        friend class GraphBuilder;
        friend class SequenceBuilder;

        RecordBuilder(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, std::source_location where);
    };

}

#endif // !DIAMOND_DOGS_LOOM_PASS_BUILDERS_HPP
