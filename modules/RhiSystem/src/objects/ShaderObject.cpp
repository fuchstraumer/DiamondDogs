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

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <debugapi.h>

void win32_OutputDebugString(const char* str)
{
    OutputDebugStringA(str);
}

#endif

namespace rhi
{

    static PFN_vkCreateShadersEXT pfn_vkCreateShadersEXT = nullptr;
    static PFN_vkDestroyShaderEXT pfn_vkDestroyShaderEXT = nullptr;

    using SlangModulePtr = Slang::ComPtr<slang::IModule>;
    using SlangBlobPtr = Slang::ComPtr<slang::IBlob>;
    using SlangProgramPtr = Slang::ComPtr<slang::IComponentType>;
    using SlangLayoutPtr = slang::ProgramLayout*;
    using SlangMetadataPtr = Slang::ComPtr<slang::IMetadata>;

    ShaderStageFlags FromSlangStage(SlangStage stage)
    {
        switch (stage)
        {
            case SLANG_STAGE_VERTEX: return ShaderStageFlags::Vertex;
            case SLANG_STAGE_HULL: return ShaderStageFlags::TesselationControl;
            case SLANG_STAGE_DOMAIN: return ShaderStageFlags::TesselationEvaluation;
            case SLANG_STAGE_GEOMETRY: return ShaderStageFlags::Geometry;
            case SLANG_STAGE_FRAGMENT: return ShaderStageFlags::Fragment;
            case SLANG_STAGE_COMPUTE: return ShaderStageFlags::Compute;
            case SLANG_STAGE_RAY_GENERATION: return ShaderStageFlags::RayGeneration;
            case SLANG_STAGE_ANY_HIT: return ShaderStageFlags::AnyHit;
            case SLANG_STAGE_CLOSEST_HIT: return ShaderStageFlags::ClosestHit;
            case SLANG_STAGE_MISS: return ShaderStageFlags::Miss;
            case SLANG_STAGE_INTERSECTION: return ShaderStageFlags::Intersection;
            case SLANG_STAGE_CALLABLE: return ShaderStageFlags::Callable;
            case SLANG_STAGE_MESH: return ShaderStageFlags::Mesh;
            case SLANG_STAGE_AMPLIFICATION: return ShaderStageFlags::Task;
            default: return ShaderStageFlags::None;
        }
    }

    struct AccessPathNode
    {
        slang::VariableLayoutReflection* VarLayout = nullptr;
        AccessPathNode* Outer = nullptr;
    };

    struct AccessPath
    {
        bool Valid = false;
        AccessPathNode* DeepestConstantBuffer = nullptr;
        AccessPathNode* DeepestParameterBlock = nullptr;
        AccessPathNode* Leaf = nullptr;
    };

    struct ExtendedAccessPath : AccessPath
    {
        ExtendedAccessPath(const AccessPath& base, slang::VariableLayoutReflection* variable_layout) : AccessPath{ base }
        {
            if (!Valid)
            {
                return;
            }

            element.VarLayout = variable_layout;
            element.Outer = Leaf;
            Leaf = &element;
        }

        AccessPathNode element;
    };

    static std::string PrintSlangTypeKind(const slang::TypeReflection::Kind kind)
    {
        switch (kind)
        {
            case slang::TypeReflection::Kind::None: return "None";
            case slang::TypeReflection::Kind::Struct: return "Struct";
            case slang::TypeReflection::Kind::Array: return "Array";
            case slang::TypeReflection::Kind::Matrix: return "Matrix";
            case slang::TypeReflection::Kind::Vector: return "Vector";
            case slang::TypeReflection::Kind::Scalar: return "Scalar";
            case slang::TypeReflection::Kind::ConstantBuffer: return "ConstantBuffer";
            case slang::TypeReflection::Kind::Resource: return "Resource";
            case slang::TypeReflection::Kind::SamplerState: return "SamplerState";
            case slang::TypeReflection::Kind::TextureBuffer: return "TextureBuffer";
            case slang::TypeReflection::Kind::ShaderStorageBuffer: return "ShaderStorageBuffer";
            case slang::TypeReflection::Kind::ParameterBlock: return "ParameterBlock";
            case slang::TypeReflection::Kind::GenericTypeParameter: return "GenericTypeParameter";
            case slang::TypeReflection::Kind::Interface: return "Interface";
            case slang::TypeReflection::Kind::OutputStream: return "OutputStream";
            case slang::TypeReflection::Kind::Specialized: return "Specialized";
            case slang::TypeReflection::Kind::Feedback: return "Feedback";
            case slang::TypeReflection::Kind::Pointer: return "Pointer";
            case slang::TypeReflection::Kind::DynamicResource: return "DynamicResource";
            case slang::TypeReflection::Kind::MeshOutput: return "MeshOutput";
            default: return "Unknown";
        }
    }

    struct YamlBuilder
    {

        YamlBuilder(std::vector<SlangMetadataPtr>* entrypointMetadata = nullptr, SlangLayoutPtr programLayout = nullptr)
            : EntrypointMetadata(entrypointMetadata)
            , ProgramLayout(programLayout)
        {
        }

        std::string result;
        size_t indentation = 0;
        bool afterArrayElement = true;
        std::vector<SlangMetadataPtr>* EntrypointMetadata = nullptr;
        SlangLayoutPtr ProgramLayout = nullptr;

        struct CumulativeOffset
        {
            // the actual offset
            size_t Value = 0;
            // the binding space/location, I think?
            size_t Space = 0;
        };

        YamlBuilder& BeginObject()
        {
            indentation++;
            return *this;
        }

        YamlBuilder& BeginArray()
        {
            indentation++;
            return *this;
        }

        YamlBuilder& EndObject()
        {
            indentation--;
            return *this;
        }

        YamlBuilder& EndArray()
        {
            indentation--;
            return *this;
        }

        YamlBuilder& PrintIndentation()
        {
            result += std::string(indentation * 2, ' ');
            return *this;
        }

        YamlBuilder& NewLine()
        {
            result += "\n";
            return *this;
        }

        struct YamlScopedObject
        {
            YamlBuilder& builder;
            YamlScopedObject(YamlBuilder& b) : builder{ b } { builder.BeginObject(); }
            ~YamlScopedObject() { builder.EndObject(); }
        };

