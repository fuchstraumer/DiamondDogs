#pragma once
#ifndef DIAMOND_DOGS_LOOM_RECORDS_HPP
#define DIAMOND_DOGS_LOOM_RECORDS_HPP
#include "Declarations.hpp"
#include "LoomKeys.hpp"
#include "PassStatements.hpp"
#include "utility/Delegate.hpp"
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The builder's log. Each record is what one builder call said, with the call's location. Records check nothing:
// Freeze reads them and judges. Rules for every record here:
//  - Keys are stored as KeyRef plus a kind, not as typed keys. A typed key has only a consteval constructor, so a
//    record that held one could not be default-constructed or resized.
//  - Text the caller passes at run time (module, entry point, binding and axis names) is owned as std::string. Key
//    names are literals, or runtime names that the graph keeps in KeyNames (CC-R5), so a KeyRef's view is safe; a
//    caller's string_view may not be.
//  - Statements that must appear exactly once (shader, dispatch) are vectors, so Freeze can see zero or two
//    (LM105, LM106).
namespace loom
{

    enum class ResourceKind : uint8_t
    {
        Invalid = 0,
        Buffer = 1,
        Image = 2,
        Sampler = 3
    };

    enum class PassKind : uint8_t
    {
        Invalid = 0,
        Dispatch = 1,
        Draw = 2,
        Record = 3
    };

    // The public AccessModifier has no "not narrowed" value, because a caller states that by calling the overload without one.
    enum class BindNarrowing : uint8_t
    {
        Invalid = 0,
        Reflected = 1,      // the Bind overload without an AccessModifier: use the shader's access as reflected
        ReadOnly = 2,
        WriteOnly = 3
    };

    enum class DispatchForm : uint8_t
    {
        Invalid = 0,
        ThreadsOver = 1,    // DispatchThreadsOver(image): the image extent, divided by the workgroup size
        Threads = 2,        // DispatchThreads(x, y, z): a thread count, divided by the workgroup size
        Indirect = 3        // DispatchIndirect(buffer)
    };

    enum class DrawForm : uint8_t
    {
        Invalid = 0,
        Draw = 1,
        DrawIndexed = 2,
        DrawIndirect = 3,
        DrawIndirectCount = 4       // the GPU reads the draw count from a second buffer
    };

    [[nodiscard]] constexpr bool IsIndirectDraw(DrawForm form) noexcept
    {
        return form == DrawForm::DrawIndirect || form == DrawForm::DrawIndirectCount;
    }

    // vkCmdDrawIndexed takes a signed vertex offset, so the unset value is one no real draw uses.
    inline constexpr int32_t k_UnsetVertexOffset = std::numeric_limits<int32_t>::min();

    // Pass statements

    struct ShaderRecord
    {
        std::string ModuleName;
        std::string EntryPoint;             // the compute entry point, or the vertex entry point of a draw
        std::string FragmentEntryPoint;     // empty for a dispatch
        std::source_location Where;
    };

    struct PinRecord
    {
        std::string AxisName;
        AxisValue Value;
        std::source_location Where;
    };

    struct AnchorUseRecord
    {
        KeyRef Anchor;
        std::source_location Where;
    };

    struct BindRecord
    {
        std::string BindingName;
        KeyRef Resource;
        ResourceKind Kind;
        BindNarrowing Narrowing;
        bool Previous{ false };             // BindPrevious: reads the history slot (R4)
        bool FrameStart{ false };           // BindFrameStart: reads the contents before this frame's writers
        std::source_location Where;
    };

    struct ParamsRecord
    {
        KeyRef Params;
        uint32_t ByteSize{};                  // sizeof(T) from the template, for the LM305 walk and the LM704 check
        uint32_t Alignment{};                 // alignof(T)
        std::source_location Where;
    };

    struct DispatchRecord
    {
        DispatchForm Form{ DispatchForm::Invalid };
        KeyRef Resource;                    // ThreadsOver: the image. Indirect: the argument buffer. Threads: empty.
        int32_t X{ -1 };
        int32_t Y{ -1 };
        int32_t Z{ -1 };
        int64_t ArgumentOffset{ 0 };        // Indirect: the byte offset of the arguments in the buffer
        std::source_location Where;
    };

