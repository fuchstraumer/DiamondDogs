#include "size/Size.hpp"
#include "Declarations.hpp"
#include "LoomKeys.hpp"
#include "PassStatements.hpp"
#include "ResourceFlags.hpp"
#include "size/ImageSize.hpp"
#include "size/SizedGraph.hpp"
#include "bind/BoundGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "Records.hpp"
#include "ShaderManifestLayout.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <string_view>
#include <vector>

// Buffers. LM401: a statement needs a byte size that the buffer does not have. LM402: a derived size that no statement
// gives. SizeImages (ImageSize.cpp) does the images.
// Each binding and statement that names a buffer is a requirement: Exact (the shader states its element count, so it
// reads exactly that many bytes) or AtLeast (a uniform block, or indirect arguments at an offset).
namespace loom
{

namespace
{
    enum class Need : uint8_t
    {
        Exact = 0,
        AtLeast = 1
    };

    struct Requirement
    {
        Need Kind{ Need::Exact };
        int64_t Bytes{ 0 };
        KeyRef Pass;
        std::string_view Binding;
        std::source_location Where;
    };

    struct BufferFacts
    {
        std::vector<Requirement> Requirements;
        lodestone::BufferUsageFlags Usage{ lodestone::BufferUsageFlags::Invalid };
    };

    // vkCmdDispatchIndirect reads three uint32 values, vkCmdDrawIndirect four
    constexpr int64_t k_DispatchArgumentBytes = 12;
    constexpr int64_t k_DrawArgumentBytes = 16;

    // The bytes a binding states, when the shader states them. DescribeCreation decides whether the shader gives the
    // size at all (G10); the element count or the block size then gives the number.
    std::optional<Requirement> StatedRequirement(const lodestone::manifest::ResolvedResource& resource)
    {
        const lodestone::manifest::CreationFields fields = lodestone::manifest::DescribeCreation(resource);
        const bool fromShader = fields.Size == lodestone::manifest::CreationSource::Shader ||
                                fields.Size == lodestone::manifest::CreationSource::ShaderPerVariant;

        if (!fromShader)
        {
            return std::nullopt;
        }

        if (resource.ElementCount() > 0u)
        {
            const uint64_t bytes = resource.ElementCount() * resource.ElementStride();
            return Requirement{ .Kind = Need::Exact, .Bytes = static_cast<int64_t>(bytes) };
        }

        // a uniform block: the buffer may hold more than one block, so it needs at least this many bytes
        if (resource.ByteSize() > 0u)
        {
            return Requirement{ .Kind = Need::AtLeast, .Bytes = static_cast<int64_t>(resource.ByteSize()) };
        }

        return std::nullopt;
    }

    lodestone::BufferUsageFlags UsageOf(BufferUse use)
    {
        switch (use)
        {
        case BufferUse::Uniform:
            return lodestone::BufferUsageFlags::Uniform;
        case BufferUse::Storage:
            return lodestone::BufferUsageFlags::Storage;
        case BufferUse::Vertex:
            return lodestone::BufferUsageFlags::Vertex;
        case BufferUse::Index:
            return lodestone::BufferUsageFlags::Index;
        case BufferUse::Indirect:
            return lodestone::BufferUsageFlags::Indirect;
        case BufferUse::TransferSource:
            return lodestone::BufferUsageFlags::CopySource;
        case BufferUse::TransferDestination:
            return lodestone::BufferUsageFlags::CopyDestination;
        case BufferUse::Invalid:
            break;
        }

        return lodestone::BufferUsageFlags::Invalid;
    }

    bool IsBuffer(const FrozenGraph& frozen, ResourceIndex resource)
    {
        return resource.IsValid() && frozen.Resources[resource.Value].Kind == ResourceKind::Buffer;
    }