        struct YamlScopedArray
        {
            YamlBuilder& builder;
            YamlScopedArray(YamlBuilder& b) : builder{ b } { builder.BeginArray(); }
            ~YamlScopedArray() { builder.EndArray(); }
        };

        YamlBuilder& Key(std::string_view key)
        {
            if (!afterArrayElement)
            {
                NewLine();
            }
            afterArrayElement = false;
            auto keyStr = std::format("\"{}\" : ", key);
            result += keyStr;
            return *this;
        }

        YamlBuilder& ArrayElement()
        {
            NewLine();
            result += "- ";
            afterArrayElement = true;
            return *this;
        }

        YamlBuilder& PrintQuotedString(const char* str)
        {
            if (str != nullptr)
            {
                result += std::format("\"{}\"", str);
            }
            else
            {
                result += "\"null\"";
            }
            return *this;
        }

        YamlBuilder& PrintBool(bool value)
        {
            result += value ? "true" : "false";
            return *this;
        }

        YamlBuilder& PrintUint32(uint32_t value)
        {
            result += std::to_string(value);
            return *this;
        }

        YamlBuilder& PrintInt32(int32_t value)
        {
            result += std::to_string(value);
            return *this;
        }

        YamlBuilder& PrintUint64(size_t value)
        {
            result += std::to_string(value);
            return *this;
        }

        YamlBuilder& PrintInt64(int64_t value)
        {
            result += std::to_string(value);
            return *this;
        }

        YamlBuilder& PrintPossiblyUnbounded(size_t value)
        {
            if (value == ~size_t(0))
            {
                result += "Unbounded";
            }
            else
            {
                result += std::to_string(value);
            }
            return *this;
        }

        YamlBuilder& PrintComment(std::string_view comment)
        {
            result += std::format("# {}", comment);
            return *this;
        }

        YamlBuilder& PrintSlangTypeKind(slang::TypeReflection::Kind kind)
        {
            result += rhi::PrintSlangTypeKind(kind);
            return *this;
        }

        YamlBuilder& PrintResourceShape(SlangResourceShape shape)
        {
            switch (shape)
            {
                case SLANG_RESOURCE_NONE: result += "None"; break;
                case SLANG_TEXTURE_1D: result += "Texture1D"; break;
                case SLANG_TEXTURE_2D: result += "Texture2D"; break;
                case SLANG_TEXTURE_3D: result += "Texture3D"; break;
                case SLANG_TEXTURE_CUBE: result += "TextureCube"; break;
                case SLANG_TEXTURE_BUFFER: result += "TextureBuffer"; break;
                case SLANG_STRUCTURED_BUFFER: result += "StructuredBuffer"; break;
                case SLANG_BYTE_ADDRESS_BUFFER: result += "ByteAddressBuffer"; break;
                case SLANG_RESOURCE_UNKNOWN: result += "ResourceUnknown"; break;
                case SLANG_ACCELERATION_STRUCTURE: result += "AccelerationStructure"; break;
                case SLANG_TEXTURE_SUBPASS: result += "TextureSubpass"; break;
                case SLANG_TEXTURE_FEEDBACK_FLAG: result += "TextureFeedbackFlag"; break;
                case SLANG_TEXTURE_SHADOW_FLAG: result += "TextureShadowFlag"; break;
                case SLANG_TEXTURE_ARRAY_FLAG: result += "TextureArrayFlag"; break;
                case SLANG_TEXTURE_MULTISAMPLE_FLAG: result += "TextureMultisampleFlag"; break;
                case SLANG_TEXTURE_COMBINED_FLAG: result += "TextureCombinedFlag"; break;
                case SLANG_TEXTURE_1D_ARRAY: result += "Texture1DArray"; break;
                case SLANG_TEXTURE_2D_ARRAY: result += "Texture2DArray"; break;
                case SLANG_TEXTURE_CUBE_ARRAY: result += "TextureCubeArray"; break;
                case SLANG_TEXTURE_2D_MULTISAMPLE: result += "Texture2DMultisample"; break;
                case SLANG_TEXTURE_2D_MULTISAMPLE_ARRAY: result += "Texture2DMultisampleArray"; break;
                case SLANG_TEXTURE_SUBPASS_MULTISAMPLE: result += "TextureSubpassMultisample"; break;
                default: result += "Unknown"; break;
            }
            return *this;
        }

        YamlBuilder& PrintResourceAccess(SlangResourceAccess access)
        {
            switch (access)
            {
                case SLANG_RESOURCE_ACCESS_NONE: result += "None"; break;
                case SLANG_RESOURCE_ACCESS_READ: result += "Read"; break;
                case SLANG_RESOURCE_ACCESS_READ_WRITE: result += "ReadWrite"; break;
                case SLANG_RESOURCE_ACCESS_RASTER_ORDERED: result += "RasterOrdered"; break;
                case SLANG_RESOURCE_ACCESS_APPEND: result += "Append"; break;
                case SLANG_RESOURCE_ACCESS_CONSUME: result += "Consume"; break;
                case SLANG_RESOURCE_ACCESS_WRITE: result += "Write"; break;
                case SLANG_RESOURCE_ACCESS_FEEDBACK: result += "Feedback"; break;
                default: result += "Unknown"; break;
            }
            return *this;
        }

        YamlBuilder& PrintLayoutUnit(slang::ParameterCategory category)
        {
            switch (category)
            {
                case slang::ParameterCategory::None: result += "None"; break;
                case slang::ParameterCategory::Mixed: result += "Mixed"; break;
                case slang::ParameterCategory::ShaderResource: result += "ShaderResource"; break;
                case slang::ParameterCategory::UnorderedAccess: result += "UnorderedAccess"; break;
                case slang::ParameterCategory::VaryingInput: result += "VaryingInput"; break;
                case slang::ParameterCategory::VaryingOutput: result += "VaryingOutput"; break;
                case slang::ParameterCategory::SamplerState: result += "SamplerState"; break;
                case slang::ParameterCategory::Uniform: result += "Uniform"; break;
                case slang::ParameterCategory::DescriptorTableSlot: result += "DescriptorTableSlot"; break;
                case slang::ParameterCategory::SpecializationConstant: result += "SpecializationConstant"; break;
                case slang::ParameterCategory::PushConstantBuffer: result += "PushConstantBuffer"; break;
                case slang::ParameterCategory::RegisterSpace: result += "RegisterSpace"; break;
                case slang::ParameterCategory::GenericResource: result += "GenericResource"; break;
                case slang::ParameterCategory::RayPayload: result += "RayPayload"; break;
                case slang::ParameterCategory::HitAttributes: result += "HitAttributes"; break;
                case slang::ParameterCategory::CallablePayload: result += "CallablePayload"; break;
                case slang::ParameterCategory::ShaderRecord: result += "ShaderRecord"; break;
                case slang::ParameterCategory::ExistentialTypeParam: result += "ExistentialTypeParam"; break;
                case slang::ParameterCategory::ExistentialObjectParam: result += "ExistentialObjectParam"; break;
                case slang::ParameterCategory::SubElementRegisterSpace: result += "SubElementRegisterSpace"; break;
                case slang::ParameterCategory::InputAttachmentIndex: result += "InputAttachmentIndex"; break;
                default: result += "Unknown"; break;
            }
            return *this;
        }

