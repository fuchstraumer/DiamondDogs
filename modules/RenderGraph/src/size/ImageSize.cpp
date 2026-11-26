#include "size/ImageSize.hpp"
#include "Declarations.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomFormats.hpp"
#include "LoomKeys.hpp"
#include "PassStatements.hpp"
#include "Records.hpp"
#include "ResourceFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifestLayout.hpp"
#include "bind/BoundGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "size/SizedGraph.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <string_view>
#include <vector>

// LM401: a stated extent or layer count that the image does not have. LM402: a derived extent or layer count that no
// binding states. LM403: a format that does not fit a binding or a target, or two bindings that see the image in
// different dimensions. A stated extent is [ls_extent_1d/2d/3d] on the binding; the axes that the attribute leaves out
// are 1. On an array texture the third value is the layer count (Lodestone C31), so its stated depth is 1.
namespace loom
{

namespace
{
    struct StatedExtent
    {
        ImageExtent Extent;
        KeyRef Pass;
        std::string_view Binding;
        std::source_location Where;
    };

    struct StatedLayers
    {
        int32_t Count{ 0 };
        KeyRef Pass;
        std::string_view Binding;
        std::source_location Where;
    };

    struct ImageFacts
    {
        std::vector<StatedExtent> Extents;
        std::vector<StatedLayers> Layers;
        lodestone::TextureDimension Dimension{ lodestone::TextureDimension::Invalid };
        // the bind that set Dimension, for the OtherWhere of a dimension LM403
        std::source_location DimensionWhere;
        lodestone::TextureUsageFlags Usage{ lodestone::TextureUsageFlags::Invalid };
        bool Arrayed{ false };
        bool Cube{ false };
    };

    // One binding of an image, with what LM403 reports about it
    struct ImageBinding
    {
        const lodestone::manifest::ResolvedResource& Reflection;
        const FrozenResource& Row;
        lodestone::TextureFormat Format{ lodestone::TextureFormat::Invalid };
        KeyRef Pass;
        std::string_view Name;
        std::source_location Where;
    };

    // A float binding reads a depth format too (the depth value in the first component). Filterability is a device
    // question, so UnfilterableFloat and Float accept the same formats here.
    bool SampleTypeAccepts(lodestone::TextureSampleType sample_type, FormatClass format_class)
    {
        switch (sample_type)
        {
        case lodestone::TextureSampleType::Float:
            [[fallthrough]];
        case lodestone::TextureSampleType::UnfilterableFloat:
            return format_class == FormatClass::Float || format_class == FormatClass::Depth;
        case lodestone::TextureSampleType::Depth:
            return format_class == FormatClass::Depth;
        case lodestone::TextureSampleType::UnsignedInteger:
            return format_class == FormatClass::Uint;
        case lodestone::TextureSampleType::SignedInteger:
            return false;
        case lodestone::TextureSampleType::Invalid:
            break;
        }

        return true;
    }

    std::string_view SampleTypeName(lodestone::TextureSampleType sample_type)
    {
        switch (sample_type)
        {
        case lodestone::TextureSampleType::Float:
            return "Float";
        case lodestone::TextureSampleType::UnfilterableFloat:
            return "UnfilterableFloat";
        case lodestone::TextureSampleType::Depth:
            return "Depth";
        case lodestone::TextureSampleType::SignedInteger:
            return "SignedInteger";
        case lodestone::TextureSampleType::UnsignedInteger:
            return "UnsignedInteger";
        case lodestone::TextureSampleType::Invalid:
            break;
        }

        return "Invalid";
    }

    std::string_view DimensionName(lodestone::TextureDimension dimension)
    {
        switch (dimension)
        {
        case lodestone::TextureDimension::Texture1D:
            return "Texture1D";
        case lodestone::TextureDimension::Texture2D:
            return "Texture2D";
        case lodestone::TextureDimension::Texture3D:
            return "Texture3D";
        case lodestone::TextureDimension::Invalid:
            break;
        }

        return "Invalid";
    }

