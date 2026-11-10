#include "Executor.hpp"
#include "ArtifactTable.hpp"
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "ExecutorTypes.hpp"
#include "KindRegistry.hpp"
#include "NodeRow.hpp"
#include "RecipeTable.hpp"
#include "async/Scheduler.hpp"
#include "async/Task.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    int32_t SlotIndexOf(std::span<const cc::Slot> slots, std::string_view name);
}

namespace cc
{
    Executor::Executor(foundation::Scheduler& _scheduler, const KindRegistry& _registry, const RecipeTable& _recipes)
        : scheduler{ &_scheduler }, registry{ &_registry }, recipes{ &_recipes }, failures(1)
    {
        assert(_registry.IsFrozen() && "Freeze the registry before the executor uses it");
    }

    Executor::~Executor()
    {
        assert(Idle() && "call Shutdown and tick the scheduler until Idle before the executor is destroyed");
        assert(roots.empty() && "destroy every Root before its executor");

        for (NodeRow& row : rows)
        {
            for (const ArtifactRef output : row.Outputs)
            {
                artifacts.Release(output);
            }
        }
    }

    void Executor::Shutdown()
    {
        // Clear unregisters nothing, but a copy keeps the loop independent of what the roots do
        const std::vector<Root*> current = roots;

        for (Root* root : current)
        {
            root->Clear();
        }
    }

    bool Executor::Idle() const noexcept
    {
        return readyCount == 0 && runningCount == 0;
    }

    const ArtifactTable& Executor::Artifacts() const noexcept
    {
        return artifacts;
    }

    std::span<const Diagnostic> Executor::FailureDiagnostics(FailureId _failure) const
    {
        assert(_failure.Value >= 1 && _failure.Value < failures.size() && "the failure was not made by this executor");

        return failures[_failure.Value];
    }

    void Executor::addRoot(Root& _root)
    {
        roots.push_back(&_root);
    }

    void Executor::removeRoot(Root& _root)
    {
        const auto removed = std::ranges::remove(roots, &_root);
        assert(!removed.empty() && "the root was not registered");

        roots.erase(removed.begin(), removed.end());
    }

    // QUESTION (want). Contract:
    // - Iterative, with an explicit stack: a chain of 10,000 rows must not overflow the stack.
    // - Every row it reaches keeps the strongest transience of its requesters (E6).
    // - Wanted 0 to 1 on an Idle row: the row becomes Waiting. For each input: add this row to the input's Dependents
    //   if the input has not succeeded, add 1 to the input's Wanted (which may start the input in turn), and count
    //   the unfinished inputs into Unresolved. With Unresolved 0 the row is spawned (spawnRun).
    // - An input that is already Failed (a memo from an earlier run) never finishes again, so it must not be waited
    //   on: the row fails at once with InputFailed and that input's Origin. A row that fails this way during the walk
    //   may already be in the Dependents of the row that wanted it, so the failure cascades as in complete. Share
    //   that cascade with complete; do not write it twice.
    // - A Waiting, Ready, Running, Succeeded or Failed row only gains the count.
    void Executor::want(RecipeId /*_recipe*/, Transience /*_transience*/)
    {
        assert(false && "want is not written yet");
    }

    // QUESTION (unwant). Contract:
    // - Takes 1 from the row's Wanted. At 0: a Waiting row is cancelled (cancel). A Ready row is left for its run to
    //   cancel when it starts. A Running row finishes. A Succeeded row stays cached.
    // - The cancel cascades down through cancel and unwant, so it must be iterative too.
    void Executor::unwant(RecipeId /*_recipe*/)
    {
        assert(false && "unwant is not written yet");
    }

    // QUESTION (cancel). Contract:
    // - Only for a Waiting row, or a Ready row whose run is starting with Wanted 0. The row becomes Idle.
    // - Remove the row from the Dependents of each input that has not finished, then unwant each input.
    void Executor::cancel(RecipeId /*_recipe*/)
    {
        assert(false && "cancel is not written yet");
    }

    // QUESTION (complete). Contract:
    // - Succeeded needs every output set, each with the kind that the output declares; otherwise the row fails with
    //   MissingOutput. The row adds each output to the artifact table and holds it.
    // - Failed: store the context's diagnostics as a new FailureId; Error = { KindFailed, this recipe, that id }.
    // - Each dependent: on success, Unresolved minus 1, and spawnRun at 0 if it is still wanted. On failure, the
    //   dependent fails at once with InputFailed and the same Origin, without running, and that cascades.
    // - Then unwant each input of this row: a finished row no longer needs them.
    // - Then notifyRoots for every row that finished here.
    void Executor::complete(RecipeId /*_recipe*/, KindResult /*_result*/, Context& /*_context*/)
    {
        assert(false && "complete is not written yet");
    }

