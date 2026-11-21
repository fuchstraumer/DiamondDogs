#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_VALIDATE_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_VALIDATE_HPP
#include "freeze/FreezeContext.hpp"
#include "ResourceTypes.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>
#include <vector>

// Step 2: check values that need no lookup (LM101, LM108, LM109). Walks the record, so duplicates are checked too.
// CheckPassBinds (LM113 for binds and params, LM306) runs later, once the binds have resolved.
namespace loom::freeze
{

    void CheckBuffer(FreezeContext& context, const DeclarationRecord<BufferDeclaration>& decl);

    void CheckImage(FreezeContext& context, const DeclarationRecord<ImageDeclaration>& decl);

    void CheckAddressMode(FreezeContext& context, std::string_view field_name, SamplerCreateInfo::AddressMode mode,
                   std::source_location where);

    bool UsesBorder(const SamplerCreateInfo& info) noexcept;

    bool ClampsToEdgeOrBorder(SamplerCreateInfo::AddressMode mode) noexcept;

    // Vulkan's rules for unnormalized coordinates (VUID-VkSamplerCreateInfo-unnormalizedCoordinates-01072 to 01076).
    // Address W is not limited, because these samplers read 1D and 2D images only.
    void CheckUnnormalizedSampler(FreezeContext& context, const SamplerCreateInfo& info, std::source_location where);

    // Every field has a usable default, so this reports only values that are set and wrong. Device limits
    // (maxSamplerAnisotropy, maxSamplerLodBias) belong to the Layout stage (LM601). CompareOp cannot be checked:
    // its None is the same value as Never, which is legal.
    void CheckSampler(FreezeContext& context, const DeclarationRecord<SamplerDeclaration>& decl);

    void CheckImport(FreezeContext& context, const DeclarationRecord<ImportDeclaration>& decl);

    void CheckOutput(FreezeContext& context, const DeclarationRecord<OutputDeclaration>& decl);

    void CheckDeclarations(FreezeContext& context);

    // LM108: a graph pin on an axis that an earlier graph pin already set. Where is the second pin, OtherWhere the
    // first. The first pin of each axis goes into the frozen graph.
    void CheckGraphPins(FreezeContext& context);

    // The ring depth of a resolved resource. Only a declared buffer or image keeps history: an import, a sampler and an
    // unresolved key (already an error) have none.
    int32_t HistoryDepthOf(const FrozenGraph& graph, ResourceIndex resource);

    // LM113: a binding name bound twice in one pass. Bind maps each name to one slot, so it can assume one bind per name.
    // Where is the second bind, OtherWhere the first.
    void CheckRepeatedBinds(FreezeContext& context, const FrozenPass& pass, const PassRecord& record);

    // LM113: Params at most once per pass, since a pass has one block of entry point constants.
    void CheckRepeatedParams(FreezeContext& context, const FrozenPass& pass, const PassRecord& record);

    // LM306: BindPrevious reads last frame's slot of the history ring, so the resource needs HistoryDepth above 0.
    void CheckHistoryBinds(FreezeContext& context, const FrozenPass& pass, const PassRecord& record);

    // The bind rules that need no shader. Runs after ResolvePasses, which keeps each pass's binds in record order. The
    // binding names come from the record: the frozen graph is dropped when Freeze fails, and a diagnostic keeps a view.
    void CheckPassBinds(FreezeContext& context, const std::vector<PassSource>& sources);

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_VALIDATE_HPP
