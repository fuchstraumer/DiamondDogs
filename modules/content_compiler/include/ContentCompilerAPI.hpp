#pragma once
#ifndef CONTENT_COMPILER_API_HPP
#define CONTENT_COMPILER_API_HPP
#include "MeshData.hpp"
#include "ObjMaterial.hpp"
#include <memory>

struct ContentCompilerImpl;
struct VkImageCreateInfo;
struct VkImageViewCreateInfo;

struct ContentCompileUpdateFns
{
    void (*StartingCompile)(void* instancePtr, const size_t numSteps, void* userData);
    bool (*StepCompleted)(void* instancePtr, const uint32_t typeHash, const void* stepData, void* userData);
    void (*EndingCompile)(void* instancePtr, const void* finalData, void* userData);
};

class ContentCompiler
{
    std::unique_ptr<ContentCompilerImpl> impl{ nullptr };
    ContentCompiler(const ContentCompiler&) = delete;
    ContentCompiler& operator=(const ContentCompiler&) = delete;
    ContentCompiler();
    ~ContentCompiler();
public:

    static ContentCompiler& Get();
    
    ObjectModelData RetrieveLoadedObjModel(const ccDataHandle& handle);
    ObjMaterialFile GetMaterial(const ccDataHandle& handle);
    void GetTextureInfos(const ccDataHandle& handle, VkImageCreateInfo& imageCreateInfo, VkImageViewCreateInfo& viewCreateInfo);
    void GetTextureData(const ccDataHandle& handle, size_t* dataSize, void** dataAddress);
};

#endif // !CONTENT_COMPILER_API_HPP