    // Each shader binding of a buffer: its usage, and its stated size
    void GatherBindings(const FrozenGraph& frozen, const BoundGraph& bound, std::vector<BufferFacts>& facts)
    {
        for (size_t slotIndex = 0u; slotIndex < bound.Slots.size(); ++slotIndex)
        {
            const ResourceIndex resource = bound.Slots[slotIndex].Resource;
            const SlotSource& source = bound.Sources[slotIndex];

            // a slot that no bind names is already LM302
            if (source.Bind < 0 || !IsBuffer(frozen, resource))
            {
                continue;
            }

            BufferFacts& buffer = facts[resource.Value];
            buffer.Usage |= source.Reflection.RequiredBufferUsage();
            std::optional<Requirement> stated = StatedRequirement(source.Reflection);
            if (!stated.has_value())
            {
                continue;
            }

            const FrozenPass& pass = frozen.Passes[source.Pass];
            const FrozenBind& bind = pass.Binds[source.Bind];
            stated->Pass = pass.Key;
            stated->Binding = bind.BindingName;
            stated->Where = bind.Where;
            buffer.Requirements.push_back(*stated);
        }
    }

    // The pass statements that name a buffer outside the shader: indirect arguments, vertex and index buffers, and a
    // Record pass's uses
    void GatherStatements(const FrozenGraph& frozen, std::vector<BufferFacts>& facts)
    {
        for (const FrozenPass& pass : frozen.Passes)
        {
            const FrozenDispatch& dispatch = pass.Dispatch;

            if (dispatch.Form == DispatchForm::Indirect && IsBuffer(frozen, dispatch.Resource))
            {
                BufferFacts& buffer = facts[dispatch.Resource.Value];
                buffer.Usage |= lodestone::BufferUsageFlags::Indirect;
                buffer.Requirements.push_back(Requirement{ .Kind = Need::AtLeast,
                                                           .Bytes = dispatch.ArgumentOffset + k_DispatchArgumentBytes,
                                                           .Pass = pass.Key,
                                                           .Where = dispatch.Where });
            }

            if (pass.Draw.Form == DrawForm::DrawIndirect && IsBuffer(frozen, pass.Draw.Arguments))
            {
                BufferFacts& buffer = facts[pass.Draw.Arguments.Value];
                buffer.Usage |= lodestone::BufferUsageFlags::Indirect;
                buffer.Requirements.push_back(Requirement{ .Kind = Need::AtLeast,
                                                           .Bytes = k_DrawArgumentBytes,
                                                           .Pass = pass.Key,
                                                           .Where = pass.Draw.Where });
            }

            for (const FrozenVertexBuffer& vertices : pass.VertexBuffers)
            {
                if (IsBuffer(frozen, vertices.Buffer))
                {
                    facts[vertices.Buffer.Value].Usage |= lodestone::BufferUsageFlags::Vertex;
                }
            }

            if (IsBuffer(frozen, pass.Index.Buffer))
            {
                facts[pass.Index.Buffer.Value].Usage |= lodestone::BufferUsageFlags::Index;
            }

            for (const FrozenUse& use : pass.Uses)
            {
                if (use.Kind == ResourceKind::Buffer && IsBuffer(frozen, use.Resource))
                {
                    facts[use.Resource.Value].Usage |= UsageOf(use.AsBuffer);
                }
            }
        }
    }

    DiagnosticValue ByteSizeValue(int64_t bytes)
    {
        return DiagnosticValue{ .FieldName = "ByteSize", .Kind = DiagnosticValueKind::Integer, .Integer = bytes };
    }

    void ReportMismatch(DiagnosticSink& sink, const FrozenResource& row, const Requirement& requirement,
                        int64_t size, std::source_location size_where)
    {
        Diagnostic diag
        {
            .Code = DiagnosticCode::LM401,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM401),
            .Where = requirement.Where,
            .OtherWhere = size_where,
            .Pass = requirement.Pass,
            .Resource = row.Key,
            .Binding = requirement.Binding,
            .Expected = ByteSizeValue(size),
            .Actual = ByteSizeValue(requirement.Bytes),
        };

