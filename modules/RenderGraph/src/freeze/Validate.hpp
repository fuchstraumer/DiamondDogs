#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_VALIDATE_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_VALIDATE_HPP
#include "Freeze.hpp"
#include "ResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include "Declarations.hpp"
#include "LoomKeys.hpp"
#include "Records.hpp"
#include <algorithm>
#include <cstdint>
#include <ranges>
#include <source_location>
#include <string_view>
#include <vector>

// Step 2: check values that need no lookup (LM101, LM108, LM109). Walks the record, so duplicates are checked too.
// CheckPassBinds (LM113 for binds and params, LM306) runs later, once the binds have resolved.
namespace loom::freeze
{

    inline void CheckBuffer(FreezeContext& context, const DeclarationRecord<BufferDeclaration>& decl)
    {
        const BufferDeclaration& buffer = decl.Value;

        if (buffer.Lifetime == ResourceLifetime::Invalid)
        {
            ReportMissing(context, "Lifetime", decl.Where);
        }

        if (buffer.Cpu == CpuAccess::Invalid)
        {
            ReportMissing(context, "Cpu", decl.Where);
        }

        if (buffer.Size.Kind == SizeSource::Kind::Invalid)
        {
            ReportMissing(context, "Size", decl.Where);
        }
        else if (buffer.Size.Kind == SizeSource::Kind::Bytes && buffer.Size.Size <= 0)
        {
            // todo-ship: this might actually be a good layer to check against current device / platform caps
            ReportOutOfRange(context, "Size", buffer.Size.Size, decl.Where);
        }

        if (buffer.HistoryDepth < 0)
        {
            ReportOutOfRange(context, "HistoryDepth", buffer.HistoryDepth, decl.Where);
        }

        if (buffer.HistoryDepth > 0 && buffer.NoHistoryAction == OnNoHistory::Invalid)
        {
            ReportMissing(context, "NoHistoryAction", decl.Where);
        }
    }

    inline void CheckImage(FreezeContext& context, const DeclarationRecord<ImageDeclaration>& decl)
    {
        const ImageDeclaration& image = decl.Value;

        if (image.Lifetime == ResourceLifetime::Invalid)
        {
            ReportMissing(context, "Lifetime", decl.Where);
        }

        if (!image.Format.IsValid())
        {
            ReportMissing(context, "Format", decl.Where);
        }

        // "Derived" images come from the reflection, so we can't check them here when resolving
        // user-declared values
        if (image.Extent.Kind == ExtentSource::Kind::Invalid)
        {
            ReportMissing(context, "Extent", decl.Where);
        }
        else if (image.Extent.Kind == ExtentSource::Kind::Fixed)
        {
            if (image.Extent.Width <= 0)
            {
                ReportOutOfRange(context, "Width", image.Extent.Width, decl.Where);
            }

            if (image.Extent.Height <= 0)
            {
                ReportOutOfRange(context, "Height", image.Extent.Height, decl.Where);
            }

            if (image.Extent.Depth <= 0)
            {
                ReportOutOfRange(context, "Depth", image.Extent.Depth, decl.Where);
            }
        }
        else if (image.Extent.Kind == ExtentSource::Kind::Target &&
                 image.Extent.Target == KeyRef{}) // if target is equal to the default-constructed KeyRef, not valid
        {
            ReportMissing(context, "Target", decl.Where);
        }

        if (image.Mips.Kind == MipSource::Kind::Invalid)
        {
            ReportMissing(context, "Mips", decl.Where);
        }

        if (image.Layers <= 0)
        {
            ReportOutOfRange(context, "Layers", image.Layers, decl.Where);
        }

        if (image.Cpu == CpuAccess::Invalid)
        {
            ReportMissing(context, "Cpu", decl.Where);
        }

        if (image.HistoryDepth < 0)
        {
            ReportOutOfRange(context, "HistoryDepth", image.HistoryDepth, decl.Where);
        }

        if (image.HistoryDepth > 0 && image.NoHistoryAction == OnNoHistory::Invalid)
        {
            ReportMissing(context, "NoHistoryAction", decl.Where);
        }

    }

    inline void CheckAddressMode(FreezeContext& context, std::string_view field_name, SamplerCreateInfo::AddressMode mode,
                          std::source_location where)
    {
        if (mode == SamplerCreateInfo::AddressMode::None)
        {
            ReportMissing(context, field_name, where);
        }
    }

