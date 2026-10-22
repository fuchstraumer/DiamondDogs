#pragma once
#ifndef DIAMOND_DOGS_RHI_QUERY_POOL_HPP
#define DIAMOND_DOGS_RHI_QUERY_POOL_HPP
#include "RhiHandle.hpp"
#include <cstdint>
#include <span>

namespace rhi
{
    enum class QueryType : uint8_t
    {
        Timestamp,
        Occlusion,
    };

    /** @brief A pool of queries of one type (VkQueryPool). Every query starts reset, so it can be written at once.
     *  Reset and results are host operations here: a host reset (Vulkan 1.2 hostQueryReset) works for queries that
     *  any queue writes, including the transfer queue, which cannot record vkCmdResetQueryPool */
    class QueryPool
    {
    public:
        QueryPool(const DeviceHandle& device, QueryType type, uint32_t count);
        ~QueryPool();
        QueryPool(const QueryPool&) = delete;
        QueryPool& operator=(const QueryPool&) = delete;
        QueryPool(QueryPool&& other) noexcept;
        QueryPool& operator=(QueryPool&& other) noexcept;

        [[nodiscard]] QueryPoolHandle Handle() const noexcept;
        [[nodiscard]] QueryType Type() const noexcept;
        [[nodiscard]] uint32_t Count() const noexcept;

        /** @brief Resets queries [first, first + count) from the host. No submitted work may still use them */
        void Reset(uint32_t first, uint32_t count);
        /** @brief Reads queries [first, first + count) as 64-bit values into `values` (one per query). Never waits:
         *  returns false, and leaves `values` unspecified, when any query in the range is not available yet */
        [[nodiscard]] bool TryGetResults(uint32_t first, uint32_t count, std::span<uint64_t> values) const;

    private:
        DeviceHandle device;
        QueryPoolHandle handle;
        QueryType type{ QueryType::Timestamp };
        uint32_t count{ 0u };
    };

}

#endif // !DIAMOND_DOGS_RHI_QUERY_POOL_HPP