        sink.Report(diag);
    }

    bool Fits(const Requirement& requirement, int64_t size)
    {
        return requirement.Kind == Need::Exact ? requirement.Bytes == size : requirement.Bytes <= size;
    }

    // A stated size (a declared Bytes(n) buffer, or an import's ByteSize): each requirement must fit it. OtherWhere is
    // the declaration.
    int64_t CheckStatedSize(DiagnosticSink& sink, const FrozenResource& row, int64_t size, const BufferFacts& buffer)
    {
        for (const Requirement& requirement : buffer.Requirements)
        {
            if (!Fits(requirement, size))
            {
                ReportMismatch(sink, row, requirement, size, row.Where);
            }
        }

        return size;
    }

    // A Derived buffer: the first Exact requirement sets the size, or the largest AtLeast one when no binding states
    // an exact count. OtherWhere is the requirement that set the size. No requirement at all is LM402.
    int64_t DeriveSize(DiagnosticSink& sink, const FrozenResource& row, const BufferFacts& buffer)
    {
        if (buffer.Requirements.empty())
        {
            Diagnostic diag
            {
                .Code = DiagnosticCode::LM402,
                .Severity = DefaultSeverityOf(DiagnosticCode::LM402),
                .Where = row.Where,
                .Resource = row.Key,
            };

            sink.Report(diag);
            return k_UnknownByteSize;
        }

        const auto isExact = [](const Requirement& requirement)
        {
            return requirement.Kind == Need::Exact;
        };

        const auto byBytes = [](const Requirement& lhs, const Requirement& rhs)
        {
            return lhs.Bytes < rhs.Bytes;
        };

        const auto exact = std::ranges::find_if(buffer.Requirements, isExact);
        const Requirement& source = (exact != buffer.Requirements.end()) ? *exact :
                                                                             *std::ranges::max_element(buffer.Requirements, byBytes);

        for (const Requirement& requirement : buffer.Requirements)
        {
            if (!Fits(requirement, source.Bytes))
            {
                ReportMismatch(sink, row, requirement, source.Bytes, source.Where);
            }
        }

        return source.Bytes;
    }

}

std::optional<SizedGraph> Size(const FrozenGraph& frozen, const BoundGraph& bound, DiagnosticSink& sink)
{
    const int32_t errorsBefore = sink.ErrorCount();
    std::vector<BufferFacts> facts(frozen.Resources.size());
    GatherBindings(frozen, bound, facts);
    GatherStatements(frozen, facts);

    SizedGraph result;
    result.Resources.resize(frozen.Resources.size());

    for (size_t rowIndex = 0u; rowIndex < frozen.Resources.size(); ++rowIndex)
    {
        const FrozenResource& row = frozen.Resources[rowIndex];
        SizedResource& sized = result.Resources[rowIndex];
        sized.BufferUsage = facts[rowIndex].Usage;

        if (row.Kind != ResourceKind::Buffer)
        {
            continue;
        }

        // an imported buffer promises its size; Tick checks the supplied buffer (LM701)
        if (row.Origin == ResourceOrigin::Imported)
        {
            const int64_t promised = frozen.BufferImports[row.DeclarationIndex].ByteSize;
            sized.ByteSize = CheckStatedSize(sink, row, promised, facts[rowIndex]);
            continue;
        }

        const BufferDeclaration& declaration = frozen.Buffers[row.DeclarationIndex];

        if (declaration.Size.Kind == SizeSource::Kind::Bytes)
        {
            sized.ByteSize = CheckStatedSize(sink, row, declaration.Size.Size, facts[rowIndex]);
            continue;
        }

        sized.ByteSize = DeriveSize(sink, row, facts[rowIndex]);
    }

    SizeImages(frozen, bound, sink, result);

    if (sink.ErrorCount() != errorsBefore)
    {
        return std::nullopt;
    }

    return result;
}

}
