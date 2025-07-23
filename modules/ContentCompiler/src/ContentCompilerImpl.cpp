#include "ContentCompilerImpl.hpp"

void ContentCompilerImpl::PerformCompileStep(const ccDataHandle handle, const uint32_t typeIn, const void* dataIn, const uint32_t typeOut, void* dataOut)
{
    const uint64_t fnHash = (typeIn << 32) | typeOut;
    auto iter = compileFns.find(fnHash);
    if (iter != compileFns.end())
    {
        auto fn = (*iter->second);
        fn(handle, dataIn, dataOut);
    }
}
