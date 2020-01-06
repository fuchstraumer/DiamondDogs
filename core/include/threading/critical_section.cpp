#include "critical_section.hpp"

critical_section::raii_scoped_lock::raii_scoped_lock(critical_section& _section) noexcept
{
}

critical_section::raii_scoped_lock::~raii_scoped_lock()
{
}
