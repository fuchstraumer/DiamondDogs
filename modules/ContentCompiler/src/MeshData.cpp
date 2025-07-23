#include "MeshData.hpp"
#include <cassert>

cStringArray::cStringArray(const size_t num_strings) : numStrings(num_strings)
{
    strings = new char*[numStrings];
    for (size_t i = 0; i < numStrings; ++i)
    {
        strings[i] = nullptr;
    }
}

cStringArray::~cStringArray()
{
    destroy();
}

cStringArray::cStringArray(const cStringArray& other)
{
    clone(const_cast<cStringArray&>(other));
}

cStringArray& cStringArray::operator=(const cStringArray& other) noexcept
{
    destroy();
    clone(const_cast<cStringArray&>(other));
    return *this;
}

cStringArray::cStringArray(cStringArray&& other) noexcept : numStrings(other.numStrings), strings(other.strings)
{
    // make it so other object doesn't destroy it's strings, since we own them now
    other.numStrings = 0;
    other.strings = nullptr;
}

cStringArray& cStringArray::operator=(cStringArray&& other) noexcept
{
    destroy();
    numStrings = other.numStrings;
    strings = other.strings;
    other.numStrings = 0u;
    other.strings = nullptr;
    return *this;
}

const char* cStringArray::operator[](const size_t idx) const
{
    assert(idx < numStrings);
    return strings[idx];
}

void cStringArray::set_string(const size_t idx, const char* str)
{
    assert((idx < numStrings) && (strings != nullptr));
    if (strings[idx] != nullptr)
    {
        free(strings[idx]);
    }
    strings[idx] = strdup(str);
}

size_t cStringArray::num_strings() const noexcept
{
    return numStrings;
}

void cStringArray::destroy()
{
    if (numStrings != 0 && strings != nullptr)
    {
        for (size_t i = 0; i < numStrings; ++i)
        {
            if (strings[i] != nullptr)
            {
                free(strings[i]);
            }
        }

        delete[] strings;
    }

    numStrings = 0u;
    strings = nullptr;
}

void cStringArray::clone(cStringArray& other)
{
    numStrings = other.numStrings;
    strings = new char*[numStrings];
    for (size_t i = 0; i < numStrings; ++i)
    {
        strings[i] = strdup(other[i]);
    }
}
