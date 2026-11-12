#pragma once
#ifndef DIAMOND_DOGS_RHI_FEATURE_CHAIN_HPP
#define DIAMOND_DOGS_RHI_FEATURE_CHAIN_HPP
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace rhi::generated
{
    struct MemberInfo;
    struct StructInfo;
}

namespace rhi
{
    /** A pNext chain of zeroed Vulkan structs, one heap block per struct, so that a move keeps every pointer valid.
     *  Members are set and read by their row in the generated feature table. The head is VkPhysicalDeviceFeatures2
     *  or VkPhysicalDeviceProperties2. */
    class FeatureChain
    {
    public:
        // `head_name` is the table name of the head: the 1.0 feature members name VkPhysicalDeviceFeatures
        FeatureChain(std::string_view head_name, VkStructureType head_type, uint32_t head_size);
        ~FeatureChain();
        FeatureChain(FeatureChain&& other) noexcept;
        FeatureChain& operator=(FeatureChain&& other) noexcept;
        FeatureChain(const FeatureChain&) = delete;
        FeatureChain& operator=(const FeatureChain&) = delete;

        /** @brief Adds the struct, zeroed, unless the chain has it already. */
        void Add(const generated::StructInfo& info);
        [[nodiscard]] bool Contains(std::string_view struct_name) const noexcept;
        /** @brief Sets one VkBool32 member to VK_TRUE. The chain must contain the member's struct. */
        void Set(const generated::MemberInfo& member) noexcept;
        /** @brief Returns the member's value, or 0 when the chain does not contain its struct. */
        [[nodiscard]] uint32_t Read(const generated::MemberInfo& member) const noexcept;
        [[nodiscard]] void* Head() noexcept;

    private:
        struct Block
        {
            std::string_view Name;
            // uint64_t words, so that every struct is 8-byte aligned
            std::unique_ptr<uint64_t[]> Storage;
        };

        [[nodiscard]] const Block* find(std::string_view struct_name) const noexcept;

        std::vector<Block> blocks;
    };
}

#endif // !DIAMOND_DOGS_RHI_FEATURE_CHAIN_HPP
