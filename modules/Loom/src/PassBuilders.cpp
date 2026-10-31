#include "PassBuilders.hpp"
#include "Declarations.hpp"
#include "LoomKeys.hpp"
#include "PassStatements.hpp"
#include "Records.hpp"
#include <cstdint>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace loom
{

    namespace
    {

        BindRecord MakeBind(std::string_view binding_name, KeyRef resource, ResourceKind kind, BindNarrowing narrowing,
            bool previous, std::source_location where)
        {
            BindRecord bind{};
            bind.BindingName = std::string{ binding_name };
            bind.Resource = resource;
            bind.Kind = kind;
            bind.Narrowing = narrowing;
            bind.Previous = previous;
            bind.Where = where;
            return bind;
        }

        // The public AccessModifier has no "reflected" value; an Invalid one maps to Invalid, and Freeze reports it (LM101).
        BindNarrowing ToBindNarrowing(AccessModifier access) noexcept
        {
            switch (access)
            {
            case AccessModifier::Invalid:
                return BindNarrowing::Invalid;
            case AccessModifier::ReadOnly:
                return BindNarrowing::ReadOnly;
            case AccessModifier::WriteOnly:
                return BindNarrowing::WriteOnly;
            }

            return BindNarrowing::Invalid;
        }

        ColorTargetRecord MakeColor(int32_t slot, KeyRef target, TargetKind kind, ColorLoad load, AttachmentStore store,
            BlendMode blend, std::source_location where)
        {
            ColorTargetRecord color{};
            color.Slot = slot;
            color.Target = target;
            color.Kind = kind;
            color.Load = load;
            color.Store = store;
            color.Blend = blend;
            color.Where = where;
            return color;
        }

    }

    // PassBuilder

    PassBuilder::PassBuilder(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, std::source_location where)
        : record{ std::make_unique<PassRecord>() }
    {
        record->Key = pass.ToRef();
        record->Policy = run_policy;
        record->Affinity = queue_affinity;
        record->Where = where;
    }

    PassBuilder::~PassBuilder() = default;

    void PassBuilder::After(AnchorKey anchor, std::source_location where)
    {
        record->After.push_back(AnchorUseRecord{ .Anchor = anchor.ToRef(), .Where = where });
    }

    void PassBuilder::Before(AnchorKey anchor, std::source_location where)
    {
        record->Before.push_back(AnchorUseRecord{ .Anchor = anchor.ToRef(), .Where = where });
    }

    std::unique_ptr<PassRecord> PassBuilder::takeRecord() noexcept
    {
        return std::move(record);
    }

    // ShaderPassBuilder

    void ShaderPassBuilder::Pin(std::string_view axis_name, AxisValue axis_value, std::source_location where)
    {
        record->Pins.push_back(PinRecord{ .AxisName = std::string{ axis_name }, .Value = std::move(axis_value), .Where = where });
    }

    void ShaderPassBuilder::Bind(std::string_view binding_name, ResourceKey<Buffer> resource, std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Buffer, BindNarrowing::Reflected, false, where));
    }

    void ShaderPassBuilder::Bind(std::string_view binding_name, ResourceKey<Buffer> resource, AccessModifier access,
        std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Buffer, ToBindNarrowing(access), false, where));
    }

    void ShaderPassBuilder::Bind(std::string_view binding_name, ResourceKey<Image> resource, std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Image, BindNarrowing::Reflected, false, where));
    }

    void ShaderPassBuilder::Bind(std::string_view binding_name, ResourceKey<Image> resource, AccessModifier access,
        std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Image, ToBindNarrowing(access), false, where));
    }

    void ShaderPassBuilder::Bind(std::string_view binding_name, ResourceKey<Sampler> resource, std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Sampler, BindNarrowing::Reflected, false, where));
    }

    // A history read never writes, so its narrowing is ReadOnly by construction.
    void ShaderPassBuilder::BindPrevious(std::string_view binding_name, ResourceKey<Buffer> resource, std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Buffer, BindNarrowing::ReadOnly, true, where));
    }

    void ShaderPassBuilder::BindPrevious(std::string_view binding_name, ResourceKey<Image> resource, std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Image, BindNarrowing::ReadOnly, true, where));
    }

    void ShaderPassBuilder::addParams(KeyRef params, uint32_t byte_size, uint32_t alignment, std::source_location where)
    {
        record->Params.push_back(ParamsRecord{ .Params = params, .ByteSize = byte_size, .Alignment = alignment, .Where = where });
    }

    // DispatchBuilder

    DispatchBuilder::DispatchBuilder(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity,
        std::source_location where)
        : ShaderPassBuilder{ pass, run_policy, queue_affinity, where }
    {
        record->Kind = PassKind::Dispatch;
    }

    void DispatchBuilder::Shader(std::string_view module_name, std::string_view entry_point, std::source_location where)
    {
        ShaderRecord shader{};
        shader.ModuleName = std::string{ module_name };
        shader.EntryPoint = std::string{ entry_point };
        shader.Where = where;
        record->Shaders.push_back(std::move(shader));
    }

    void DispatchBuilder::DispatchThreadsOver(ResourceKey<Image> resource, std::source_location where)
    {
        DispatchRecord dispatch{};
        dispatch.Form = DispatchForm::ThreadsOver;
        dispatch.Resource = resource.ToRef();
        dispatch.Where = where;
        record->Dispatches.push_back(dispatch);
    }

    void DispatchBuilder::DispatchThreads(int32_t x, int32_t y, int32_t z, std::source_location where)
    {
        DispatchRecord dispatch{};
        dispatch.Form = DispatchForm::Threads;
        dispatch.X = x;
        dispatch.Y = y;
        dispatch.Z = z;
        dispatch.Where = where;
        record->Dispatches.push_back(dispatch);
    }

    void DispatchBuilder::DispatchIndirect(ResourceKey<Buffer> arguments, std::source_location where)
    {
        DispatchRecord dispatch{};
        dispatch.Form = DispatchForm::Indirect;
        dispatch.Resource = arguments.ToRef();
        dispatch.Where = where;
        record->Dispatches.push_back(dispatch);
    }

    // DrawBuilder

    DrawBuilder::DrawBuilder(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, std::source_location where)
        : ShaderPassBuilder{ pass, run_policy, queue_affinity, where }
    {
        record->Kind = PassKind::Draw;
    }

    void DrawBuilder::Shaders(std::string_view module_name, std::string_view vertex_entry_point,
        std::string_view fragment_entry_point, std::source_location where)
    {
        ShaderRecord shader{};
        shader.ModuleName = std::string{ module_name };
        shader.EntryPoint = std::string{ vertex_entry_point };
        shader.FragmentEntryPoint = std::string{ fragment_entry_point };
        shader.Where = where;
        record->Shaders.push_back(std::move(shader));
    }

    void DrawBuilder::Color(int32_t slot, ResourceKey<Image> target, ColorLoad load, AttachmentStore store,
        BlendMode blend, std::source_location where)
    {
        record->Colors.push_back(MakeColor(slot, target.ToRef(), TargetKind::Image, load, store, blend, where));
    }

    void DrawBuilder::Color(int32_t slot, FrameTargetKey target, ColorLoad load, AttachmentStore store,
        BlendMode blend, std::source_location where)
    {
        record->Colors.push_back(MakeColor(slot, target.ToRef(), TargetKind::FrameTarget, load, store, blend, where));
    }

    void DrawBuilder::Depth(ResourceKey<Image> target, DepthLoad load, AttachmentStore store, rhi::CompareOp compare,
        DepthWrite write, std::source_location where)
    {
        DepthTargetRecord depth{};
        depth.Target = target.ToRef();
        depth.Load = load;
        depth.Store = store;
        depth.Compare = compare;
        depth.Write = write;
        depth.Where = where;
        record->Depths.push_back(depth);
    }

    void DrawBuilder::Raster(const RasterState& state, std::source_location where)
    {
        record->RasterStates.push_back(RasterStateRecord{ .State = state, .Where = where });
    }

    void DrawBuilder::VertexBuffer(int32_t slot, ResourceKey<Buffer> buffer, std::source_location where)
    {
        record->VertexBuffers.push_back(VertexBufferRecord{ .Slot = slot, .Buffer = buffer.ToRef(), .Where = where });
    }

    void DrawBuilder::IndexBuffer(ResourceKey<Buffer> buffer, IndexType type, std::source_location where)
    {
        record->IndexBuffers.push_back(IndexBufferRecord{ .Buffer = buffer.ToRef(), .Type = type, .Where = where });
    }

    void DrawBuilder::Draw(int32_t vertex_count, int32_t instance_count, int32_t first_vertex, int32_t first_instance,
        std::source_location where)
    {
        DrawRecord draw{};
        draw.Form = DrawForm::Draw;
        draw.VertexOrIndexCount = vertex_count;
        draw.InstanceCount = instance_count;
        draw.FirstVertexOrIndex = first_vertex;
        draw.FirstInstance = first_instance;
        draw.Where = where;
        record->Draws.push_back(draw);
    }

    void DrawBuilder::DrawIndexed(int32_t index_count, int32_t instance_count, int32_t first_index,
        int32_t vertex_offset, int32_t first_instance, std::source_location where)
    {
        DrawRecord draw{};
        draw.Form = DrawForm::DrawIndexed;
        draw.VertexOrIndexCount = index_count;
        draw.InstanceCount = instance_count;
        draw.FirstVertexOrIndex = first_index;
        draw.FirstInstance = first_instance;
        draw.VertexOffset = vertex_offset;
        draw.Where = where;
        record->Draws.push_back(draw);
    }

    void DrawBuilder::DrawIndirect(ResourceKey<Buffer> arguments, std::source_location where)
    {
        DrawRecord draw{};
        draw.Form = DrawForm::DrawIndirect;
        draw.Arguments = arguments.ToRef();
        draw.Where = where;
        record->Draws.push_back(draw);
    }

    // RecordBuilder

    RecordBuilder::RecordBuilder(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity,
        std::source_location where)
        : PassBuilder{ pass, run_policy, queue_affinity, where }
    {
        record->Kind = PassKind::Record;
    }

    void RecordBuilder::Uses(ResourceKey<Buffer> resource, ResourceAccess access, BufferUse use, std::source_location where)
    {
        UseRecord useRecord{};
        useRecord.Resource = resource.ToRef();
        useRecord.Kind = ResourceKind::Buffer;
        useRecord.Access = access;
        useRecord.AsBuffer = use;
        useRecord.Where = where;
        record->Uses.push_back(useRecord);
    }

    void RecordBuilder::Uses(ResourceKey<Image> resource, ResourceAccess access, ImageUse use, std::source_location where)
    {
        UseRecord useRecord{};
        useRecord.Resource = resource.ToRef();
        useRecord.Kind = ResourceKind::Image;
        useRecord.Target = TargetKind::Image;
        useRecord.Access = access;
        useRecord.AsImage = use;
        useRecord.Where = where;
        record->Uses.push_back(useRecord);
    }

    void RecordBuilder::Uses(FrameTargetKey target, ResourceAccess access, ImageUse use, std::source_location where)
    {
        UseRecord useRecord{};
        useRecord.Resource = target.ToRef();
        useRecord.Kind = ResourceKind::Image;
        useRecord.Target = TargetKind::FrameTarget;
        useRecord.Access = access;
        useRecord.AsImage = use;
        useRecord.Where = where;
        record->Uses.push_back(useRecord);
    }

    void RecordBuilder::Record(Delegate<void(RecordContext&)> callback, std::source_location where)
    {
        record->Recorders.push_back(RecorderRecord{ .Callback = callback, .Where = where });
    }

}
