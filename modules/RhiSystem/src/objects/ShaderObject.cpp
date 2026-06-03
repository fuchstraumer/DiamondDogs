#include "ShaderObject.hpp"
#include "Device.hpp"
#include "RhiDefines.hpp"
#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iostream>
#include <format>
#include <vector>

#ifdef RHI_SYSTEM_USE_VULKAN
    #include <vulkan/vulkan.h>
#elif defined(RHI_SYSTEM_USE_DX12)
    #include <d3d12.h>
    #include <dxgi1_6.h>
#endif

namespace rhi
{

    static PFN_vkCreateShadersEXT pfn_vkCreateShadersEXT = nullptr;
    static PFN_vkDestroyShaderEXT pfn_vkDestroyShaderEXT = nullptr;

#ifdef RHI_SYSTEM_USE_VULKAN
    struct ShaderObjectImpl
    {
        ShaderObjectImpl(DeviceHandle device) :
            ParentDevice{ device },
            ShaderObject{ VK_NULL_HANDLE },
            Stage{ ShaderStageFlags::None },
            Bytecode{},
            SpecializationConstants{},
            PushConstantRanges{},
            EntryPointName{},
            SourcePath{},
            isValid{ false }
        {
            if ((pfn_vkCreateShadersEXT == nullptr) || (pfn_vkDestroyShaderEXT == nullptr))
            {
                pfn_vkCreateShadersEXT = reinterpret_cast<PFN_vkCreateShadersEXT>(vkGetDeviceProcAddr(device.As<VkDevice>(), "vkCreateShadersEXT"));
                pfn_vkDestroyShaderEXT = reinterpret_cast<PFN_vkDestroyShaderEXT>(vkGetDeviceProcAddr(device.As<VkDevice>(), "vkDestroyShaderEXT"));
                if ((pfn_vkCreateShadersEXT == nullptr) || (pfn_vkDestroyShaderEXT == nullptr))
                {
                    throw std::runtime_error("Failed to load Vulkan shader object extension functions");
                }
            }
        }

        ~ShaderObjectImpl()
        {
            if (ShaderObject != VK_NULL_HANDLE)
            {
                assert(pfn_vkDestroyShaderEXT);
                pfn_vkDestroyShaderEXT(ParentDevice.As<VkDevice>(), ShaderObject, nullptr);
                ShaderObject = VK_NULL_HANDLE;
            }
        }

        DeviceHandle ParentDevice;
        VkShaderEXT ShaderObject;
        ShaderStageFlags Stage;
        std::vector<uint8_t> Bytecode;
        std::vector<ShaderObject::ReflectedSpecializationConstant> SpecializationConstants;
        std::vector<PushConstantRange> PushConstantRanges;
        std::string CompilationLog;
        std::string EntryPointName;
        std::filesystem::path SourcePath;
        bool isValid;
        Slang::ComPtr<slang::IGlobalSession> SlangGlobalSession;

        static VkShaderStageFlagBits ConvertStageToVulkan(ShaderStageFlags stage)
        {
            switch (stage)
            {
                case ShaderStageFlags::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
                case ShaderStageFlags::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
                case ShaderStageFlags::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
                case ShaderStageFlags::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
                case ShaderStageFlags::TesselationControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                case ShaderStageFlags::TesselationEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                default:
                    throw std::invalid_argument("Unsupported shader stage for Vulkan conversion");
            }
        }
#endif // RHI_SYSTEM_USE_VULKAN

        bool CreateVulkanShaderObject()
        {
            if (Bytecode.empty())
            {
                CompilationLog = "No bytecode available for Vulkan shader object creation";
                return false;
            }

            VkShaderCreateInfoEXT createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
            createInfo.pNext = nullptr;
            createInfo.flags = 0;
            createInfo.stage = ConvertStageToVulkan(Stage);
            createInfo.nextStage = 0; // Will be set dynamically
            createInfo.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
            createInfo.codeSize = Bytecode.size();
            createInfo.pCode = Bytecode.data();
            createInfo.pName = EntryPointName.c_str();
            createInfo.setLayoutCount = 0; // Descriptor set layouts - not needed for minimal implementation
            createInfo.pSetLayouts = nullptr;
            createInfo.pushConstantRangeCount = static_cast<uint32_t>(PushConstantRanges.size());
            createInfo.pPushConstantRanges = reinterpret_cast<const VkPushConstantRange*>(PushConstantRanges.data());
            assert(pfn_vkCreateShadersEXT);
            VkResult result = pfn_vkCreateShadersEXT(
                ParentDevice.As<VkDevice>(),
                1,
                &createInfo,
                nullptr,
                &ShaderObject);

            if (result != VK_SUCCESS)
            {
                CompilationLog = std::format("Failed to create Vulkan shader object: VkResult = {}", static_cast<int>(result));
                return false;
            }

            isValid = true;
            return true;
        }
    };

#if defined(RHI_SYSTEM_USE_DX12)
    // DX12 implementation would go here
    struct ShaderObjectImpl
    {
        ShaderObjectImpl(const Device* device) :
            ParentDevice{ device }
        {
            // DX12 shader object implementation
            throw std::runtime_error("DX12 ShaderObject implementation not yet available");
        }