    std::string_view ImageUseName(ImageUse use)
    {
        switch (use)
        {
        case ImageUse::Sampled:
            return "Sampled";
        case ImageUse::Storage:
            return "Storage";
        case ImageUse::ColorTarget:
            return "ColorTarget";
        case ImageUse::DepthTarget:
            return "DepthTarget";
        case ImageUse::TransferSource:
            return "TransferSource";
        case ImageUse::TransferDestination:
            return "TransferDestination";
        case ImageUse::Invalid:
            break;
        }

        return "Invalid";
    }

    // An array or a cube is a 2D image with layers. Invalid for a shape that allocates no image (a subpass input).
    lodestone::TextureDimension DimensionOf(lodestone::ResourceShape shape)
    {
        switch (lodestone::GetBaseShape(shape))
        {
        case lodestone::ResourceShape::Texture1D:
            return lodestone::TextureDimension::Texture1D;
        case lodestone::ResourceShape::Texture2D:
            [[fallthrough]];
        case lodestone::ResourceShape::TextureCube:
            return lodestone::TextureDimension::Texture2D;
        case lodestone::ResourceShape::Texture3D:
            return lodestone::TextureDimension::Texture3D;
        default:
            break;
        }

        return lodestone::TextureDimension::Invalid;
    }

    lodestone::TextureUsageFlags UsageOfBinding(lodestone::BindingKind kind)
    {
        switch (kind)
        {
        case lodestone::BindingKind::Texture:
            [[fallthrough]];
        case lodestone::BindingKind::CombinedTextureSampler:
            return lodestone::TextureUsageFlags::Sampled;
        case lodestone::BindingKind::StorageTexture:
            return lodestone::TextureUsageFlags::Storage;
        case lodestone::BindingKind::InputRenderTarget:
            return lodestone::TextureUsageFlags::RenderAttachment;
        default:
            break;
        }

        return lodestone::TextureUsageFlags::Invalid;
    }

    lodestone::TextureUsageFlags UsageOfStatement(ImageUse use)
    {
        switch (use)
        {
        case ImageUse::Sampled:
            return lodestone::TextureUsageFlags::Sampled;
        case ImageUse::Storage:
            return lodestone::TextureUsageFlags::Storage;
        case ImageUse::ColorTarget:
            [[fallthrough]];
        case ImageUse::DepthTarget:
            return lodestone::TextureUsageFlags::RenderAttachment;
        case ImageUse::TransferSource:
            return lodestone::TextureUsageFlags::CopySource;
        case ImageUse::TransferDestination:
            return lodestone::TextureUsageFlags::CopyDestination;
        case ImageUse::Invalid:
            break;
        }

        return lodestone::TextureUsageFlags::Invalid;
    }

    bool IsImage(const FrozenGraph& frozen, ResourceIndex resource)
    {
        return resource.IsValid() && frozen.Resources[resource.Value].Kind == ResourceKind::Image;
    }

    DiagnosticValue TextValue(std::string_view field_name, std::string_view text)
    {
        return DiagnosticValue{ .FieldName = field_name, .Kind = DiagnosticValueKind::Text, .Text = text };
    }

    DiagnosticValue IntegerValue(std::string_view field_name, int64_t value)
    {
        return DiagnosticValue{ .FieldName = field_name, .Kind = DiagnosticValueKind::Integer, .Integer = value };
    }

    // Expected is the image's side, Actual the use's side
    void ReportLM403(DiagnosticSink& sink, const FrozenResource& row, KeyRef pass, std::string_view binding,
                     std::source_location where, std::source_location other_where, DiagnosticValue expected,
                     DiagnosticValue actual)
    {
        Diagnostic diag
        {
            .Code = DiagnosticCode::LM403,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM403),
            .Where = where,
            .OtherWhere = other_where,
            .Pass = pass,
            .Resource = row.Key,
            .Binding = binding,
            .Expected = expected,
            .Actual = actual,
        };

        sink.Report(diag);
    }