        YamlBuilder& PrintScalarType(slang::TypeReflection::ScalarType scalar_type)
        {
            switch (scalar_type)
            {
                case slang::TypeReflection::ScalarType::None: result += "None"; break;
                case slang::TypeReflection::ScalarType::Void: result += "Void"; break;
                case slang::TypeReflection::ScalarType::Bool: result += "Bool"; break;
                case slang::TypeReflection::ScalarType::Int32: result += "Int32"; break;
                case slang::TypeReflection::ScalarType::UInt32: result += "UInt32"; break;
                case slang::TypeReflection::ScalarType::Int64: result += "Int64"; break;
                case slang::TypeReflection::ScalarType::UInt64: result += "UInt64"; break;
                case slang::TypeReflection::ScalarType::Float16: result += "Float16"; break;
                case slang::TypeReflection::ScalarType::Float32: result += "Float32"; break;
                case slang::TypeReflection::ScalarType::Float64: result += "Float64"; break;
                case slang::TypeReflection::ScalarType::Int8: result += "Int8"; break;
                case slang::TypeReflection::ScalarType::UInt8: result += "UInt8"; break;
                case slang::TypeReflection::ScalarType::Int16: result += "Int16"; break;
                case slang::TypeReflection::ScalarType::UInt16: result += "UInt16"; break;
                default: result += "Unknown"; break;
            }
            return *this;
        }

        YamlBuilder& PrintBindingType(slang::BindingType type)
        {
            switch (type)
            {
                case slang::BindingType::Sampler: result += "Sampler"; break;
                case slang::BindingType::Texture: result += "Texture"; break;
                case slang::BindingType::ConstantBuffer: result += "ConstantBuffer"; break;
                case slang::BindingType::ParameterBlock: result += "ParameterBlock"; break;
                case slang::BindingType::TypedBuffer: result += "TypedBuffer"; break;
                case slang::BindingType::RawBuffer: result += "RawBuffer"; break;
                case slang::BindingType::CombinedTextureSampler: result += "CombinedTextureSampler"; break;
                case slang::BindingType::InputRenderTarget: result += "InputRenderTarget"; break;
                case slang::BindingType::InlineUniformData: result += "InlineUniformData"; break;
                case slang::BindingType::RayTracingAccelerationStructure: result += "RayTracingAccelerationStructure"; break;
                case slang::BindingType::VaryingInput: result += "VaryingInput"; break;
                case slang::BindingType::VaryingOutput: result += "VaryingOutput"; break;
                case slang::BindingType::ExistentialValue: result += "ExistentialValue"; break;
                case slang::BindingType::PushConstant: result += "PushConstant"; break;
                default: result += "Unknown"; break;
            }
            return *this;
        }

        YamlBuilder& PrintShaderStageMask(ShaderStageFlags stage)
        {
            if (stage == ShaderStageFlags::None)
            {
                result += "None";
                return *this;
            }
            bool first = true;
            if ((stage & ShaderStageFlags::Vertex) != ShaderStageFlags::None)
            {
                result += "Vertex";
                first = false;
            }
            if ((stage & ShaderStageFlags::TesselationControl) != ShaderStageFlags::None)
            {
                if (!first) result += " | ";
                result += "TesselationControl";
                first = false;
            }
            if ((stage & ShaderStageFlags::TesselationEvaluation) != ShaderStageFlags::None)
            {
                if (!first) result += " | ";
                result += "TesselationEvaluation";
                first = false;
            }
            if ((stage & ShaderStageFlags::Geometry) != ShaderStageFlags::None)
            {
                if (!first) result += " | ";
                result += "Geometry";
                first = false;
            }
            if ((stage & ShaderStageFlags::Fragment) != ShaderStageFlags::None)
            {
                if (!first) result += " | ";
                result += "Fragment";
                first = false;
            }
            if ((stage & ShaderStageFlags::Compute) != ShaderStageFlags::None)
            {
                if (!first) result += " | ";
                result += "Compute";
                first = false;
            }
            if ((stage & ShaderStageFlags::RayGeneration) != ShaderStageFlags::None)
            {
                if (!first) result += " | ";
                result += "RayGeneration";
                first = false;
            }
            if ((stage & ShaderStageFlags::AnyHit) != ShaderStageFlags::None)
            {
                if (!first) result += " | ";
                result += "AnyHit";
                first = false;
            }
            if ((stage & ShaderStageFlags::ClosestHit) != ShaderStageFlags::None)
            {
                if (!first) result += " | ";
                result += "ClosestHit";
                first = false;
            }
            if ((stage & ShaderStageFlags::Miss) != ShaderStageFlags::None)
            {
                if (!first) result += " | ";
                result += "Miss";
                first = false;
            }
            if ((stage & ShaderStageFlags::Intersection) != ShaderStageFlags::None)
            {
                if (!first) result += " | ";
                result += "Intersection";
                first = false;
            }

            // insert quotes at beginning and end since this is a string value in yaml
            result = "\"" + result + "\"";
            return *this;
        }

        void PrintVariable(slang::VariableReflection* variable)
        {
            YamlScopedObject(*this);
            const char* name = variable->getName();
            slang::TypeReflection* type = variable->getType();

            Key("Name").PrintQuotedString(name);
            Key("Type").PrintSlangType(type);
            int64_t value;
            if (SLANG_SUCCEEDED(variable->getDefaultValueInt(&value)))
            {
                Key("Value").PrintInt64(value);
            }

        }

