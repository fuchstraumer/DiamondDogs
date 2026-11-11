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
    bool OutputsMatch(std::span<const cc::Output> declared, std::span<const cc::Artifact> made);
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
    // - Count per input edge, not per source row: a row that reads two outputs of one input adds itself to that
    //   input's Dependents twice, and its Wanted and Unresolved by 2. complete and releaseInputs count the same way.
    // - Finish all the inputs of one row before you pop the next row. Dependents may only hold rows that are set up.
    // - An input that is already Failed (a memo from an earlier run) never finishes again. Check the inputs for one
    //   before the row wants anything. If one is Failed, the row wants none of its inputs: set it Failed with
    //   { InputFailed, that input's Origin, that input's Failure }, then call failDependents, because the row that
    //   wanted it may already wait on it.
    // - Collect the unwants and the finished rows in two local lists, and use them only after the walk: drainUnwants,
    //   then notifyRoots for each finished row. An unwant during the walk could cancel a row that is still on the
    //   walk's stack, before that row has wanted its own inputs.
    // - A Waiting, Ready, Running, Succeeded or Failed row only gains the count.
    void Executor::want(RecipeId /*_recipe*/, Transience /*_transience*/)
    {
        assert(false && "want is not written yet");
    }

    void Executor::unwant(RecipeId _recipe)
    {
        std::vector<RecipeId> unwants{ _recipe };
        drainUnwants(unwants);
    }

    void Executor::cancel(RecipeId _recipe, std::vector<RecipeId>& _unwants)
    {
        NodeRow& row = rowOf(_recipe);
        const bool cancellable = row.State == NodeState::Waiting || row.State == NodeState::Ready;
        assert(cancellable && row.Wanted == 0 && "only a Waiting or Ready row that nobody wants is cancelled");
        assert(row.Dependents.empty() && "each dependent counts in Wanted, so a row with Wanted 0 has none");

        row.State = NodeState::Idle;
        row.Unresolved = 0;
        row.Strongest = Transience::Invalid;

        releaseInputs(_recipe, _unwants);
    }

    void Executor::complete(RecipeId _recipe, KindResult _result, Context& _context)
    {
        const Recipe& recipe = recipes->Get(_recipe);
        const KindOps& kind = registry->Ops(recipe.Kind);
        ContentErrorCode code = ContentErrorCode::Invalid;

        // a status that is not Succeeded, Invalid included, is a failure: bad content degrades and never crashes
        if (!_result.Succeeded())
        {
            code = ContentErrorCode::KindFailed;
        }
        else if (!OutputsMatch(kind.Outputs, _context.Outputs()))
        {
            code = ContentErrorCode::MissingOutput;
        }

        std::vector<RecipeId> unwants;
        std::vector<RecipeId> finished;
        NodeRow& row = rowOf(_recipe);
        assert(row.State == NodeState::Running && "only a Running row completes");

        if (code == ContentErrorCode::Invalid)
        {
            for (Artifact& output : _context.Outputs())
            {
                // the table's first holder is the row; the destructor releases it
                row.Outputs.push_back(artifacts.Add(std::move(output)));
            }

            row.State = NodeState::Succeeded;
            const std::vector<RecipeId> dependents = std::move(row.Dependents);
            row.Dependents.clear();

            releaseInputs(_recipe, unwants);
            finished.push_back(_recipe);

            for (const RecipeId dependent : dependents)
            {
                NodeRow& dependentRow = rowOf(dependent);
                assert(dependentRow.State == NodeState::Waiting && dependentRow.Unresolved > 0);

                --dependentRow.Unresolved;

                // a Waiting row is still wanted: cancel takes it out of every Dependents list
                if (dependentRow.Unresolved == 0)
                {
                    spawnRun(dependent);
                }
            }
        }
        else
        {
            const std::span<const Diagnostic> diagnostics = _context.Diagnostics();
            const FailureId failure{ static_cast<uint32_t>(failures.size()) };
            failures.emplace_back(diagnostics.begin(), diagnostics.end());

            row.State = NodeState::Failed;
            row.Error = ContentError{ .Code = code, .Origin = _recipe, .Failure = failure };

            releaseInputs(_recipe, unwants);
            failDependents(_recipe, unwants, finished);
        }

        drainUnwants(unwants);

        for (const RecipeId done : finished)
        {
            notifyRoots(done);
        }
    }

    void Executor::drainUnwants(std::vector<RecipeId>& _unwants)
    {
        while (!_unwants.empty())
        {
            const RecipeId current = _unwants.back();
            _unwants.pop_back();

            NodeRow& row = rowOf(current);
            assert(row.Wanted > 0 && "an unwant needs a matching want");

            --row.Wanted;

            // a Ready row is cancelled by its run when it starts; Running, Succeeded and Failed rows keep their state
            if (row.Wanted == 0 && row.State == NodeState::Waiting)
            {
                cancel(current, _unwants);
            }
        }
    }

    void Executor::releaseInputs(RecipeId _recipe, std::vector<RecipeId>& _unwants)
    {
        const Recipe& recipe = recipes->Get(_recipe);

        for (const RecipeInputRef& input : recipe.Inputs)
        {
            NodeRow& source = rowOf(input.Source);
            const bool unfinished = source.State == NodeState::Waiting || source.State == NodeState::Ready ||
                                    source.State == NodeState::Running;

            // one entry for each edge, so a source that feeds two slots loses one entry here for each
            if (unfinished)
            {
                const auto found = std::ranges::find(source.Dependents, _recipe);
                assert(found != source.Dependents.end() && "an unfinished input lists each row that waits on it");

                source.Dependents.erase(found);
            }

            _unwants.push_back(input.Source);
        }
    }

    void Executor::failDependents(RecipeId _failed, std::vector<RecipeId>& _unwants, std::vector<RecipeId>& _finished)
    {
        std::vector<RecipeId> pending{ _failed };

        while (!pending.empty())
        {
            const RecipeId current = pending.back();
            pending.pop_back();
            _finished.push_back(current);

            NodeRow& row = rowOf(current);
            assert(row.State == NodeState::Failed && "failDependents starts from a Failed row");

            const ContentError inherited
            {
                .Code = ContentErrorCode::InputFailed,
                .Origin = row.Error.Origin,
                .Failure = row.Error.Failure,
            };
            const std::vector<RecipeId> dependents = std::move(row.Dependents);
            row.Dependents.clear();

            for (const RecipeId dependent : dependents)
            {
                NodeRow& dependentRow = rowOf(dependent);

                // a row that reads two outputs of this row is listed twice, and fails on the first entry
                if (dependentRow.State == NodeState::Failed)
                {
                    continue;
                }

                assert(dependentRow.State == NodeState::Waiting && "only a Waiting row waits on an unfinished input");

                dependentRow.State = NodeState::Failed;
                dependentRow.Error = inherited;
                dependentRow.Unresolved = 0;

                releaseInputs(dependent, _unwants);
                pending.push_back(dependent);
            }
        }
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
            std::vector<RecipeId> unwants;
            cancel(_recipe, unwants);
            drainUnwants(unwants);

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

    // An output that Run did not set keeps the kind Invalid, so it fails the comparison too.
    bool OutputsMatch(std::span<const cc::Output> declared, std::span<const cc::Artifact> made)
    {
        assert(declared.size() == made.size() && "the context has one artifact for each output of the kind");

        for (size_t index = 0; index < declared.size(); ++index)
        {
            if (made[index].Kind != declared[index].Produces)
            {
                return false;
            }
        }

        return true;
    }
}
