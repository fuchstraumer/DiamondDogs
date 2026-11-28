#include "GraphJson.hpp"
#include "ContentDiagnostic.hpp"
#include "Ingest.hpp"
#include "KindRegistry.hpp"
#include "content/JsonDecode.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    using nlohmann::json;

    // a node name to its authored index
    using NodeIndexMap = std::unordered_map<std::string, uint32_t>;

    [[nodiscard]] cc::Diagnostic MakeFileError(cc::DiagnosticCode code, std::string message);
    [[nodiscard]] cc::Diagnostic MakeNodeError(cc::DiagnosticCode code, uint32_t node, std::string message);

    // Adds a MalformedGraphFile error for each member of the object that is not on the list.
    void CheckMembers(const json& object, std::span<const std::string_view> known, std::string_view where,
                      std::vector<cc::Diagnostic>& diagnostics);

    // Returns the string member, or adds a MalformedGraphFile error and returns nullopt.
    [[nodiscard]] std::optional<std::string> ReadString(const json& object, std::string_view member,
                                                        std::string_view where,
                                                        std::vector<cc::Diagnostic>& diagnostics);

    // Checks the schema and the version, and returns the data object, or null after an error.
    [[nodiscard]] const json* ReadEnvelope(const json& root, std::vector<cc::Diagnostic>& diagnostics);

    void ReadNodes(const json& nodes, const cc::KindRegistry& registry, cc::GraphFile& file,
                   NodeIndexMap& node_indices, std::vector<cc::Diagnostic>& diagnostics);
    void ReadNode(const json& node, uint32_t index, const cc::KindRegistry& registry, cc::GraphFile& file,
                  NodeIndexMap& node_indices, std::vector<cc::Diagnostic>& diagnostics);
    void ReadParams(const json* params, uint32_t index, const cc::KindOps& kind, cc::AuthoredNode& node,
                    std::vector<cc::Diagnostic>& diagnostics);

    void ReadEdges(const json& edges, const NodeIndexMap& node_indices, cc::GraphFile& file,
                   std::vector<cc::Diagnostic>& diagnostics);
    // Returns the index of the named node, or adds an UnknownNodeName error and returns nullopt.
    [[nodiscard]] std::optional<uint32_t> FindNode(const NodeIndexMap& node_indices, const std::string& name,
                                                   std::string_view where,
                                                   std::vector<cc::Diagnostic>& diagnostics);

    constexpr std::string_view k_EnvelopeMembers[]{ "schema", "version", "data" };
    constexpr std::string_view k_DataMembers[]{ "nodes", "edges" };
    constexpr std::string_view k_NodeMembers[]{ "name", "kind", "params" };
    constexpr std::string_view k_EdgeMembers[]{ "from", "output", "to", "slot" };
}

namespace cc
{
    std::expected<GraphFile, std::vector<Diagnostic>> ReadGraphJson(std::string_view json_text,
                                                                    const KindRegistry& registry)
    {
        std::vector<Diagnostic> diagnostics;

        // no exceptions: a parse error gives a discarded value
        const json root = json::parse(json_text, nullptr, false);

        if (root.is_discarded() || !root.is_object())
        {
            diagnostics.push_back(MakeFileError(DiagnosticCode::MalformedGraphFile,
                                                "the file is not a JSON object"));

            return std::unexpected(std::move(diagnostics));
        }

        const json* data = ReadEnvelope(root, diagnostics);

        if (data == nullptr)
        {
            return std::unexpected(std::move(diagnostics));
        }

        CheckMembers(*data, k_DataMembers, "data", diagnostics);

        const auto nodes = data->find("nodes");
        const auto edges = data->find("edges");
        const bool hasNodes = nodes != data->end() && nodes->is_array();
        const bool hasEdges = edges != data->end() && edges->is_array();

        if (!hasNodes || !hasEdges)
        {
            diagnostics.push_back(MakeFileError(DiagnosticCode::MalformedGraphFile,
                                                "data needs a \"nodes\" array and an \"edges\" array"));

            return std::unexpected(std::move(diagnostics));
        }

        GraphFile file;
        NodeIndexMap nodeIndices;

        ReadNodes(*nodes, registry, file, nodeIndices, diagnostics);
        ReadEdges(*edges, nodeIndices, file, diagnostics);

        if (!diagnostics.empty())
        {
            return std::unexpected(std::move(diagnostics));
        }

        return file;
    }
}

namespace
{
    cc::Diagnostic MakeFileError(cc::DiagnosticCode code, std::string message)
    {
        return cc::Diagnostic
        {
            .Level = cc::Severity::Error,
            .Code = code,
            .Message = std::move(message),
        };
    }

