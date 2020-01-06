#pragma once
#ifndef CORE_THREADING_SRW_LOCK_HPP
#define CORE_THREADING_SRW_LOCK_HPP
#include <new>

struct srw_lock
{
    struct scoped_read_lock
    {
        scoped_read_lock(srw_lock& lock) noexcept;
        ~scoped_read_lock();
        scoped_read_lock(const scoped_read_lock&) = delete;
        scoped_read_lock& operator=(const scoped_read_lock&) = delete;
    private:
        srw_lock& lock;
    };
    
    struct scoped_write_lock
    {
        scoped_write_lock(srw_lock& lock) noexcept;
        ~scoped_write_lock();
        scoped_write_lock(const scoped_write_lock&) = delete;
        scoped_write_lock& operator=(const scoped_write_lock&) = delete;
    private:
        srw_lock& lock;
    };

    srw_lock();
    ~srw_lock();
    srw_lock(const srw_lock&) = delete;
    srw_lock& operator=(const srw_lock&) = delete;
    srw_lock(srw_lock&& other) noexcept;
    srw_lock& operator=(srw_lock&& other) noexcept;

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