        void PrintSlangType(slang::TypeReflection* type)
        {
            YamlScopedObject(*this);
            const char* name = type->getName();
            slang::TypeReflection::Kind kind = type->getKind();
            Key("Name").PrintQuotedString(name);
            Key("Kind").PrintSlangTypeKind(kind);
            PrintCommonTypeInfo(type);

            switch (type->getKind())
            {
                case slang::TypeReflection::Kind::Struct:
                {
                    unsigned int fieldCount = type->getFieldCount();
                    Key("Fields").PrintUint32(static_cast<uint32_t>(fieldCount));
                    {
                        YamlScopedArray array(*this);
                        for (unsigned int i = 0; i < fieldCount; i++)
                        {
                            ArrayElement(); // start new array element
                            slang::VariableReflection* field = type->getFieldByIndex(i);
                            PrintVariable(field);
                        }
                    }
                    break;
                }
                case slang::TypeReflection::Kind::Array:
                    [[fallthrough]];
                case slang::TypeReflection::Kind::Vector:
                    [[fallthrough]];
                case slang::TypeReflection::Kind::Matrix:
                {
                    slang::TypeReflection* elementType = type->getElementType();
                    Key("ElementType").PrintSlangType(elementType);
                    break;
                }
                case slang::TypeReflection::Kind::Resource:
                {
                    Key("ResultType").PrintSlangType(type->getResourceResultType());
                    break;
                }
                case slang::TypeReflection::Kind::ConstantBuffer:
                    [[fallthrough]];
                case slang::TypeReflection::Kind::ParameterBlock:
                    [[fallthrough]];
                case slang::TypeReflection::Kind::TextureBuffer:
                    [[fallthrough]];
                case slang::TypeReflection::Kind::ShaderStorageBuffer:
                {
                    Key("ElementType").PrintSlangType(type->getElementType());
                    break;
                }
                default:
                    break;
            }
        }

        void PrintCommonTypeInfo(slang::TypeReflection* type)
        {
            switch (type->getKind())
            {
            case slang::TypeReflection::Kind::Scalar:
            {
                slang::TypeReflection::ScalarType scalar_type = type->getScalarType();
                Key("ScalarType").PrintScalarType(scalar_type);
                break;
            }
            case slang::TypeReflection::Kind::Array:
            {
                slang::TypeReflection* elementType = type->getElementType();
                size_t elementCount = type->getElementCount();
                Key("ElementCount").PrintPossiblyUnbounded(elementCount);
                break;
            }
            case slang::TypeReflection::Kind::Vector:
            {
                slang::TypeReflection* elementType = type->getElementType();
                size_t elementCount = type->getElementCount();
                Key("ElementCount").PrintUint32(static_cast<uint32_t>(elementCount));
                break;
            }
            case slang::TypeReflection::Kind::Matrix:
            {
                slang::TypeReflection* elementType = type->getElementType();
                size_t rowCount = type->getRowCount();
                size_t columnCount = type->getColumnCount();
                Key("RowCount").PrintUint32(static_cast<uint32_t>(rowCount));
                Key("ColumnCount").PrintUint32(static_cast<uint32_t>(columnCount));
                break;
            }
            case slang::TypeReflection::Kind::Resource:
            {
                SlangResourceShape shape = type->getResourceShape();
                SlangResourceAccess access = type->getResourceAccess();
                Key("Shape").PrintResourceShape(shape);
                Key("Access").PrintResourceAccess(access);
                break;
            }
            default:
                break;
            }
        }

        void PrintOffset(slang::ParameterCategory layout_unit, size_t offset, size_t space_offset)
        {
            YamlScopedObject(*this);
            Key("Value").PrintUint64(offset);
            Key("Unit").PrintLayoutUnit(layout_unit);
            switch (layout_unit)
            {
            case slang::ParameterCategory::ConstantBuffer:
                [[fallthrough]];
            case slang::ParameterCategory::ShaderResource:
                [[fallthrough]];
            case slang::ParameterCategory::UnorderedAccess:
                [[fallthrough]];
            case slang::ParameterCategory::SamplerState:
                [[fallthrough]];
            case slang::ParameterCategory::DescriptorTableSlot:
                Key("BindingSpace").PrintUint64(space_offset);
                break;
            default:
                break;
            }
        }

        void PrintOffset(slang::VariableLayoutReflection* variable_layout, slang::ParameterCategory layout_unit)
        {
            PrintOffset(layout_unit, variable_layout->getOffset(layout_unit), variable_layout->getBindingSpace(layout_unit));
        }

        void PrintRelativeOffsets(slang::VariableLayoutReflection* variable_layout)
        {
            Key("RelativeOffsets");
            int usedLayoutUnitCount = variable_layout->getCategoryCount();
            {
                YamlScopedArray array(*this);
                for (int i = 0; i < usedLayoutUnitCount; i++)
                {
                    ArrayElement(); // start new array element
                    slang::ParameterCategory category = variable_layout->getCategoryByIndex(i);
                    
                }
            }
        }

        CumulativeOffset CalculateCumulativeOffset(slang::ParameterCategory layout_unit, AccessPath access_path)
        {
            CumulativeOffset result = {};
            switch (layout_unit)
            {
            case slang::ParameterCategory::Uniform:
                for (auto node = access_path.Leaf; node != access_path.DeepestConstantBuffer; node = node->Outer)
                {
                    result.Value += node->VarLayout->getOffset(layout_unit);
                }
                break;
            case slang::ParameterCategory::ConstantBuffer:
                [[fallthrough]];
            case slang::ParameterCategory::ShaderResource:
                [[fallthrough]];
            case slang::ParameterCategory::UnorderedAccess:
                [[fallthrough]];
            case slang::ParameterCategory::SamplerState:
                [[fallthrough]];
            case slang::ParameterCategory::DescriptorTableSlot:
                // accumulate offsets for bound resources
                for (auto node = access_path.Leaf; node != access_path.DeepestParameterBlock; node = node->Outer)
                {
                    result.Value += node->VarLayout->getOffset(layout_unit);
                    result.Space += node->VarLayout->getBindingSpace(layout_unit);
                }
                for (auto node = access_path.DeepestParameterBlock; node != nullptr; node = node->Outer)
                {
                    result.Space += node->VarLayout->getOffset(slang::ParameterCategory::SubElementRegisterSpace);
                }
                break;
            default:
                for (auto node = access_path.Leaf; node != nullptr; node = node->Outer)
                {
                    result.Value += node->VarLayout->getOffset(layout_unit);
                }
                break;
            }
            return result;
        }

