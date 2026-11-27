#pragma once
#ifndef DIAMOND_DOGS_LOOM_PATH_LIST_BUILDER_HPP
#define DIAMOND_DOGS_LOOM_PATH_LIST_BUILDER_HPP
#include "Declarations.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include <memory>
#include <source_location>
#include <utility>

// Path lists (step 5): alternative bodies for one part of the graph, one per device tier, chosen once per device.
// Compile picks the first path that the device satisfies. The list's contract with the rest of the graph is what it
// produces (Produces) and the anchors its paths expose; a path's inputs are its own (Declare, Import), so a ray-traced
// path and a software path can read entirely different data. The module README explains the rules.
namespace loom
{

    struct PathRecord;
    struct PathListRecord;
    class GraphBuilder;
    class PathListBuilder;

    // The builder a Path lambda receives: the sequence calls, plus the resources that only this path declares or
    // imports. Its passes run in source order (Q3d), as a sequence's do.
    class PathBuilder
    {
    public:
        ~PathBuilder();

        PathBuilder(const PathBuilder&) = delete;
        PathBuilder& operator=(const PathBuilder&) = delete;
        PathBuilder(PathBuilder&&) = delete;
        PathBuilder& operator=(PathBuilder&&) = delete;

        void Declare(const BufferDeclaration& declaration, std::source_location where = std::source_location::current());
        void Declare(const ImageDeclaration& declaration, std::source_location where = std::source_location::current());
        void Declare(const SamplerDeclaration& declaration, std::source_location where = std::source_location::current());
        // A path-local import: the app supplies it only when this path is chosen (CompiledGraph::ChosenPath)
        void Import(const ImportDeclaration& declaration, std::source_location where = std::source_location::current());
        void Import(const BufferImportDeclaration& declaration,
                    std::source_location where = std::source_location::current());

        void After(AnchorKey anchor, std::source_location where = std::source_location::current());
        void Before(AnchorKey anchor, std::source_location where = std::source_location::current());
        // Every path of a list must expose the same anchors (LM116): they are part of the list's contract.
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

        template<typename Build>
        void AddDraw(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, Build&& build,
                     std::source_location where = std::source_location::current())
        {
            DrawBuilder builder{ pass, run_policy, queue_affinity, where };
            std::forward<Build>(build)(builder);
            commitPass(builder);
        }

        template<typename Build>
        void AddRecord(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, Build&& build,
                       std::source_location where = std::source_location::current())
        {
            RecordBuilder builder{ pass, run_policy, queue_affinity, where };
            std::forward<Build>(build)(builder);
            commitPass(builder);
        }

    private:
        friend class PathListBuilder;

        PathBuilder(PathKey path, std::source_location where);

        // Takes the builder, not its record: PassRecord is incomplete here
        void commitPass(PassBuilder& pass);

        [[nodiscard]] std::unique_ptr<PathRecord> takeRecord() noexcept;

        std::unique_ptr<PathRecord> record;
    };

    // The builder an AddPaths lambda receives. Paths are in order of preference: the first one the device satisfies
    // is chosen; the last one should run everywhere the graph must run.
    class PathListBuilder
    {
    public:
        ~PathListBuilder();

        PathListBuilder(const PathListBuilder&) = delete;
        PathListBuilder& operator=(const PathListBuilder&) = delete;
        PathListBuilder(PathListBuilder&&) = delete;
        PathListBuilder& operator=(PathListBuilder&&) = delete;

        // The keys the rest of the graph may read from this list. Every path must write each one (LM308), and a path
        // writes no key from outside the list that is not produced here (LM309).
        void Produces(ResourceKey<Buffer> resource, std::source_location where = std::source_location::current());
        void Produces(ResourceKey<Image> resource, std::source_location where = std::source_location::current());

        // The list sits in the graph like one sequence
        void After(AnchorKey anchor, std::source_location where = std::source_location::current());
        void Before(AnchorKey anchor, std::source_location where = std::source_location::current());

        template<typename Build>
        void Path(PathKey path, Build&& build, std::source_location where = std::source_location::current())
        {
            PathBuilder builder{ path, where };
            std::forward<Build>(build)(builder);
            commitPath(builder);
        }

    private:
        friend class GraphBuilder;

        PathListBuilder(PathListKey list, std::source_location where);

        void commitPath(PathBuilder& path);

        [[nodiscard]] std::unique_ptr<PathListRecord> takeRecord() noexcept;

        std::unique_ptr<PathListRecord> record;
    };

}

#endif // !DIAMOND_DOGS_LOOM_PATH_LIST_BUILDER_HPP