    // A storage binding reads and writes raw texels, so its format must equal the image's. A sampled binding must
    // read the scalar class that the format holds. OtherWhere is the declaration, which states the format.
    void CheckBindingFormat(DiagnosticSink& sink, const ImageBinding& binding)
    {
        const lodestone::BindingKind kind = binding.Reflection.Kind();
        const DiagnosticValue imageFormat = TextValue("Format", TextureFormatName(binding.Format));

        if (kind == lodestone::BindingKind::StorageTexture)
        {
            const lodestone::TextureFormat stated = binding.Reflection.StorageFormat();
            if (stated != lodestone::TextureFormat::Invalid && stated != binding.Format)
            {
                ReportLM403(sink, binding.Row, binding.Pass, binding.Name, binding.Where, binding.Row.Where,
                            imageFormat, TextValue("Format", TextureFormatName(stated)));
            }

            return;
        }

        const bool sampled = kind == lodestone::BindingKind::Texture ||
                             kind == lodestone::BindingKind::CombinedTextureSampler;
        const lodestone::TextureSampleType sampleType = binding.Reflection.SampleType();
        if (sampled && !SampleTypeAccepts(sampleType, FormatClassOf(binding.Format)))
        {
            ReportLM403(sink, binding.Row, binding.Pass, binding.Name, binding.Where, binding.Row.Where,
                        imageFormat, TextValue("SampleType", SampleTypeName(sampleType)));
        }
    }

    // Every binding of one image must see one dimension. The first binding sets it; OtherWhere of a conflict is that
    // binding's bind.
    void CheckBindingDimension(DiagnosticSink& sink, const ImageBinding& binding, ImageFacts& image)
    {
        const lodestone::TextureDimension dimension = DimensionOf(binding.Reflection.Shape());

        if (dimension == lodestone::TextureDimension::Invalid)
        {
            return;
        }

        image.Arrayed = image.Arrayed || lodestone::ResourceShapeIsArray(binding.Reflection.Shape());
        image.Cube = image.Cube || lodestone::GetBaseShape(binding.Reflection.Shape()) == lodestone::ResourceShape::TextureCube;

        if (image.Dimension == lodestone::TextureDimension::Invalid)
        {
            image.Dimension = dimension;
            image.DimensionWhere = binding.Where;
            return;
        }

        if (dimension != image.Dimension)
        {
            ReportLM403(sink, binding.Row, binding.Pass, binding.Name, binding.Where, image.DimensionWhere,
                        TextValue("Dimension", DimensionName(image.Dimension)),
                        TextValue("Dimension", DimensionName(dimension)));
        }
    }

    // The extent that a binding states, when the shader states one. DescribeCreation decides whether it does (G10).
    std::optional<ImageExtent> StatedExtentOf(const lodestone::manifest::ResolvedResource& resource)
    {
        const lodestone::manifest::CreationFields fields = lodestone::manifest::DescribeCreation(resource);
        const bool fromShader = fields.Extent == lodestone::manifest::CreationSource::Shader ||
                                fields.Extent == lodestone::manifest::CreationSource::ShaderPerVariant;

        if (!fromShader)
        {
            return std::nullopt;
        }

        // the third value of an array texture is its layer count, not a depth
        const bool arrayed = lodestone::ResourceShapeIsArray(resource.Shape());

        return ImageExtent
        {
            .Width = static_cast<int32_t>(resource.ExtentX()),
            .Height = static_cast<int32_t>(resource.ExtentY()),
            .Depth = arrayed ? 1 : static_cast<int32_t>(resource.ExtentZ()),
        };
    }

    // The layer count that a binding states, when the shader states one (Lodestone C31: an array texture whose third
    // extent value is above 1)
    std::optional<int32_t> StatedLayersOf(const lodestone::manifest::ResolvedResource& resource)
    {
        // a texture that is not an array has no layer count to state, whatever DescribeCreation reports for it
        if (!lodestone::ResourceShapeIsArray(resource.Shape()))
        {
            return std::nullopt;
        }

        const lodestone::manifest::CreationFields fields = lodestone::manifest::DescribeCreation(resource);
        const bool fromShader = fields.ArrayLayers == lodestone::manifest::CreationSource::Shader ||
                                fields.ArrayLayers == lodestone::manifest::CreationSource::ShaderPerVariant;

        if (!fromShader)
        {
            return std::nullopt;
        }

        return static_cast<int32_t>(resource.ExtentZ());
    }

