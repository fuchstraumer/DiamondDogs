#include "PathListBuilder.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "Records.hpp"
#include <cstdint>
#include <memory>
#include <source_location>
#include <utility>

namespace loom
{

    PathBuilder::PathBuilder(PathKey path, std::source_location where)
        : record{ std::make_unique<PathRecord>() }
    {
        record->Key = path.ToRef();
        record->Where = where;
        record->Body.Where = where;
    }

    PathBuilder::~PathBuilder() = default;

    void PathBuilder::Declare(const BufferDeclaration& declaration, std::source_location where)
    {
        record->Buffers.push_back(DeclarationRecord<BufferDeclaration>{ .Value = declaration, .Where = where });
    }

    void PathBuilder::Declare(const ImageDeclaration& declaration, std::source_location where)
    {
        record->Images.push_back(DeclarationRecord<ImageDeclaration>{ .Value = declaration, .Where = where });
    }

    void PathBuilder::Declare(const SamplerDeclaration& declaration, std::source_location where)
    {
        record->Samplers.push_back(DeclarationRecord<SamplerDeclaration>{ .Value = declaration, .Where = where });
    }

    void PathBuilder::Import(const ImportDeclaration& declaration, std::source_location where)
    {
        record->Imports.push_back(DeclarationRecord<ImportDeclaration>{ .Value = declaration, .Where = where });
    }

    void PathBuilder::Import(const BufferImportDeclaration& declaration, std::source_location where)
    {
        record->BufferImports.push_back(DeclarationRecord<BufferImportDeclaration>{ .Value = declaration, .Where = where });
    }

    void PathBuilder::After(AnchorKey anchor, std::source_location where)
    {
        record->Body.After.push_back(AnchorUseRecord{ .Anchor = anchor.ToRef(), .Where = where });
    }

    void PathBuilder::Before(AnchorKey anchor, std::source_location where)
    {
        record->Body.Before.push_back(AnchorUseRecord{ .Anchor = anchor.ToRef(), .Where = where });
    }

    void PathBuilder::ExposeAnchor(AnchorKey anchor, std::source_location where)
    {
        ExposedAnchorRecord exposed{};
        exposed.Anchor = anchor.ToRef();
        exposed.Position = static_cast<int32_t>(record->Body.Passes.size());
        exposed.Where = where;
        record->Body.ExposedAnchors.push_back(exposed);
    }

    void PathBuilder::commitPass(PassBuilder& pass)
    {
        std::unique_ptr<PassRecord> passRecord = pass.takeRecord();
        record->Body.Passes.push_back(std::move(*passRecord));
    }

    std::unique_ptr<PathRecord> PathBuilder::takeRecord() noexcept
    {
        return std::move(record);
    }

    PathListBuilder::PathListBuilder(PathListKey list, std::source_location where)
        : record{ std::make_unique<PathListRecord>() }
    {
        record->Key = list.ToRef();
        record->Where = where;
    }

    PathListBuilder::~PathListBuilder() = default;

    void PathListBuilder::Produces(ResourceKey<Buffer> resource, std::source_location where)
    {
        record->Produces.push_back(ProducesRecord{ .Resource = resource.ToRef(), .Where = where });
    }

    void PathListBuilder::Produces(ResourceKey<Image> resource, std::source_location where)
    {
        record->Produces.push_back(ProducesRecord{ .Resource = resource.ToRef(), .Where = where });
    }

    void PathListBuilder::After(AnchorKey anchor, std::source_location where)
    {
        record->After.push_back(AnchorUseRecord{ .Anchor = anchor.ToRef(), .Where = where });
    }

    void PathListBuilder::Before(AnchorKey anchor, std::source_location where)
    {
        record->Before.push_back(AnchorUseRecord{ .Anchor = anchor.ToRef(), .Where = where });
    }

    void PathListBuilder::commitPath(PathBuilder& path)
    {
        std::unique_ptr<PathRecord> pathRecord = path.takeRecord();
        record->Paths.push_back(std::move(*pathRecord));
    }

    std::unique_ptr<PathListRecord> PathListBuilder::takeRecord() noexcept
    {
        return std::move(record);
    }

}