    cc::Diagnostic MakeNodeError(cc::DiagnosticCode code, uint32_t node, std::string message)
    {
        return cc::Diagnostic
        {
            .Level = cc::Severity::Error,
            .Code = code,
            .Nodes = { node },
            .Message = std::move(message),
        };
    }

    void CheckMembers(const json& object, std::span<const std::string_view> known, std::string_view where,
                      std::vector<cc::Diagnostic>& diagnostics)
    {
        for (const auto& item : object.items())
        {
            const std::string& member = item.key();

            if (std::ranges::find(known, member) == known.end())
            {
                diagnostics.push_back(MakeFileError(cc::DiagnosticCode::MalformedGraphFile,
                                                    std::format("{} has an unknown member \"{}\"", where, member)));
            }
        }
    }

    std::optional<std::string> ReadString(const json& object, std::string_view member, std::string_view where,
                                          std::vector<cc::Diagnostic>& diagnostics)
    {
        const auto found = object.find(std::string{ member });

        if (found == object.end() || !found->is_string())
        {
            diagnostics.push_back(MakeFileError(cc::DiagnosticCode::MalformedGraphFile,
                                                std::format("{} needs a string \"{}\"", where, member)));

            return std::nullopt;
        }

        return found->get<std::string>();
    }

    const json* ReadEnvelope(const json& root, std::vector<cc::Diagnostic>& diagnostics)
    {
        CheckMembers(root, k_EnvelopeMembers, "the file", diagnostics);

        const std::optional<std::string> schema = ReadString(root, "schema", "the file", diagnostics);

        if (!schema.has_value())
        {
            return nullptr;
        }

        if (*schema != cc::k_GraphFileSchema)
        {
            diagnostics.push_back(MakeFileError(cc::DiagnosticCode::UnsupportedGraphFile,
                                                std::format("the schema is \"{}\", not \"{}\"", *schema,
                                                            cc::k_GraphFileSchema)));

            return nullptr;
        }

        const auto version = root.find("version");

        if (version == root.end() || !version->is_number_integer())
        {
            diagnostics.push_back(MakeFileError(cc::DiagnosticCode::MalformedGraphFile,
                                                "the file needs an integer \"version\""));

            return nullptr;
        }

        const int64_t versionNumber = version->get<int64_t>();

        if (versionNumber != cc::k_GraphFileVersion)
        {
            diagnostics.push_back(MakeFileError(cc::DiagnosticCode::UnsupportedGraphFile,
                                                std::format("version {} is not supported; this reader knows "
                                                            "version {}", versionNumber, cc::k_GraphFileVersion)));

            return nullptr;
        }

        const auto data = root.find("data");

        if (data == root.end() || !data->is_object())
        {
            diagnostics.push_back(MakeFileError(cc::DiagnosticCode::MalformedGraphFile,
                                                "the file needs a \"data\" object"));

            return nullptr;
        }

        return &*data;
    }

    void ReadNodes(const json& nodes, const cc::KindRegistry& registry, cc::GraphFile& file,
                   NodeIndexMap& node_indices, std::vector<cc::Diagnostic>& diagnostics)
    {
        file.Graph.Nodes.resize(nodes.size());
        file.NodeNames.resize(nodes.size());

        for (size_t index = 0; index < nodes.size(); ++index)
        {
            ReadNode(nodes[index], static_cast<uint32_t>(index), registry, file, node_indices, diagnostics);
        }
    }

    void ReadNode(const json& node, uint32_t index, const cc::KindRegistry& registry, cc::GraphFile& file,
                  NodeIndexMap& node_indices, std::vector<cc::Diagnostic>& diagnostics)
    {
        const std::string where = std::format("node {}", index);

        if (!node.is_object())
        {
            diagnostics.push_back(MakeNodeError(cc::DiagnosticCode::MalformedGraphFile, index,
                                                std::format("{} is not an object", where)));

            return;
        }

        CheckMembers(node, k_NodeMembers, where, diagnostics);

        const std::optional<std::string> name = ReadString(node, "name", where, diagnostics);

        if (name.has_value())
        {
            const auto [existing, inserted] = node_indices.try_emplace(*name, index);

            if (!inserted)
            {
                diagnostics.push_back(cc::Diagnostic
                {
                    .Level = cc::Severity::Error,
                    .Code = cc::DiagnosticCode::DuplicateNodeName,
                    .Nodes = { existing->second, index },
                    .Message = std::format("nodes {} and {} are both named \"{}\"", existing->second, index, *name),
                });
            }

            file.NodeNames[index] = *name;
        }

        const std::optional<std::string> kindName = ReadString(node, "kind", where, diagnostics);

        if (!kindName.has_value())
        {
            return;
        }

        file.Graph.Nodes[index].KindName = *kindName;

        const std::optional<cc::KindId> kindId = registry.Find(*kindName);

        if (!kindId.has_value())
        {
            diagnostics.push_back(MakeNodeError(cc::DiagnosticCode::UnknownKind, index,
                                                std::format("{} has the kind \"{}\", which is not registered", where,
                                                            *kindName)));

            return;
        }

        const auto params = node.find("params");
        const json* paramsValue = params == node.end() ? nullptr : &*params;

        ReadParams(paramsValue, index, registry.Ops(*kindId), file.Graph.Nodes[index], diagnostics);
    }

