#pragma once
#ifndef CONTENT_COMPILER_IMPL_HPP
#define CONTENT_COMPILER_IMPL_HPP
#include "InternalTypes.hpp"
#include <unordered_map>

struct ContentCompilerImpl
{
    ContentCompilerImpl() = default;
    ~ContentCompilerImpl() = default;

    using NewFnForType = void*(*)(void* initial_data, void* user_data);
    using DeleteFnForType = void(*)(void* instance, void* user_data);

    // modifies data at given handle using typeIn, from dataIn, to typeOut using dataOut
    using ContentCompileStepFn = void(*)(const ccDataHandle handle, const void* dataIn, void* dataOut);
    // in-place modification of data
    using ContentModifyFn = void(*)(const ccDataHandle handle, void* data);

    void RegisterCompileStep(const uint32_t inputType, const uint32_t outputType, ContentCompileStepFn fn);

    void PerformCompileStep(const ccDataHandle handle, const uint32_t typeIn, const void* dataIn, const uint32_t typeOut, void* dataOut);

    std::vector<ccDataHandle> EnqueueTextureLoads(const std::vector<std::string_view>& textures);

private:

    std::unordered_map<uint64_t, ContentCompileStepFn> compileFns;
    std::unordered_map<ccDataHandle, uint32_t> handleToContentTypeMap;

    std::unordered_map<ccDataHandle, StoredTextureDataImpl> loadedTextures;
};

#endif // !CONTENT_COMPILER_IMPL_HPP