    inline bool UsesBorder(const SamplerCreateInfo& info) noexcept
    {
        constexpr SamplerCreateInfo::AddressMode k_Border = SamplerCreateInfo::AddressMode::ClampToBorder;
        return info.AddressU == k_Border || info.AddressV == k_Border || info.AddressW == k_Border;
    }

    inline bool ClampsToEdgeOrBorder(SamplerCreateInfo::AddressMode mode) noexcept
    {
        return mode == SamplerCreateInfo::AddressMode::ClampToEdge ||
               mode == SamplerCreateInfo::AddressMode::ClampToBorder;
    }

    // Vulkan's rules for unnormalized coordinates (VUID-VkSamplerCreateInfo-unnormalizedCoordinates-01072 to 01076).
    // Address W is not limited, because these samplers read 1D and 2D images only.
    inline void CheckUnnormalizedSampler(FreezeContext& context, const SamplerCreateInfo& info, std::source_location where)
    {
        if (info.MinFilter != info.MagFilter)
        {
            ReportOutOfRangeEnum(context, "MinFilter", info.MinFilter, where);
        }

        if (info.MipFilter != ImageFilteringMode::Nearest)
        {
            ReportOutOfRangeEnum(context, "MipFilter", info.MipFilter, where);
        }

        if (info.MinLod != 0.0f)
        {
            ReportOutOfRangeReal(context, "MinLod", info.MinLod, where);
        }

        if (info.MaxLod != 0.0f)
        {
            ReportOutOfRangeReal(context, "MaxLod", info.MaxLod, where);
        }

        if (!ClampsToEdgeOrBorder(info.AddressU))
        {
            ReportOutOfRangeEnum(context, "AddressU", info.AddressU, where);
        }

        if (!ClampsToEdgeOrBorder(info.AddressV))
        {
            ReportOutOfRangeEnum(context, "AddressV", info.AddressV, where);
        }

        if (info.AnisotropyEnabled)
        {
            ReportOutOfRange(context, "AnisotropyEnabled", 1, where);
        }

        if (info.CompareEnabled)
        {
            ReportOutOfRange(context, "CompareEnabled", 1, where);
        }
    }

    // Every field has a usable default, so this reports only values that are set and wrong. Device limits
    // (maxSamplerAnisotropy, maxSamplerLodBias) belong to the Layout stage (LM601). CompareOp cannot be checked:
    // its None is the same value as Never, which is legal.
    inline void CheckSampler(FreezeContext& context, const DeclarationRecord<SamplerDeclaration>& decl)
    {
        const SamplerCreateInfo& info = decl.Value.CreateInfo;

        if (info.MagFilter == ImageFilteringMode::None)
        {
            ReportMissing(context, "MagFilter", decl.Where);
        }

        if (info.MinFilter == ImageFilteringMode::None)
        {
            ReportMissing(context, "MinFilter", decl.Where);
        }

        if (info.MipFilter == ImageFilteringMode::None)
        {
            ReportMissing(context, "MipFilter", decl.Where);
        }
        else if (info.MipFilter == ImageFilteringMode::Cubic)
        {
            ReportOutOfRangeEnum(context, "MipFilter", info.MipFilter, decl.Where);
        }

        CheckAddressMode(context, "AddressU", info.AddressU, decl.Where);
        CheckAddressMode(context, "AddressV", info.AddressV, decl.Where);
        CheckAddressMode(context, "AddressW", info.AddressW, decl.Where);

        if (info.AnisotropyEnabled && info.MaxAnisotropy < 1.0f)
        {
            ReportOutOfRangeReal(context, "MaxAnisotropy", info.MaxAnisotropy, decl.Where);
        }

        if (info.CompareEnabled && info.CompareOp == ImageCompareOp::None)
        {
            ReportOutOfRangeEnum(context, "CompareOp", info.CompareOp, decl.Where);
        }

        if (info.MaxLod < info.MinLod)
        {
            ReportOutOfRangeReal(context, "MaxLod", info.MaxLod, decl.Where);
        }

        // SamplerCreateInfo has no custom color field yet, so a custom border color cannot be honored.
        const bool customBorder = info.BorderColor == SamplerCreateInfo::ImageBorderColor::FloatCustom ||
                                  info.BorderColor == SamplerCreateInfo::ImageBorderColor::IntCustom;

        if (UsesBorder(info) && info.BorderColor == SamplerCreateInfo::ImageBorderColor::None)
        {
            ReportMissing(context, "BorderColor", decl.Where);
        }
        else if (customBorder && !info.CustomBorderColor.has_value())
        {
            ReportMissing(context, "CustomBorderColor", decl.Where);
        }

        if (info.UnnormalizedCoordinates)
        {
            CheckUnnormalizedSampler(context, info, decl.Where);
        }
    }