        CumulativeOffset CalculateCumulativeOffset(slang::VariableLayoutReflection* variable_layout, slang::ParameterCategory layout_unit, AccessPath access_path)
        {
            CumulativeOffset result = CalculateCumulativeOffset(layout_unit, access_path);
            result.Value = variable_layout->getOffset(layout_unit);
            result.Space = variable_layout->getBindingSpace(layout_unit);
            return result;
        }

        void PrintCumulativeOffset(slang::VariableLayoutReflection* variable_layout, slang::ParameterCategory layout_unit, AccessPath access_path)
        {
            CumulativeOffset offset = CalculateCumulativeOffset(variable_layout, layout_unit, access_path);
            PrintOffset(layout_unit, offset.Value, offset.Space);
        }

        void PrintCumulativeOffsets(slang::VariableLayoutReflection* variable_layout, AccessPath access_path)
        {
            Key("CumulativeOffsets");
            int usedLayoutUnitCount = variable_layout->getCategoryCount();
            {
                YamlScopedArray array(*this);
                for (int i = 0; i < usedLayoutUnitCount; i++)
                {
                    ArrayElement(); // start new array element
                    slang::ParameterCategory category = variable_layout->getCategoryByIndex(i);
                    
                }
            }
        }

        ShaderStageFlags CalculateParameterStageMask(slang::ParameterCategory layout_unit, CumulativeOffset offset)
        {
            ShaderStageFlags stageMask = ShaderStageFlags::None;
            auto entryPointCount = EntrypointMetadata->size();
            for (size_t i = 0; i < entryPointCount; ++i)
            {
                bool isUsed = false;
                auto metadata = (*EntrypointMetadata)[i];
                metadata->isParameterLocationUsed(SlangParameterCategory(layout_unit), uint32_t(offset.Value), uint32_t(offset.Space), isUsed);
                if (isUsed)
                {
                    SlangStage entryPointStage = ProgramLayout->getEntryPointByIndex(SlangUInt(i))->getStage();
                    stageMask |= FromSlangStage(entryPointStage);
                }
            }
            return stageMask;
        }

        ShaderStageFlags CalculateStageMask(slang::VariableLayoutReflection* variable_layout, AccessPath access_path)
        {
            ShaderStageFlags stageMask = ShaderStageFlags::None;
            unsigned int usedLayoutUnitCount = variable_layout->getCategoryCount();
            for (unsigned int i = 0; i < usedLayoutUnitCount; ++i)
            {
                auto layoutUnit = variable_layout->getCategoryByIndex(i);
                auto offset = CalculateCumulativeOffset(variable_layout, layoutUnit, access_path);
                stageMask |= CalculateParameterStageMask(layoutUnit, offset);
            }
            return stageMask;
        }

        void PrintStageUsage(slang::VariableLayoutReflection* variable_layout, AccessPath access_path)
        {
            ShaderStageFlags stageMask = CalculateStageMask(variable_layout, access_path);
            Key("StageUsage").PrintShaderStageMask(stageMask);
        }

        void PrintStageSpecificInfo(slang::EntryPointReflection* entry_point_layout)
        {
            switch (entry_point_layout->getStage())
            {
            case SLANG_STAGE_COMPUTE:
            {
                constexpr static size_t kAxisCount = 3u;
                SlangUInt sizes[kAxisCount];
                entry_point_layout->getComputeThreadGroupSize(kAxisCount, sizes);
                Key("ComputeThreadGroupSize");
                {
                    YamlScopedObject obj(*this);
                    Key("X").PrintUint64(sizes[0]);
                    Key("Y").PrintUint64(sizes[1]);
                    Key("Z").PrintUint64(sizes[2]);
                }
                break;
            }
            case SLANG_STAGE_FRAGMENT:
            {
                Key("UsesAnySampleRateInputs").PrintBool(entry_point_layout->usesAnySampleRateInput());
                break;
            }
            default:
                break;
            }
        }

        void PrintOffsets(slang::VariableLayoutReflection* variable_layout, AccessPath access_path)
        {
            Key("Offset");
            {
                YamlScopedObject obj(*this);
                PrintRelativeOffsets(variable_layout);
                if (access_path.Valid)
                {
                    PrintCumulativeOffsets(variable_layout, access_path);
                }
            }

            if (access_path.Valid)
            {
                PrintStageUsage(variable_layout, access_path);
            }
        }

        void PrintSize(slang::ParameterCategory layout_unit, size_t size)
        {
            YamlScopedObject(*this);
            Key("Value").PrintPossiblyUnbounded(size);
            Key("Unit").PrintLayoutUnit(layout_unit);
        }

        void PrintSize(slang::TypeLayoutReflection* type_layout, slang::ParameterCategory layout_unit)
        {
            PrintSize(layout_unit, type_layout->getSize(layout_unit));
        }

        void PrintSizes(slang::TypeLayoutReflection* type_layout)
        {
            Key("Size");
            unsigned int usedLayoutUnitCount = type_layout->getCategoryCount();
            {
                YamlScopedArray obj(*this);
                for (unsigned int i = 0; i < usedLayoutUnitCount; ++i)
                {
                    ArrayElement();
                    slang::ParameterCategory layout_unit = type_layout->getCategoryByIndex(i);
                    PrintSize(type_layout, layout_unit);
                }
            }

            if (type_layout->getSize() != 0)
            {
                Key("ByteAlignment").PrintInt32(type_layout->getAlignment());
                Key("ByteStride").PrintUint64(type_layout->getStride());
            }
        }

        void PrintVaryingParameterInfo(slang::VariableLayoutReflection* variable_layout)
        {
            if (auto semanticName = variable_layout->getSemanticName())
            {
                Key("Semantic");
                YamlScopedObject obj(*this);
                Key("Name").PrintQuotedString(semanticName);
                Key("Index").PrintUint64(variable_layout->getSemanticIndex());
            }
        }

