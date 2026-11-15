#include "DevicePlanner.hpp"
#include "FeatureChain.hpp"
#include "GeneratedFeatureTable.hpp"
#include "RhiSystemStart.hpp"
#include "ShaderManifestDevice.hpp"
#include "VulkanCapabilityTable.hpp"
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <cstdint>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace
{
    namespace generated = rhi::generated;
    namespace vulkan = lodestone::vulkan;

    constexpr std::string_view k_FeaturesHead{ "VkPhysicalDeviceFeatures" };
    constexpr std::string_view k_PropertiesHead{ "VkPhysicalDeviceProperties2" };
    // the structs that absorb the features of promoted structs, in version order
    constexpr std::string_view k_CoreFeatureStructs[]
    {
        "VkPhysicalDeviceVulkan11Features",
        "VkPhysicalDeviceVulkan12Features",
        "VkPhysicalDeviceVulkan13Features",
        "VkPhysicalDeviceVulkan14Features",
    };

    // the order in which PlanCapability tries the enables of one capability
    enum class EnablePass : int32_t
    {
        Version,
        CoreFeature,
        ExtensionFeature,
        Extension,
        Property,
    };

    /** What the device supports, read once. */
    struct DeviceFacts
    {
        uint32_t ApiVersion{ 0u };
        // sorted
        std::vector<std::string> Extensions;
        rhi::FeatureChain Features;
        rhi::FeatureChain Properties;
    };

    [[nodiscard]] const generated::StructInfo* FindStruct(std::string_view struct_name) noexcept;
    [[nodiscard]] const generated::MemberInfo* FindMember(std::string_view struct_name, std::string_view member_name) noexcept;
    [[nodiscard]] std::optional<uint32_t> FindFlagBit(std::string_view bit_name) noexcept;
    [[nodiscard]] uint32_t VersionOf(std::string_view token) noexcept;
    [[nodiscard]] uint32_t WithoutPatch(uint32_t api_version) noexcept;
    [[nodiscard]] std::string VersionName(uint32_t api_version);
    [[nodiscard]] bool Contains(const std::vector<std::string>& sorted_names, std::string_view name) noexcept;
    // returns the extension that makes the struct available, an empty view for a version, or nothing
    [[nodiscard]] std::optional<std::string_view> StructRoute(const generated::StructInfo& info, const DeviceFacts& facts);
    [[nodiscard]] DeviceFacts ReadFacts(VkPhysicalDevice physical_device,
                                        uint32_t api_version,
                                        const rhi::DeviceStartInfo& start_info);
    void AddQueriedStruct(DeviceFacts& facts, std::string_view struct_name);
    [[nodiscard]] const generated::MemberInfo* FoldIntoCore(const generated::MemberInfo& member,
                                                            const generated::StructInfo& info,
                                                            const DeviceFacts& facts) noexcept;
    [[nodiscard]] bool EnableFeatureMember(rhi::DevicePlan& plan,
                                           const DeviceFacts& facts,
                                           const generated::StructInfo& info,
                                           const generated::MemberInfo& member);
    [[nodiscard]] bool TryEnable(rhi::DevicePlan& plan,
                                 const DeviceFacts& facts,
                                 const vulkan::CapabilityEnable& enable,
                                 EnablePass pass);
    [[nodiscard]] bool PropertyHolds(const DeviceFacts& facts, const vulkan::CapabilityEnable& enable) noexcept;
    [[nodiscard]] bool PlanExtensions(rhi::DevicePlan& plan,
                                      const DeviceFacts& facts,
                                      std::span<const rhi::DeviceNeed> needs,
                                      std::span<const std::string_view> declined);
    [[nodiscard]] bool PlanFeatures(rhi::DevicePlan& plan,
                                    const DeviceFacts& facts,
                                    std::span<const rhi::DeviceNeed> needs,
                                    std::span<const std::string_view> declined);
    [[nodiscard]] bool PlanCapabilities(rhi::DevicePlan& plan,
                                        const DeviceFacts& facts,
                                        std::span<const rhi::DeviceNeed> needs,
                                        std::span<const std::string_view> declined);
    [[nodiscard]] bool PlanCapability(rhi::DevicePlan& plan, const DeviceFacts& facts, std::string_view primary_name);
    // a Wanted need that the start info declines is left off
    [[nodiscard]] bool IsDeclined(const rhi::DeviceNeed& need, std::span<const std::string_view> declined) noexcept;
    // the first queue family with graphics, the same family that Device chooses, must present to the surface
    [[nodiscard]] bool PlanPresent(rhi::DevicePlan& plan, VkPhysicalDevice physical_device, uint64_t present_surface);
    [[nodiscard]] bool PlanLimits(rhi::DevicePlan& plan, const lodestone::manifest::DeviceLimits& required);
    // records the first Required need that is not met, and returns false
    [[nodiscard]] bool Unmet(rhi::DevicePlan& plan, std::string_view need, std::string reason);
    [[nodiscard]] bool IsExcludedFeature(std::string_view struct_name, std::string_view member_name) noexcept;
    [[nodiscard]] bool IsMemberDeclined(const generated::StructInfo& info,
                                        const generated::MemberInfo& member,
                                        std::span<const std::string_view> declined,
                                        const DeviceFacts& facts) noexcept;
    void EnableStructFeatures(rhi::DevicePlan& plan,
                              const DeviceFacts& facts,
                              const generated::StructInfo& info,
                              std::span<const std::string_view> declined);
    void EnableSupportedFeatures(rhi::DevicePlan& plan,
                                 const DeviceFacts& facts,
                                 std::span<const std::string_view> declined);
    void SortUnique(std::vector<std::string>& names);
    void ReadIdentity(rhi::DevicePlan& plan, VkPhysicalDevice physical_device, const VkPhysicalDeviceProperties& properties);
}

