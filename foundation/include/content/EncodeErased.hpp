#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_ENCODE_ERASED_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_ENCODE_ERASED_HPP

namespace foundation::reflect
{
    struct TypeInfo;

}

namespace foundation::content
{
    class CanonicalSink;

    /** @brief Writes the value at this address to the sink, through its table. Tools use this when they do
     *  not know T at compile time. The output is byte-identical to Encode<T>. Where Encode asserts on an enum
     *  value with no case, this writes the same "<no case>" text and does not assert, because tools read live
     *  data. */
    void EncodeErased(const reflect::TypeInfo& type, const void* value, CanonicalSink& sink);
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_ENCODE_ERASED_HPP
