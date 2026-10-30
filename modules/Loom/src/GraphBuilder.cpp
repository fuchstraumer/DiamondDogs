#include "GraphBuilder.hpp"
#include "Declarations.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "Records.hpp"
#include "SequenceBuilder.hpp"
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace loom
{

    GraphBuilder::GraphBuilder(std::string_view name, std::string_view shader_library_name)
        : record{ std::make_unique<GraphRecord>() }
    {
        record->Name = std::string{ name };
        record->ShaderLibraryName = std::string{ shader_library_name };
    }

    GraphBuilder::~GraphBuilder() = default;
    GraphBuilder::GraphBuilder(GraphBuilder&&) noexcept = default;
    GraphBuilder& GraphBuilder::operator=(GraphBuilder&&) noexcept = default;

    void GraphBuilder::Pin(std::string_view axis_name, AxisValue value, std::source_location where)
    {
        record->Pins.push_back(PinRecord{ .AxisName = std::string{ axis_name }, .Value = std::move(value), .Where = where });
    }

    void GraphBuilder::Declare(const BufferDeclaration& declaration, std::source_location where)
    {
        record->Buffers.push_back(DeclarationRecord<BufferDeclaration>{ .Value = declaration, .Where = where });
    }

    void GraphBuilder::Declare(const ImageDeclaration& declaration, std::source_location where)
    {
        record->Images.push_back(DeclarationRecord<ImageDeclaration>{ .Value = declaration, .Where = where });
    }

    void GraphBuilder::Declare(const SamplerDeclaration& declaration, std::source_location where)
    {
        record->Samplers.push_back(DeclarationRecord<SamplerDeclaration>{ .Value = declaration, .Where = where });
    }

    void GraphBuilder::Import(const ImportDeclaration& declaration, std::source_location where)
    {
        record->Imports.push_back(DeclarationRecord<ImportDeclaration>{ .Value = declaration, .Where = where });
    }

    void GraphBuilder::Import(FrameTargetKey target, std::source_location where)
    {
        record->FrameTargets.push_back(FrameTargetImportRecord{ .Target = target.ToRef(), .Where = where });
    }

    void GraphBuilder::Output(const OutputDeclaration& declaration, std::source_location where)
    {
        record->Outputs.push_back(DeclarationRecord<OutputDeclaration>{ .Value = declaration, .Where = where });
    }

    void GraphBuilder::DeclareAnchor(AnchorKey anchor, std::source_location where)
    {
        record->Anchors.push_back(AnchorDeclarationRecord{ .Anchor = anchor.ToRef(), .Where = where });
    }

    void GraphBuilder::OrderAnchors(AnchorKey earlier, AnchorKey later, std::source_location where)
    {
        record->AnchorOrders.push_back(AnchorOrderRecord{ .Earlier = earlier.ToRef(), .Later = later.ToRef(), .Where = where });
    }

    const GraphRecord& GraphBuilder::Record() const noexcept
    {
        return *record;
    }

    void GraphBuilder::commitDispatch(DispatchBuilder& pass)
    {
        std::unique_ptr<PassRecord> passRecord = pass.takeRecord();
        passRecord->DeclarationOrder = record->NextDeclarationOrder++;
        record->Passes.push_back(std::move(*passRecord));
    }

    // A nested pass gets its declaration order when its sequence commits, so the order is the commit order. A pass
    // that a sequence lambda adds to the graph directly commits first, and so comes first.
    void GraphBuilder::commitSequence(SequenceBuilder& sequence)
    {
        std::unique_ptr<SequenceRecord> sequenceRecord = sequence.takeRecord();

        for (PassRecord& pass : sequenceRecord->Passes)
        {
            pass.DeclarationOrder = record->NextDeclarationOrder++;
        }

        record->Sequences.push_back(std::move(*sequenceRecord));
    }

}
