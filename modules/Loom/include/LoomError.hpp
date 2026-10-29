#pragma once
#ifndef DIAMOND_DOGS_LOOM_ERROR_HPP
#define DIAMOND_DOGS_LOOM_ERROR_HPP
#include <cstdint>
#include <string_view>

namespace loom
{

    enum class DiagnosticStage : uint8_t
    {
        Invalid = 0,
        Freeze = 1,
        Resolve = 2,
        Bind = 3,
        Size = 4,
        Order = 5,
        Layout = 6,
        Runtime = 7
    };

    enum class DiagnosticSeverity : uint8_t
    {
        Invalid = 0,
        Note = 1,
        Warning = 2,
        Error = 3,
        Fatal = 4
    };

    // The hundreds digit of each value is its DiagnosticStage. StageOf depends on this.
    enum class DiagnosticCode : uint16_t
    {
        Invalid = 0,

        // Freeze
        LM101 = 101, // A required field is Invalid
        LM102 = 102, // A key has two publishers
        LM103 = 103, // A key is used and never published
        LM104 = 104, // An anchor is used and never declared
        LM105 = 105, // A pass does not have exactly one shader or recorder
        LM106 = 106, // A pass does not have exactly one dispatch or draw statement
        LM107 = 107, // A key is declared with two kinds
        LM108 = 108, // A graph pin is set twice
        LM109 = 109, // A value is out of range

        // Resolve
        LM201 = 201, // Unknown module or entry point
        LM202 = 202, // Shader stage does not match the pass kind
        LM203 = 203, // No variant matches
        LM204 = 204, // More than one variant matches
        LM205 = 205, // An axis is unpinned, and the declared default is used

        // Bind
        LM301 = 301, // A Bind name is in no variant of the entry point
        LM302 = 302, // A binding of the selected variant has no Bind
        LM303 = 303, // A narrowing widens the reflected access
        LM304 = 304, // Resource kind does not match the binding
        LM305 = 305, // A params struct field differs from the reflected member
        LM306 = 306, // BindPrevious on a resource with HistoryDepth 0

        // Size
        LM401 = 401, // Two bindings derive different sizes for one resource
        LM402 = 402, // A derived size or extent has no source
        LM403 = 403, // The format does not fit the binding

        // Order
        LM501 = 501, // A cycle
        LM502 = 502, // A hazard with no ordering path
        LM503 = 503, // A read before any write in the frame
        LM504 = 504, // Dead write
        LM505 = 505, // Orphan pass

        // Layout
        LM601 = 601, // A device limit is exceeded
        LM602 = 602, // Color targets do not match the attachments
        LM603 = 603, // Vertex input semantic not found

        // Runtime
        LM701 = 701, // A supplied future does not match its ImportDeclaration
        LM702 = 702, // A future failed
        LM703 = 703, // A params key was never set before the tick
        LM704 = 704, // Set with a type other than the key's
    };

    [[nodiscard]] constexpr DiagnosticStage StageOf(DiagnosticCode code) noexcept
    {
        return static_cast<DiagnosticStage>(static_cast<uint16_t>(code) / 100u);
    }

    static_assert(StageOf(DiagnosticCode::Invalid) == DiagnosticStage::Invalid);
    static_assert(StageOf(DiagnosticCode::LM101) == DiagnosticStage::Freeze);
    static_assert(StageOf(DiagnosticCode::LM501) == DiagnosticStage::Order);
    static_assert(StageOf(DiagnosticCode::LM704) == DiagnosticStage::Runtime);

    // "LM101", "Freeze", "Error". Each returns "Invalid" for a value outside its enum.
    [[nodiscard]] std::string_view ToString(DiagnosticCode code) noexcept;
    [[nodiscard]] std::string_view ToString(DiagnosticStage stage) noexcept;
    [[nodiscard]] std::string_view ToString(DiagnosticSeverity severity) noexcept;

    // The catalog's one-line meaning of a code, for a sink to print after the code.
    [[nodiscard]] std::string_view SummaryOf(DiagnosticCode code) noexcept;

    // The catalog severity. A caller can raise it (strict mode makes a warning an error).
    [[nodiscard]] DiagnosticSeverity DefaultSeverityOf(DiagnosticCode code) noexcept;

}

#endif // !DIAMOND_DOGS_LOOM_ERROR_HPP