        void PrintKindSpecificInfo(slang::TypeLayoutReflection* type_layout, AccessPath access_path)
        {
            switch (type_layout->getKind())
            {
            case slang::TypeReflection::Kind::Struct:
            {
                Key("Fields");
                unsigned int fieldCount = type_layout->getFieldCount();
                {
                    YamlScopedArray array(*this);
                    for (unsigned int i = 0; i < fieldCount; i++)
                    {
                        ArrayElement(); // start new array element
                        slang::VariableLayoutReflection* field = type_layout->getFieldByIndex(i);
                        PrintVariableLayout(field, access_path);
                    }
                }
                break;
            }
            case slang::TypeReflection::Kind::Array:
                [[fallthrough]];
            case slang::TypeReflection::Kind::Matrix:
                [[fallthrough]];
            case slang::TypeReflection::Kind::Vector:
            {
                Key("ElementTypeLayout");
                PrintTypeLayout(type_layout->getElementTypeLayout(), access_path);
                break;
            }
            case slang::TypeReflection::Kind::ConstantBuffer:
                [[fallthrough]];
            case slang::TypeReflection::Kind::ParameterBlock:
                [[fallthrough]];
            case slang::TypeReflection::Kind::TextureBuffer:
                [[fallthrough]];
            case slang::TypeReflection::Kind::ShaderStorageBuffer:
            {
                slang::VariableLayoutReflection* containerVarLayout = type_layout->getContainerVarLayout();
                slang::VariableLayoutReflection* elementVarLayout = type_layout->getElementVarLayout();
                AccessPath innerOffsets = access_path;
                innerOffsets.DeepestConstantBuffer = innerOffsets.Leaf;
                if (containerVarLayout->getTypeLayout()->getSize(slang::ParameterCategory::SubElementRegisterSpace) != 0)
                {
                    innerOffsets.DeepestParameterBlock = innerOffsets.Leaf;
                }

                Key("Container");
                {
                    YamlScopedObject obj(*this);
                    PrintOffsets(containerVarLayout, innerOffsets);
                }

                Key("Contents");
                {
                    YamlScopedObject obj(*this);
                    PrintOffsets(elementVarLayout, innerOffsets);
                    ExtendedAccessPath elementOffsets(innerOffsets, elementVarLayout);
                    Key("TypeLayout");
                    PrintTypeLayout(elementVarLayout->getTypeLayout(), elementOffsets);
                }

                break;
            }
            case slang::TypeReflection::Kind::Resource:
            {
                if ((type_layout->getResourceShape() & SLANG_RESOURCE_BASE_SHAPE_MASK) == SLANG_STRUCTURED_BUFFER)
                {
                    Key("ElementTypeLayout");
                    PrintTypeLayout(type_layout->getElementTypeLayout(), access_path);
                }
                else
                {
                    Key("ResultTypeLayout");
                    PrintSlangType(type_layout->getResourceResultType());
                }
                break;
            }
            default:
                break;
            }
        }

        void PrintTypeLayout(slang::TypeLayoutReflection* type_layout, AccessPath access_path)
        {
            YamlScopedObject(*this);
            const char* type_name = type_layout->getName();
            Key("Name").PrintQuotedString(type_name);
            Key("Kind").PrintSlangTypeKind(type_layout->getKind());
            PrintCommonTypeInfo(type_layout->getType());
            PrintSizes(type_layout);
            PrintKindSpecificInfo(type_layout, access_path);
        }

        void PrintVariableLayout(slang::VariableLayoutReflection* variable_layout, AccessPath access_path)
        {
            YamlScopedObject(*this);
            const char* name = variable_layout->getName();
            Key("Name").PrintQuotedString(name);
            PrintOffsets(variable_layout, access_path);
            PrintVaryingParameterInfo(variable_layout);
            ExtendedAccessPath variable_path(access_path, variable_layout);
            Key("TypeLayout");
            PrintTypeLayout(variable_layout->getTypeLayout(), variable_path);
        }

        void PrintScope(slang::VariableLayoutReflection* variable_layout, AccessPath access_path)
        {
            ExtendedAccessPath scopeOffsets(access_path, variable_layout);
            auto type_layout = variable_layout->getTypeLayout();
            switch (type_layout->getKind())
            {
            case slang::TypeReflection::Kind::Struct:
            {
                Key("Fields");
                unsigned int fieldCount = type_layout->getFieldCount();
                for (unsigned int i = 0; i < fieldCount; i++)
                {
                    ArrayElement();
                    slang::VariableLayoutReflection* field = type_layout->getFieldByIndex(i);
                    PrintVariableLayout(field, scopeOffsets);
                }
                break;
            }
            case slang::TypeReflection::Kind::ConstantBuffer:
            {
                PrintComment("This seems to represent a constant buffer automatically introduced by Slang for global-scope variables");
                Key("AutomaticallyIntroducedConstantBuffer");
                {
                    YamlScopedObject obj(*this);
                    PrintOffsets(type_layout->getContainerVarLayout(), scopeOffsets);
                }
                PrintScope(type_layout->getElementVarLayout(), scopeOffsets);
                break;
            }
            case slang::TypeReflection::Kind::ParameterBlock:
            {
                PrintComment("This seems to represent a parameter block automatically introduced by Slang for global-scope variables");
                Key("AutomaticallyIntroducedParameterBlock");
                {
                    YamlScopedObject obj(*this);
                    PrintOffsets(type_layout->getContainerVarLayout(), scopeOffsets);
                }
                PrintScope(type_layout->getElementVarLayout(), scopeOffsets);
                break;
            }
            default:
                Key("VariableLayout");
                PrintVariableLayout(variable_layout, access_path);
                break;
            }
        }

        void PrintEntryPointLayout(slang::EntryPointReflection* entry_point_layout, AccessPath access_path)
        {
            YamlScopedObject(*this);
            Key("Stage").PrintShaderStageMask(FromSlangStage(entry_point_layout->getStage()));
            PrintStageSpecificInfo(entry_point_layout);
            PrintScope(entry_point_layout->getVarLayout(), access_path);
            auto result_variable_layout = entry_point_layout->getResultVarLayout();
            if (result_variable_layout->getTypeLayout()->getKind() != slang::TypeReflection::Kind::None)
            {
                Key("Result");
                PrintVariableLayout(result_variable_layout, access_path);
            }
        }

