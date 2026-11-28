#pragma once
#ifndef DIAMOND_DOGS_CC_EXECUTOR_HPP
#define DIAMOND_DOGS_CC_EXECUTOR_HPP
#include "ArtifactTable.hpp"
#include "ContentTypes.hpp"
#include "ExecutorTypes.hpp"
#include "async/Task.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace foundation
{
    class Scheduler;
    template<typename T, std::size_t N>
    class SmallVector;
}

namespace cc
{
    class KindRegistry;
    class RecipeTable;
    class Root;
    struct NodeRow;

    // The executor's walk lists. 16 ids are 64 bytes, SmallVector's limit for its fast inline copy; a longer list
    // moves to the heap.
    using RecipeList = foundation::SmallVector<RecipeId, 16>;

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
        // Returns a Waiting or Ready row with Wanted 0 to Idle. Its inputs go on _unwants for the caller to drain.
        void cancel(RecipeId _recipe, RecipeList& _unwants);
        // Stores the result of a run, tells the dependents, releases the inputs, and tells the roots.
        void complete(RecipeId _recipe, KindResult _result, Context& _context);

        // The steps of want. addWant adds one to the row's Wanted, and on 0 to 1 it makes an Idle row Waiting and
        // puts it on _starting. startRow wants each input of a row from _starting, or fails it on a Failed input.
        void addWant(RecipeId _recipe, NodeRow& _row, Transience _transience, RecipeList& _starting);
        void startRow(RecipeId _recipe, Transience _transience, RecipeList& _starting,
                      RecipeList& _unwants, RecipeList& _finished);

        // Shared by the four parts.
        // Takes one from the Wanted of each row on the list, and cancels each Waiting row that reaches 0. Iterative:
        // a cancel adds its inputs to the list. The list is empty when it returns.
        void drainUnwants(RecipeList& _unwants);
        // The row no longer needs its inputs: it leaves the Dependents of each input that has not finished, and each
        // input goes on _unwants. Call it once for each row that leaves Waiting, Ready or Running.
        void releaseInputs(RecipeId _recipe, RecipeList& _unwants);
        // The row is Failed already, with its Error set. Every row that waits on it, through Dependents, fails with
        // InputFailed and the same Origin and Failure, and releases its inputs. Each failed row, this one included,
        // goes on _finished.
        void failDependents(RecipeId _failed, RecipeList& _unwants, RecipeList& _finished);

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
