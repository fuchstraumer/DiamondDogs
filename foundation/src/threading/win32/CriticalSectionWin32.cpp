#include "threading/CriticalSection.hpp"
#include <cstdint>
#include <cstddef>
#define NO_MINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef WIN32_LEAN_AND_MEAN
#undef NO_MINMAX

CriticalSection::CriticalSection()
{
    criticalSectionObject = new CRITICAL_SECTION();
    memset(criticalSectionObject, 0, sizeof(CRITICAL_SECTION));
#ifdef NDEBUG
    // Initializes without debug info
    InitializeCriticalSectionEx(
        reinterpret_cast<LPCRITICAL_SECTION>(criticalSectionObject), 40000,
        CRITICAL_SECTION_NO_DEBUG_INFO);
#else
    InitializeCriticalSection(static_cast<LPCRITICAL_SECTION>(criticalSectionObject));
#endif
}

CriticalSection::~CriticalSection()
{
    if (criticalSectionObject != nullptr)
    {
        DeleteCriticalSection(static_cast<LPCRITICAL_SECTION>(criticalSectionObject));
        delete static_cast<LPCRITICAL_SECTION>(criticalSectionObject);
    }
}

CriticalSection::CriticalSection(CriticalSection&& other) noexcept : criticalSectionObject(other.criticalSectionObject)
{
    other.criticalSectionObject = nullptr;
}

CriticalSection& CriticalSection::operator=(CriticalSection&& other) noexcept
{
    criticalSectionObject = other.criticalSectionObject;
    other.criticalSectionObject = nullptr;
    return *this;
}

void CriticalSection::Lock()
{
    EnterCriticalSection(static_cast<LPCRITICAL_SECTION>(criticalSectionObject));
}

void CriticalSection::lock()
{
    EnterCriticalSection(static_cast<LPCRITICAL_SECTION>(criticalSectionObject));
}

bool CriticalSection::TryLock()
{
    return static_cast<bool>(TryEnterCriticalSection(static_cast<LPCRITICAL_SECTION>(criticalSectionObject)));
}

void CriticalSection::Unlock()
{
    LeaveCriticalSection(static_cast<LPCRITICAL_SECTION>(criticalSectionObject));
}

void CriticalSection::unlock()
{
    LeaveCriticalSection(static_cast<LPCRITICAL_SECTION>(criticalSectionObject));
}

size_t CriticalSection::SpinCount() const noexcept
{
    return static_cast<size_t>(static_cast<LPCRITICAL_SECTION>(criticalSectionObject)->SpinCount);
}

void CriticalSection::SetSpinCount(const size_t new_spin_count) noexcept
{
    SetCriticalSectionSpinCount(static_cast<LPCRITICAL_SECTION>(criticalSectionObject), static_cast<DWORD>(new_spin_count));
}