    // Each shader binding of an image: its usage, its dimension, its stated extent, and the LM403 format checks
    void GatherBindings(const FrozenGraph& frozen, const BoundGraph& bound, DiagnosticSink& sink,
                        std::vector<ImageFacts>& facts)
    {
        for (size_t slotIndex = 0u; slotIndex < bound.Slots.size(); ++slotIndex)
        {
            const ResourceIndex resource = bound.Slots[slotIndex].Resource;
            const SlotSource& source = bound.Sources[slotIndex];

            // a slot that no bind names is already LM302
            if (source.Bind < 0 || !IsImage(frozen, resource))
            {
                continue;
            }

            const FrozenPass& pass = frozen.Passes[source.Pass];
            const FrozenBind& bind = pass.Binds[source.Bind];
            const FrozenResource& row = frozen.Resources[resource.Value];
            const ImageBinding binding
            {
                .Reflection = source.Reflection,
                .Row = row,
                .Format = ImageFormatOf(frozen, row),
                .Pass = pass.Key,
                .Name = bind.BindingName,
                .Where = bind.Where,
            };

            ImageFacts& image = facts[resource.Value];
            image.Usage |= UsageOfBinding(source.Reflection.Kind());
            CheckBindingFormat(sink, binding);
            CheckBindingDimension(sink, binding, image);

            const std::optional<ImageExtent> stated = StatedExtentOf(source.Reflection);
            if (stated.has_value())
            {
                image.Extents.push_back(StatedExtent{ .Extent = *stated,
                                                      .Pass = pass.Key,
                                                      .Binding = bind.BindingName,
                                                      .Where = bind.Where });
            }

            const std::optional<int32_t> statedLayers = StatedLayersOf(source.Reflection);
            if (statedLayers.has_value())
            {
                image.Layers.push_back(StatedLayers{ .Count = *statedLayers,
                                                     .Pass = pass.Key,
                                                     .Binding = bind.BindingName,
                                                     .Where = bind.Where });
            }
        }
    }

    // A color target needs a color format, and a depth target a depth format. OtherWhere is the declaration.
    void CheckTargetFormat(DiagnosticSink& sink, const FrozenGraph& frozen, const FrozenPass& pass,
                           ResourceIndex resource, ImageUse use, std::source_location where)
    {
        const FrozenResource& row = frozen.Resources[resource.Value];
        const lodestone::TextureFormat format = ImageFormatOf(frozen, row);
        const bool isDepth = FormatClassOf(format) == FormatClass::Depth;
        const bool fits = (use == ImageUse::DepthTarget) == isDepth;

        if (!fits)
        {
            ReportLM403(sink, row, pass.Key, std::string_view{}, where, row.Where,
                        TextValue("Format", TextureFormatName(format)), TextValue("Use", ImageUseName(use)));
        }
    }

    // The pass statements that name an image outside the shader: a draw's targets, and a Record pass's uses
    void GatherStatements(const FrozenGraph& frozen, DiagnosticSink& sink, std::vector<ImageFacts>& facts)
    {
        for (const FrozenPass& pass : frozen.Passes)
        {
            for (const FrozenColorTarget& color : pass.Colors)
            {
                if (color.Kind == TargetKind::Image && IsImage(frozen, color.Image))
                {
                    facts[color.Image.Value].Usage |= lodestone::TextureUsageFlags::RenderAttachment;
                    CheckTargetFormat(sink, frozen, pass, color.Image, ImageUse::ColorTarget, color.Where);
                }
            }

            if (IsImage(frozen, pass.Depth.Image))
            {
                facts[pass.Depth.Image.Value].Usage |= lodestone::TextureUsageFlags::RenderAttachment;
                CheckTargetFormat(sink, frozen, pass, pass.Depth.Image, ImageUse::DepthTarget, pass.Depth.Where);
            }

            for (const FrozenUse& use : pass.Uses)
            {
                if (use.Target != TargetKind::Image || !IsImage(frozen, use.Resource))
                {
                    continue;
                }

                facts[use.Resource.Value].Usage |= UsageOfStatement(use.AsImage);

                if (use.AsImage == ImageUse::ColorTarget || use.AsImage == ImageUse::DepthTarget)
                {
                    CheckTargetFormat(sink, frozen, pass, use.Resource, use.AsImage, use.Where);
                }
            }
        }
    }

