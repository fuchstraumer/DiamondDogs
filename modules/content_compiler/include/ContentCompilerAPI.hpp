#pragma once
#ifndef CONTENT_COMPILER_API_HPP
#define CONTENT_COMPILER_API_HPP
#include "MeshData.hpp"
#include "ObjMaterial.hpp"
#include <memory>

struct ccStateImpl;
struct VkImageCreateInfo;
struct VkImageViewCreateInfo;
constexpr static uint32_t FromFileHashCode = "FromFile"_ccHashName;

using ccStartCompileFn = ccDataHandle(*)(const void* initialData, void** output, void* userData);
using ccCompileStepFn = void(*)(const ccDataHandle handle, const void* input, void* output, void* userData);
using ccCompletionFn = void(*)(const ccDataHandle handle, void* finalData, void* userData);
using ccPackageFn = void(*)(const ccDataHandle handle, const void* data, size_t* archiveSz, std::byte* archiveBytes, void* userData);
using ccDepackageFn = ccDataHandle(*)(const size_t archiveLen, std::byte* archiveBytes, void* unpackedData, void* userData);

struct ContentCompilerRecipe
{
    ContentCompilerRecipe()
    {
        memset(this, 0, sizeof(ContentCompilerRecipe));
    }
    uint32_t endpointType;
    uint32_t startCompileFn{ 0u };
    uint32_t steps[8];
    uint32_t completionFn{ 0u };
    uint32_t packageFn{ 0u };
    uint32_t depackakeFn{ 0u };
    uint32_t reserved[3];
};

constexpr size_t recipeSize = sizeof(ContentCompilerRecipe);

struct ContentCompilerJob
{

    void AddStartCompileFn(ccStartCompileFn fn);
    void AddCompileStepFn(ccCompileStepFn fn);
    void AddCompletionFn(ccCompletionFn fn);
    void* RetrieveData(const ccDataHandle handle, const uint32_t typeHash);
private:
    std::unique_ptr<ccStateImpl> impl;
};

class ContentCompiler
{
    ContentCompiler(const ContentCompiler&) = delete;
    ContentCompiler& operator=(const ContentCompiler&) = delete;
    ContentCompiler();
    ~ContentCompiler();
public:

    static ContentCompiler& Get();

    ccDataHandle CompileFromFile(const char* fname, const uint32_t typeHash, void* userData);
    ccDataHandle StartCompile(const uint32_t startType, const void* initialData, const uint32_t destType, void* userData);
    // Multiple types of data can be attached to a single handle
    void* RetrieveData(const ccDataHandle handle, const uint32_t typeHash);

};

#endif // !CONTENT_COMPILER_API_HPP