    struct DrawRecord
    {
        DrawForm Form{ DrawForm::Invalid };
        KeyRef Arguments;                   // DrawIndirect and DrawIndirectCount
        int64_t ArgumentOffset{ 0 };        // the byte offset of the first draw's arguments
        KeyRef Count;                       // DrawIndirectCount: the buffer that holds the draw count
        int64_t CountOffset{ 0 };
        int32_t MaxDraws{ -1 };
        int32_t VertexOrIndexCount{ -1 };
        int32_t InstanceCount{ -1 };
        int32_t FirstVertexOrIndex{ -1 };
        int32_t FirstInstance{ -1 };
        int32_t VertexOffset{ k_UnsetVertexOffset };    // DrawIndexed only. Negative offsets are legal, so -1 is not "unset".
        std::source_location Where;
    };

    // A color target or a Record use names either a graph image or a frame target (the swapchain, for example).
    enum class TargetKind : uint8_t
    {
        Invalid = 0,
        Image = 1,
        FrameTarget = 2
    };

    struct ColorTargetRecord
    {
        int32_t Slot{ -1 };
        KeyRef Target;
        TargetKind Kind{ TargetKind::Invalid };
        ColorLoad Load;
        AttachmentStore Store{ AttachmentStore::Invalid };
        BlendMode Blend{ BlendMode::Invalid };
        std::source_location Where;
    };

    struct DepthTargetRecord
    {
        KeyRef Target;
        DepthLoad Load;
        AttachmentStore Store{ AttachmentStore::Invalid };
        CompareOp Compare{ CompareOp::Invalid };
        DepthWrite Write{ DepthWrite::Invalid };
        std::source_location Where;
    };

    struct RasterStateRecord
    {
        RasterState State;
        std::source_location Where;
    };

    struct VertexBufferRecord
    {
        int32_t Slot{ -1 };
        KeyRef Buffer;
        std::source_location Where;
    };

    struct IndexBufferRecord
    {
        KeyRef Buffer;
        IndexType Type{ IndexType::Invalid };
        std::source_location Where;
    };

    // One Uses statement of a Record pass. Use is the ImageUse or BufferUse value that Kind selects.
    struct UseRecord
    {
        KeyRef Resource;
        ResourceKind Kind{ ResourceKind::Invalid };
        TargetKind Target{ TargetKind::Invalid };       // images only: a graph image or a frame target
        ResourceAccess Access{ ResourceAccess::Invalid };
        ImageUse AsImage{ ImageUse::Invalid };
        BufferUse AsBuffer{ BufferUse::Invalid };
        std::source_location Where;
    };

    struct RecorderRecord
    {
        Delegate<void(RecordContext&)> Callback;
        std::source_location Where;
    };

    struct PassRecord
    {
        KeyRef Key;
        PassKind Kind;
        RunPolicy Policy;
        QueueAffinity Affinity;
        std::source_location Where;         // the Add* call

        // The order of the Add* call across the whole graph, nested passes included. The Order stage breaks ties with
        // it (Q3c). The builder assigns it at commit.
        int32_t DeclarationOrder{ -1 };

        std::vector<ShaderRecord> Shaders;
        std::vector<PinRecord> Pins;
        std::vector<AnchorUseRecord> After;
        std::vector<AnchorUseRecord> Before;
        std::vector<BindRecord> Binds;
        std::vector<ParamsRecord> Params;
        std::vector<DispatchRecord> Dispatches;

        // Draw passes. Depth, raster state and index buffer may appear at most once (LM113); a vector lets Freeze see two.
        std::vector<DrawRecord> Draws;
        std::vector<ColorTargetRecord> Colors;
        std::vector<DepthTargetRecord> Depths;
        std::vector<RasterStateRecord> RasterStates;
        std::vector<VertexBufferRecord> VertexBuffers;
        std::vector<IndexBufferRecord> IndexBuffers;

        // Record passes
        std::vector<UseRecord> Uses;
        std::vector<RecorderRecord> Recorders;
    };

    // Sequences

    struct ExposedAnchorRecord
    {
        KeyRef Anchor;
        // The count of passes in the sequence before the ExposeAnchor call. The anchor sits after pass Position - 1
        // and before pass Position (Q3d). 0 is the start of the sequence; Passes.size() is the end.
        int32_t Position{ -1 };
        std::source_location Where;
    };