        const Device* ParentDevice;
        // DX12 shader object members would go here
    };
#endif

    // ShaderObject implementation
    ShaderObject::ShaderObject() :
        impl{},
        handle{}
    {
    }

    ShaderObject::ShaderObject(std::unique_ptr<ShaderObjectImpl> impl) :
        impl{ std::move(impl) },
        handle{}
    {
        if (this->impl)
        {
            const uint64_t implHandle = reinterpret_cast<uint64_t>(this->impl.get());
            handle.Set(implHandle);
        }
    }

    ShaderObject::~ShaderObject() = default;

    void ShaderObject::Destroy() noexcept
    {
        impl.reset();
        handle = {};
    }

    ShaderObject::ShaderObject(ShaderObject&& other) noexcept :
        impl{ std::move(other.impl) },
        handle{ std::move(other.handle) }
    {
        other.handle = {};
    }

    ShaderObject& ShaderObject::operator=(ShaderObject&& other) noexcept
    {
        if (this != &other)
        {
            impl = std::move(other.impl);
            handle = std::move(other.handle);
            other.handle = {};
        }
        return *this;
    }

    Result ShaderObject::Create(DeviceHandle device, const CompileOptions& options, ShaderObject& outShaderObject)
    {
        try
        {
            auto impl = std::make_unique<ShaderObjectImpl>(device);
 
            // Create platform-specific shader object
#ifdef RHI_SYSTEM_USE_VULKAN
            if (!impl->CreateVulkanShaderObject())
            {
                return Result(Result::Code::InitializationFailed);
            }
#elif defined(RHI_SYSTEM_USE_DX12)
            // DX12 shader object creation would go here
            return Result(Result::Code::FeatureNotPresent);
#endif

            outShaderObject = ShaderObject(std::move(impl));
            return Result(Result::Code::Success);
        }
        catch (...)
        {
            return Result(Result::Code::InitializationFailed);
        }
    }

    Result ShaderObject::Create(DeviceHandle device, const BinaryOptions& options, ShaderObject& outShaderObject)
    {
        try
        {
            auto impl = std::make_unique<ShaderObjectImpl>(device);
            
            // Copy bytecode (uint32_t -> uint8_t)
            size_t byteSize = options.Bytecode.size_bytes();
            impl->Bytecode.resize(byteSize);
            std::memcpy(impl->Bytecode.data(), options.Bytecode.data(), byteSize);
            
            impl->Stage = options.Stage;
            impl->EntryPointName = options.EntryPointName;

            // Create platform-specific shader object
#ifdef RHI_SYSTEM_USE_VULKAN
            if (!impl->CreateVulkanShaderObject())
            {
                return Result(Result::Code::InitializationFailed);
            }
#elif defined(RHI_SYSTEM_USE_DX12)
            return Result(Result::Code::FeatureNotPresent);
#endif

            outShaderObject = ShaderObject(std::move(impl));
            return Result(Result::Code::Success);
        }
        catch (...)
        {
            return Result(Result::Code::InitializationFailed);
        }
    }

    ShaderObjectHandle ShaderObject::Handle() const noexcept
    {
        return handle;
    }

    ShaderStageFlags ShaderObject::GetStage() const noexcept
    {
        return impl ? impl->Stage : ShaderStageFlags::None;
    }

    std::span<const uint8_t> ShaderObject::GetBytecode() const noexcept
    {
        return impl ? std::span<const uint8_t>(impl->Bytecode) : std::span<const uint8_t>{};
    }

    size_t ShaderObject::GetBytecodeSize() const noexcept
    {
        return impl ? impl->Bytecode.size() : 0;
    }

    const std::vector<ShaderObject::ReflectedSpecializationConstant>& ShaderObject::GetSpecializationConstants() const noexcept
    {
        static const std::vector<ReflectedSpecializationConstant> empty;
        return impl ? impl->SpecializationConstants : empty;
    }

    const std::vector<PushConstantRange>& ShaderObject::GetPushConstantRanges() const noexcept
    {
        static const std::vector<PushConstantRange> empty;
        return impl ? impl->PushConstantRanges : empty;
    }

    bool ShaderObject::IsValid() const noexcept
    {
        return impl && impl->isValid;
    }

    std::string_view ShaderObject::GetCompilationLog() const noexcept
    {
        return impl ? std::string_view(impl->CompilationLog) : std::string_view{};
    }

    std::string_view ShaderObject::GetEntryPointName() const noexcept
    {
        return impl ? std::string_view(impl->EntryPointName) : std::string_view{};
    }

    const std::filesystem::path& ShaderObject::GetSourcePath() const noexcept
    {
        static const std::filesystem::path empty;
        return impl ? impl->SourcePath : empty;
    }

} // namespace rhi