        void PrintProgramLayout()
        {
            if (!ProgramLayout)
            {
                return;
            }

            YamlScopedObject topLevel(*this);
            AccessPath rootOffsets{};
            rootOffsets.Valid = true;

            Key("GlobalScope");
            {
                YamlScopedObject obj(*this);
                PrintScope(ProgramLayout->getGlobalParamsVarLayout(), rootOffsets);
            }

            Key("EntryPoints");
            {
                YamlScopedArray array(*this);
                auto entryPointCount = ProgramLayout->getEntryPointCount();
                for (SlangUInt i = 0; i < entryPointCount; ++i)
                {
                    ArrayElement(); // start new array element
                    slang::EntryPointReflection* entry_point_layout = ProgramLayout->getEntryPointByIndex(i);
                    PrintEntryPointLayout(entry_point_layout, rootOffsets);
                }
            }
        }

    };


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
            CompilationLog{},
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
        SlangProgramPtr LinkedProgram;
        // Program layout ptr is not reference counted, so we need to keep the program alive
        SlangLayoutPtr ProgramLayout;
        std::vector<SlangMetadataPtr> slangMetadata;

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

        bool CompileShaderFromSlang(const ShaderObject::CompileOptions& options)
        {
            try
            {
                // Read source file
                if (!std::filesystem::exists(options.SlangSourcePath))
                {
                    CompilationLog = std::format("Slang source file not found: {}", options.SlangSourcePath.string());
                    return false;
                }

                std::ifstream file(options.SlangSourcePath, std::ios::binary | std::ios::ate);
                if (!file.is_open())
                {
                    CompilationLog = std::format("Failed to open Slang source file: {}", options.SlangSourcePath.string());
                    return false;
                }

                std::streampos fileSize = file.tellg(); // Get current position (file size)
                file.seekg(0); // Seek back to the beginning

                std::string sourceCode;
                sourceCode.resize(static_cast<size_t>(fileSize));
                file.read(sourceCode.data(), fileSize);

                if (sourceCode.empty())
                {
                    CompilationLog = std::format("Failed to read Slang source file: {}", options.SlangSourcePath.string());
                    return false;
                }

                // Initialize Slang
                if (!SlangGlobalSession)
                {
                    // attempt to create global session
                    SlangResult result = slang::createGlobalSession(SlangGlobalSession.writeRef());
                    if (result != SLANG_OK)
                    {
                        CompilationLog = "Failed to create Slang global session";
                        return false;
                    }
                }

                // Will eventually cache this and make it more reusable, but for now this gets us running
                slang::SessionDesc sessionDesc = {};
                sessionDesc.searchPaths = options.SearchPaths.data();
                sessionDesc.searchPathCount = static_cast<SlangInt>(options.SearchPaths.size());
                sessionDesc.allowGLSLSyntax = true; // We write with GLSL-like syntax in many cases

                // Create compilation target
                slang::TargetDesc targetDesc = {};
                if (options.target == "spirv")
                {
                    targetDesc.format = SLANG_SPIRV;
                    targetDesc.profile = SlangGlobalSession->findProfile("spirv_1_5");
                }
                else if (options.target == "dxil")
                {
                    targetDesc.format = SLANG_DXIL;
                    targetDesc.profile = SlangGlobalSession->findProfile("sm_6_6");
                }
                else
                {
                    CompilationLog = std::format("Unsupported compilation target: {}", options.target);
                    SlangGlobalSession->release();
                    return false;
                }

                sessionDesc.targets = &targetDesc;
                sessionDesc.targetCount = 1;

                // Add compiler options
                std::vector<slang::CompilerOptionEntry> compilerOptions;
                
                if (options.enableOptimizations)
                {
                    slang::CompilerOptionEntry opt;
                    opt.name = slang::CompilerOptionName::Optimization;
                    opt.value.intValue0 = SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
                    compilerOptions.push_back(opt);
                }

                if (options.enableDebugInfo)
                {
                    slang::CompilerOptionEntry debug;
                    debug.name = slang::CompilerOptionName::DebugInformation;
                    debug.value.intValue0 = SLANG_DEBUG_INFO_LEVEL_MAXIMAL;
                    compilerOptions.push_back(debug);
                }

#ifdef RHI_SYSTEM_USE_VULKAN
                slang::CompilerOptionEntry vulkanOption;
                vulkanOption.name = slang::CompilerOptionName::VulkanEmitReflection;
                vulkanOption.value.intValue0 = 1; // Enable reflection
                compilerOptions.push_back(vulkanOption);
                vulkanOption.name = slang::CompilerOptionName::EmitSpirvDirectly;
                vulkanOption.value.intValue0 = 1; // Emit SPIR-V directly, so we can use it as-is
                compilerOptions.push_back(vulkanOption);
#endif

                sessionDesc.compilerOptionEntries = compilerOptions.data();
                sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(compilerOptions.size());

                // Create session
                Slang::ComPtr<slang::ISession> resultSession;
                SlangResult result = SlangGlobalSession->createSession(sessionDesc, resultSession.writeRef());
                if (SLANG_FAILED(result) || !resultSession)
                {
                    CompilationLog = "Failed to create Slang session";
                    SlangGlobalSession->release();
                    return false;
                }

                std::vector<SlangModulePtr> loadedModules;
                for (const char* moduleName : options.ModuleNames)
                {
                    SlangModulePtr module;
                    module = resultSession->loadModule(moduleName);
                    if (SLANG_FAILED(result) || !module)
                    {
                        CompilationLog = std::format("Failed to load Slang module: {}", moduleName);
                        for (auto& mod : loadedModules) mod->release();
                        resultSession->release();
                        SlangGlobalSession->release();
                        return false;
                    }
                    loadedModules.push_back(module);
                }

                // Now load the main module from given source path
                SlangModulePtr sourceModule;
                std::filesystem::path absolutePath = std::filesystem::absolute(options.SlangSourcePath);
                std::string filePath = absolutePath.string();
                SlangBlobPtr diagnosticsBlob;
                sourceModule = resultSession->loadModuleFromSourceString(
                    options.SlangSourcePath.filename().string().c_str(),
                    filePath.c_str(),
                    sourceCode.c_str(),
                    diagnosticsBlob.writeRef());

                if (!sourceModule)
                {
                    CompilationLog = "Failed to load Slang module from source";
                    if (diagnosticsBlob != nullptr)
                    {
                        const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
#ifdef WIN32
                        win32_OutputDebugString(diagStr);
#endif
                        std::string diagString = std::format("Slang Diagnostics: {}\n", diagStr);
                        CompilationLog += "\n" + diagString;
                    }
                    resultSession->release();
                    SlangGlobalSession->release();
                    return false;
                }

                // Create entry point
                Slang::ComPtr<slang::IEntryPoint> entryPoint;
                SlangResult entryPointResult = sourceModule->findEntryPointByName(options.EntryPointName.c_str(), entryPoint.writeRef());
                if (SLANG_FAILED(entryPointResult) || !entryPoint)
                {
                    CompilationLog = std::format("Entry point '{}' not found in Slang module", options.EntryPointName);
                    resultSession->release();
                    SlangGlobalSession->release();
                    return false;
                }

                // Create component types and compile
                std::vector<slang::IComponentType*> componentTypes = { sourceModule, entryPoint };
                // add any additional loaded modules
                for (const auto& mod : loadedModules)
                {
                    componentTypes.push_back(mod);
                }

                Slang::ComPtr<slang::IComponentType> composedProgram;
                SlangResult composeResult = resultSession->createCompositeComponentType(
                    componentTypes.data(),
                    componentTypes.size(),
                    composedProgram.writeRef(),
                    diagnosticsBlob.writeRef());

                if (SLANG_FAILED(composeResult) || !composedProgram)
                {
                    CompilationLog = "Failed to create composed Slang program";
                    if (diagnosticsBlob != nullptr)
                    {
                        const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
#ifdef WIN32
                        win32_OutputDebugString(diagStr);
#endif
                        std::string diagString = std::format("Slang Diagnostics: {}\n", diagStr);
                        CompilationLog += "\n" + diagString;
                    }
                    resultSession->release();
                    SlangGlobalSession->release();
                    return false;
                }

                SlangResult linkResult = composedProgram->link(
                    LinkedProgram.writeRef(),
                    diagnosticsBlob.writeRef());

                if (SLANG_FAILED(linkResult) || !LinkedProgram)
                {
                    CompilationLog = "Failed to link Slang program";
                    if (diagnosticsBlob != nullptr)
                    {
                        const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
#ifdef WIN32
                        win32_OutputDebugString(diagStr);
#endif
                        std::string diagString = std::format("Slang Diagnostics: {}\n", diagStr);
                        CompilationLog += "\n" + diagString;
                    }
                    resultSession->release();
                    SlangGlobalSession->release();
                    return false;
                }

                // Get bytecode now
                Slang::ComPtr<slang::IBlob> codeBlob;
                SlangResult getCodeResult = LinkedProgram->getEntryPointCode(0, 0, codeBlob.writeRef(), diagnosticsBlob.writeRef());
                if (SLANG_FAILED(getCodeResult) || !codeBlob)
                {
                    CompilationLog = "Failed to get Slang entry point bytecode";
                    if (diagnosticsBlob != nullptr)
                    {
                        const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
#ifdef WIN32
                        win32_OutputDebugString(diagStr);
#endif
                        std::string diagString = std::format("Slang Diagnostics: {}\n", diagStr);
                        CompilationLog += "\n" + diagString;
                    }
                    LinkedProgram->release();
                    resultSession->release();
                    SlangGlobalSession->release();
                    return false;
                }

                size_t codeSize = codeBlob->getBufferSize();
                if (codeSize % 4 != 0)
                {
                    CompilationLog = "Slang bytecode size is not a multiple of 4";
                    LinkedProgram->release();
                    resultSession->release();
                    SlangGlobalSession->release();
                    return false;
                }
                else
                {
                    Bytecode.resize(codeSize);
                    std::memcpy(Bytecode.data(), codeBlob->getBufferPointer(), codeSize);
                }

                // Store diagnostics even on success (warnings, etc.)
                if (diagnosticsBlob)
                {
                    CompilationLog = std::string(static_cast<const char*>(diagnosticsBlob->getBufferPointer()),
                        diagnosticsBlob->getBufferSize());
                    diagnosticsBlob->release();
                }

                // only ever one entry point for now, and target index is always zero
                // TODO: extend to multiple entry points and varying target indices
                CollectEntryPointMetadata(1);

                YamlBuilder yamlPrinter(&slangMetadata, LinkedProgram->getLayout());
                yamlPrinter.PrintProgramLayout();
                // dump generated YAML to current directory for inspection
                std::ofstream yamlFile("SlangReflectionOutput.yaml");
                assert(yamlFile.is_open());
                yamlFile << yamlPrinter.result;
                yamlFile.close();

                // Store compilation info
                EntryPointName = options.EntryPointName;
                SourcePath = options.SlangSourcePath;
                Stage = options.Stage;

                // Cleanup
                LinkedProgram->release();
                resultSession->release();
                SlangGlobalSession->release();

                return true;
            }
            catch (const std::exception& e)
            {
                CompilationLog = std::format("Exception during Slang compilation: {}", e.what());
                return false;
            }
        }

