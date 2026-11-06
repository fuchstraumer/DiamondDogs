#include "reflect/TypeInfo.hpp"
#include <algorithm>

namespace foundation::reflect
{
    const FieldInfo* FindField(const TypeInfo& type, std::string_view name)
    {
        const auto found = std::ranges::find(type.Fields, name, &FieldInfo::Name);

        if (found == type.Fields.end())
        {
            return nullptr;
        }

        return &*found;
    }

    const EnumCaseInfo* FindEnumCase(const TypeInfo& type, std::string_view name)
    {
        const auto found = std::ranges::find(type.Cases, name, &EnumCaseInfo::Name);

        if (found == type.Cases.end())
        {
            return nullptr;
        }

        return &*found;
    }

    const EnumCaseInfo* FindEnumCase(const TypeInfo& type, int64_t value)
    {
        const auto found = std::ranges::find(type.Cases, value, &EnumCaseInfo::Value);

        if (found == type.Cases.end())
        {
            return nullptr;
        }

        return &*found;
    }

    const void* FieldAddress(const FieldInfo& field, const void* owner) noexcept
    {
        // one thunk serves both directions. The const comes back on the return, and nothing writes through
        // this path
        return field.Address(const_cast<void*>(owner));
    }

    namespace detail
    {
        void WriteStdString(void* value, std::string_view text)
        {
            static_cast<std::string*>(value)->assign(text);
        }
    }
}
