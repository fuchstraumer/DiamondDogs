#include "FutureTable.hpp"
#include "RhiHandle.hpp"
#include <cstdint>

namespace rhi
{

// this is a strict no-op for WebGPU
bool WaitForTransferTimeline([[maybe_unused]] void* wait_device,
                             [[maybe_unused]] TimelineSemaphoreHandle transfer_timeline,
                             [[maybe_unused]] uint64_t wait_value,
                             [[maybe_unused]] uint64_t timeout_ns) noexcept
{
    return true;
}

bool WaitCanBlock() noexcept
{
    return false;
}

} // namespace rhi
