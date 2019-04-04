#include "ResourceContext.hpp"
#include "ResourceContextImpl.hpp"

ResourceContext::ResourceContext() : impl(nullptr) {}

ResourceContext::~ResourceContext()
{
    Destroy();
}

ResourceContext & ResourceContext::Get()
{
    static ResourceContext context;
    return context;
}

void ResourceContext::Initialize(vpr::Device* device, vpr::PhysicalDevice* physical_device)
{
    impl->construct(device, physical_device);
}

void ResourceContext::Update() {
    impl->update();
}

void ResourceContext::Destroy()
{
    impl->destroy();
}

void ResourceContext::FillBuffer(VulkanResource * dest_buffer, const uint32_t value, const size_t offset, const size_t fill_size)
{
    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    auto guard = transfer_system.AcquireSpinLock();
    auto cmd = transfer_system.TransferCmdBuffer();
    vkCmdFillBuffer(cmd, (VkBuffer)dest_buffer->Handle, offset, fill_size, value);
}

void ResourceContext::SetImageData(VulkanResource* image, const size_t num_data, const gpu_image_resource_data_t* data) 
{
    impl->setImageInitialData(image, num_data, data, impl->resourceAllocations.at(image));
}

VulkanResource* ResourceContext::CreateSampler(const VkSamplerCreateInfo* info, void* user_data) 
{
    return impl->createSampler(info, user_data);
}

VulkanResource* ResourceContext::CreateCombinedImageSampler(const VkImageCreateInfo * info, const VkImageViewCreateInfo * view_info, const VkSamplerCreateInfo * sampler_info, 
    const size_t num_data, const gpu_image_resource_data_t * initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void * user_data) 
{
    VulkanResource* resource = CreateImage(info, view_info, num_data, initial_data, _resource_usage, _flags, user_data);
    resource->Type = resource_type::COMBINED_IMAGE_SAMPLER;
    resource->Sampler = CreateSampler(sampler_info);
    return resource;
}

VulkanResource* ResourceContext::CreateResourceCopy(VulkanResource * src) 
{
    VulkanResource* result = nullptr;
    CopyResource(src, &result);
    return result;
}

void ResourceContext::CopyResource(VulkanResource * src, VulkanResource** dest) 
{
    switch (src->Type) 
    {
    case resource_type::BUFFER:
        impl->createBufferResourceCopy(src, dest);
        break;
    case resource_type::SAMPLER:
        impl->createSamplerResourceCopy(src, dest);
        break;
    case resource_type::IMAGE:
        impl->createImageResourceCopy(src, dest);
        break;
    case resource_type::COMBINED_IMAGE_SAMPLER:
        impl->createCombinedImageSamplerResourceCopy(src, dest);
        break;
    default:
        throw std::domain_error("Passed source resource to CopyResource had invalid resource_type value.");
    };
}

void ResourceContext::CopyResourceContents(VulkanResource* src, VulkanResource* dst) 
{
    if (src->Type == dst->Type) 
    {
        switch (src->Type)
        {
        case resource_type::BUFFER:
            break;
        case resource_type::IMAGE:
            break;
        case resource_type::COMBINED_IMAGE_SAMPLER:
            break;
        case resource_type::SAMPLER:
            [[fallthrough]];
        default:
            break;
        }
    }
    else 
    {

    }
}
