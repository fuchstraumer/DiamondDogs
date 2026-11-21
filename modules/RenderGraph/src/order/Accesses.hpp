#pragma once
#ifndef DIAMOND_DOGS_LOOM_ORDER_ACCESSES_HPP
#define DIAMOND_DOGS_LOOM_ORDER_ACCESSES_HPP
#include "order/OrderContext.hpp"
#include "PassStatements.hpp"
#include <vector>

// Accesses.cpp: each pass's accesses, by slot, from its binds, its draw targets and its Record uses; then the same
// accesses collated by slot.
namespace loom
{
    struct FrozenColorTarget;
    struct FrozenDepthTarget;
    struct FrozenPass;

    namespace order
    {
        void AppendBindingAccesses(const std::vector<ResourceAccess>& inputs,
                                   std::vector<PassAccess>& results,
                                   const FrozenPass& pass);
        void AppendRecordAccesses(OrderContext& context,
                                  std::vector<PassAccess>& results,
                                  const FrozenPass& pass);
        ResourceAccess ColorTargetAccess(const FrozenColorTarget& color);
        ResourceAccess DepthTargetAccess(const FrozenDepthTarget& depth);
        void AppendDrawAccesses(const OrderContext& context,
                                std::vector<PassAccess>& results,
                                const FrozenPass& pass);
        void AppendDispatchAccesses(std::vector<PassAccess>& results, const FrozenPass& pass);
        void GatherAccesses(OrderContext& context);
        CollatedAccesses CollateAccessesBySlot(OrderContext& context);
    }

}

#endif // !DIAMOND_DOGS_LOOM_ORDER_ACCESSES_HPP
