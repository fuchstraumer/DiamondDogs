#include "PassBuilders.hpp"
#include "Declarations.hpp"
#include "LoomKeys.hpp"
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

        // The public Narrow has no "reflected" value; an Invalid one maps to Invalid, and Freeze reports it (LM101).
        BindNarrowing ToBindNarrowing(Narrow narrow) noexcept
        {
            switch (narrow)
            {
            case Narrow::Invalid:
                return BindNarrowing::Invalid;
            case Narrow::ReadOnly:
                return BindNarrowing::ReadOnly;
            case Narrow::WriteOnly:
                return BindNarrowing::WriteOnly;
            }

            return BindNarrowing::Invalid;
        }

    }

    DispatchBuilder::DispatchBuilder(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity,
        std::source_location where)
        : record{ std::make_unique<PassRecord>() }
    {
        record->Key = pass.ToRef();
        record->Kind = PassKind::Dispatch;
        record->Policy = run_policy;
        record->Affinity = queue_affinity;
        record->Where = where;
    }

    DispatchBuilder::~DispatchBuilder() = default;

    void DispatchBuilder::Shader(std::string_view module_name, std::string_view entry_point, std::source_location where)
    {
        ShaderRecord shader{};
        shader.ModuleName = std::string{ module_name };
        shader.EntryPoint = std::string{ entry_point };
        shader.Where = where;
        record->Shaders.push_back(std::move(shader));
    }

    void DispatchBuilder::Pin(std::string_view axis_name, AxisValue axis_value, std::source_location where)
    {
        record->Pins.push_back(PinRecord{ .AxisName = std::string{ axis_name }, .Value = std::move(axis_value), .Where = where });
    }

    void DispatchBuilder::After(AnchorKey anchor, std::source_location where)
    {
        record->After.push_back(AnchorUseRecord{ .Anchor = anchor.ToRef(), .Where = where });
    }

    void DispatchBuilder::Before(AnchorKey anchor, std::source_location where)
    {
        record->Before.push_back(AnchorUseRecord{ .Anchor = anchor.ToRef(), .Where = where });
    }

    void DispatchBuilder::Bind(std::string_view binding_name, ResourceKey<Buffer> resource, std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Buffer, BindNarrowing::Reflected, false, where));
    }

    void DispatchBuilder::Bind(std::string_view binding_name, ResourceKey<Buffer> resource, Narrow narrow,
        std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Buffer, ToBindNarrowing(narrow), false, where));
    }

    void DispatchBuilder::Bind(std::string_view binding_name, ResourceKey<Image> resource, std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Image, BindNarrowing::Reflected, false, where));
    }

    void DispatchBuilder::Bind(std::string_view binding_name, ResourceKey<Image> resource, Narrow narrow,
        std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Image, ToBindNarrowing(narrow), false, where));
    }

    void DispatchBuilder::Bind(std::string_view binding_name, ResourceKey<Sampler> resource, std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Sampler, BindNarrowing::Reflected, false, where));
    }

    // A history read never writes, so its narrowing is ReadOnly by construction.
    void DispatchBuilder::BindPrevious(std::string_view binding_name, ResourceKey<Buffer> resource, std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Buffer, BindNarrowing::ReadOnly, true, where));
    }

    void DispatchBuilder::BindPrevious(std::string_view binding_name, ResourceKey<Image> resource, std::source_location where)
    {
        record->Binds.push_back(MakeBind(binding_name, resource.ToRef(), ResourceKind::Image, BindNarrowing::ReadOnly, true, where));
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

    void DispatchBuilder::addParams(KeyRef params, uint32_t byte_size, uint32_t alignment, std::source_location where)
    {
        record->Params.push_back(ParamsRecord{ .Params = params, .ByteSize = byte_size, .Alignment = alignment, .Where = where });
    }

    std::unique_ptr<PassRecord> DispatchBuilder::takeRecord() noexcept
    {
        return std::move(record);
    }

}
