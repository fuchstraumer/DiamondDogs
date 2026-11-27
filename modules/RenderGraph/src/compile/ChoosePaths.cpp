#include "compile/ChoosePaths.hpp"
#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "DeviceCapability.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassStatements.hpp"
#include "Records.hpp"
#include "compile/CompiledData.hpp"
#include "compile/PathChoice.hpp"
#include "freeze/FrozenGraph.hpp"
#include "order/Accesses.hpp"
#include "order/OrderedGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <ranges>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace loom
{

namespace
{

    using bootstrap::DeviceCapability;

    // One list while ChoosePaths works: the path it holds now, and the paths it passed over.
    struct ListState
    {
        const PathListRecord* List{ nullptr };
        int32_t Current{ -1 };      // -1: no path is left
        bool Forced{ false };
        std::vector<RejectedPath> Rejected;
    };

    struct MissingCapability
    {
        DeviceCapability Capability{ DeviceCapability::Invalid };
        KeyRef Pass;
    };

    bool Contains(std::span<const KeyRef> keys, KeyRef key)
    {
        return std::ranges::find(keys, key) != keys.end();
    }

    MissingCapability FirstMissingCapability(const PathRecord& path, std::span<const DeviceCapability> enabled)
    {
        std::vector<DeviceCapability> needed;

        for (const PassRecord& pass : path.Body.Passes)
        {
            needed.clear();
            AppendStatementCapabilities(pass, needed);

            for (const DeviceCapability capability : needed)
            {
                if (!bootstrap::HasCapability(enabled, capability))
                {
                    return MissingCapability{ .Capability = capability, .Pass = pass.Key };
                }

            }

        }

        return MissingCapability{};
    }

    // Moves the list to the first path from `start` whose statements the device satisfies, and records each path that
    // it passes over.
    void AdvanceFrom(ListState& state, int32_t start, std::span<const DeviceCapability> enabled)
    {
        const std::vector<PathRecord>& paths = state.List->Paths;

        for (int32_t index = start; index < std::ssize(paths); ++index)
        {
            const MissingCapability missing = FirstMissingCapability(paths[index], enabled);

            if (missing.Capability == DeviceCapability::Invalid)
            {
                state.Current = index;
                return;
            }

            state.Rejected.push_back(RejectedPath
            {
                .Path = paths[index].Key,
                .Where = paths[index].Where,
                .Reason = PathRejection::Capability,
                .Capability = missing.Capability,
                .Pass = missing.Pass,
            });
        }

        state.Current = -1;
    }

    std::vector<KeyRef> ExposedAnchorsOf(const PathRecord& path)
    {
        std::vector<KeyRef> anchors;

        for (const ExposedAnchorRecord& exposed : path.Body.ExposedAnchors)
        {
            anchors.push_back(exposed.Anchor);
        }

        return anchors;
    }

    void ReportAnchorDifference(DiagnosticSink& sink, const PathRecord& path, KeyRef anchor, bool missing)
    {
        const DiagnosticValue named
        {
            .FieldName = "ExposeAnchor",
            .Kind = DiagnosticValueKind::Text,
            .Text = anchor.Name,
        };

        Diagnostic diagnostic
        {
            .Code = DiagnosticCode::LM116,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM116),
            .Where = path.Where,
        };

        // A missing anchor is one the first path exposes and this path does not; an extra one is the reverse
        if (missing)
        {
            diagnostic.Expected = named;
        }
        else
        {
            diagnostic.Actual = named;
        }

        sink.Report(diagnostic);
    }

    // LM114 (a list with no paths) and LM116 (paths that expose different anchors), on every path, chosen or not.
    bool CheckListShapes(const GraphRecord& record, DiagnosticSink& sink)
    {
        bool valid = true;

        for (const PathListRecord& list : record.PathLists)
        {
            if (list.Paths.empty())
            {
                sink.Report(Diagnostic
                {
                    .Code = DiagnosticCode::LM114,
                    .Severity = DefaultSeverityOf(DiagnosticCode::LM114),
                    .Where = list.Where,
                });

                valid = false;
                continue;
            }

            const std::vector<KeyRef> first = ExposedAnchorsOf(list.Paths.front());

            for (const PathRecord& path : list.Paths | std::views::drop(1))
            {
                const std::vector<KeyRef> anchors = ExposedAnchorsOf(path);

                for (const KeyRef anchor : first)
                {
                    if (!Contains(anchors, anchor))
                    {
                        ReportAnchorDifference(sink, path, anchor, true);
                        valid = false;
                    }

                }

                for (const KeyRef anchor : anchors)
                {
                    if (!Contains(first, anchor))
                    {
                        ReportAnchorDifference(sink, path, anchor, false);
                        valid = false;
                    }

                }

            }

        }

        return valid;
    }

    void ReportUnknownForced(DiagnosticSink& sink, std::source_location where, std::string_view name)
    {
        sink.Report(Diagnostic
        {
            .Code = DiagnosticCode::LM103,
            .Severity = DefaultSeverityOf(DiagnosticCode::LM103),
            .Where = where,
            .Actual = DiagnosticValue
            {
                .FieldName = "ForcedPaths",
                .Kind = DiagnosticValueKind::Text,
                .Text = name,
            },
        });
    }

    // Returns false when a forced path names no list or no path (LM103).
    bool ApplyForcedPaths(std::vector<ListState>& states, std::span<const ForcedPath> forced_paths, DiagnosticSink& sink)
    {
        bool valid = true;

        for (const ForcedPath& forced : forced_paths)
        {
            const KeyRef listKey = forced.List.ToRef();

            const auto isList = [listKey](const ListState& state)
            {
                return state.List->Key == listKey;
            };

            const auto state = std::ranges::find_if(states, isList);

            if (state == states.end())
            {
                ReportUnknownForced(sink, std::source_location{}, forced.List.Name);
                valid = false;
                continue;
            }

            const KeyRef pathKey = forced.Path.ToRef();

            const auto isPath = [pathKey](const PathRecord& path)
            {
                return path.Key == pathKey;
            };

            const auto path = std::ranges::find_if(state->List->Paths, isPath);

            if (path == state->List->Paths.end())
            {
                ReportUnknownForced(sink, state->List->Where, forced.Path.Name);
                valid = false;
                continue;
            }

            state->Current = static_cast<int32_t>(std::distance(state->List->Paths.begin(), path));
            state->Forced = true;
        }

        return valid;
    }

    template<typename Declaration>
    void AppendDeclarations(std::vector<DeclarationRecord<Declaration>>& to,
                            const std::vector<DeclarationRecord<Declaration>>& from)
    {
        to.insert(to.end(), from.begin(), from.end());
    }

    // Every state must hold a path
    GraphRecord BuildChosenRecord(const GraphRecord& record, std::span<const ListState> states)
    {
        GraphRecord chosen = record;
        chosen.PathLists.clear();

        for (const ListState& state : states)
        {
            const PathListRecord& list = *state.List;
            const PathRecord& path = list.Paths[state.Current];

            // The body keeps the path's location, so LM111 on an empty path points at the path
            SequenceRecord body = path.Body;
            body.Key = list.Key;
            body.After.insert(body.After.begin(), list.After.begin(), list.After.end());
            body.Before.insert(body.Before.begin(), list.Before.begin(), list.Before.end());
            chosen.Sequences.push_back(std::move(body));

            AppendDeclarations(chosen.Buffers, path.Buffers);
            AppendDeclarations(chosen.Images, path.Images);
            AppendDeclarations(chosen.Samplers, path.Samplers);
            AppendDeclarations(chosen.Imports, path.Imports);
            AppendDeclarations(chosen.BufferImports, path.BufferImports);
        }

        return chosen;
    }

    bool PathHasPass(const PathRecord& path, KeyRef pass)
    {
        const auto isPass = [pass](const PassRecord& record)
        {
            return record.Key == pass;
        };

        return std::ranges::any_of(path.Body.Passes, isPass);
    }

    bool AllHoldPaths(std::span<const ListState> states)
    {
        const auto holdsPath = [](const ListState& state)
        {
            return state.Current >= 0;
        };

        return std::ranges::all_of(states, holdsPath);
    }

    // Runs Freeze and Resolve on the current choice into a scratch sink. A list whose path has a pass with LM207 moves
    // to its next path; each round moves at least one list or ends, so the loop ends. Stops when a list runs out of
    // paths (the caller reports LM115), or when Freeze fails (the real Freeze reports that).
    void RejectByShaders(const GraphRecord& record,
                         std::vector<ListState>& states,
                         std::span<const DeviceCapability> enabled,
                         const ResolveInputs& resolve_inputs)
    {
        std::vector<bool> movedThisRound(states.size());

        while (AllHoldPaths(states))
        {
            const GraphRecord trial = BuildChosenRecord(record, states);
            CollectingSink scratch;
            const std::optional<FrozenGraph> frozen = Freeze(trial, scratch);

            if (!frozen.has_value())
            {
                return;
            }

            static_cast<void>(Resolve(*frozen, resolve_inputs, scratch));
            std::ranges::fill(movedThisRound, false);
            bool moved = false;

            for (const Diagnostic& diagnostic : scratch.Diagnostics())
            {
                if (diagnostic.Code != DiagnosticCode::LM207)
                {
                    continue;
                }

                // Two paths may share a pass key, so a list that moved this round ignores the rest of its old path
                for (size_t index = 0; index < states.size(); ++index)
                {
                    ListState& state = states[index];

                    if (state.Forced || movedThisRound[index] || state.Current < 0)
                    {
                        continue;
                    }

                    const PathRecord& path = state.List->Paths[state.Current];

                    if (!PathHasPass(path, diagnostic.Pass))
                    {
                        continue;
                    }

                    state.Rejected.push_back(RejectedPath
                    {
                        .Path = path.Key,
                        .Where = path.Where,
                        .Reason = PathRejection::Shader,
                        .Pass = diagnostic.Pass,
                        .ShaderCapability = std::string{ diagnostic.Binding },
                    });

                    AdvanceFrom(state, state.Current + 1, enabled);
                    movedThisRound[index] = true;
                    moved = true;
                }

            }

            if (!moved)
            {
                return;
            }

        }

    }

    std::string_view MissingName(const RejectedPath& rejected)
    {
        if (rejected.Reason == PathRejection::Shader)
        {
            return rejected.ShaderCapability;
        }

        return bootstrap::ToString(rejected.Capability);
    }

    // LM115 once for each path of a list that has no path left. Returns false when it reported one.
    bool ReportUnsatisfied(std::span<const ListState> states, DiagnosticSink& sink)
    {
        bool valid = true;

        for (const ListState& state : states)
        {
            if (state.Current >= 0)
            {
                continue;
            }

            for (const RejectedPath& rejected : state.Rejected)
            {
                sink.Report(Diagnostic
                {
                    .Code = DiagnosticCode::LM115,
                    .Severity = DefaultSeverityOf(DiagnosticCode::LM115),
                    .Where = rejected.Where,
                    .OtherWhere = state.List->Where,
                    .Pass = rejected.Pass,
                    .Expected = DiagnosticValue
                    {
                        .FieldName = "Capability",
                        .Kind = DiagnosticValueKind::Text,
                        .Text = MissingName(rejected),
                    },
                });
            }

            valid = false;
        }

        return valid;
    }

    template<typename Declaration>
    void AppendKeys(std::vector<KeyRef>& keys, const std::vector<DeclarationRecord<Declaration>>& declarations)
    {
        for (const DeclarationRecord<Declaration>& declaration : declarations)
        {
            keys.push_back(declaration.Value.Key.ToRef());
        }

    }

    PathChoice MakeChoice(ListState& state)
    {
        const PathListRecord& list = *state.List;
        const PathRecord& path = list.Paths[state.Current];

        PathChoice choice
        {
            .List = list.Key,
            .Where = list.Where,
            .Path = path.Key,
            .Forced = state.Forced,
            .Produces = list.Produces,
            .Rejected = std::move(state.Rejected),
        };

        for (const PassRecord& pass : path.Body.Passes)
        {
            choice.Passes.push_back(pass.Key);
        }

        AppendKeys(choice.LocalResources, path.Buffers);
        AppendKeys(choice.LocalResources, path.Images);
        AppendKeys(choice.LocalResources, path.Samplers);
        AppendKeys(choice.LocalResources, path.Imports);
        AppendKeys(choice.LocalResources, path.BufferImports);
        return choice;
    }

    // A write of one pass, by statement: the resource row and the statement's location
    struct PathWrite
    {
        ResourceIndex Resource;
        std::source_location Where;
    };

    bool IsWrite(ResourceAccess access)
    {
        return access == ResourceAccess::Write || access == ResourceAccess::ReadWrite;
    }

    void AppendWrites(const FrozenPass& pass, std::span<const ResourceAccess> bind_accesses, std::vector<PathWrite>& writes)
    {
        for (size_t bind = 0; bind < pass.Binds.size(); ++bind)
        {
            if (IsWrite(bind_accesses[bind]))
            {
                writes.push_back(PathWrite{ .Resource = pass.Binds[bind].Resource, .Where = pass.Binds[bind].Where });
            }

        }

        // A frame target is no resource row: the list contract covers graph resources only
        for (const FrozenColorTarget& color : pass.Colors)
        {
            if (color.Kind == TargetKind::Image)
            {
                writes.push_back(PathWrite{ .Resource = color.Image, .Where = color.Where });
            }

        }

        // Order's rule: a Clear writes the target even with depth writes off
        if (pass.Depth.Image.IsValid() && IsWrite(order::DepthTargetAccess(pass.Depth)))
        {
            writes.push_back(PathWrite{ .Resource = pass.Depth.Image, .Where = pass.Depth.Where });
        }

        for (const FrozenUse& use : pass.Uses)
        {
            if (use.Resource.IsValid() && IsWrite(use.Access))
            {
                writes.push_back(PathWrite{ .Resource = use.Resource, .Where = use.Where });
            }

        }

    }

    bool IsProduced(const PathChoice& choice, KeyRef resource)
    {
        const auto producesResource = [resource](const ProducesRecord& produces)
        {
            return produces.Resource == resource;
        };

        return std::ranges::any_of(choice.Produces, producesResource);
    }

    std::string ExplainRejection(const RejectedPath& rejected)
    {
        if (rejected.Reason == PathRejection::Shader)
        {
            return std::format("  passed over {}: pass '{}' has no shader variant that the device can run ('{}')\n",
                               rejected.Path.Name, rejected.Pass.Name, rejected.ShaderCapability);
        }

        return std::format("  passed over {}: pass '{}' needs {}\n",
                           rejected.Path.Name, rejected.Pass.Name, bootstrap::ToString(rejected.Capability));
    }

}

    void AppendStatementCapabilities(const PassRecord& pass, std::vector<DeviceCapability>& capabilities)
    {
        for (const DrawRecord& draw : pass.Draws)
        {
            if (draw.Form == DrawForm::DrawIndirectCount)
            {
                capabilities.push_back(DeviceCapability::DrawIndirectCount);
            }

        }

    }

    std::optional<ChosenGraph> ChoosePaths(const GraphRecord& record,
                                           std::span<const DeviceCapability> capabilities,
                                           std::span<const ForcedPath> forced_paths,
                                           const ResolveInputs& resolve_inputs,
                                           DiagnosticSink& sink)
    {
        if (!CheckListShapes(record, sink))
        {
            return std::nullopt;
        }

        std::vector<ListState> states;

        for (const PathListRecord& list : record.PathLists)
        {
            states.push_back(ListState{ .List = &list });
        }

        if (!ApplyForcedPaths(states, forced_paths, sink))
        {
            return std::nullopt;
        }

        for (ListState& state : states)
        {
            if (!state.Forced)
            {
                AdvanceFrom(state, 0, capabilities);
            }

        }

        RejectByShaders(record, states, capabilities, resolve_inputs);

        if (!ReportUnsatisfied(states, sink))
        {
            return std::nullopt;
        }

        ChosenGraph chosen{ .Record = BuildChosenRecord(record, states) };

        for (ListState& state : states)
        {
            chosen.Choices.push_back(MakeChoice(state));
        }

        return chosen;
    }

    bool CheckPathOutputs(const FrozenGraph& frozen,
                          const BindAccessTable& binds,
                          std::span<const PathChoice> choices,
                          DiagnosticSink& sink)
    {
        bool valid = true;
        std::vector<PathWrite> writes;
        std::vector<KeyRef> written;

        for (const PathChoice& choice : choices)
        {
            written.clear();

            for (size_t passIndex = 0; passIndex < frozen.Passes.size(); ++passIndex)
            {
                const FrozenPass& pass = frozen.Passes[passIndex];

                if (!Contains(choice.Passes, pass.Key))
                {
                    continue;
                }

                writes.clear();
                AppendWrites(pass, binds.Accesses[passIndex], writes);

                for (const PathWrite& write : writes)
                {
                    const KeyRef resource = frozen.Resources[write.Resource.Value].Key;
                    written.push_back(resource);

                    if (Contains(choice.LocalResources, resource) || IsProduced(choice, resource))
                    {
                        continue;
                    }

                    sink.Report(Diagnostic
                    {
                        .Code = DiagnosticCode::LM309,
                        .Severity = DefaultSeverityOf(DiagnosticCode::LM309),
                        .Where = write.Where,
                        .OtherWhere = choice.Where,
                        .Pass = pass.Key,
                        .Resource = resource,
                    });

                    valid = false;
                }

            }

            for (const ProducesRecord& produces : choice.Produces)
            {
                if (Contains(written, produces.Resource))
                {
                    continue;
                }

                sink.Report(Diagnostic
                {
                    .Code = DiagnosticCode::LM308,
                    .Severity = DefaultSeverityOf(DiagnosticCode::LM308),
                    .Where = produces.Where,
                    .Resource = produces.Resource,
                    .Actual = DiagnosticValue
                    {
                        .FieldName = "Path",
                        .Kind = DiagnosticValueKind::Text,
                        .Text = choice.Path.Name,
                    },
                });

                valid = false;
            }

        }

        return valid;
    }

    std::string ExplainPaths(const CompiledGraph& compiled)
    {
        std::string text;

        for (const PathChoice& choice : compiled.Data().PathChoices)
        {
            text += std::format("{}: chose {}{}\n", choice.List.Name, choice.Path.Name, choice.Forced ? " (forced)" : "");

            for (const RejectedPath& rejected : choice.Rejected)
            {
                text += ExplainRejection(rejected);
            }

        }

        return text;
    }

}