    bool SameExtent(const ImageExtent& lhs, const ImageExtent& rhs)
    {
        return lhs.Width == rhs.Width && lhs.Height == rhs.Height && lhs.Depth == rhs.Depth;
    }

    // Reports the first axis that differs, so each value stays one number
    void ReportExtentMismatch(DiagnosticSink& sink, const FrozenResource& row, const StatedExtent& stated,
                              const ImageExtent& extent, std::source_location extent_where)
    {
        struct AxisPair
        {
            std::string_view Name;
            int32_t Expected{ 0 };
            int32_t Actual{ 0 };
        };

        const std::array<AxisPair, 3u> axes
        {
            AxisPair{ "Width", extent.Width, stated.Extent.Width },
            AxisPair{ "Height", extent.Height, stated.Extent.Height },
            AxisPair{ "Depth", extent.Depth, stated.Extent.Depth },
        };

        const auto differs = [](const AxisPair& axis)
        {
            return axis.Expected != axis.Actual;
        };

        const AxisPair& axis = *std::ranges::find_if(axes, differs);

        Diagnostic diag
        {
            .Code = DiagnosticCode::LM401,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM401),
            .Where = stated.Where,
            .OtherWhere = extent_where,
            .Pass = stated.Pass,
            .Resource = row.Key,
            .Binding = stated.Binding,
            .Expected = IntegerValue(axis.Name, axis.Expected),
            .Actual = IntegerValue(axis.Name, axis.Actual),
        };

