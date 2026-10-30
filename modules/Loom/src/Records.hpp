#pragma once
#ifndef DIAMOND_DOGS_LOOM_RECORDS_HPP
#define DIAMOND_DOGS_LOOM_RECORDS_HPP
#include "Declarations.hpp"
#include "LoomKeys.hpp"
#include <cstdint>
#include <limits>
#include <source_location>
#include <string>
#include <vector>

// The builder's log. Each record is what one builder call said, with the call's location. Records check nothing:
// Freeze reads them and judges. Rules for every record here:
//  - Keys are stored as KeyRef plus a kind, not as typed keys. A typed key has only a consteval constructor, so a
//    record that held one could not be default-constructed or resized.
//  - Text the caller passes at run time (module, entry point, binding and axis names) is owned as std::string. Key
//    names are literals (consteval), so a KeyRef's view is safe; a caller's string_view may not be.
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
        DrawIndirect = 3
    };

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
        DispatchForm Form;
        KeyRef Resource;                    // ThreadsOver: the image. Indirect: the argument buffer. Threads: empty.
        int32_t X{ -1 };
        int32_t Y{ -1 };
        int32_t Z{ -1 };
        std::source_location Where;
    };

    // Step 7 adds the color, depth and raster state records, and the Record pass's uses and recorder.
    struct DrawRecord
    {
        DrawForm Form;
        KeyRef Arguments;                   // DrawIndirect only
        int32_t VertexOrIndexCount{ -1 };
        int32_t InstanceCount{ -1 };
        int32_t FirstVertexOrIndex{ -1 };
        int32_t FirstInstance{ -1 };
        int32_t VertexOffset{ k_UnsetVertexOffset };    // DrawIndexed only. Negative offsets are legal, so -1 is not "unset".
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
        std::vector<DrawRecord> Draws;
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
        std::vector<PassRecord> Passes;     // source order is the run order (Q3d)
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
    struct GraphRecord
    {
        std::string Name;
        std::string ShaderLibraryName;

        std::vector<PinRecord> Pins;

        std::vector<DeclarationRecord<BufferDeclaration>> Buffers;
        std::vector<DeclarationRecord<ImageDeclaration>> Images;
        std::vector<DeclarationRecord<SamplerDeclaration>> Samplers;
        std::vector<DeclarationRecord<ImportDeclaration>> Imports;
        std::vector<FrameTargetImportRecord> FrameTargets;
        std::vector<DeclarationRecord<OutputDeclaration>> Outputs;

        std::vector<AnchorDeclarationRecord> Anchors;
        std::vector<AnchorOrderRecord> AnchorOrders;

        std::vector<PassRecord> Passes;     // top-level passes only; nested passes live in their sequence
        std::vector<SequenceRecord> Sequences;

        int32_t NextDeclarationOrder{ 0 };  // the next PassRecord::DeclarationOrder; 0 is the first real value
    };

}

#endif // !DIAMOND_DOGS_LOOM_RECORDS_HPP