    struct SequenceRecord
    {
        KeyRef Key;
        std::source_location Where;

        std::vector<AnchorUseRecord> After;
        std::vector<AnchorUseRecord> Before;
        // Source order is the run order - but only within a single sequence. This makes
        // it easier for local reasoning and manipulation of passes within the sequence,
        // without having to consider the global order of passes across different sequences
        // outside of the local sequence a user is working on.
        std::vector<PassRecord> Passes;
        // ExposedAnchors are extension points - anchors exposed to other passes or sequences, which
        // can be used to inject custom behaviors at the given anchor points.
        std::vector<ExposedAnchorRecord> ExposedAnchors;
    };

    // Graph-level records

    // A declaration as the caller wrote it. Its typed key cannot be default-constructed, so build these whole.
    template<typename Declaration>
    struct DeclarationRecord
    {
        Declaration Value;
        std::source_location Where;
    };

    // Path lists (step 5). A path is one alternative body: a sequence, and the resources that only it declares or
    // imports. ChoosePaths (the first step of Compile) replaces each list with its chosen path: the body becomes an
    // ordinary sequence under the list's key, and the path's declarations join the graph's.
    struct PathRecord
    {
        KeyRef Key;
        std::source_location Where;
        SequenceRecord Body;

        std::vector<DeclarationRecord<BufferDeclaration>> Buffers;
        std::vector<DeclarationRecord<ImageDeclaration>> Images;
        std::vector<DeclarationRecord<SamplerDeclaration>> Samplers;
        std::vector<DeclarationRecord<ImportDeclaration>> Imports;
        std::vector<DeclarationRecord<BufferImportDeclaration>> BufferImports;
    };

    // A key that the list hands to the rest of the graph: every path must write it, and a path writes no outside key
    // that the list does not produce.
    struct ProducesRecord
    {
        KeyRef Resource;
        std::source_location Where;
    };

    struct PathListRecord
    {
        KeyRef Key;
        std::source_location Where;
        std::vector<AnchorUseRecord> After;
        std::vector<AnchorUseRecord> Before;
        std::vector<ProducesRecord> Produces;
        std::vector<PathRecord> Paths;      // in order of preference
    };

    struct FrameTargetImportRecord
    {
        KeyRef Target;
        std::source_location Where;
    };

    struct AnchorDeclarationRecord
    {
        KeyRef Anchor;
        std::source_location Where;
    };

    struct AnchorOrderRecord
    {
        KeyRef Earlier;
        KeyRef Later;
        std::source_location Where;
    };

    // Everything one GraphBuilder recorded. The builder owns one; Freeze reads it through a const reference.
    // The runtime key names of one graph (CC-R5). A deque never moves its elements, so every view stays valid as it
    // grows, and nothing is ever removed. The builder and each CompiledGraph made from it share it, so the names live
    // as long as the last of them.
    struct KeyNames
    {
        std::deque<std::string> Names;
        std::unordered_map<uint64_t, std::string_view> ByHash;
    };

    struct GraphRecord
    {
        std::string Name;
        std::string ShaderLibraryName;
        std::shared_ptr<KeyNames> RuntimeNames;

        std::vector<PinRecord> Pins;

        std::vector<DeclarationRecord<BufferDeclaration>> Buffers;
        std::vector<DeclarationRecord<ImageDeclaration>> Images;
        std::vector<DeclarationRecord<SamplerDeclaration>> Samplers;
        std::vector<DeclarationRecord<ImportDeclaration>> Imports;
        std::vector<DeclarationRecord<BufferImportDeclaration>> BufferImports;
        std::vector<FrameTargetImportRecord> FrameTargets;
        std::vector<DeclarationRecord<OutputDeclaration>> Outputs;

        std::vector<AnchorDeclarationRecord> Anchors;
        std::vector<AnchorOrderRecord> AnchorOrders;

        std::vector<PassRecord> Passes;     // top-level passes only; nested passes live in their sequence
        std::vector<SequenceRecord> Sequences;
        std::vector<PathListRecord> PathLists;  // empty after ChoosePaths: each chosen path is in Sequences

        int32_t NextDeclarationOrder{ 0 };  // the next PassRecord::DeclarationOrder; 0 is the first real value
    };

}

#endif // !DIAMOND_DOGS_LOOM_RECORDS_HPP