    NodeRow& Executor::rowOf(RecipeId _recipe)
    {
        assert(_recipe.Value >= 1 && _recipe.Value <= recipes->Count() && "the recipe is not in the table");

        // the recipe table only grows, so the rows follow it
        if (_recipe.Value >= rows.size())
        {
            rows.resize(recipes->Count() + 1);
        }

        return rows[_recipe.Value];
    }

    void Executor::spawnRun(RecipeId _recipe)
    {
        NodeRow& row = rowOf(_recipe);
        assert(row.State == NodeState::Waiting && row.Unresolved == 0 && "only a Waiting row with no input left is spawned");

        row.State = NodeState::Ready;
        ++readyCount;
        scheduler->Spawn(runNode(_recipe));
    }

    foundation::Task<void> Executor::runNode(RecipeId _recipe)
    {
        --readyCount;
        NodeRow& readyRow = rowOf(_recipe);

        // cooperative cancellation: the last check before any work
        if (readyRow.Wanted == 0)
        {
            cancel(_recipe);
            co_return;
        }

        readyRow.State = NodeState::Running;
        ++runningCount;

        const Recipe& recipe = recipes->Get(_recipe);
        const KindOps& kind = registry->Ops(recipe.Kind);

        // each slot's artifact, through the output index of its edge; an optional slot with no edge stays invalid
        std::vector<ArtifactRef> inputRefs(kind.Slots.size());

        for (const RecipeInputRef& input : recipe.Inputs)
        {
            const int32_t slot = SlotIndexOf(kind.Slots, input.Slot);
            const NodeRow& source = rowOf(input.Source);
            assert(source.State == NodeState::Succeeded && "a node runs only after every input succeeded");

            inputRefs[slot] = source.Outputs[input.Output];
        }

        Context context{ recipe.Level, static_cast<int32_t>(kind.Outputs.size()) };
        const Inputs inputs{ artifacts, inputRefs };
        const KindResult result = co_await kind.Run(recipe.Params.Data(), inputs, context);

        // the inputs stay alive until here: this row still counts in their Wanted. complete looks the row up again,
        // because rows can grow while Run is suspended
        --runningCount;
        complete(_recipe, result, context);
    }

    void Executor::notifyRoots(RecipeId _recipe)
    {
        const NodeRow& row = rowOf(_recipe);

        for (Root* root : roots)
        {
            if (root->target == _recipe)
            {
                root->onFinished(row);
            }
        }
    }

    Root::Root(Executor& _executor) : executor{ &_executor }
    {
        executor->addRoot(*this);
    }

    Root::~Root()
    {
        Clear();
        releaseShown();
        executor->removeRoot(*this);
    }

    void Root::Retarget(RecipeId _recipe, Transience _transience)
    {
        const RecipeId previous = target;

        // want first: rows that the old and the new target share keep a count all through
        executor->want(_recipe, _transience);
        target = _recipe;
        error = ContentError{};

        if (previous.Value != 0)
        {
            executor->unwant(previous);
        }

        const NodeRow& row = executor->rowOf(_recipe);
        const bool finished = row.State == NodeState::Succeeded || row.State == NodeState::Failed;

        // a cached row does not finish again, so the root reads it now
        if (finished)
        {
            onFinished(row);
        }
    }

    void Root::Clear()
    {
        if (target.Value == 0)
        {
            return;
        }

        executor->unwant(target);
        target = RecipeId{};
    }

    RootView Root::View() const noexcept
    {
        return RootView
        {
            .Target = target,
            .Shown = shown,
            .Outputs = shownOutputs,
            .Stale = shown != target,
            .Error = error,
        };
    }

    void Root::onFinished(const NodeRow& _row)
    {
        if (_row.State == NodeState::Failed)
        {
            error = _row.Error;
            return;
        }

        assert(_row.State == NodeState::Succeeded);

        // hold the new outputs before the old ones go, in case they are the same artifacts
        for (const ArtifactRef output : _row.Outputs)
        {
            executor->artifacts.Hold(output);
        }

        releaseShown();
        shown = target;
        shownOutputs = _row.Outputs;
        error = ContentError{};
    }

    void Root::releaseShown()
    {
        for (const ArtifactRef output : shownOutputs)
        {
            executor->artifacts.Release(output);
        }

        shownOutputs.clear();
        shown = RecipeId{};
    }
}

namespace
{
    int32_t SlotIndexOf(std::span<const cc::Slot> slots, std::string_view name)
    {
        const auto found = std::ranges::find(slots, name, &cc::Slot::Name);
        assert(found != slots.end() && "ingestion checked every slot name");

        return static_cast<int32_t>(found - slots.begin());
    }
}
