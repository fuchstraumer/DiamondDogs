#pragma once
#ifndef DIAMOND_DOGS_AGENT_TOOLS_TOOL_SCHEMA_HPP
#define DIAMOND_DOGS_AGENT_TOOLS_TOOL_SCHEMA_HPP

namespace foundation
{
    namespace content
    {
        class JsonWriter;
    }

    namespace reflect
    {
        struct TypeInfo;
        struct FieldMetadata;
    }
}

namespace agent::detail
{
    /** Writes the JSON Schema of a type. Metadata can be null. Hidden fields are left out. */
    void WriteTypeSchema(foundation::content::JsonWriter& writer,
                         const foundation::reflect::TypeInfo& type,
                         const foundation::reflect::FieldMetadata* metadata);
}

#endif // !DIAMOND_DOGS_AGENT_TOOLS_TOOL_SCHEMA_HPP