namespace rhi
{
    DevicePlan PlanDevice(VkPhysicalDevice physical_device, uint32_t instance_api_version, const DeviceStartInfo& start_info)
    {
        DevicePlan plan
        {
            .PhysicalDevice = physical_device,
            .Enabled = FeatureChain(k_FeaturesHead,
                                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                    static_cast<uint32_t>(sizeof(VkPhysicalDeviceFeatures2))),
        };

        VkPhysicalDeviceProperties properties{};

        vkGetPhysicalDeviceProperties(physical_device, &properties);
        ReadIdentity(plan, physical_device, properties);

        const uint32_t instanceVersion = WithoutPatch(instance_api_version);
        const uint32_t deviceVersion = WithoutPatch(properties.apiVersion);

        plan.ApiVersion = std::min(instanceVersion, deviceVersion);

        if (deviceVersion < instanceVersion)
        {
            (void)Unmet(plan, VersionName(instanceVersion), "the device supports " + VersionName(deviceVersion));
            return plan;
        }

        if (!PlanPresent(plan, physical_device, start_info.PresentSurface) ||
            !PlanLimits(plan, start_info.RequiredLimits))
        {
            return plan;
        }

        const DeviceFacts facts = ReadFacts(physical_device, plan.ApiVersion, start_info);

        if (!PlanExtensions(plan, facts, start_info.Extensions, start_info.Declined) ||
            !PlanFeatures(plan, facts, start_info.Features, start_info.Declined) ||
            !PlanCapabilities(plan, facts, start_info.Capabilities, start_info.Declined))
        {
            return plan;
        }

        SortUnique(plan.Extensions);
        EnableSupportedFeatures(plan, facts, start_info.Declined);
        SortUnique(plan.EnabledCapabilities);
        return plan;
    }
}

namespace
{
    const generated::StructInfo* FindStruct(std::string_view struct_name) noexcept
    {
        const auto alias = std::ranges::lower_bound(generated::k_StructAliases, struct_name, {}, &generated::StructAlias::Alias);

        if (alias != generated::k_StructAliases.end() && alias->Alias == struct_name)
        {
            struct_name = alias->Name;
        }

        const auto found = std::ranges::lower_bound(generated::k_Structs, struct_name, {}, &generated::StructInfo::Name);

        if (found == generated::k_Structs.end() || found->Name != struct_name)
        {
            return nullptr;
        }

        return &*found;
    }

