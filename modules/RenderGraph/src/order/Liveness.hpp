#pragma once
#ifndef DIAMOND_DOGS_LOOM_ORDER_LIVENESS_HPP
#define DIAMOND_DOGS_LOOM_ORDER_LIVENESS_HPP
#include "order/OrderContext.hpp"
#include "containers/SmallVector.hpp"
#include <cstdint>
#include <vector>

// Liveness.cpp: dead writes (LM504) and orphan passes (LM505).
namespace loom::order
{

    // What LM504 learns about the writes, for LM505 to walk.
    struct Liveness
    {
        using FeederList = foundation::SmallVector<int32_t, 8>;
        foundation::SmallVector<FeederList, 4> Feeders; // [reader pass] -> the writer passes whose write it reads
        foundation::SmallVector<uint8_t, 64> Roots;                     // [pass] is 1 when a sink reads one of its writes
    };

    bool HasSink(const OrderContext& context, ResourceSlot slot);
    void ReportDeadWrite(const OrderContext& context,
                         ResourceSlot slot,
                         const CollatedAccesses::AccessRef& writer,
                         const CollatedAccesses::AccessRef& next_writer);
    Liveness CheckDeadWrites(OrderContext& context,
                             const CollatedAccesses& collated,
                             const std::vector<int32_t>& positions);
    void CheckOrphans(OrderContext& context, const Liveness& liveness);

}

#endif // !DIAMOND_DOGS_LOOM_ORDER_LIVENESS_HPP
