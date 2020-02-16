#include "threading/critical_section.hpp"
#include <cstdint>
#include <cstddef>
#define NO_MINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef WIN32_LEAN_AND_MEAN
#undef NO_MINMAX

critical_section::critical_section()
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

critical_section::~critical_section()
{
    if (criticalSectionObject != nullptr)
    {
        DeleteCriticalSection(static_cast<LPCRITICAL_SECTION>(criticalSectionObject));
        delete static_cast<LPCRITICAL_SECTION>(criticalSectionObject);
    }
}

critical_section::critical_section(critical_section&& other) noexcept : criticalSectionObject(other.criticalSectionObject)
{
    other.criticalSectionObject = nullptr;
}

critical_section& critical_section::operator=(critical_section&& other) noexcept
{
    criticalSectionObject = other.criticalSectionObject;
    other.criticalSectionObject = nullptr;
    return *this;
}

void critical_section::lock()
{
    EnterCriticalSection(static_cast<LPCRITICAL_SECTION>(criticalSectionObject));
}

bool critical_section::try_lock()
{
    return static_cast<bool>(TryEnterCriticalSection(static_cast<LPCRITICAL_SECTION>(criticalSectionObject)));
}

void critical_section::unlock()
{
    LeaveCriticalSection(static_cast<LPCRITICAL_SECTION>(criticalSectionObject));
}

size_t critical_section::spin_count() const noexcept
{
    return static_cast<size_t>(static_cast<LPCRITICAL_SECTION>(criticalSectionObject)->SpinCount);
}

void critical_section::set_spin_count(const size_t new_spin_count) noexcept
{
    SetCriticalSectionSpinCount(static_cast<LPCRITICAL_SECTION>(criticalSectionObject), static_cast<DWORD>(new_spin_count));
}

critical_section::raii_scoped_lock critical_section::get_lock() noexcept
{
    return raii_scoped_lock(*this);
}

critical_section::raii_scoped_lock::raii_scoped_lock(critical_section& _section) noexcept : section(_section)
{
    section.lock();
}

critical_section::raii_scoped_lock::~raii_scoped_lock()
{
    section.unlock();
}
