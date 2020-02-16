#include "threading/srw_lock.hpp"
#include <cstdint>
#include <cstddef>
#define NO_MINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef WIN32_LEAN_AND_MEAN
#undef NO_MINMAX

srw_lock::srw_lock()
{
    auto pSrw = static_cast<PSRWLOCK>(srwLockPtr);
    pSrw = new SRWLOCK();
    memset(pSrw, 0, sizeof(SRWLOCK));
    InitializeSRWLock(pSrw);
}

srw_lock::~srw_lock()
{
    if (srwLockPtr != nullptr)
    {
        delete static_cast<PSRWLOCK>(srwLockPtr);
    }
}

srw_lock::srw_lock(srw_lock&& other) noexcept : srwLockPtr(other.srwLockPtr)
{
    other.srwLockPtr = nullptr;
}

srw_lock& srw_lock::operator=(srw_lock&& other) noexcept
{
    srwLockPtr = other.srwLockPtr;
    other.srwLockPtr = nullptr;
    return *this;
}

void srw_lock::lock_exclusive()
{
    AcquireSRWLockExclusive(static_cast<PSRWLOCK>(srwLockPtr));
}

void srw_lock::lock_shared()
{
    AcquireSRWLockShared(static_cast<PSRWLOCK>(srwLockPtr));
}

bool srw_lock::try_lock_exclusive()
{
    return static_cast<bool>(TryAcquireSRWLockExclusive(static_cast<PSRWLOCK>(srwLockPtr)));
}

bool srw_lock::try_lock_shared()
{
    return static_cast<bool>(TryAcquireSRWLockShared(static_cast<PSRWLOCK>(srwLockPtr)));
}

void srw_lock::unlock_exclusive()
{
    ReleaseSRWLockExclusive(static_cast<PSRWLOCK>(srwLockPtr));
}

void srw_lock::unlock_shared()
{
    ReleaseSRWLockShared(static_cast<PSRWLOCK>(srwLockPtr));
}

srw_lock::scoped_write_lock::scoped_write_lock(srw_lock& _lock) noexcept : lock(_lock)
{
    lock.lock_exclusive();
}

srw_lock::scoped_write_lock::~scoped_write_lock()
{
    lock.unlock_exclusive();
}

srw_lock::scoped_read_lock::scoped_read_lock(srw_lock& _lock) noexcept : lock(_lock)
{
    lock.lock_shared();
}

srw_lock::scoped_read_lock::~scoped_read_lock()
{
    lock.unlock_shared();
}