    const generated::MemberInfo* FindMember(std::string_view struct_name, std::string_view member_name) noexcept
    {
        const auto byStructThenMember = [](const generated::MemberInfo& row, std::pair<std::string_view, std::string_view> key)
        {
            return std::pair{ row.Struct, row.Member } < key;
        };
        const std::pair<std::string_view, std::string_view> key{ struct_name, member_name };
        const auto found = std::lower_bound(generated::k_Members.begin(), generated::k_Members.end(), key, byStructThenMember);

        if (found == generated::k_Members.end() || found->Struct != struct_name || found->Member != member_name)
        {
            return nullptr;
        }

        return &*found;
    }

    std::optional<uint32_t> FindFlagBit(std::string_view bit_name) noexcept
    {
        const auto found = std::ranges::lower_bound(generated::k_FlagBits, bit_name, {}, &generated::FlagBit::Name);

        if (found == generated::k_FlagBits.end() || found->Name != bit_name)
        {
            return std::nullopt;
        }

        return found->Value;
    }

    uint32_t VersionOf(std::string_view token) noexcept
    {
        constexpr std::string_view k_Prefix{ "VK_VERSION_" };

        if (!token.starts_with(k_Prefix) || token.size() < k_Prefix.size() + 3u)
        {
            return 0u;
        }

        uint32_t major = 0u;
        uint32_t minor = 0u;
        const std::string_view digits = token.substr(k_Prefix.size());
        const std::from_chars_result majorRead = std::from_chars(digits.data(), digits.data() + digits.size(), major);

        if (majorRead.ec != std::errc{} || majorRead.ptr == digits.data() + digits.size() || *majorRead.ptr != '_')
        {
            return 0u;
        }

        (void)std::from_chars(majorRead.ptr + 1, digits.data() + digits.size(), minor);
        return VK_MAKE_API_VERSION(0u, major, minor, 0u);
    }

    uint32_t WithoutPatch(uint32_t api_version) noexcept
    {
        return VK_MAKE_API_VERSION(0u, VK_API_VERSION_MAJOR(api_version), VK_API_VERSION_MINOR(api_version), 0u);
    }

    std::string VersionName(uint32_t api_version)
    {
        return "VK_VERSION_" + std::to_string(VK_API_VERSION_MAJOR(api_version)) + "_" +
               std::to_string(VK_API_VERSION_MINOR(api_version));
    }

    bool Contains(const std::vector<std::string>& sorted_names, std::string_view name) noexcept
    {
        return std::ranges::binary_search(sorted_names, name, std::less<>{});
    }

    std::optional<std::string_view> StructRoute(const generated::StructInfo& info, const DeviceFacts& facts)
    {
        std::optional<std::string_view> route;

        for (const auto token : std::views::split(info.Requires, ','))
        {
            const std::string_view name{ token.begin(), token.end() };
            const uint32_t version = VersionOf(name);

            // a version wins over an extension: it needs nothing enabled by name
            if (version != 0u && version <= facts.ApiVersion)
            {
                return std::string_view{};
            }

            if (version == 0u && !route.has_value() && Contains(facts.Extensions, name))
            {
                route = name;
            }
        }

        return route;
    }

    DeviceFacts ReadFacts(VkPhysicalDevice physical_device, uint32_t api_version, const rhi::DeviceStartInfo& start_info)
    {
        DeviceFacts facts
        {
            .ApiVersion = api_version,
            .Features = rhi::FeatureChain(k_FeaturesHead,
                                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                          static_cast<uint32_t>(sizeof(VkPhysicalDeviceFeatures2))),
            .Properties = rhi::FeatureChain(k_PropertiesHead,
                                            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                            static_cast<uint32_t>(sizeof(VkPhysicalDeviceProperties2))),
        };

        uint32_t extensionCount = 0u;

        vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> extensions(extensionCount);

        vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extensionCount, extensions.data());

        for (const VkExtensionProperties& extension : extensions)
        {
            facts.Extensions.emplace_back(extension.extensionName);
        }

        std::ranges::sort(facts.Extensions);

        // query only the structs that a need can reach, plus the core structs that absorb promoted features
        for (const std::string_view coreStruct : k_CoreFeatureStructs)
        {
            AddQueriedStruct(facts, coreStruct);
        }

