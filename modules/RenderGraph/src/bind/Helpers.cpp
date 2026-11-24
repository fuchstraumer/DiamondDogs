#include "bind/Helpers.hpp"
#include "LoomFrame.hpp"
#include "PassStatements.hpp"
#include "Records.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifestLayout.hpp"
#include <cstdint>
#include <string_view>

namespace loom::bind
{

    std::string_view BindingNameOf(const lodestone::manifest::ResolvedResource& resource)
    {
        constexpr std::string_view k_EntryPointScope{ "entryPointParams_" };
        const std::string_view scope = resource.ScopeName();
        if (scope.starts_with(k_EntryPointScope))
        {
            return scope.substr(k_EntryPointScope.size());
        }

        return resource.Name();
    }

    std::string_view ViewRootOf(const lodestone::manifest::ResolvedResource& resource)
    {
        constexpr std::string_view k_ViewTag{ "ViewOf:" };

        for (uint32_t tagIndex = 0u; tagIndex < resource.TagCount(); ++tagIndex)
        {
            const std::string_view tag = resource.Tag(tagIndex);
            if (tag.starts_with(k_ViewTag))
            {
                return tag.substr(k_ViewTag.size());
            }
        }

        return {};
    }

    ResourceAccess ToResourceAccess(lodestone::ResourceAccess access)
    {
        switch (access)
        {
        case lodestone::ResourceAccess::ReadOnly:
            return ResourceAccess::Read;
        case lodestone::ResourceAccess::WriteOnly:
            return ResourceAccess::Write;
        case lodestone::ResourceAccess::ReadWrite:
            [[fallthrough]];
        case lodestone::ResourceAccess::RasterizerOrdered:
            [[fallthrough]];
        case lodestone::ResourceAccess::Append:
            [[fallthrough]];
        case lodestone::ResourceAccess::Consume:
            return ResourceAccess::ReadWrite;
        case lodestone::ResourceAccess::Feedback:
            return ResourceAccess::Write;
        case lodestone::ResourceAccess::Invalid:
            // uniform buffers and samplers: the shader only reads them
            return ResourceAccess::Read;
        }

        return ResourceAccess::Read;
    }

    ResourceAccess NarrowedAccess(ResourceAccess reflected, BindNarrowing narrowing)
    {
        switch (narrowing)
        {
        case BindNarrowing::ReadOnly:
            return ResourceAccess::Read;
        case BindNarrowing::WriteOnly:
            return ResourceAccess::Write;
        case BindNarrowing::Reflected:
            [[fallthrough]];
        case BindNarrowing::Invalid:
            break;
        }

        return reflected;
    }

    bool KeyFitsBinding(ResourceKind kind, lodestone::BindingKind binding_kind)
    {
        switch (binding_kind)
        {
        case lodestone::BindingKind::UniformBuffer:
            [[fallthrough]];
        case lodestone::BindingKind::ParameterBlock:
            [[fallthrough]];
        case lodestone::BindingKind::StorageBuffer:
            [[fallthrough]];
        case lodestone::BindingKind::TexelBuffer:
            return kind == ResourceKind::Buffer;
        case lodestone::BindingKind::Texture:
            [[fallthrough]];
        case lodestone::BindingKind::StorageTexture:
            [[fallthrough]];
        case lodestone::BindingKind::InputRenderTarget:
            return kind == ResourceKind::Image;
        case lodestone::BindingKind::Sampler:
            return kind == ResourceKind::Sampler;
        case lodestone::BindingKind::CombinedTextureSampler:
            [[fallthrough]];
        case lodestone::BindingKind::InlineUniform:
            [[fallthrough]];
        case lodestone::BindingKind::RayTracingAccelerationStructure:
            [[fallthrough]];
        case lodestone::BindingKind::PushConstant:
            [[fallthrough]];
        case lodestone::BindingKind::Invalid:
            // no Loom key names one of these yet
            break;
        }

        return false;
    }

    bool IsLoomFrameBinding(const lodestone::manifest::ResolvedResource& resource)
    {
        return resource.Kind() == lodestone::BindingKind::UniformBuffer && BindingNameOf(resource) == k_LoomFrameBindingName;
    }

}
