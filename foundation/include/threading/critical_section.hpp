#pragma once
#ifndef FOUNDATION_THREADING_CRITICAL_SECTION_HPP
#define FOUNDATION_THREADING_CRITICAL_SECTION_HPP

struct critical_section
{
    
    critical_section();
    ~critical_section();
    critical_section(const critical_section&) = delete;
    critical_section& operator=(const critical_section&) = delete;
    critical_section(critical_section&& other) noexcept;
    critical_section& operator=(critical_section&& other) noexcept;

    void lock();
    bool try_lock();
    void unlock();

    size_t spin_count() const noexcept;
    void set_spin_count(const size_t new_spin_count) noexcept;

    struct raii_scoped_lock
    {
        raii_scoped_lock(critical_section& _section) noexcept;
        ~raii_scoped_lock();

        raii_scoped_lock(const raii_scoped_lock&) = delete;
        raii_scoped_lock& operator=(const raii_scoped_lock&) = delete;

    private:
        critical_section& section;
    };

    raii_scoped_lock get_lock() noexcept;

private:
    void* criticalSectionObject{ nullptr };
}

#endif //!FOUNDATION_THREADING_CRITICAL_SECTION_HPP