    inline void CheckImport(FreezeContext& context, const DeclarationRecord<ImportDeclaration>& decl)
    {
        // this is the only real field we can check on this decl type
        const ImageExpectation& expect = decl.Value.Expectation;

        if (expect.Kind == ImageExpectation::Kind::Invalid)
        {
            ReportMissing(context, "ImageExpectation.Kind", decl.Where);
        }
        else if (expect.Kind == ImageExpectation::Kind::Exact)
        {
            // check the dimensions now
            if (expect.Width < 1)
            {
                ReportOutOfRange(context, "ImageExpectation.Width", expect.Width, decl.Where);
            }

            if (expect.Height < 1)
            {
                ReportOutOfRange(context, "ImageExpectation.Height", expect.Height, decl.Where);
            }

            if (expect.Depth < 1)
            {
                ReportOutOfRange(context, "ImageExpectation.Depth", expect.Depth, decl.Where);
            }

            if (expect.MipLevels < 1)
            {
                ReportOutOfRange(context, "ImageExpectation.MipLevels", expect.MipLevels, decl.Where);
            }
            
            if (expect.Layers < 1)
            {
                ReportOutOfRangeReal(context, "ImageExpectation.Layers", expect.Layers, decl.Where);
            }

            if (!expect.Format.IsValid())
            {
                ReportOutOfRangeEnum(context, "ImageExpectation.Format", expect.Format.DataType, decl.Where);
            }
        }
        else
        {
            // `AnyExtent` kind of expectations only set format and layers
            if (expect.Layers < 1)
            {
                ReportOutOfRangeReal(context, "ImageExpectation.Layers", expect.Layers, decl.Where);
            }

            if (!expect.Format.IsValid())
            {
                ReportOutOfRangeEnum(context, "ImageExpectation.Format", expect.Format.DataType, decl.Where);
            }
        }
    }

    inline void CheckOutput(FreezeContext& context, const DeclarationRecord<OutputDeclaration>& decl)
    {
        if (decl.Value.Target.Hash == 0u)
        {
            ReportMissing(context, "Target", decl.Where);
        }

        const OutputFallback& fallback = decl.Value.OnSkip;

        if (fallback.Kind == OutputFallback::Kind::Invalid)
        {
            ReportMissing(context, "OutputFallback.Kind", decl.Where);
        }
        else if (fallback.Kind == OutputFallback::Kind::Clear)
        {
            // Whether a clear color makes sense for a target's format depends on knowing the
            // format, which we can't even know here. We could check against inf/nan, though
            // that is vanishingly rare
        }
    }

    inline void CheckDeclarations(FreezeContext& context)
    {
        for (const DeclarationRecord<BufferDeclaration>& decl : context.Record.Buffers)
        {
            CheckBuffer(context, decl);
        }

        for (const DeclarationRecord<ImageDeclaration>& decl : context.Record.Images)
        {
            CheckImage(context, decl);
        }

        for (const DeclarationRecord<SamplerDeclaration>& decl : context.Record.Samplers)
        {
            CheckSampler(context, decl);
        }

        for (const DeclarationRecord<ImportDeclaration>& decl : context.Record.Imports)
        {
            CheckImport(context, decl);
        }

        for (const DeclarationRecord<OutputDeclaration>& decl : context.Record.Outputs)
        {
            CheckOutput(context, decl);
        }
    }

