#pragma once
#ifndef DIAMOND_DOGS_LOOM_COMPILE_CHOOSE_PATHS_HPP
#define DIAMOND_DOGS_LOOM_COMPILE_CHOOSE_PATHS_HPP
#include "Compile.hpp"
#include "DeviceCapability.hpp"
#include "Records.hpp"
#include "compile/PathChoice.hpp"
#include <optional>
#include <span>
#include <vector>

// ChoosePaths: the first step of Compile when the graph has path lists. It replaces each list with one of its paths,
// so Freeze to Layout never see a list. CheckPathOutputs runs after Bind, when every write is known.
namespace loom
{

    class DiagnosticSink;
    struct BindAccessTable;
    struct FrozenGraph;
    struct ResolveInputs;

    // A copy of the builder's record with each list replaced by its chosen path: the path's body is a sequence under
    // the list's key, with the list's After and Before added, and the path's declarations join the graph's.
    struct ChosenGraph
    {
        GraphRecord Record;
        std::vector<PathChoice> Choices;
    };

    // The capabilities that a pass's statements need, appended to `capabilities`. A DrawIndirectCount draw needs
    // DrawIndirectCount. Shader needs are not here: Resolve decides those from the bundle.
    void AppendStatementCapabilities(const PassRecord& pass, std::vector<bootstrap::DeviceCapability>& capabilities);

    // A forced path wins. Otherwise each list takes its first path whose statements the device satisfies; then a trial
    // Freeze and Resolve into a scratch sink moves a list on while a pass in its path has no variant the device can run
    // (LM207). Reports LM114, LM115, LM116, and LM103 for a forced path that names no list or no path.
    [[nodiscard]] std::optional<ChosenGraph> ChoosePaths(const GraphRecord& record,
                                                         std::span<const bootstrap::DeviceCapability> capabilities,
                                                         std::span<const ForcedPath> forced_paths,
                                                         const ResolveInputs& resolve_inputs,
                                                         DiagnosticSink& sink);

    // LM308: a produced key that the chosen path does not write. LM309: the path writes a key from outside the list
    // that the list does not produce. Returns false when it reported either.
    [[nodiscard]] bool CheckPathOutputs(const FrozenGraph& frozen,
                                        const BindAccessTable& binds,
                                        std::span<const PathChoice> choices,
                                        DiagnosticSink& sink);

}

#endif // !DIAMOND_DOGS_LOOM_COMPILE_CHOOSE_PATHS_HPP
