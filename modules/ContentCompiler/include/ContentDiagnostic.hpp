#pragma once
#ifndef DIAMOND_DOGS_CC_CONTENT_DIAGNOSTIC_HPP
#define DIAMOND_DOGS_CC_CONTENT_DIAGNOSTIC_HPP
#include <cstdint>
#include <string>
#include <vector>

namespace cc
{
    enum class Severity : uint8_t
    {
        Invalid = 0,
        Note,
        Warning,
        Error,
    };

    enum class DiagnosticCode : uint16_t
    {
        Invalid = 0,
        UnknownKind,
        ParamsTypeMismatch,
        NaNParameter,
        CycleDetected,
        UnknownSlot,
        MissingRequiredSlot,
        SlotKindMismatch,
        DuplicateKindName,
        DanglingEdge,
        KeyCollision,
        SlotAlreadyConnected,
        UnknownOutput,
        // graph files (ReadGraphJson)
        // not JSON, or a member is missing or has the wrong JSON type
        MalformedGraphFile,
        // the schema is not cc.AuthoredGraph, or the version is not one that the reader knows
        UnsupportedGraphFile,
        DuplicateNodeName,
        // an edge names a node that the file does not have
        UnknownNodeName,
        // the params of a node do not decode as the params type of its kind
        InvalidParams,
    };

    struct Diagnostic
    {
        Severity Level{ Severity::Invalid };
        DiagnosticCode Code{ DiagnosticCode::Invalid };
        // authored node indices: one node, or every node on a cycle
        std::vector<uint32_t> Nodes;
        // for NaNParameter and InvalidParams; else empty
        std::string FieldPath;
        std::string Message;
    };
}

#endif // !DIAMOND_DOGS_CC_CONTENT_DIAGNOSTIC_HPP
