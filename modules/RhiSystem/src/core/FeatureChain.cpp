#include "FeatureChain.hpp"
#include "GeneratedFeatureTable.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace
{
    [[nodiscard]] std::unique_ptr<uint64_t[]> ZeroedStruct(VkStructureType type, uint32_t size);
    [[nodiscard]] VkBaseOutStructure* AsBase(uint64_t* storage) noexcept;
}

namespace rhi
{
    FeatureChain::FeatureChain(std::string_view head_name, VkStructureType head_type, uint32_t head_size)
    {
        blocks.push_back(Block{ .Name = head_name, .Storage = ZeroedStruct(head_type, head_size) });
    }

    FeatureChain::~FeatureChain() = default;
    FeatureChain::FeatureChain(FeatureChain&& other) noexcept = default;
    FeatureChain& FeatureChain::operator=(FeatureChain&& other) noexcept = default;

    void FeatureChain::Add(const generated::StructInfo& info)
    {
        if (Contains(info.Name))
        {
            return;
        }

        Block block{ .Name = info.Name, .Storage = ZeroedStruct(info.SType, info.Size) };
        VkBaseOutStructure* head = AsBase(blocks.front().Storage.get());
        VkBaseOutStructure* added = AsBase(block.Storage.get());

        // insert right after the head: the order of a pNext chain does not matter
        added->pNext = head->pNext;
        head->pNext = added;
        blocks.push_back(std::move(block));
    }

    bool FeatureChain::Contains(std::string_view struct_name) const noexcept
    {
        return find(struct_name) != nullptr;
    }

    void FeatureChain::Set(const generated::MemberInfo& member) noexcept
    {
        const Block* block = find(member.Struct);

        if (block == nullptr)
        {
            return;
        }

        const uint32_t value = VK_TRUE;
        std::byte* bytes = reinterpret_cast<std::byte*>(block->Storage.get());

        std::memcpy(bytes + member.Offset, &value, sizeof(value));
    }

    uint32_t FeatureChain::Read(const generated::MemberInfo& member) const noexcept
    {
        const Block* block = find(member.Struct);

        if (block == nullptr)
        {
            return 0u;
        }

        uint32_t value = 0u;
        const std::byte* bytes = reinterpret_cast<const std::byte*>(block->Storage.get());

        std::memcpy(&value, bytes + member.Offset, sizeof(value));
        return value;
    }

    void* FeatureChain::Head() noexcept
    {
        return blocks.front().Storage.get();
    }

    const FeatureChain::Block* FeatureChain::find(std::string_view struct_name) const noexcept
    {
        const auto found = std::ranges::find(blocks, struct_name, &Block::Name);

        if (found == blocks.end())
        {
            return nullptr;
        }

        return &*found;
    }
}

namespace
{
    std::unique_ptr<uint64_t[]> ZeroedStruct(VkStructureType type, uint32_t size)
    {
        const size_t wordCount = (static_cast<size_t>(size) + sizeof(uint64_t) - 1u) / sizeof(uint64_t);
        // make_unique of an array value-initializes, so every word is zero
        std::unique_ptr<uint64_t[]> storage = std::make_unique<uint64_t[]>(wordCount);

        AsBase(storage.get())->sType = type;
        return storage;
    }

    VkBaseOutStructure* AsBase(uint64_t* storage) noexcept
    {
        return reinterpret_cast<VkBaseOutStructure*>(storage);
    }
}
