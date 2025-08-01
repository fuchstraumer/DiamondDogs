#pragma once
#ifndef CORE_THREADING_SRW_LOCK_HPP
#define CORE_THREADING_SRW_LOCK_HPP

struct SrwLock
{

    SrwLock();
    ~SrwLock();
    SrwLock(const SrwLock&) = delete;
    SrwLock& operator=(const SrwLock&) = delete;
    SrwLock(SrwLock&& other) noexcept;
    SrwLock& operator=(SrwLock&& other) noexcept;

    void lock_exclusive();
    void lock_shared();
    bool try_lock_exclusive();
    bool try_lock_shared();
    void unlock_exclusive();
    void unlock_shared();

private:
    void* srwLockPtr{ nullptr };
};

#endif // !CORE_THREADING_SRW_LOCK_HPP
