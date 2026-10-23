#include "RhiAssert.hpp"

#if defined(_DEBUG) || defined(DEBUG)
    #include <format>
    #include <stdexcept>

    namespace rhi
    {
        void AssertImpl(const Result& result, const char* expr, const char* file, int line)
        {
            if (result.IsFailure())
            {
                // if tdr, try to use device fault extension to extract more about the fault
                if (result.IsDeviceLost() && (g_DeviceLossHandler != nullptr))
                {
                    g_DeviceLossHandler(result, std::format("Device lost detected: {} [{}] at {}:{}",
                        result.GetMessage(), expr, file, line).c_str());
                }

                throw std::runtime_error(std::format("RHI failed: {} [{}] at {}:{}",
                    result.GetMessage(), expr, file, line));
            }
        }
    }
#endif
