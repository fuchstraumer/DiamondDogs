#pragma once
#ifndef DIAMOND_DOGS_CC_EXECUTOR_HPP
#define DIAMOND_DOGS_CC_EXECUTOR_HPP
#include "ArtifactTable.hpp"
#include "ContentTypes.hpp"
#include "ExecutorTypes.hpp"
#include "async/Task.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace foundation
{
    class Scheduler;
}

namespace cc
{
    class KindRegistry;
    class RecipeTable;
    class Root;
    struct NodeRow;

    // Committed is stronger than Transient: a row that both want keeps Committed (E6).
    enum class Transience : uint8_t
    {
        Invalid = 0,
        Transient,
        Committed,
    };

    // Runs recipes on one scheduler, on its owner thread. One row per recipe: the row is the in-flight entry, the
    // memory cache and the failure memo. The module README, section "Executor", explains the counters.
    class Executor
    {
    public:
        Executor(foundation::Scheduler& _scheduler, const KindRegistry& _registry, const RecipeTable& _recipes);
        // Asserts Idle(): call Shutdown and tick the scheduler until Idle() first.
        ~Executor();
        // rows and roots hold the address of the executor
        Executor(const Executor&) = delete;
        Executor& operator=(const Executor&) = delete;
        Executor(Executor&&) = delete;
        Executor& operator=(Executor&&) = delete;

        // Releases the request of every root. Running nodes finish; tick the scheduler until Idle().
        void Shutdown();
        // True when no node is Ready or Running.
        [[nodiscard]] bool Idle() const noexcept;

        [[nodiscard]] const ArtifactTable& Artifacts() const noexcept;
        [[nodiscard]] std::span<const Diagnostic> FailureDiagnostics(FailureId _failure) const;

    private:
        friend class Root;

        void addRoot(Root& _root);
        void removeRoot(Root& _root);

        // The four parts that decide correctness. Each walk is iterative, never recursive.
        // Adds one to the row's Wanted; on 0 to 1 it starts an Idle row and its unfinished inputs.
        void want(RecipeId _recipe, Transience _transience);
        // Takes one from the row's Wanted; at 0 it cancels a Waiting row and releases its inputs.
        void unwant(RecipeId _recipe);
        // Returns a Waiting or Ready row to Idle and takes its count from each input's Wanted.
        void cancel(RecipeId _recipe);
        // Stores the result of a run, tells the dependents, releases the inputs, and tells the roots.
        void complete(RecipeId _recipe, KindResult _result, Context& _context);

        [[nodiscard]] NodeRow& rowOf(RecipeId _recipe);
        void spawnRun(RecipeId _recipe);
        foundation::Task<void> runNode(RecipeId _recipe);
        void notifyRoots(RecipeId _recipe);

        foundation::Scheduler* scheduler{ nullptr };
        const KindRegistry* registry{ nullptr };
        const RecipeTable* recipes{ nullptr };
        ArtifactTable artifacts;
        // indexed by RecipeId; index 0 is unused, because id 0 is invalid
        std::vector<NodeRow> rows;
        std::vector<Root*> roots;
        // the diagnostics of each failure, indexed by FailureId; index 0 is unused
        std::vector<std::vector<Diagnostic>> failures;
        int32_t readyCount{ 0 };
        int32_t runningCount{ 0 };
    };

    // What a root shows. Outputs are the last good outputs, which can be older than Error.
    struct RootView
    {
        RecipeId Target;
        // the recipe that Outputs came from, or 0 before the first success
        RecipeId Shown;
        std::span<const ArtifactRef> Outputs;
        // true when Shown is not Target: the target is still running or it failed
        bool Stale{ false };
        // Code is Invalid when the target has not failed
        ContentError Error;
    };

    // One viewer slot: the viewport, or a pinned or inspected node (rule 8). It holds its last good outputs, so a
    // retarget keeps showing them until the new target succeeds.
    class Root
    {
    public:
        explicit Root(Executor& _executor);
        ~Root();
        // the executor holds the address of each root
        Root(const Root&) = delete;
        Root& operator=(const Root&) = delete;
        Root(Root&&) = delete;
        Root& operator=(Root&&) = delete;

        // Wants the new target, then releases the old one, so rows that both share are not cancelled in between.
        void Retarget(RecipeId _recipe, Transience _transience);
        // Releases the target. The last good outputs stay until the next success or the destructor.
        void Clear();

        [[nodiscard]] RootView View() const noexcept;

    private:
        friend class Executor;

        void onFinished(const NodeRow& _row);
        void releaseShown();

        Executor* executor{ nullptr };
        RecipeId target;
        RecipeId shown;
        std::vector<ArtifactRef> shownOutputs;
        ContentError error;
    };
}

#endif // !DIAMOND_DOGS_CC_EXECUTOR_HPP