        /**
         * Slang reflection scopes/layouts from the example:
         * - global layout done at the top level
         *    - seems to include various global scope resources? ones shared between stages I believe
         * - entry point layout for each entry point
         *    - to be discovered
         * 
         * plan: get basic printing working so I can see how this works. will need to try with a complex example like those in VolumetriceTiledForward subfolder
         * also need to assess how we specialize, especially looking at dx12 side of things since that doesn't have specialization constants (could just be a #define?)
         * 
         */
        void CollectEntryPointMetadata(size_t entryPointCount, size_t targetIndex = 0)
        {
            for (size_t i = 0; i < entryPointCount; ++i)
            {
                Slang::ComPtr<slang::IMetadata> metadata;
                Slang::ComPtr<slang::IBlob> diagnostics;
                SlangResult result = LinkedProgram->getEntryPointMetadata(i, targetIndex, metadata.writeRef(), diagnostics.writeRef());
                if (SLANG_FAILED(result) || !metadata)
                {
                    if (diagnostics)
                    {
                        const char* diagStr = static_cast<const char*>(diagnostics->getBufferPointer());
#ifdef WIN32
                        win32_OutputDebugString(diagStr);
#endif
                        std::string diagString = std::format("Slang Metadata Diagnostics: {}\n", diagStr);
                        CompilationLog += "\n" + diagString;
                    }
                    continue;
                }
                slangMetadata.emplace_back(metadata);
            }
        }

        void ExtractReflectionData()
        {
            // Get reflection layout
            SlangLayoutPtr ProgramLayout = LinkedProgram->getLayout();
            if (!ProgramLayout)
            {
                return;
            }
            
            // to understand and learn this reflection system, we're just printing data first
            std::string reflectionStr;
        }

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
            
            // Compile shader from Slang source
            if (!impl->CompileShaderFromSlang(options))
            {
                return Result(Result::Code::InitializationFailed);
            }

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