    void ReadParams(const json* params, uint32_t index, const cc::KindOps& kind, cc::AuthoredNode& node,
                    std::vector<cc::Diagnostic>& diagnostics)
    {
        // A node with no params member gets the default value of its params type, through the same decode.
        std::string paramsText{ "{}" };

        if (params != nullptr)
        {
            if (!params->is_object())
            {
                diagnostics.push_back(MakeNodeError(cc::DiagnosticCode::MalformedGraphFile, index,
                                                    std::format("node {}: \"params\" is not an object", index)));

                return;
            }

            paramsText = params->dump(-1, ' ', false, json::error_handler_t::replace);
        }

        // strict: a misspelled field is an error, not a field that silently keeps its default
        cc::ParamsDecode decoded = kind.DecodeParams(paramsText, foundation::content::k_StrictDecodeOptions);

        // a warning is an error here too: the params must decode exactly
        const std::vector<foundation::content::DecodeIssue>* issueLists[]{ &decoded.Issues.Errors,
                                                                          &decoded.Issues.Warnings };

        bool hasIssue = false;

        for (const std::vector<foundation::content::DecodeIssue>* issues : issueLists)
        {
            for (const foundation::content::DecodeIssue& issue : *issues)
            {
                hasIssue = true;

                diagnostics.push_back(cc::Diagnostic
                {
                    .Level = cc::Severity::Error,
                    .Code = cc::DiagnosticCode::InvalidParams,
                    .Nodes = { index },
                    .FieldPath = issue.Path,
                    .Message = std::format("node {} ({}): {}: {}", index, kind.Name,
                                           foundation::content::ToString(issue.Kind), issue.Message),
                });
            }
        }

        if (!hasIssue)
        {
            node.Params = std::move(decoded.Params);
        }
    }

    void ReadEdges(const json& edges, const NodeIndexMap& node_indices, cc::GraphFile& file,
                   std::vector<cc::Diagnostic>& diagnostics)
    {
        for (size_t index = 0; index < edges.size(); ++index)
        {
            const json& edge = edges[index];
            const std::string where = std::format("edge {}", index);

            if (!edge.is_object())
            {
                diagnostics.push_back(MakeFileError(cc::DiagnosticCode::MalformedGraphFile,
                                                    std::format("{} is not an object", where)));

                continue;
            }

            CheckMembers(edge, k_EdgeMembers, where, diagnostics);

            const std::optional<std::string> from = ReadString(edge, "from", where, diagnostics);
            const std::optional<std::string> output = ReadString(edge, "output", where, diagnostics);
            const std::optional<std::string> to = ReadString(edge, "to", where, diagnostics);
            const std::optional<std::string> slot = ReadString(edge, "slot", where, diagnostics);
            const bool isComplete = from.has_value() && output.has_value() && to.has_value() && slot.has_value();

            if (!isComplete)
            {
                continue;
            }

            const std::optional<uint32_t> fromNode = FindNode(node_indices, *from, where, diagnostics);
            const std::optional<uint32_t> toNode = FindNode(node_indices, *to, where, diagnostics);

            if (!fromNode.has_value() || !toNode.has_value())
            {
                continue;
            }

            file.Graph.Edges.push_back(cc::AuthoredEdge
            {
                .FromNode = *fromNode,
                .FromOutput = *output,
                .ToNode = *toNode,
                .ToSlot = *slot,
            });
        }
    }

    std::optional<uint32_t> FindNode(const NodeIndexMap& node_indices, const std::string& name,
                                     std::string_view where, std::vector<cc::Diagnostic>& diagnostics)
    {
        const auto found = node_indices.find(name);

        if (found == node_indices.end())
        {
            diagnostics.push_back(MakeFileError(cc::DiagnosticCode::UnknownNodeName,
                                                std::format("{} names the node \"{}\", which the file does not "
                                                            "have", where, name)));

            return std::nullopt;
        }

        return found->second;
    }
}