        sink.Report(diag);
    }

    // A known extent (Fixed, or an Exact import): every stated extent must equal it. OtherWhere is the declaration.
    ImageExtent CheckKnownExtent(DiagnosticSink& sink, const FrozenResource& row, const ImageExtent& extent,
                                 const ImageFacts& image)
    {
        for (const StatedExtent& stated : image.Extents)
        {
            if (!SameExtent(stated.Extent, extent))
            {
                ReportExtentMismatch(sink, row, stated, extent, row.Where);
            }
        }

        return extent;
    }

    // A Derived extent: the first stated extent sets it, and every other one must equal it. OtherWhere is the binding
    // that set it. No stated extent at all is LM402.
    ImageExtent DeriveExtent(DiagnosticSink& sink, const FrozenResource& row, const ImageFacts& image)
    {
        if (image.Extents.empty())
        {
            Diagnostic diag
            {
                .Code = DiagnosticCode::LM402,
                .Severity = DefaultSeverityOf(DiagnosticCode::LM402),
                .Where = row.Where,
                .Resource = row.Key,
            };

            sink.Report(diag);
            return ImageExtent{};
        }

        const StatedExtent& source = image.Extents.front();

        for (const StatedExtent& stated : image.Extents)
        {
            if (!SameExtent(stated.Extent, source.Extent))
            {
                ReportExtentMismatch(sink, row, stated, source.Extent, source.Where);
            }
        }

        return source.Extent;
    }

    // An import of AnyExtent brings its extent with its content, and Realize checks the stated extents against it
    // (LM701). So does an image sized from a frame target, whose size Realize knows.
    ImageExtent ExtentOf(DiagnosticSink& sink, const FrozenGraph& frozen, const FrozenResource& row,
                         const ImageFacts& image)
    {
        if (row.Origin == ResourceOrigin::Imported)
        {
            const ImageExpectation& expectation = frozen.Imports[row.DeclarationIndex].Expectation;

            if (expectation.Kind != ImageExpectation::Kind::Exact)
            {
                return ImageExtent{};
            }

            const ImageExtent promised{ expectation.Width, expectation.Height, expectation.Depth };
            return CheckKnownExtent(sink, row, promised, image);
        }

        const ExtentSource& source = frozen.Images[row.DeclarationIndex].Declaration.Extent;

        switch (source.Kind)
        {
        case ExtentSource::Kind::Fixed:
            return CheckKnownExtent(sink, row, ImageExtent{ source.Width, source.Height, source.Depth }, image);
        case ExtentSource::Kind::Derived:
            return DeriveExtent(sink, row, image);
        case ExtentSource::Kind::Target:
            [[fallthrough]];
        case ExtentSource::Kind::Invalid:
            break;
        }

        return ImageExtent{};
    }

    void ReportLayersMismatch(DiagnosticSink& sink, const FrozenResource& row, const StatedLayers& stated,
                              int32_t count, std::source_location count_where)
    {
        Diagnostic diag
        {
            .Code = DiagnosticCode::LM401,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM401),
            .Where = stated.Where,
            .OtherWhere = count_where,
            .Pass = stated.Pass,
            .Resource = row.Key,
            .Binding = stated.Binding,
            .Expected = IntegerValue("Layers", count),
            .Actual = IntegerValue("Layers", stated.Count),
        };

        sink.Report(diag);
    }

    // A known count (Fixed, or an import's expectation): every stated count must equal it. OtherWhere is the
    // declaration.
    int32_t CheckKnownLayers(DiagnosticSink& sink, const FrozenResource& row, int32_t count, const ImageFacts& image)
    {
        for (const StatedLayers& stated : image.Layers)
        {
            if (stated.Count != count)
            {
                ReportLayersMismatch(sink, row, stated, count, row.Where);
            }
        }

        return count;
    }

    // A Derived count: the first stated count sets it, and every other one must equal it. OtherWhere is the binding
    // that set it. No stated count at all is LM402.
    int32_t DeriveLayers(DiagnosticSink& sink, const FrozenResource& row, const ImageFacts& image)
    {
        if (image.Layers.empty())
        {
            Diagnostic diag
            {
                .Code = DiagnosticCode::LM402,
                .Severity = DefaultSeverityOf(DiagnosticCode::LM402),
                .Where = row.Where,
                .Resource = row.Key,
                .Expected = DiagnosticValue{ .FieldName = "Layers" },
            };

            sink.Report(diag);
            return k_UnknownExtent;
        }

        const StatedLayers& source = image.Layers.front();

        for (const StatedLayers& stated : image.Layers)
        {
            if (stated.Count != source.Count)
            {
                ReportLayersMismatch(sink, row, stated, source.Count, source.Where);
            }
        }

        return source.Count;
    }

    int32_t LayersOf(DiagnosticSink& sink, const FrozenGraph& frozen, const FrozenResource& row,
                     const ImageFacts& image)
    {
        if (row.Origin == ResourceOrigin::Imported)
        {
            const ImageExpectation& expectation = frozen.Imports[row.DeclarationIndex].Expectation;
            return CheckKnownLayers(sink, row, expectation.Layers, image);
        }

        const LayerSource& source = frozen.Images[row.DeclarationIndex].Declaration.Layers;

        switch (source.Kind)
        {
        case LayerSource::Kind::Fixed:
            return CheckKnownLayers(sink, row, source.Count, image);
        case LayerSource::Kind::Derived:
            return DeriveLayers(sink, row, image);
        case LayerSource::Kind::Invalid:
            break;
        }

        return k_UnknownExtent;
    }

}

void SizeImages(const FrozenGraph& frozen, const BoundGraph& bound, DiagnosticSink& sink, SizedGraph& sized)
{
    std::vector<ImageFacts> facts(frozen.Resources.size());
    GatherBindings(frozen, bound, sink, facts);
    GatherStatements(frozen, sink, facts);

    for (size_t rowIndex = 0u; rowIndex < frozen.Resources.size(); ++rowIndex)
    {
        const FrozenResource& row = frozen.Resources[rowIndex];

        if (row.Kind != ResourceKind::Image)
        {
            continue;
        }

        const ImageFacts& image = facts[rowIndex];
        SizedResource& result = sized.Resources[rowIndex];
        result.TextureUsage = image.Usage;
        result.Extent = ExtentOf(sink, frozen, row, image);
        result.Layers = LayersOf(sink, frozen, row, image);

        // an image that only targets and copies see is a 2D image
        const bool noShaderBinding = image.Dimension == lodestone::TextureDimension::Invalid;
        result.Dimension = noShaderBinding ? lodestone::TextureDimension::Texture2D : image.Dimension;
        result.Arrayed = image.Arrayed;
        result.Cube = image.Cube;
    }
}

}
