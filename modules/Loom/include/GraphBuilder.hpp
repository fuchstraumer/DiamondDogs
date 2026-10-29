#pragma once
#ifndef DIAMOND_DOGS_LOOM_GRAPH_BUILDER_HPP
#define DIAMOND_DOGS_LOOM_GRAPH_BUILDER_HPP
#include "LoomError.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomKeys.hpp"
#include "Declarations.hpp"
#include <cstdint>
#include <string_view>
#include <source_location>

namespace rhi
{
    class ShaderLibrary;
}

namespace loom
{   
    // todo: wrapper for the swapchain that offers `TryAcquire` and `Present`, without blocking
    struct FrameTarget {};

    // Thin shim type to allow us to represent lodestone::AxisValue without leaking lodestone
    // includes outside of loom:: 
    struct AxisValue
    {
        static AxisValue Unsigned(uint32_t value) noexcept;
        static AxisValue Boolean(bool value) noexcept;
        static AxisValue EnumCase(std::string_view value) noexcept;
        union
        {
            uint32_t ValueU32;
            bool ValueBool;
            std::string_view ValueEnumCase;
        };
    };

    /**
     * @brief GraphBuilder is responsible for constructing the rendering graph.
     *
     * It allows declaring resources, importing external targets, defining outputs, and specifying the
     * execution order of passes and sequences. Every function uses a default constructed source_location
     * so that when the graph is built, reports about where it breaks are trivially tracked back to the
     * causative source code location.
     */
    class GraphBuilder
    {
    public:
        GraphBuilder(std::string_view name, std::string_view shader_library_name) noexcept;

        void Pin(std::string_view axis_name, AxisValue value); // needs AxisValue includes from lodestone, shoot
        void Declare(const BufferDeclaration& declaration,
                     std::source_location where = std::source_location::current());
        void Declare(const ImageDeclaration& declaration,
                     std::source_location where = std::source_location::current());
        void Declare(const SamplerDeclaration& declaration,
                     std::source_location where = std::source_location::current());
        void Import(const ImportDeclaration& declaration,
                    std::source_location where = std::source_location::current());
        void Import(FrameTargetKey target, FrameTarget& frame_target,
                    std::source_location where = std::source_location::current());
        void Output(const OutputDeclaration& declaration,
                    std::source_location where = std::source_location::current());

        void DeclareAnchor(AnchorKey anchor,
                           std::source_location where = std::source_location::current());
        void OrderAnchors(AnchorKey earlier, AnchorKey later,
                          std::source_location where = std::source_location::current());

        template<typename Build>
        void AddDispatch(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, Build&& build,
                         std::source_location where = std::source_location::current());   // S4
        template<typename Build>
        void AddDraw(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, Build&& build,
                     std::source_location where = std::source_location::current());
        template<typename Build>
        void AddRecord(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, Build&& build,
                       std::source_location where = std::source_location::current());
        template<typename Build>
        void AddSequence(SequenceKey sequence, Build&& build,
                         std::source_location where = std::source_location::current());
    };
}

#endif // !DIAMOND_DOGS_LOOM_GRAPH_BUILDER_HPP
