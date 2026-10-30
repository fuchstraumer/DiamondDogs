#include "SequenceBuilder.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "Records.hpp"
#include <cstdint>
#include <memory>
#include <source_location>
#include <utility>

namespace loom
{

    SequenceBuilder::SequenceBuilder(SequenceKey sequence, std::source_location where)
        : record{ std::make_unique<SequenceRecord>() }
    {
        record->Key = sequence.ToRef();
        record->Where = where;
    }

    SequenceBuilder::~SequenceBuilder() = default;

    void SequenceBuilder::After(AnchorKey anchor, std::source_location where)
    {
        record->After.push_back(AnchorUseRecord{ .Anchor = anchor.ToRef(), .Where = where });
    }

    void SequenceBuilder::Before(AnchorKey anchor, std::source_location where)
    {
        record->Before.push_back(AnchorUseRecord{ .Anchor = anchor.ToRef(), .Where = where });
    }

    void SequenceBuilder::ExposeAnchor(AnchorKey anchor, std::source_location where)
    {
        ExposedAnchorRecord exposed{};
        exposed.Anchor = anchor.ToRef();
        exposed.Position = static_cast<int32_t>(record->Passes.size());
        exposed.Where = where;
        record->ExposedAnchors.push_back(exposed);
    }

    void SequenceBuilder::commitPass(DispatchBuilder& pass)
    {
        std::unique_ptr<PassRecord> passRecord = pass.takeRecord();
        record->Passes.push_back(std::move(*passRecord));
    }

    std::unique_ptr<SequenceRecord> SequenceBuilder::takeRecord() noexcept
    {
        return std::move(record);
    }

}
