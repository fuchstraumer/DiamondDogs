#pragma once
#ifndef DIAMOND_DOGS_LOOM_SEQUENCE_BUILDER_HPP
#define DIAMOND_DOGS_LOOM_SEQUENCE_BUILDER_HPP
#include "Declarations.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include <memory>
#include <source_location>
#include <utility>

namespace loom
{

    struct SequenceRecord;
    class GraphBuilder;

    // The builder an AddSequence lambda receives. Its passes run in source order, with no pass-to-pass dependencies
    // (Q3d). An exposed anchor sits between the pass added before it and the pass added after it.
    class SequenceBuilder
    {
    public:
        ~SequenceBuilder();

        SequenceBuilder(const SequenceBuilder&) = delete;
        SequenceBuilder& operator=(const SequenceBuilder&) = delete;
        SequenceBuilder(SequenceBuilder&&) = delete;
        SequenceBuilder& operator=(SequenceBuilder&&) = delete;

        void After(AnchorKey anchor, std::source_location where = std::source_location::current());
        void Before(AnchorKey anchor, std::source_location where = std::source_location::current());
        void ExposeAnchor(AnchorKey anchor, std::source_location where = std::source_location::current());

        // `where` is forwarded into the pass builder, so Freeze points at this call, not at this header (S4).
        template<typename Build>
        void AddDispatch(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, Build&& build,
                         std::source_location where = std::source_location::current())
        {
            DispatchBuilder builder{ pass, run_policy, queue_affinity, where };
            std::forward<Build>(build)(builder);
            commitPass(builder);
        }

        // AddDraw and AddRecord come with step 7.

    private:
        friend class GraphBuilder;

        SequenceBuilder(SequenceKey sequence, std::source_location where);

        // Takes the builder, not its record: PassRecord is incomplete here, so its unique_ptr must not be destroyed in
        // this header.
        void commitPass(DispatchBuilder& pass);

        [[nodiscard]] std::unique_ptr<SequenceRecord> takeRecord() noexcept;

        std::unique_ptr<SequenceRecord> record;
    };

}

#endif // !DIAMOND_DOGS_LOOM_SEQUENCE_BUILDER_HPP