    // LM108: a graph pin on an axis that an earlier graph pin already set. Where is the second pin, OtherWhere the
    // first. The first pin of each axis goes into the frozen graph.
    inline void CheckGraphPins(FreezeContext& context)
    {
        for (const PinRecord& pin : context.Record.Pins)
        {
            const auto sameAxis = [&pin](const PinRecord& earlier) { return earlier.AxisName == pin.AxisName; };
            const auto earlier = std::ranges::find_if(context.Graph.Pins, sameAxis);

            if (earlier != context.Graph.Pins.end())
            {
                Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM108, pin.Where);
                diag.OtherWhere = earlier->Where;
                context.Sink.Report(diag);
                continue;
            }

            context.Graph.Pins.push_back(pin);
        }
    }

    // The ring depth of a resolved resource. Only a declared buffer or image keeps history: an import, a sampler and an
    // unresolved key (already an error) have none.
    inline int32_t HistoryDepthOf(const FrozenGraph& graph, ResourceIndex resource)
    {
        if (!resource.IsValid())
        {
            return 0;
        }

        const FrozenResource& row = graph.Resources[resource.Value];

        if (row.Origin != ResourceOrigin::Declared)
        {
            return 0;
        }

        switch (row.Kind)
        {
        case ResourceKind::Buffer:
            return graph.Buffers[row.DeclarationIndex].HistoryDepth;
        case ResourceKind::Image:
            return graph.Images[row.DeclarationIndex].Declaration.HistoryDepth;
        case ResourceKind::Sampler:
            [[fallthrough]];
        case ResourceKind::Invalid:
            break;
        }

        return 0;
    }

    // LM113: a binding name bound twice in one pass. Bind maps each name to one slot, so it can assume one bind per name.
    // Where is the second bind, OtherWhere the first.
    inline void CheckRepeatedBinds(FreezeContext& context, const FrozenPass& pass, const PassRecord& record)
    {
        for (auto&& [bindIdx, bind] : record.Binds | std::views::enumerate)
        {
            const auto sameName = [&bind](const BindRecord& earlier)
            {
                return earlier.BindingName == bind.BindingName;
            };

            const auto earlierBinds = record.Binds | std::views::take(bindIdx);
            const auto earlier = std::ranges::find_if(earlierBinds, sameName);

            if (earlier == earlierBinds.end())
            {
                continue;
            }

            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM113, bind.Where);
            diag.OtherWhere = earlier->Where;
            diag.Pass = pass.Key;
            diag.Binding = bind.BindingName;
            context.Sink.Report(diag);
        }
    }

    // LM113: Params at most once per pass, since a pass has one block of entry point constants.
    inline void CheckRepeatedParams(FreezeContext& context, const FrozenPass& pass, const PassRecord& record)
    {
        if (record.Params.size() < 2u)
        {
            return;
        }

        for (const ParamsRecord& params : record.Params | std::views::drop(1))
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM113, params.Where);
            diag.OtherWhere = record.Params.front().Where;
            diag.Pass = pass.Key;
            context.Sink.Report(diag);
        }
    }

    // LM306: BindPrevious reads last frame's slot of the history ring, so the resource needs HistoryDepth above 0.
    inline void CheckHistoryBinds(FreezeContext& context, const FrozenPass& pass, const PassRecord& record)
    {
        for (auto&& [bindIdx, bind] : record.Binds | std::views::enumerate)
        {
            const ResourceIndex resource = pass.Binds[bindIdx].Resource;
            const bool hasHistory = HistoryDepthOf(context.Graph, resource) > 0;

            // an unresolved key is already LM103 or LM107: one error per mistake
            if (!bind.Previous || hasHistory || !resource.IsValid())
            {
                continue;
            }

            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM306, bind.Where);
            diag.Pass = pass.Key;
            diag.Resource = bind.Resource;
            diag.Binding = bind.BindingName;
            context.Sink.Report(diag);
        }
    }

    // The bind rules that need no shader. Runs after ResolvePasses, which keeps each pass's binds in record order. The
    // binding names come from the record: the frozen graph is dropped when Freeze fails, and a diagnostic keeps a view.
    inline void CheckPassBinds(FreezeContext& context, const std::vector<PassSource>& sources)
    {
        for (auto&& [passIdx, source] : sources | std::views::enumerate)
        {
            const FrozenPass& pass = context.Graph.Passes[passIdx];
            CheckRepeatedBinds(context, pass, *source.Pass);
            CheckRepeatedParams(context, pass, *source.Pass);
            CheckHistoryBinds(context, pass, *source.Pass);
        }
    }

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_VALIDATE_HPP
