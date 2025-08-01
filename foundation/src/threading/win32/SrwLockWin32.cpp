#include "threading/SrwLock.hpp"
#include <cstdint>
#include <cstddef>
#define NO_MINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef WIN32_LEAN_AND_MEAN
#undef NO_MINMAX

SrwLock::SrwLock()
{
    PSRWLOCK pSrw = new SRWLOCK();
    memset(pSrw, 0, sizeof(SRWLOCK));
    InitializeSRWLock(pSrw);
    srwLockPtr = static_cast<void*>(pSrw);
}

SrwLock::~SrwLock()
{
    if (srwLockPtr != nullptr)
    {
        delete static_cast<PSRWLOCK>(srwLockPtr);
    }
}

SrwLock::SrwLock(SrwLock&& other) noexcept : srwLockPtr(other.srwLockPtr)
{
    other.srwLockPtr = nullptr;
}

SrwLock& SrwLock::operator=(SrwLock&& other) noexcept
{
    srwLockPtr = other.srwLockPtr;
    other.srwLockPtr = nullptr;
    return *this;
}

void SrwLock::lock_exclusive()
{
    AcquireSRWLockExclusive(static_cast<PSRWLOCK>(srwLockPtr));
}

void SrwLock::lock_shared()
{
    AcquireSRWLockShared(static_cast<PSRWLOCK>(srwLockPtr));
}

bool SrwLock::try_lock_exclusive()
{
    return static_cast<bool>(TryAcquireSRWLockExclusive(static_cast<PSRWLOCK>(srwLockPtr)));
}

bool SrwLock::try_lock_shared()
{
    return static_cast<bool>(TryAcquireSRWLockShared(static_cast<PSRWLOCK>(srwLockPtr)));
}

void SrwLock::unlock_exclusive()
{
    // Add annotation to inform static analysis that the lock is held
#ifdef _PREFAST_
    __analysis_assume_lock_held(*(static_cast<PSRWLOCK>(srwLockPtr)));
#endif
    ReleaseSRWLockExclusive(static_cast<PSRWLOCK>(srwLockPtr));
}

void SrwLock::unlock_shared()
{    
    // Add annotation to inform static analysis that the lock is held
#ifdef _PREFAST_
    __analysis_assume_lock_held(*(static_cast<PSRWLOCK>(srwLockPtr)));
#endif
    ReleaseSRWLockShared(static_cast<PSRWLOCK>(srwLockPtr));
}
