#include "LoomKeys.hpp"
#include "freeze/FrozenGraph.hpp"
#include "layout/LaidOutGraph.hpp"
#include "layout/LayoutContext.hpp"
#include "bind/BindContext.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomFormats.hpp"
#include "Records.hpp"
#include "ResourceFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestFormat.hpp"
#include "ShaderManifestLayout.hpp"
#include "resolve/ResolvedGraph.hpp"
#include <algorithm>
#include <cstdint>
#include <source_location>
#include <span>
#include <string_view>
#include <vector>

// LM602: the attachments of a draw against the outputs of its fragment shader. Each output location needs a Color
// statement at that slot, and each Color slot needs an output: an attachment with no output receives undefined values.
// The output's scalar class must fit the format, and the shader must write every component that the format holds,
// because Vulkan does not fill the components that a fragment shader leaves out. A shader that writes depth needs a
// Depth statement. A frame target's format is known at Realize, so Realize checks its class and components.
// In LM602, Expected is the attachment's side and Actual the shader's side.
namespace loom::layout
{

namespace
{
    namespace lsm = lodestone::manifest;

    // The fragment shader's outputs: none for a depth-only draw, which has no fragment entry point
    struct FragmentOutputs
    {
        std::span<const lsm::ColorTarget> Colors;
        bool WritesDepth{ false };
    };

    FragmentOutputs OutputsOf(const ResolvedPass& resolved)
    {
        if (resolved.FragmentEntryPoint == k_NoEntryPoint)
        {
            return FragmentOutputs{};
        }

        const std::vector<lsm::EntryPointInstanceView> instances = bind::PassInstances(resolved);
        const uint32_t raster = instances.back().Raster();

        return FragmentOutputs
        {
            .Colors = resolved.Environment->ColorTargets(raster),
            .WritesDepth = resolved.Environment->WritesFragDepth(raster),
        };
    }

    std::string_view ScalarTypeName(lodestone::VertexScalarType scalar_type)
    {
        switch (scalar_type)
        {
        case lodestone::VertexScalarType::Float16:
            return "Float16";
        case lodestone::VertexScalarType::Float32:
            return "Float32";
        case lodestone::VertexScalarType::SignedInteger32:
            return "SignedInteger32";
        case lodestone::VertexScalarType::UnsignedInteger32:
            return "UnsignedInteger32";
        case lodestone::VertexScalarType::Invalid:
            break;
        }

        return "Invalid";
    }

    // A float output writes a float format, a uint output a uint format. Lodestone has no signed integer format.
    bool ScalarTypeWrites(lodestone::VertexScalarType scalar_type, FormatClass format_class)
    {
        switch (scalar_type)
        {
        case lodestone::VertexScalarType::Float16:
            [[fallthrough]];
        case lodestone::VertexScalarType::Float32:
            return format_class == FormatClass::Float;
        case lodestone::VertexScalarType::UnsignedInteger32:
            return format_class == FormatClass::Uint;
        case lodestone::VertexScalarType::SignedInteger32:
            [[fallthrough]];
        case lodestone::VertexScalarType::Invalid:
            break;
        }

        return false;
    }

    DiagnosticValue NoValue(std::string_view field_name)
    {
        return DiagnosticValue{ .FieldName = field_name, .Kind = DiagnosticValueKind::None };
    }

    void ReportLM602(LayoutContext& context, const FrozenPass& pass, KeyRef resource, std::source_location where,
                     std::source_location other_where, DiagnosticValue expected, DiagnosticValue actual)
    {
        Diagnostic diag
        {
            .Code = DiagnosticCode::LM602,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM602),
            .Where = where,
            .OtherWhere = other_where,
            .Pass = pass.Key,
            .Resource = resource,
            .Expected = expected,
            .Actual = actual,
        };

        context.Sink.Report(diag);
    }

    const lsm::ColorTarget* OutputAt(const FragmentOutputs& outputs, int32_t slot)
    {
        for (const lsm::ColorTarget& output : outputs.Colors)
        {
            if (static_cast<int32_t>(output.Location) == slot)
            {
                return &output;
            }
        }

        return nullptr;
    }

    bool HasColorAt(const FrozenPass& pass, uint32_t location)
    {
        for (const FrozenColorTarget& color : pass.Colors)
        {
            if (color.Slot == static_cast<int32_t>(location))
            {
                return true;
            }
        }

        return false;
    }

