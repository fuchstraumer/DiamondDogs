#pragma once
#ifndef DIAMOND_DOGS_LOOM_GRAPH_BUILDER_HPP
#define DIAMOND_DOGS_LOOM_GRAPH_BUILDER_HPP
#include "LoomError.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomKeys.hpp"
#include "Declarations.hpp"
#include "PassBuilders.hpp"
#include "SequenceBuilder.hpp"
#include <cstdint>
#include <memory>
#include <string_view>
#include <source_location>
#include <utility>

namespace loom
{
    // todo: wrapper for the swapchain that offers `TryAcquire` and `Present`, without blocking
    struct FrameTarget {};

    struct GraphRecord;

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
        GraphBuilder(std::string_view name, std::string_view shader_library_name);
        ~GraphBuilder();

        GraphBuilder(const GraphBuilder&) = delete;
        GraphBuilder& operator=(const GraphBuilder&) = delete;
        GraphBuilder(GraphBuilder&&) noexcept;
        GraphBuilder& operator=(GraphBuilder&&) noexcept;

        void Pin(std::string_view axis_name, AxisValue value,
                 std::source_location where = std::source_location::current());
        void Declare(const BufferDeclaration& declaration,
                     std::source_location where = std::source_location::current());
        void Declare(const ImageDeclaration& declaration,
                     std::source_location where = std::source_location::current());
        void Declare(const SamplerDeclaration& declaration,
                     std::source_location where = std::source_location::current());
        void Import(const ImportDeclaration& declaration,
                    std::source_location where = std::source_location::current());
        // The FrameTarget object itself goes to Realize, so Compile stays pure (T2, correction 2).
        void Import(FrameTargetKey target,
                    std::source_location where = std::source_location::current());
        void Output(const OutputDeclaration& declaration,
                    std::source_location where = std::source_location::current());

        void DeclareAnchor(AnchorKey anchor,
                           std::source_location where = std::source_location::current());
        void OrderAnchors(AnchorKey earlier, AnchorKey later,
                          std::source_location where = std::source_location::current());

        // `where` is forwarded into the child builder, so Freeze points at the caller, not at this header (S4).
        template<typename Build>
        void AddDispatch(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, Build&& build,
                         std::source_location where = std::source_location::current())
        {
            DispatchBuilder builder{ pass, run_policy, queue_affinity, where };
            std::forward<Build>(build)(builder);
            commitDispatch(builder);
        }

        template<typename Build>
        void AddSequence(SequenceKey sequence, Build&& build,
                         std::source_location where = std::source_location::current())
        {
            SequenceBuilder builder{ sequence, where };
            std::forward<Build>(build)(builder);
            commitSequence(builder);
        }

        // AddDraw and AddRecord come with step 7, with DrawBuilder and RecordBuilder.

        // What the builder recorded, for Freeze and for tests. GraphRecord is defined in src/Records.hpp.
        [[nodiscard]] const GraphRecord& Record() const noexcept;

    private:
        // The committers take the child builder, not its record: the records are incomplete in this header.
        void commitDispatch(DispatchBuilder& pass);
        void commitSequence(SequenceBuilder& sequence);

        std::unique_ptr<GraphRecord> record;
    };
}

#endif // !DIAMOND_DOGS_LOOM_GRAPH_BUILDER_HPP