        for (const rhi::DeviceNeed& need : start_info.Features)
        {
            AddQueriedStruct(facts, need.Name.substr(0u, need.Name.find('.')));
        }

        for (const rhi::DeviceNeed& need : start_info.Capabilities)
        {
            const std::optional<vulkan::CapabilityEntry> entry = vulkan::FindCapability(vulkan::PrimaryCapabilityName(need.Name));

            if (!entry.has_value())
            {
                continue;
            }

            for (const vulkan::CapabilityEnable& enable : vulkan::EnablesOf(*entry))
            {
                if (enable.Kind == vulkan::CapabilityEnableKind::Feature || enable.Kind == vulkan::CapabilityEnableKind::Property)
                {
                    AddQueriedStruct(facts, enable.Name);
                }
            }
        }

        const auto matchesExtension = [&](std::string_view route)
        {
            for (const rhi::DeviceNeed& need : start_info.Extensions)
            {
                if (need.Name == route)
                {
                    return true;
                }
            }

            for (const rhi::DeviceNeed& need : start_info.Capabilities)
            {
                if (need.Name == route)
                {
                    return true;
                }

                const std::optional<vulkan::CapabilityEntry> entry = vulkan::FindCapability(vulkan::PrimaryCapabilityName(need.Name));

                if (!entry.has_value())
                {
                    continue;
                }

                for (const vulkan::CapabilityEnable& enable : vulkan::EnablesOf(*entry))
                {
                    if (enable.Kind == vulkan::CapabilityEnableKind::Extension && enable.Name == route)
                    {
                        return true;
                    }
                }
            }

            return false;
        };

        for (const generated::StructInfo& info : generated::k_Structs)
        {
            if (info.Kind != generated::StructKind::Feature)
            {
                continue;
            }

            const std::optional<std::string_view> route = StructRoute(info, facts);

            if (!route.has_value() || route->empty())
            {
                continue;
            }

            if (std::ranges::contains(start_info.Declined, *route))
            {
                continue;
            }

            if (!Contains(facts.Extensions, *route))
            {
                continue;
            }

            if (!matchesExtension(*route))
            {
                continue;
            }

            facts.Features.Add(info);
        }

        // the *2 queries are core from 1.1. A 1.0 device fills the 1.0 structs only
        if (api_version < VK_API_VERSION_1_1)
        {
            vkGetPhysicalDeviceFeatures(physical_device, &static_cast<VkPhysicalDeviceFeatures2*>(facts.Features.Head())->features);
            vkGetPhysicalDeviceProperties(physical_device, &static_cast<VkPhysicalDeviceProperties2*>(facts.Properties.Head())->properties);
            return facts;
        }