    // The scalar class, then the component count. OtherWhere is the image's declaration, which states the format.
    void CheckColorFormat(LayoutContext& context, const FrozenPass& pass, const FrozenColorTarget& color,
                          const lsm::ColorTarget& output)
    {
        const FrozenResource& row = context.Frozen.Resources[color.Image.Value];
        const lodestone::TextureFormat format = ImageFormatOf(context.Frozen, row);
        const auto scalarType = static_cast<lodestone::VertexScalarType>(output.ScalarType);
        if (!ScalarTypeWrites(scalarType, FormatClassOf(format)))
        {
            ReportLM602(context, pass, row.Key, color.Where, row.Where,
                        TextValue("Format", TextureFormatName(format)),
                        TextValue("ScalarType", ScalarTypeName(scalarType)));
            return;
        }

        const int32_t formatComponents = ComponentCountOf(format);
        const int32_t written = static_cast<int32_t>(output.ComponentCount);
        if (written < formatComponents)
        {
            ReportLM602(context, pass, row.Key, color.Where, row.Where,
                        IntegerValue("Components", formatComponents), IntegerValue("Components", written));
        }
    }

    LaidOutColor LayOutColor(const LayoutContext& context, const FrozenColorTarget& color)
    {
        LaidOutColor result
        {
            .Slot = color.Slot,
            .FrameTarget = color.FrameTarget,
            .Blend = color.Blend,
        };

        if (color.Kind == TargetKind::Image)
        {
            result.Format = ImageFormatOf(context.Frozen, context.Frozen.Resources[color.Image.Value]);
        }

        return result;
    }

}

    std::vector<LaidOutColor> CheckAttachments(LayoutContext& context, const FrozenPass& pass,
                                               const ResolvedPass& resolved)
    {
        const FragmentOutputs outputs = OutputsOf(resolved);
        std::vector<LaidOutColor> colors;

        // an output with no attachment is lost. OtherWhere is the Shaders statement.
        for (const lsm::ColorTarget& output : outputs.Colors)
        {
            if (!HasColorAt(pass, output.Location))
            {
                ReportLM602(context, pass, KeyRef{}, pass.Where, pass.Shader.Where,
                            NoValue("Slot"), IntegerValue("Slot", output.Location));
            }
        }

        for (const FrozenColorTarget& color : pass.Colors)
        {
            colors.push_back(LayOutColor(context, color));

            // an attachment with no output receives undefined values
            const lsm::ColorTarget* output = OutputAt(outputs, color.Slot);
            if (output == nullptr)
            {
                ReportLM602(context, pass, KeyRef{}, color.Where, pass.Shader.Where,
                            IntegerValue("Slot", color.Slot), NoValue("Slot"));
                continue;
            }

            if (color.Kind == TargetKind::Image)
            {
                CheckColorFormat(context, pass, color, *output);
            }
        }

        if (outputs.WritesDepth && !pass.Depth.Image.IsValid())
        {
            ReportLM602(context, pass, KeyRef{}, pass.Shader.Where, pass.Where,
                        NoValue("Depth"), TextValue("Depth", "Written"));
        }

        const auto bySlot = [](const LaidOutColor& lhs, const LaidOutColor& rhs)
        {
            return lhs.Slot < rhs.Slot;
        };

        std::ranges::sort(colors, bySlot);
        return colors;
    }

    void CheckVertexInputs(LayoutContext& context, const FrozenPass& pass, const ResolvedPass& resolved)
    {
        const std::vector<lsm::EntryPointInstanceView> instances = bind::PassInstances(resolved);
        const lsm::EnvironmentView& environment = *resolved.Environment;
        const std::span<const lsm::VertexInput> inputs = environment.VertexInputs(instances.front().Raster());

        for (const lsm::VertexInput& input : inputs)
        {
            Diagnostic diag
            {
                .Code = DiagnosticCode::LM603,
                .Severity = DefaultSeverityOf(DiagnosticCode::LM603),
                .Where = pass.Shader.Where,
                .Pass = pass.Key,
                .Binding = environment.String(input.SemanticNameString),
                .Expected = NoValue("Semantic"),
                .Actual = IntegerValue("SemanticIndex", input.SemanticIndex),
                .Suggestion = "Loom has no vertex format yet: read the vertices from a buffer with SV_VertexID",
            };

            context.Sink.Report(diag);
        }
    }

}
