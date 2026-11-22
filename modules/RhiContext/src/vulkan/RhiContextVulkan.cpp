#include "RhiContextVulkan.hpp"
#include "RhiContext.hpp"
#include "RhiSystem.hpp"
#include "ShaderManifestDevice.hpp"
#include <memory>
#include <string_view>
#include <utility>

namespace rhi
{

namespace
{

const lodestone::manifest::DeviceLimits& EmptyLimits() noexcept;

} // namespace

struct RhiContextImpl
{
    RhiBackend Backend{ RhiBackend::Vulkan };
    VulkanRhiContextView View;
    const RhiSystem* System{ nullptr };
    lodestone::manifest::DeviceLimits Limits;
};

RhiContext::RhiContext() noexcept = default;

RhiContext::RhiContext(std::unique_ptr<RhiContextImpl> _impl) noexcept :
  impl{ std::move(_impl) }
{}

RhiContext::~RhiContext() = default;

RhiContext::RhiContext(RhiContext&& other) noexcept = default;

RhiContext& RhiContext::operator=(RhiContext&& other) noexcept = default;

bool RhiContext::IsValid() const noexcept
{
    return impl != nullptr;
}

RhiBackend RhiContext::Backend() const noexcept
{
    if (impl == nullptr)
    {
        return RhiBackend::Invalid;
    }

    return impl->Backend;
}

const lodestone::manifest::DeviceLimits& RhiContext::Limits() const noexcept
{
    if (impl == nullptr)
    {
        return EmptyLimits();
    }

    return impl->Limits;
}

const RhiContextImpl* RhiContext::Impl() const noexcept
{
    return impl.get();
}
bool RhiContext::IsFeatureEnabled(std::string_view capability_name) const noexcept
{
    if (impl == nullptr)
    {
        return false;
    }

    if (impl->System == nullptr)
    {
        return false;
    }

    return impl->System->SupportsCapability(capability_name);
}

RhiContext MakeVulkanRhiContext(RhiSystem& rhi_system, bool validation_enabled)
{
    std::unique_ptr<RhiContextImpl> impl = std::make_unique<RhiContextImpl>();
    impl->View.InstancePtr = rhi_system.GetInstance();
    impl->View.DevicePtr = rhi_system.GetDevice();
    impl->View.VulkanApiVersion = rhi_system.GetVulkanApiVersion();
    impl->View.ValidationEnabled = validation_enabled;
    impl->System = &rhi_system;
    impl->Limits = rhi_system.Limits();
    return RhiContext{ std::move(impl) };
}

RhiContext MakeVulkanRhiContext(const VulkanRhiContextView& view, const lodestone::manifest::DeviceLimits& limits)
{
    std::unique_ptr<RhiContextImpl> impl = std::make_unique<RhiContextImpl>();
    impl->View = view;
    impl->Limits = limits;
    return RhiContext{ std::move(impl) };
}

VulkanRhiContextView UnwrapVulkanRhiContext(const RhiContext& context) noexcept
{
    const RhiContextImpl* impl = context.Impl();

    if (impl == nullptr)
    {
        return VulkanRhiContextView{};
    }

    if (impl->Backend != RhiBackend::Vulkan)
    {
        return VulkanRhiContextView{};
    }

    return impl->View;
}

namespace
{

const lodestone::manifest::DeviceLimits& EmptyLimits() noexcept
{
    static const lodestone::manifest::DeviceLimits emptyLimits{};
    return emptyLimits;
}

} // namespace

} // namespace rhi
