#include "QueryPool.hpp"
#include "RhiHandle.hpp"
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace rhi
{
#ifdef RHI_SYSTEM_USE_VULKAN
    namespace
    {
        constexpr VkQueryType ToVkQueryType(QueryType type) noexcept
        {
            switch (type)
            {
            case QueryType::Occlusion:
                return VK_QUERY_TYPE_OCCLUSION;
            default:
                return VK_QUERY_TYPE_TIMESTAMP;
            }
        }
    }

    QueryPool::QueryPool(const DeviceHandle& _device, QueryType _type, uint32_t _count)
        : device{ _device },
          handle{ QueryPoolHandle{ 0 } },
          type{ _type },
          count{ _count }
    {
        const VkQueryPoolCreateInfo createInfo
        {
            VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            nullptr,
            0u,
            ToVkQueryType(type),
            count,
            0u
        };
        VkQueryPool vkPool = VK_NULL_HANDLE;

        if (vkCreateQueryPool(device.As<VkDevice>(), &createInfo, nullptr, &vkPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create a query pool!");
        }

        handle.Set<VkQueryPool>(vkPool);
        // a new query is undefined until it is reset
        Reset(0u, count);
    }

    QueryPool::QueryPool(QueryPool&& other) noexcept
        : device{ other.device }, handle{ std::move(other.handle) }, type{ other.type }, count{ other.count }
    {
        other.handle = QueryPoolHandle{ 0 };
        other.device = DeviceHandle{ 0 };
        other.count = 0u;
    }

    QueryPool& QueryPool::operator=(QueryPool&& other) noexcept
    {
        if (this != &other)
        {
            if (handle.IsValid() && device)
            {
                vkDestroyQueryPool(device.As<VkDevice>(), handle.As<VkQueryPool>(), nullptr);
            }

            device = other.device;
            handle = std::move(other.handle);
            type = other.type;
            count = other.count;
            other.handle = QueryPoolHandle{ 0 };
            other.device = DeviceHandle{ 0 };
            other.count = 0u;
        }

        return *this;
    }

    QueryPool::~QueryPool()
    {
        if (handle.IsValid() && device)
        {
            vkDestroyQueryPool(device.As<VkDevice>(), handle.As<VkQueryPool>(), nullptr);
            handle = QueryPoolHandle{ 0 };
        }
    }

    QueryPoolHandle QueryPool::Handle() const noexcept
    {
        return handle;
    }

    QueryType QueryPool::Type() const noexcept
    {
        return type;
    }

    uint32_t QueryPool::Count() const noexcept
    {
        return count;
    }

    void QueryPool::Reset(uint32_t first, uint32_t reset_count)
    {
        vkResetQueryPool(device.As<VkDevice>(), handle.As<VkQueryPool>(), first, reset_count);
    }

    bool QueryPool::TryGetResults(uint32_t first, uint32_t result_count, std::span<uint64_t> values) const
    {
        if (result_count == 0u || values.size() < result_count)
        {
            return false;
        }

        // each query's value, then its availability word. Without VK_QUERY_RESULT_WAIT_BIT this never blocks: an
        // unwritten query returns VK_NOT_READY and an availability of 0
        std::vector<uint64_t> words(size_t{ result_count } * 2u);
        constexpr VkQueryResultFlags k_Flags = VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
        const VkResult result = vkGetQueryPoolResults(device.As<VkDevice>(), handle.As<VkQueryPool>(), first, result_count,
            words.size() * sizeof(uint64_t), words.data(), 2u * sizeof(uint64_t), k_Flags);

        if (result != VK_SUCCESS && result != VK_NOT_READY)
        {
            throw std::runtime_error("Failed to read query pool results!");
        }

        for (uint32_t i = 0; i < result_count; ++i)
        {
            if (words[size_t{ i } * 2u + 1u] == 0u)
            {
                return false;
            }

            values[i] = words[size_t{ i } * 2u];
        }

        return true;
    }
#endif

}
