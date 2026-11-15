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
    namespace generated = rhi::generated;

    constexpr std::string_view k_CoreFeatureStructs[]
    {
        "VkPhysicalDeviceVulkan11Features",
        "VkPhysicalDeviceVulkan12Features",
        "VkPhysicalDeviceVulkan13Features",
        "VkPhysicalDeviceVulkan14Features",
    };

    [[nodiscard]] std::unique_ptr<uint64_t[]> ZeroedStruct(VkStructureType type, uint32_t size);
    [[nodiscard]] VkBaseOutStructure* AsBase(uint64_t* storage) noexcept;
    [[nodiscard]] const generated::MemberInfo* FindMember(std::string_view struct_name, std::string_view member_name) noexcept;
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

    bool FeatureChain::IsEnabled(std::string_view feature_name) const noexcept
    {
        const size_t dot = feature_name.find('.');

        if (dot == std::string_view::npos || dot == 0u || dot == feature_name.size() - 1u)
        {
            return false;
        }

        std::string_view structName = feature_name.substr(0u, dot);
        const std::string_view memberName = feature_name.substr(dot + 1u);

        const auto alias = std::ranges::lower_bound(generated::k_StructAliases, structName, {}, &generated::StructAlias::Alias);

        if (alias != generated::k_StructAliases.end() && alias->Alias == structName)
        {
            structName = alias->Name;
        }

        const std::string_view canonicalStruct = structName;
        const auto foundStruct = std::ranges::lower_bound(generated::k_Structs, canonicalStruct, {}, &generated::StructInfo::Name);

        if (foundStruct == generated::k_Structs.end() || foundStruct->Name != canonicalStruct || foundStruct->Kind != generated::StructKind::Feature)
        {
            return false;
        }

        const generated::StructInfo* info = &*foundStruct;
        const generated::MemberInfo* member = FindMember(canonicalStruct, memberName);

        if (member == nullptr)
        {
            return false;
        }

        if (Read(*member) != 0u)
        {
            return true;
        }

        const bool notFeaturesHead = info->Name != "VkPhysicalDeviceFeatures";
        const bool notInCore = !std::ranges::contains(k_CoreFeatureStructs, info->Name);
        const bool hasVersion = info->Requires.contains("VK_VERSION_");

        if (notFeaturesHead && notInCore && hasVersion)
        {
            for (const std::string_view coreStruct : k_CoreFeatureStructs)
            {
                const generated::MemberInfo* coreMember = FindMember(coreStruct, memberName);

                if (coreMember != nullptr && Read(*coreMember) != 0u)
                {
                    return true;
                }
            }
        }

        return false;
    }

    void* FeatureChain::Head() noexcept
    {
        if (blocks.empty())
        {
            return nullptr;
        }

        return blocks.front().Storage.get();
    }

    const void* FeatureChain::Head() const noexcept
    {
        if (blocks.empty())
        {
            return nullptr;
        }

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

    const generated::MemberInfo* FindMember(std::string_view struct_name, std::string_view member_name) noexcept
    {
        const auto byStructThenMember = [](const generated::MemberInfo& row, std::pair<std::string_view, std::string_view> key)
        {
            return std::pair{ row.Struct, row.Member } < key;
        };
        const std::pair<std::string_view, std::string_view> key{ struct_name, member_name };
        const auto found = std::lower_bound(generated::k_Members.begin(),
                                            generated::k_Members.end(),
                                            key,
                                            byStructThenMember);

        if (found == generated::k_Members.end() || found->Struct != struct_name || found->Member != member_name)
        {
            return nullptr;
        }

        return &*found;
    }
}