        vkGetPhysicalDeviceFeatures2(physical_device, static_cast<VkPhysicalDeviceFeatures2*>(facts.Features.Head()));
        vkGetPhysicalDeviceProperties2(physical_device, static_cast<VkPhysicalDeviceProperties2*>(facts.Properties.Head()));
        return facts;
    }

    void AddQueriedStruct(DeviceFacts& facts, std::string_view struct_name)
    {
        const generated::StructInfo* info = FindStruct(struct_name);

        // a struct from a newer header than ours, or one that the device doesn't support
        if (info == nullptr || info->Name == k_FeaturesHead || !StructRoute(*info, facts).has_value())
        {
            return;
        }

        if (info->Kind == generated::StructKind::Feature)
        {
            facts.Features.Add(*info);
        }
        else
        {
            facts.Properties.Add(*info);
        }
    }

    const generated::MemberInfo* FoldIntoCore(const generated::MemberInfo& member,
                                              const generated::StructInfo& info,
                                              const DeviceFacts& facts) noexcept
    {
        const bool isCoreStruct = info.Name == k_FeaturesHead || std::ranges::contains(k_CoreFeatureStructs, info.Name);
        const bool promoted = info.Requires.contains("VK_VERSION_");

        if (isCoreStruct || !promoted)
        {
            return &member;
        }

        for (const std::string_view coreStruct : k_CoreFeatureStructs)
        {
            const generated::MemberInfo* folded = FindMember(coreStruct, member.Member);

            if (folded != nullptr && facts.Features.Contains(coreStruct))
            {
                return folded;
            }
        }

        return &member;
    }

    bool EnableFeatureMember(rhi::DevicePlan& plan,
                             const DeviceFacts& facts,
                             const generated::StructInfo& info,
                             const generated::MemberInfo& member)
    {
        const std::optional<std::string_view> route = StructRoute(info, facts);

        if (!route.has_value())
        {
            return false;
        }

        const generated::MemberInfo* target = FoldIntoCore(member, info, facts);

        if (facts.Features.Read(*target) == 0u)
        {
            return false;
        }

        const generated::StructInfo* targetInfo = FindStruct(target->Struct);

        plan.Enabled.Add(*targetInfo);
        plan.Enabled.Set(*target);

        // a member of an extension struct needs the extension; a folded member is core
        if (!route->empty() && target == &member)
        {
            plan.Extensions.emplace_back(*route);
        }

        return true;
    }

    bool TryEnable(rhi::DevicePlan& plan, const DeviceFacts& facts, const vulkan::CapabilityEnable& enable, EnablePass pass)
    {
        switch (enable.Kind)
        {
        case vulkan::CapabilityEnableKind::Version:
            return pass == EnablePass::Version && VersionOf(enable.Name) != 0u && VersionOf(enable.Name) <= facts.ApiVersion;
        case vulkan::CapabilityEnableKind::Extension:
            if (pass != EnablePass::Extension || !Contains(facts.Extensions, enable.Name))
            {
                return false;
            }

            plan.Extensions.emplace_back(enable.Name);
            return true;
        case vulkan::CapabilityEnableKind::Property:
            return pass == EnablePass::Property && PropertyHolds(facts, enable);
        case vulkan::CapabilityEnableKind::Feature:
            break;
        }

        const generated::StructInfo* info = FindStruct(enable.Name);
        const generated::MemberInfo* member = info != nullptr ? FindMember(info->Name, enable.Member) : nullptr;

        if (member == nullptr)
        {
            return false;
        }

        const std::optional<std::string_view> route = StructRoute(*info, facts);
        const bool coreRoute = route.has_value() && route->empty();
        const bool extensionRoute = route.has_value() && !route->empty();

        if ((pass == EnablePass::CoreFeature && coreRoute) || (pass == EnablePass::ExtensionFeature && extensionRoute))
        {
            return EnableFeatureMember(plan, facts, *info, *member);
        }

        return false;
    }

    bool PropertyHolds(const DeviceFacts& facts, const vulkan::CapabilityEnable& enable) noexcept
    {
        const generated::StructInfo* info = FindStruct(enable.Name);
        const generated::MemberInfo* member = info != nullptr ? FindMember(info->Name, enable.Member) : nullptr;

        if (member == nullptr)
        {
            return false;
        }

        const uint32_t value = facts.Properties.Read(*member);

        if (enable.Value == "VK_TRUE")
        {
            return value != 0u;
        }

        const std::optional<uint32_t> bit = FindFlagBit(enable.Value);

        return bit.has_value() && (value & *bit) != 0u;
    }

    bool PlanExtensions(rhi::DevicePlan& plan,
                        const DeviceFacts& facts,
                        std::span<const rhi::DeviceNeed> needs,
                        std::span<const std::string_view> declined)
    {
        for (const rhi::DeviceNeed& need : needs)
        {
            if (IsDeclined(need, declined))
            {
                continue;
            }

            if (Contains(facts.Extensions, need.Name))
            {
                plan.Extensions.emplace_back(need.Name);
                continue;
            }

            if (need.Level == rhi::DeviceNeedLevel::Required)
            {
                return Unmet(plan, need.Name, "the device does not support the extension");
            }
        }

        return true;
    }

    bool PlanFeatures(rhi::DevicePlan& plan,
                      const DeviceFacts& facts,
                      std::span<const rhi::DeviceNeed> needs,
                      std::span<const std::string_view> declined)
    {
        for (const rhi::DeviceNeed& need : needs)
        {
            if (IsDeclined(need, declined))
            {
                continue;
            }

            const size_t dot = need.Name.find('.');
            const generated::StructInfo* info = dot != std::string_view::npos ? FindStruct(need.Name.substr(0u, dot)) : nullptr;
            const generated::MemberInfo* member =
                info != nullptr && info->Kind == generated::StructKind::Feature ? FindMember(info->Name, need.Name.substr(dot + 1u)) : nullptr;

            if (member == nullptr)
            {
                if (need.Level == rhi::DeviceNeedLevel::Required)
                {
                    return Unmet(plan, need.Name, "not a feature member in this Vulkan header (use Struct.member)");
                }

                continue;
            }

            if (EnableFeatureMember(plan, facts, *info, *member))
            {
                continue;
            }

            if (need.Level == rhi::DeviceNeedLevel::Required)
            {
                return Unmet(plan, need.Name, "the device does not support the feature");
            }
        }

        return true;
    }

    bool PlanCapabilities(rhi::DevicePlan& plan,
                          const DeviceFacts& facts,
                          std::span<const rhi::DeviceNeed> needs,
                          std::span<const std::string_view> declined)
    {
        for (const rhi::DeviceNeed& need : needs)
        {
            const std::string_view primaryName = vulkan::PrimaryCapabilityName(need.Name);
            const bool wanted = need.Level == rhi::DeviceNeedLevel::Wanted;

            if (wanted && (std::ranges::contains(declined, need.Name) || std::ranges::contains(declined, primaryName)))
            {
                continue;
            }

            if (PlanCapability(plan, facts, primaryName))
            {
                plan.EnabledCapabilities.emplace_back(primaryName);
                continue;
            }

            if (!wanted)
            {
                const bool known = vulkan::FindCapability(primaryName).has_value();

                return Unmet(plan, need.Name, known ? "no enable of this capability is supported" :
                                                      "Lodestone's Vulkan capability table has no enable for it");
            }
        }

        return true;
    }

    bool PlanCapability(rhi::DevicePlan& plan, const DeviceFacts& facts, std::string_view primary_name)
    {
        const std::optional<vulkan::CapabilityEntry> entry = vulkan::FindCapability(primary_name);

        if (!entry.has_value())
        {
            return false;
        }

        constexpr EnablePass k_Passes[]
        {
            EnablePass::Version,
            EnablePass::CoreFeature,
            EnablePass::ExtensionFeature,
            EnablePass::Extension,
            EnablePass::Property,
        };

        for (const EnablePass pass : k_Passes)
        {
            for (const vulkan::CapabilityEnable& enable : vulkan::EnablesOf(*entry))
            {
                if (TryEnable(plan, facts, enable, pass))
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool IsDeclined(const rhi::DeviceNeed& need, std::span<const std::string_view> declined) noexcept
    {
        return need.Level == rhi::DeviceNeedLevel::Wanted && std::ranges::contains(declined, need.Name);
    }

    bool PlanPresent(rhi::DevicePlan& plan, VkPhysicalDevice physical_device, uint64_t present_surface)
    {
        if (present_surface == 0u)
        {
            return true;
        }

        uint32_t familyCount = 0u;

        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &familyCount, nullptr);

        std::vector<VkQueueFamilyProperties> families(familyCount);

        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &familyCount, families.data());

        const auto hasGraphics = [](const VkQueueFamilyProperties& family) noexcept
        {
            return (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u;
        };
        const auto graphics = std::ranges::find_if(families, hasGraphics);

        if (graphics == families.end())
        {
            return Unmet(plan, "present", "the device has no graphics queue family");
        }

        const uint32_t graphicsFamily = static_cast<uint32_t>(graphics - families.begin());
        VkBool32 presents = VK_FALSE;
        const VkResult queried = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device,
                                                                      graphicsFamily,
                                                                      reinterpret_cast<VkSurfaceKHR>(present_surface),
                                                                      &presents);

        if (queried != VK_SUCCESS || presents != VK_TRUE)
        {
            return Unmet(plan, "present", "the graphics queue family cannot present to the window surface");
        }

        return true;
    }

    bool PlanLimits(rhi::DevicePlan& plan, const lodestone::manifest::DeviceLimits& required)
    {
        struct LimitCheck
        {
            std::string_view Name;
            uint32_t Required;
            uint32_t Device;
        };

        const LimitCheck checks[]
        {
            { "maxComputeSharedMemorySize", required.MaxWorkgroupMemoryBytes, plan.Limits.MaxWorkgroupMemoryBytes },
            { "maxComputeWorkGroupInvocations", required.MaxInvocationsPerWorkgroup, plan.Limits.MaxInvocationsPerWorkgroup },
            { "maxComputeWorkGroupSize[0]", required.MaxWorkgroupSizeX, plan.Limits.MaxWorkgroupSizeX },
            { "maxComputeWorkGroupSize[1]", required.MaxWorkgroupSizeY, plan.Limits.MaxWorkgroupSizeY },
            { "maxComputeWorkGroupSize[2]", required.MaxWorkgroupSizeZ, plan.Limits.MaxWorkgroupSizeZ },
            { "maxPushConstantsSize", required.MaxPushConstantBytes, plan.Limits.MaxPushConstantBytes },
        };

        for (const LimitCheck& check : checks)
        {
            if (check.Device < check.Required)
            {
                return Unmet(plan, check.Name, std::format("the device limit is {}, and an entry point needs {}", check.Device, check.Required));
            }
        }

        return true;
    }

    bool Unmet(rhi::DevicePlan& plan, std::string_view need, std::string reason)
    {
        if (plan.UnmetNeed.empty())
        {
            plan.UnmetNeed = std::string{ need };
            plan.UnmetReason = std::move(reason);
        }

        return false;
    }

    void SortUnique(std::vector<std::string>& names)
    {
        std::ranges::sort(names);

        const auto [firstDuplicate, last] = std::ranges::unique(names);

        names.erase(firstDuplicate, last);
    }

    void ReadIdentity(rhi::DevicePlan& plan, VkPhysicalDevice physical_device, const VkPhysicalDeviceProperties& properties)
    {
        // find first null terminator in the device name string provided by Vulkan

        int32_t nullPos = 0;
        for (; std::cmp_less(nullPos, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE); ++nullPos)
        {
            if (properties.deviceName[nullPos] == '\0')
            {
                break;
            }
        }
        assert(nullPos < VK_MAX_PHYSICAL_DEVICE_NAME_SIZE && "Device name must be null-terminated within the maximum allowed size.");

        plan.DeviceName = std::string{ properties.deviceName, properties.deviceName + nullPos };

        plan.Limits = lodestone::manifest::DeviceLimits
        {
            .MaxWorkgroupMemoryBytes = properties.limits.maxComputeSharedMemorySize,
            .MaxInvocationsPerWorkgroup = properties.limits.maxComputeWorkGroupInvocations,
            .MaxWorkgroupSizeX = properties.limits.maxComputeWorkGroupSize[0],
            .MaxWorkgroupSizeY = properties.limits.maxComputeWorkGroupSize[1],
            .MaxWorkgroupSizeZ = properties.limits.maxComputeWorkGroupSize[2],
            .MaxPushConstantBytes = properties.limits.maxPushConstantsSize,
        };

        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            plan.Score += 1000;
        }
        else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            plan.Score += 100;
        }

        VkPhysicalDeviceMemoryProperties memory{};

        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory);

        constexpr VkDeviceSize k_Gibibyte = VkDeviceSize{ 1024u } * 1024u * 1024u;
        const std::span<const VkMemoryHeap> heaps{ memory.memoryHeaps, memory.memoryHeapCount };

        for (const VkMemoryHeap& heap : heaps)
        {
            if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u)
            {
                plan.Score += static_cast<int32_t>(heap.size / k_Gibibyte);
            }
        }
    }

    bool IsExcludedFeature(std::string_view struct_name, std::string_view member_name) noexcept
    {
        if (member_name.contains("CaptureReplay"))
        {
            return true;
        }

        if (struct_name == "VkPhysicalDeviceRobustness2FeaturesKHR" ||
            struct_name == "VkPhysicalDeviceRobustness2FeaturesEXT")
        {
            return true;
        }

        // these are all features that have some actual driver cost or performance implications,
        // so unlike most feature toggles in feature/prop structs where leaving them all on is
        // totally fine and dandy, these are the exclusions
        if (member_name == "robustBufferAccess" ||
            member_name == "robustImageAccess" ||
            member_name == "pipelineRobustness" ||
            member_name == "vertexAttributeRobustness" ||
            member_name == "cooperativeMatrixRobustBufferAccess" ||
            member_name == "protectedMemory" ||
            member_name == "pipelineProtectedAccess")
        {
            return true;
        }

        return false;
    }

    bool IsMemberDeclined(const generated::StructInfo& info,
                          const generated::MemberInfo& member,
                          std::span<const std::string_view> declined,
                          const DeviceFacts& facts) noexcept
    {
        for (const std::string_view entry : declined)
        {
            const size_t dot = entry.find('.');

            if (dot == std::string_view::npos || dot == 0u || dot == entry.size() - 1u)
            {
                continue;
            }

            const std::string_view declStructName = entry.substr(0u, dot);
            const std::string_view declMember = entry.substr(dot + 1u);

            if (declMember != member.Member)
            {
                continue;
            }

            const generated::StructInfo* declInfo = FindStruct(declStructName);

            if (declInfo == nullptr)
            {
                continue;
            }

            const std::string_view declStruct = declInfo->Name;

            if (declStruct == info.Name)
            {
                return true;
            }

            const generated::MemberInfo* declMemberInfo = FindMember(declStruct, declMember);

            if (declMemberInfo != nullptr)
            {
                const generated::MemberInfo* declFolded = FoldIntoCore(*declMemberInfo, *declInfo, facts);

                if (declFolded != nullptr && declFolded->Struct == info.Name)
                {
                    return true;
                }
            }

            const generated::MemberInfo* infoFolded = FoldIntoCore(member, info, facts);

            if (infoFolded != nullptr && infoFolded->Struct == declStruct)
            {
                return true;
            }
        }

        return false;
    }

    void EnableStructFeatures(rhi::DevicePlan& plan,
                              const DeviceFacts& facts,
                              const generated::StructInfo& info,
                              std::span<const std::string_view> declined)
    {
        const auto byStruct = [](const generated::MemberInfo& m, std::string_view s)
        {
            return m.Struct < s;
        };
        
        auto iter = std::lower_bound(generated::k_Members.begin(), generated::k_Members.end(), info.Name, byStruct);

        while (iter != generated::k_Members.end() && iter->Struct == info.Name)
        {
            const generated::MemberInfo& member = *iter++;

            if (facts.Features.Read(member) == 0u)
            {
                continue;
            }

            if (IsExcludedFeature(info.Name, member.Member))
            {
                continue;
            }

            if (IsMemberDeclined(info, member, declined, facts))
            {
                continue;
            }

            const generated::MemberInfo* target = FoldIntoCore(member, info, facts);

            if (facts.Features.Read(*target) == 0u)
            {
                continue;
            }

            const generated::StructInfo* targetInfo = FindStruct(target->Struct);

            if (targetInfo != nullptr)
            {
                plan.Enabled.Add(*targetInfo);
                plan.Enabled.Set(*target);
            }
        }
    }

    void EnableSupportedFeatures(rhi::DevicePlan& plan,
                                 const DeviceFacts& facts,
                                 std::span<const std::string_view> declined)
    {
        const generated::StructInfo* headInfo = FindStruct(k_FeaturesHead);

        if (headInfo != nullptr)
        {
            EnableStructFeatures(plan, facts, *headInfo, declined);
        }

        for (const std::string_view coreStruct : k_CoreFeatureStructs)
        {
            if (!facts.Features.Contains(coreStruct))
            {
                continue;
            }

            const generated::StructInfo* info = FindStruct(coreStruct);

            if (info != nullptr)
            {
                EnableStructFeatures(plan, facts, *info, declined);
            }
        }

        for (const generated::StructInfo& info : generated::k_Structs)
        {
            if (info.Kind != generated::StructKind::Feature)
            {
                continue;
            }

            const std::optional<std::string_view> route = StructRoute(info, facts);

            if (!route.has_value() || route->empty())
            {
                continue;
            }

            if (Contains(plan.Extensions, *route))
            {
                EnableStructFeatures(plan, facts, info, declined);
            }
        }
    }
}
