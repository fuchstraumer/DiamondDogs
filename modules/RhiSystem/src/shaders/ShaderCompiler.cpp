#include "ShaderCompiler.hpp"
#include "ShaderCompilerReply.hpp"
#include "ShaderCompilerMessages.hpp"
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
#include <unordered_map>
#include <mutex>
#include <algorithm>

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
    // Compile-time toggle for YAML reflection output (debug only)
    #if defined(_DEBUG) || !defined(NDEBUG)
    constexpr static bool SHADER_COMPILER_ENABLE_YAML_REFLECTION = true;
    #else
    constexpr static bool SHADER_COMPILER_ENABLE_YAML_REFLECTION = false;
    #endif

    using SlangModulePtr = Slang::ComPtr<slang::IModule>;
    using SlangBlobPtr = Slang::ComPtr<slang::IBlob>;
    using SlangProgramPtr = Slang::ComPtr<slang::IComponentType>;
    using SlangLayoutPtr = slang::ProgramLayout*;
    using SlangMetadataPtr = Slang::ComPtr<slang::IMetadata>;

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
            for (int i = 1; i < indentation; ++i)
            {
                result += "  ";
            }
            return *this;
        }

        YamlBuilder& NewLine()
        {
            result += "\n";
            PrintIndentation();
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
            result += std::format("{}: ", key);
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
                result += "null";
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
            std::string stageStr;
            if (stage == ShaderStageFlags::None)
            {
                result += "None";
                return *this;
            }
            bool first = true;
            if ((stage & ShaderStageFlags::Vertex) != ShaderStageFlags::None)
            {
                stageStr += "Vertex";
                first = false;
            }
            if ((stage & ShaderStageFlags::TesselationControl) != ShaderStageFlags::None)
            {
                if (!first) stageStr += " | ";
                stageStr += "TesselationControl";
                first = false;
            }
            if ((stage & ShaderStageFlags::TesselationEvaluation) != ShaderStageFlags::None)
            {
                if (!first) stageStr += " | ";
                stageStr += "TesselationEvaluation";
                first = false;
            }
            if ((stage & ShaderStageFlags::Geometry) != ShaderStageFlags::None)
            {
                if (!first) stageStr += " | ";
                stageStr += "Geometry";
                first = false;
            }
            if ((stage & ShaderStageFlags::Fragment) != ShaderStageFlags::None)
            {
                if (!first) stageStr += " | ";
                stageStr += "Fragment";
                first = false;
            }
            if ((stage & ShaderStageFlags::Compute) != ShaderStageFlags::None)
            {
                if (!first) stageStr += " | ";
                stageStr += "Compute";
                first = false;
            }
            if ((stage & ShaderStageFlags::RayGeneration) != ShaderStageFlags::None)
            {
                if (!first) stageStr += " | ";
                stageStr += "RayGeneration";
                first = false;
            }
            if ((stage & ShaderStageFlags::AnyHit) != ShaderStageFlags::None)
            {
                if (!first) stageStr += " | ";
                stageStr += "AnyHit";
                first = false;
            }
            if ((stage & ShaderStageFlags::ClosestHit) != ShaderStageFlags::None)
            {
                if (!first) stageStr += " | ";
                stageStr += "ClosestHit";
                first = false;
            }
            if ((stage & ShaderStageFlags::Miss) != ShaderStageFlags::None)
            {
                if (!first) stageStr += " | ";
                stageStr += "Miss";
                first = false;
            }
            if ((stage & ShaderStageFlags::Intersection) != ShaderStageFlags::None)
            {
                if (!first) stageStr += " | ";
                stageStr += "Intersection";
                first = false;
            }

            result += stageStr;
            return *this;
        }

        void PrintVariable(slang::VariableReflection* variable)
        {
            YamlScopedObject scopedObj(*this);
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
            YamlScopedObject scopedObj(*this);
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
            YamlScopedObject scopedObj(*this);
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
            YamlScopedObject scopedObj(*this);
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
            YamlScopedObject scopedObj(*this);
            const char* type_name = type_layout->getName();
            Key("Name").PrintQuotedString(type_name);
            Key("Kind").PrintSlangTypeKind(type_layout->getKind());
            PrintCommonTypeInfo(type_layout->getType());
            PrintSizes(type_layout);
            PrintKindSpecificInfo(type_layout, access_path);
        }

        void PrintVariableLayout(slang::VariableLayoutReflection* variable_layout, AccessPath access_path)
        {
            YamlScopedObject scopedObj(*this);
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
            YamlScopedObject scopedObj(*this);
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


    // ShaderIdentifier implementation
    bool ShaderCompiler::ShaderIdentifier::operator==(const ShaderIdentifier& other) const noexcept
    {
        return ModuleName == other.ModuleName &&
               EntryPointName == other.EntryPointName &&
               Stage == other.Stage;
    }

    size_t ShaderCompiler::ShaderIdentifier::Hash() const noexcept
    {
        size_t h1 = std::hash<std::string>{}(ModuleName);
        size_t h2 = std::hash<std::string>{}(EntryPointName);
        size_t h3 = std::hash<uint32_t>{}(static_cast<uint32_t>(Stage));
        
        // Combine hashes
        h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        h1 ^= h3 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        return h1;
    }

    // Implementation details
    struct ShaderCompilerImpl
    {
        DeviceHandle ParentDevice;
        
        // Slang global session (reused across compilations)
        Slang::ComPtr<slang::IGlobalSession> SlangGlobalSession;
        
        // Module storage: moduleName -> module data
        struct ModuleStorage
        {
            std::string ModuleName;
            std::filesystem::path SourcePath;
            SlangProgramPtr LinkedProgram;
            SlangLayoutPtr ProgramLayout;
            std::vector<SlangMetadataPtr> Metadata;
            std::string CompilationLog;
            
            // Per-entry-point storage
            struct EntryPointData
            {
                std::string Name;
                ShaderStageFlags Stage;
                std::vector<uint8_t> Bytecode;
                
                // Optional cached reflection
                std::optional<ShaderCompiler::ShaderReflection> CachedReflection;
            };
            std::unordered_map<std::string, EntryPointData> EntryPoints;
        };
        
        // Thread-safe module storage
        mutable std::mutex moduleStorageMutex;
        std::unordered_map<std::string, ModuleStorage> compiledModules;
        
        // For synchronous processing (future: replace with mwsrQueue + worker thread)
        void ProcessMessage(ShaderCompilerMessagePayload message);
        
        // Message handlers
        void ProcessCompileModuleMessage(CompileModuleMessage&& message);
        void ProcessQueryReflectionMessage(QueryReflectionMessage&& message);
        
        // Core compilation logic
        Result CompileSlangModule(
            const CompileModuleMessage::OwnedModuleCompileOptions& options,
            ModuleStorage& outStorage);
        
        // Helper: Extract entry point bytecode from linked program
        bool ExtractEntryPointBytecode(
            SlangProgramPtr linkedProgram,
            size_t entryPointIndex,
            std::vector<uint8_t>& outBytecode,
            std::string& outError);
        
        // Reflection generation (placeholder - will be implemented)
        ShaderCompiler::ShaderReflection GenerateReflection(
            const ModuleStorage& module,
            const std::string& entryPointName,
            bool includeDescriptors,
            bool includeMemberReflection);
        
        // Helper: Create composite component type from module and entry points
        Result CreateComposite(
            slang::ISession* session,
            const std::vector<slang::IComponentType*>& componentTypes,
            Slang::ComPtr<slang::IComponentType>& outComposite,
            std::string& outDiagnostics);
        
        // Helper: Link a composite component type
        Result LinkComposite(
            Slang::ComPtr<slang::IComponentType> composite,
            SlangProgramPtr& outLinkedProgram,
            std::string& outDiagnostics);
    };

    void ShaderCompilerImpl::ProcessMessage(ShaderCompilerMessagePayload message)
    {
        std::visit([this](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, CompileModuleMessage>)
            {
                ProcessCompileModuleMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, QueryReflectionMessage>)
            {
                ProcessQueryReflectionMessage(std::move(arg));
            }
        }, std::move(message));
    }

    void ShaderCompilerImpl::ProcessCompileModuleMessage(CompileModuleMessage&& message)
    {
        message.Reply->SetStatus(ShaderModuleCompileReply::Status::Compiling);
        
        ModuleStorage storage;
        Result compileResult = CompileSlangModule(message.Options, storage);
        
        if (compileResult != Result::Success())
        {
            message.Reply->SetStatus(ShaderModuleCompileReply::Status::Failed);
            message.Reply->SetCompilationLog(storage.CompilationLog);
            return;
        }
        
        // Store module
        {
            std::lock_guard<std::mutex> lock{ moduleStorageMutex };
            message.Reply->SetModuleName(storage.ModuleName);
            
            // Add all compiled shaders to reply
            for (const auto& [entryPointName, entryData] : storage.EntryPoints)
            {
                ShaderCompiler::CompiledShader shader;
                shader.Identifier.ModuleName = storage.ModuleName;
                shader.Identifier.EntryPointName = entryPointName;
                shader.Identifier.Stage = entryData.Stage;
                shader.Bytecode = std::span<const uint8_t>{ entryData.Bytecode };
                shader.IsValid = !entryData.Bytecode.empty();
                
                message.Reply->AddCompiledShader(std::move(shader));
            }
            
            message.Reply->SetCompilationLog(storage.CompilationLog);
            compiledModules[storage.ModuleName] = std::move(storage);
        }
        
        message.Reply->SetStatus(ShaderModuleCompileReply::Status::Complete);
    }

    void ShaderCompilerImpl::ProcessQueryReflectionMessage(QueryReflectionMessage&& message)
    {
        message.Reply->SetStatus(ShaderReflectionQueryReply::Status::Processing);
        
        std::lock_guard<std::mutex> lock{ moduleStorageMutex };
        
        // Find the module
        auto moduleIt = compiledModules.find(message.Identifier.ModuleName);
        if (moduleIt == compiledModules.end())
        {
            message.Reply->SetError("Module not found");
            message.Reply->SetStatus(ShaderReflectionQueryReply::Status::Failed);
            return;
        }
        
        // Find the entry point
        auto& module = moduleIt->second;
        auto entryIt = module.EntryPoints.find(message.Identifier.EntryPointName);
        if (entryIt == module.EntryPoints.end())
        {
            message.Reply->SetError("Entry point not found in module");
            message.Reply->SetStatus(ShaderReflectionQueryReply::Status::Failed);
            return;
        }
        
        // Check if we have cached reflection
        auto& entryData = entryIt->second;
        if (entryData.CachedReflection.has_value())
        {
            message.Reply->SetReflection(entryData.CachedReflection.value());
            message.Reply->SetStatus(ShaderReflectionQueryReply::Status::Complete);
            return;
        }
        
        // Generate reflection on-demand (placeholder - needs implementation)
        ShaderCompiler::ShaderReflection reflection = GenerateReflection(
            module,
            message.Identifier.EntryPointName,
            true,  // includeDescriptors
            false  // includeMemberReflection
        );
        
        // Cache it
        entryData.CachedReflection = reflection;
        
        message.Reply->SetReflection(std::move(reflection));
        message.Reply->SetStatus(ShaderReflectionQueryReply::Status::Complete);
    }

    Result ShaderCompilerImpl::CompileSlangModule(
        const CompileModuleMessage::OwnedModuleCompileOptions& options,
        ModuleStorage& outStorage)
    {
        try
        {
            // Initialize Slang global session if needed
            if (!SlangGlobalSession)
            {
                SlangResult result = slang::createGlobalSession(SlangGlobalSession.writeRef());
                if (result != SLANG_OK)
                {
                    outStorage.CompilationLog = "Failed to create Slang global session";
                    return Result::Failure();
                }
            }

            // Convert search paths to C-string pointers
            std::vector<const char*> searchPathPtrs;
            searchPathPtrs.reserve(options.SearchPaths.size());
            for (const auto& path : options.SearchPaths)
            {
                searchPathPtrs.push_back(path.c_str());
            }

            // Create session descriptor
            slang::SessionDesc sessionDesc = {};
            sessionDesc.searchPaths = searchPathPtrs.data();
            sessionDesc.searchPathCount = static_cast<SlangInt>(searchPathPtrs.size());

            // Create compilation target
            slang::TargetDesc targetDesc = {};
            if (options.Target == "spirv")
            {
                targetDesc.format = SLANG_SPIRV;
                targetDesc.profile = SlangGlobalSession->findProfile("spirv_1_6");
            }
            else if (options.Target == "dxil")
            {
                targetDesc.format = SLANG_DXIL;
                targetDesc.profile = SlangGlobalSession->findProfile("sm_6_6");
            }
            else
            {
                outStorage.CompilationLog = std::format("Unsupported compilation target: {}", options.Target);
                return Result::Failure();
            }

            sessionDesc.targets = &targetDesc;
            sessionDesc.targetCount = 1;

            // Add compiler options
            std::vector<slang::CompilerOptionEntry> compilerOptions;
            
            if (options.EnableOptimizations)
            {
                slang::CompilerOptionEntry opt;
                opt.name = slang::CompilerOptionName::Optimization;
                opt.value.intValue0 = SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
                compilerOptions.push_back(opt);
            }

            if (options.EnableDebugInfo)
            {
                slang::CompilerOptionEntry debug;
                debug.name = slang::CompilerOptionName::DebugInformation;
                debug.value.intValue0 = SLANG_DEBUG_INFO_LEVEL_MAXIMAL;
                compilerOptions.push_back(debug);
            }

            #ifdef RHI_SYSTEM_USE_VULKAN
            slang::CompilerOptionEntry vulkanOption;
            vulkanOption.name = slang::CompilerOptionName::VulkanEmitReflection;
            vulkanOption.value.intValue0 = 1;
            compilerOptions.push_back(vulkanOption);
            
            slang::CompilerOptionEntry spirvOption;
            spirvOption.name = slang::CompilerOptionName::EmitSpirvDirectly;
            spirvOption.value.intValue0 = 1;
            compilerOptions.push_back(spirvOption);
            #endif

            sessionDesc.compilerOptionEntries = compilerOptions.data();
            sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(compilerOptions.size());

            // Create session
            Slang::ComPtr<slang::ISession> resultSession;
            SlangResult result = SlangGlobalSession->createSession(sessionDesc, resultSession.writeRef());
            if (SLANG_FAILED(result) || !resultSession)
            {
                outStorage.CompilationLog = "Failed to create Slang session";
                return Result::Failure();
            }

            // Load additional modules
            std::vector<SlangModulePtr> loadedModules;
            for (const auto& moduleName : options.AdditionalModules)
            {
                SlangBlobPtr moduleLoadDiagnostics;
                SlangModulePtr module;
                module = resultSession->loadModule(moduleName.c_str(), moduleLoadDiagnostics.writeRef());
                if (!module)
                {
                    outStorage.CompilationLog = std::format("Failed to load additional Slang module: {}", moduleName);
                    if (moduleLoadDiagnostics)
                    {
                        const char* diagStr = static_cast<const char*>(moduleLoadDiagnostics->getBufferPointer());
                        #ifdef WIN32
                        win32_OutputDebugString(diagStr);
                        #endif
                        outStorage.CompilationLog += std::format("\nDiagnostics: {}", diagStr);
                    }
                    return Result::Failure();
                }
                loadedModules.push_back(module);
            }

            // Load the main module from source
            SlangModulePtr sourceModule;
            std::filesystem::path absolutePath = std::filesystem::absolute(options.SlangSourcePath);
            std::string filePath = absolutePath.string();
            SlangBlobPtr diagnosticsBlob;
            
            std::string moduleNameToUse = options.ModuleName.empty() ? 
                options.SlangSourcePath.stem().string() : 
                options.ModuleName;

            // Important: use loadModule, not loadModuleFromSourceString, to respect search paths and import declarations
            // Otherwise, this crashes since things can't be resolved correctly (especially with multiple entry points and dependencies)
            sourceModule = resultSession->loadModule(
                moduleNameToUse.c_str(),
                diagnosticsBlob.writeRef());

            if (!sourceModule)
            {
                outStorage.CompilationLog = "Failed to load Slang module from source";
                if (diagnosticsBlob)
                {
                    const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
                    #ifdef WIN32
                    win32_OutputDebugString(diagStr);
                    #endif
                    outStorage.CompilationLog += std::format("\nSlang Diagnostics: {}", diagStr);
                }
                return Result::Failure();
            }
            else if (diagnosticsBlob)
            {
                if (diagnosticsBlob)
                {
                    const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
#ifdef WIN32
                    win32_OutputDebugString(diagStr);
#endif
                    outStorage.CompilationLog += std::format("\nSlang Diagnostics: {}", diagStr);
                }
            }

            // Discover all entry points in the module
            SlangUInt entryPointCount = sourceModule->getDefinedEntryPointCount();
            if (entryPointCount == 0)
            {
                outStorage.CompilationLog = "No entry points found in Slang module";
                return Result::Failure();
            }

            std::vector<std::string> entryPointsToCompile;
            
            if (options.CompileAllEntryPoints)
            {
                // Get all entry point names
                for (SlangUInt i = 0; i < entryPointCount; ++i)
                {
                    Slang::ComPtr<slang::IEntryPoint> entryPoint;
                    SlangResult epResult = sourceModule->getDefinedEntryPoint(static_cast<SlangInt32>(i), entryPoint.writeRef());
                    if (SLANG_SUCCEEDED(epResult) && entryPoint)
                    {
                        const char* epName = entryPoint->getFunctionReflection()->getName();
                        if (epName)
                        {
                            entryPointsToCompile.push_back(epName);
                        }
                    }
                }
            }
            else
            {
                // Use specific entry points
                entryPointsToCompile = options.SpecificEntryPoints;
            }

            // Set output storage fields
            outStorage.ModuleName = moduleNameToUse;
            outStorage.SourcePath = options.SlangSourcePath;

            // start with root module, then add each entry point and do a collective link and compile
            std::vector<slang::IComponentType*> componentTypes;
            std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPointStorage;
            
            componentTypes.push_back(sourceModule);

            // add entry points - store ComPtrs to maintain lifetime
            for (const auto& entryPointName : entryPointsToCompile)
            {
                Slang::ComPtr<slang::IEntryPoint> entryPoint;
                SlangResult epResult = sourceModule->findEntryPointByName(
                    entryPointName.c_str(), entryPoint.writeRef());
                if (SLANG_SUCCEEDED(epResult) && entryPoint)
                {
                    entryPointStorage.push_back(entryPoint);
                    componentTypes.push_back(entryPoint.get());
                }
                else
                {
                    // Track failed entry point but continue with others (per-entry-point error handling)
                    std::string errorMsg = std::format("Entry point '{}' not found in module", entryPointName);
                    outStorage.CompilationLog += errorMsg + "\n";
                }
            }

            // Create composite component type from module + all entry points
            Slang::ComPtr<slang::IComponentType> composedProgram;
            std::string compositeDiagnostics;
            Result composeResult = CreateComposite(
                resultSession,
                componentTypes,
                composedProgram,
                compositeDiagnostics);
            
            if (composeResult != Result::Success())
            {
                outStorage.CompilationLog += std::format("Failed to create composite: {}\n", compositeDiagnostics);
                return Result::Failure();
            }

            // Link the composite to resolve all dependencies
            SlangProgramPtr linkedProgram;
            std::string linkDiagnostics;
            Result linkResult = LinkComposite(composedProgram, linkedProgram, linkDiagnostics);
            
            if (linkResult != Result::Success())
            {
                outStorage.CompilationLog += std::format("Failed to link composite: {}\n", linkDiagnostics);
                return Result::Failure();
            }

            // Store the linked program and layout for later reflection queries
            outStorage.LinkedProgram = linkedProgram;
            outStorage.ProgramLayout = linkedProgram->getLayout();

            std::vector<SlangMetadataPtr> entrypointMetadata;
            for (size_t i = 0; i < entryPointCount; ++i)
            {
                Slang::ComPtr<slang::IMetadata> metadata;
                Slang::ComPtr<slang::IBlob> diagnostics;
                SlangResult result = linkedProgram->getEntryPointMetadata(i, 0, metadata.writeRef(), diagnostics.writeRef());
                if (SLANG_FAILED(result) || !metadata)
                {
                    if (diagnostics)
                    {
                        const char* diagStr = static_cast<const char*>(diagnostics->getBufferPointer());
                        std::string diagString = std::format("Slang Metadata Diagnostics: {}\n", diagStr);
                        outStorage.CompilationLog += "\n" + diagString;
                    }
                    continue;
                }
                entrypointMetadata.emplace_back(metadata);
            }

            YamlBuilder yamlBuilder(&entrypointMetadata, outStorage.ProgramLayout);
            yamlBuilder.PrintProgramLayout();
            // dump generated YAML to current directory for inspection
            std::ofstream yamlFile("SlangReflectionOutput.yaml");
            assert(yamlFile.is_open());
            yamlFile << yamlBuilder.result;
            yamlFile.close();

            // Now extract bytecode for each entry point from the linked program
            // Entry points start at index 0 (module is not an entry point in the composite)
            for (size_t i = 0; i < entryPointsToCompile.size(); ++i)
            {
                const std::string& entryPointName = entryPointsToCompile[i];
                
                // Extract bytecode
                std::vector<uint8_t> bytecode;
                std::string bytecodeError;
                if (!ExtractEntryPointBytecode(linkedProgram, i, bytecode, bytecodeError))
                {
                    std::string errorMsg = std::format("Failed to extract bytecode for entry point '{}': {}", 
                        entryPointName, bytecodeError);
                    outStorage.CompilationLog += errorMsg + "\n";
                    continue; // Per-entry-point error handling
                }

                // Get stage from entry point reflection
                slang::EntryPointReflection* epReflection = outStorage.ProgramLayout->getEntryPointByIndex(i);
                SlangStage slangStage = epReflection->getStage();
                ShaderStageFlags stage = FromSlangStage(slangStage);

                // Store entry point data
                ModuleStorage::EntryPointData epData;
                epData.Name = entryPointName;
                epData.Stage = stage;
                epData.Bytecode = std::move(bytecode);

                outStorage.EntryPoints[entryPointName] = std::move(epData);
            }

            // Check if we compiled at least one entry point
            if (outStorage.EntryPoints.empty())
            {
                outStorage.CompilationLog = "Failed to compile any entry points from module";
                return Result::Failure();
            }

            return Result::Success();
        }
        catch (const std::exception& e)
        {
            outStorage.CompilationLog = std::format("Exception during Slang compilation: {}", e.what());
            return Result::Failure();
        }
    }

    bool ShaderCompilerImpl::ExtractEntryPointBytecode(
        SlangProgramPtr linkedProgram,
        size_t entryPointIndex,
        std::vector<uint8_t>& outBytecode,
        std::string& outError)
    {
        Slang::ComPtr<slang::IBlob> codeBlob;
        SlangBlobPtr diagnosticsBlob;
        
        SlangResult getCodeResult = linkedProgram->getEntryPointCode(
            entryPointIndex, 
            0, // target index
            codeBlob.writeRef(), 
            diagnosticsBlob.writeRef());
        
        if (SLANG_FAILED(getCodeResult) || !codeBlob)
        {
            outError = "Failed to get entry point bytecode";
            if (diagnosticsBlob)
            {
                const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
                #ifdef WIN32
                win32_OutputDebugString(diagStr);
                #endif
                outError += std::format("\nDiagnostics: {}", diagStr);
            }
            return false;
        }

        size_t codeSize = codeBlob->getBufferSize();
        if (codeSize % 4 != 0)
        {
            outError = "Bytecode size is not a multiple of 4";
            return false;
        }

        outBytecode.resize(codeSize);
        std::memcpy(outBytecode.data(), codeBlob->getBufferPointer(), codeSize);
        return true;
    }

    ShaderCompiler::ShaderReflection ShaderCompilerImpl::GenerateReflection(
        const ModuleStorage& module,
        const std::string& entryPointName,
        bool includeDescriptors,
        bool includeMemberReflection)
    {
        // Placeholder implementation - will be expanded with actual reflection extraction
        ShaderCompiler::ShaderReflection reflection;
        
        auto entryIt = module.EntryPoints.find(entryPointName);
        if (entryIt != module.EntryPoints.end())
        {
            reflection.Identifier.ModuleName = module.ModuleName;
            reflection.Identifier.EntryPointName = entryPointName;
            reflection.Identifier.Stage = entryIt->second.Stage;
        }
        
        // TODO: Extract actual reflection data from module.ProgramLayout
        // This will be implemented in a follow-up
        
        return reflection;
    }

    Result ShaderCompilerImpl::CreateComposite(
        slang::ISession* session,
        const std::vector<slang::IComponentType*>& componentTypes,
        Slang::ComPtr<slang::IComponentType>& outComposite,
        std::string& outDiagnostics)
    {
        SlangBlobPtr diagnosticsBlob;
        
        SlangResult result = session->createCompositeComponentType(
            componentTypes.data(),
            componentTypes.size(),
            outComposite.writeRef(),
            diagnosticsBlob.writeRef());
        
        if (diagnosticsBlob)
        {
            const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
            #ifdef WIN32
            win32_OutputDebugString(diagStr);
            #endif
            outDiagnostics += diagStr;
        }
        
        if (SLANG_FAILED(result) || !outComposite)
        {
            if (outDiagnostics.empty())
            {
                outDiagnostics = "Failed to create composite component type";
            }
            return Result::Failure();
        }
        
        return Result::Success();
    }

    Result ShaderCompilerImpl::LinkComposite(
        Slang::ComPtr<slang::IComponentType> composite,
        SlangProgramPtr& outLinkedProgram,
        std::string& outDiagnostics)
    {
        SlangBlobPtr diagnosticsBlob;
        
        SlangResult result = composite->link(
            outLinkedProgram.writeRef(),
            diagnosticsBlob.writeRef());
        
        if (diagnosticsBlob)
        {
            const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
            #ifdef WIN32
            win32_OutputDebugString(diagStr);
            #endif
            outDiagnostics += diagStr;
        }
        
        if (SLANG_FAILED(result) || !outLinkedProgram)
        {
            if (outDiagnostics.empty())
            {
                outDiagnostics = "Failed to link composite component type";
            }
            return Result::Failure();
        }
        
        return Result::Success();
    }

    // ShaderCompiler public API implementation
    ShaderCompiler::ShaderCompiler() :
        impl{ std::make_unique<ShaderCompilerImpl>() }
    {
    }

    ShaderCompiler::~ShaderCompiler() = default;

    Result ShaderCompiler::Initialize(DeviceHandle device)
    {
        impl->ParentDevice = device;
        return Result::Success();
    }

    void ShaderCompiler::Shutdown()
    {
        // For now, just cleanup. Later this will signal worker thread to exit
        std::lock_guard<std::mutex> lock{ impl->moduleStorageMutex };
        impl->compiledModules.clear();
        
        if (impl->SlangGlobalSession)
        {
            impl->SlangGlobalSession.setNull();
        }
    }

    std::shared_ptr<ShaderModuleCompileReply> ShaderCompiler::CompileModule(const ModuleCompileOptions& options)
    {
        auto reply = std::make_shared<ShaderModuleCompileReply>();
        reply->SetStatus(ShaderModuleCompileReply::Status::Pending);
        
        CompileModuleMessage message;
        message.Options = CompileModuleMessage::OwnedModuleCompileOptions{ options };
        message.Reply = reply;
        
        // Process synchronously for now
        impl->ProcessMessage(std::move(message));
        
        return reply;
    }

    std::shared_ptr<ShaderReflectionQueryReply> ShaderCompiler::QueryReflection(const ShaderIdentifier& identifier)
    {
        auto reply = std::make_shared<ShaderReflectionQueryReply>();
        reply->SetStatus(ShaderReflectionQueryReply::Status::Pending);
        
        QueryReflectionMessage message;
        message.Identifier = identifier;
        message.Reply = reply;
        
        // Process synchronously for now
        impl->ProcessMessage(std::move(message));
        
        return reply;
    }

    ShaderCompiler::CompiledShader ShaderCompiler::GetCompiledShader(const ShaderIdentifier& identifier)
    {
        std::lock_guard<std::mutex> lock{ impl->moduleStorageMutex };
        
        auto moduleIt = impl->compiledModules.find(identifier.ModuleName);
        if (moduleIt == impl->compiledModules.end())
        {
            CompiledShader notFound;
            notFound.IsValid = false;
            notFound.ErrorMessage = "Module not found";
            return notFound;
        }
        
        auto& module = moduleIt->second;
        auto entryIt = module.EntryPoints.find(identifier.EntryPointName);
        if (entryIt == module.EntryPoints.end())
        {
            CompiledShader notFound;
            notFound.IsValid = false;
            notFound.ErrorMessage = "Entry point not found";
            return notFound;
        }
        
        auto& entryData = entryIt->second;
        CompiledShader shader;
        shader.Identifier = identifier;
        shader.Bytecode = std::span<const uint8_t>{ entryData.Bytecode };
        shader.IsValid = !entryData.Bytecode.empty();
        
        return shader;
    }

    bool ShaderCompiler::IsModuleCompiled(const std::string& moduleName) const
    {
        std::lock_guard<std::mutex> lock{ impl->moduleStorageMutex };
        return impl->compiledModules.find(moduleName) != impl->compiledModules.end();
    }

    std::vector<std::string> ShaderCompiler::GetModuleEntryPoints(const std::string& moduleName) const
    {
        std::lock_guard<std::mutex> lock{ impl->moduleStorageMutex };
        
        auto it = impl->compiledModules.find(moduleName);
        if (it == impl->compiledModules.end())
        {
            return {};
        }
        
        std::vector<std::string> entryPoints;
        entryPoints.reserve(it->second.EntryPoints.size());
        for (const auto& [name, data] : it->second.EntryPoints)
        {
            entryPoints.push_back(name);
        }
        
        return entryPoints;
    }

} // namespace rhi
