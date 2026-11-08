#pragma once
#ifndef DIAMOND_DOGS_CC_NODE_ROW_HPP
#define DIAMOND_DOGS_CC_NODE_ROW_HPP
#include "ContentTypes.hpp"
#include "Executor.hpp"
#include "ExecutorTypes.hpp"
#include <cstdint>
#include <vector>

namespace cc
{
    enum class NodeState : uint8_t
    {
        Invalid = 0,
        // no request wants it, and it holds no result
        Idle,
        // wanted; some input has not succeeded yet
        Waiting,
        // wanted, every input succeeded, and its run is spawned but has not started
        Ready,
        Running,
        Succeeded,
        Failed,
    };

    struct NodeRow
    {
        NodeState State{ NodeState::Idle };
        // roots that target this row, plus dependents that are Waiting, Ready or Running
        int32_t Wanted{ 0 };
        // inputs that have not succeeded yet: the in-degree counter of Kahn's algorithm
        int32_t Unresolved{ 0 };
        // the rows to tell when this row finishes; empty once it has finished
        std::vector<RecipeId> Dependents;
        // E6: Committed wins over Transient
        Transience Strongest{ Transience::Invalid };
        // one for each output of the kind; Succeeded only. The row holds each one in the artifact table
        std::vector<ArtifactRef> Outputs;
        // Failed only
        ContentError Error;
    };
}

#endif // !DIAMOND_DOGS_CC_NODE_ROW_HPP
