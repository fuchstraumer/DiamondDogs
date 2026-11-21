#pragma once
#ifndef DIAMOND_DOGS_LOOM_ORDER_DATA_CHECKS_HPP
#define DIAMOND_DOGS_LOOM_ORDER_DATA_CHECKS_HPP
#include "order/OrderContext.hpp"
#include <cstdint>
#include <source_location>
#include <vector>

// DataChecks.cpp: the hazards with no ordering path (LM502), and the reads before any write (LM503).
namespace loom::order
{

    struct Hazard
    {
        int32_t LowPass{ -1 };
        int32_t HighPass{ -1 };
        int32_t Slot{ -1 };
        std::source_location LowWhere;
        std::source_location HighWhere;
    };

    void RecordHazard(const OrderContext& context,
                      std::vector<Hazard>& hazards,
                      int32_t slot,
                      const CollatedAccesses::AccessRef& lhs,
                      const CollatedAccesses::AccessRef& rhs);
    void ReportHazards(const OrderContext& context, const std::vector<Hazard>& hazards);
    std::vector<uint8_t> CheckHazards(OrderContext& context,
                                      const CollatedAccesses& collated,
                                      const Reachability& reachability);
    void ReportReadBeforeWrite(const OrderContext& context,
                               const ResourceSlot slot,
                               const CollatedAccesses::AccessRef& first_reader,
                               const CollatedAccesses::AccessRef& first_writer);
    void CheckReadsBeforeWrites(OrderContext& context,
                                const CollatedAccesses& collated,
                                const std::vector<int32_t>& positions,
                                const std::vector<uint8_t>& hazard_slots);

    // True when resource at the given slot has a source (for it's contents) from outside the frame/graph
    bool HasSource(const OrderContext& context, ResourceSlot slot);
    // A writer whose run policy is not EveryFrame keeps its contents across frames, so they exist at frame start.
    bool HasPersistentWriter(const OrderContext& context, const CollatedAccesses& collated, ResourceSlot slot);

}

#endif // !DIAMOND_DOGS_LOOM_ORDER_DATA_CHECKS_HPP
