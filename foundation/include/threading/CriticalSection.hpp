#pragma once
#ifndef FOUNDATION_THREADING_CRITICAL_SECTION_HPP
#define FOUNDATION_THREADING_CRITICAL_SECTION_HPP

struct CriticalSection
{

    CriticalSection();
    ~CriticalSection();
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
    CriticalSection(CriticalSection&& other) noexcept;
    CriticalSection& operator=(CriticalSection&& other) noexcept;

    void Lock();
    bool TryLock();
    void Unlock();

    size_t SpinCount() const noexcept;
    void SetSpinCount(const size_t new_spin_count) noexcept;

    struct ScopedLock
    {
        ScopedLock(CriticalSection& _section) noexcept;
        ~ScopedLock();

        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

    private:
        CriticalSection& section;
    };

    ScopedLock GetLock() noexcept;

private:
    void* criticalSectionObject{ nullptr };
};

#endif //!FOUNDATION_THREADING_CRITICAL_SECTION_HPP
