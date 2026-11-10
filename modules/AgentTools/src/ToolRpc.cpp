#include "ToolRpc.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    using nlohmann::json;
    using agent::rpc::OwnedArgument;
    using agent::rpc::OwnedScalar;
    using agent::rpc::RpcCode;
    using agent::rpc::RpcMethod;
    using agent::rpc::RpcRequest;
    using agent::rpc::RpcResult;

    // a line under the size limit can still nest a million arrays, and the flatten walk recurses
    constexpr int32_t k_MaxArgumentDepth = 32;
    constexpr int32_t k_ToolFailureWireCode = -32000;

    constexpr std::array<std::string_view, 1u> k_DescribeParams{ "path" };
    constexpr std::array<std::string_view, 1u> k_GetParams{ "path" };
    constexpr std::array<std::string_view, 2u> k_SetParams{ "path", "value" };
    constexpr std::array<std::string_view, 2u> k_CallParams{ "name", "arguments" };
    constexpr std::array<std::string_view, 0u> k_PingParams{};

    [[nodiscard]] RpcResult MakeSuccess();
    [[nodiscard]] RpcResult MakeRefusal(RpcCode code, std::string message, std::optional<int64_t> client_id);
    [[nodiscard]] std::expected<int64_t, RpcResult> ReadClientId(const json& document);
    [[nodiscard]] RpcMethod FindMethod(std::string_view name);
    [[nodiscard]] RpcResult CheckParamNames(const json& params,
                                            std::span<const std::string_view> allowed,
                                            int64_t client_id);
    [[nodiscard]] RpcResult ParseParams(const json& params, RpcRequest& request);
    [[nodiscard]] RpcResult ParseDescribe(const json& params, RpcRequest& request);
    [[nodiscard]] RpcResult ParseGet(const json& params, RpcRequest& request);
    [[nodiscard]] RpcResult ParseSet(const json& params, RpcRequest& request);
    [[nodiscard]] RpcResult ParseCall(const json& params, RpcRequest& request);
    [[nodiscard]] RpcResult ReadRequiredString(const json& params,
                                               std::string_view name,
                                               int64_t client_id,
                                               std::string& text);
    [[nodiscard]] std::optional<OwnedScalar> ReadScalar(const json& value);
    [[nodiscard]] RpcResult FlattenObject(const json& object,
                                          int64_t client_id,
                                          int32_t depth,
                                          std::string& path,
                                          std::vector<OwnedArgument>& arguments);
    [[nodiscard]] RpcResult FlattenArray(const json& array,
                                         int64_t client_id,
                                         int32_t depth,
                                         std::string& path,
                                         std::vector<OwnedArgument>& arguments);
    [[nodiscard]] RpcResult FlattenValue(const json& value,
                                         int64_t client_id,
                                         int32_t depth,
                                         std::string& path,
                                         std::vector<OwnedArgument>& arguments);
    [[nodiscard]] int32_t WireCode(RpcCode code) noexcept;
    [[nodiscard]] std::string EncodeText(std::string_view text);
}

namespace agent::rpc
{
    LineFramer::LineFramer(int64_t max_line_bytes) noexcept :
        maxLineBytes{ max_line_bytes }
    {}

    void LineFramer::Append(std::span<const std::byte> bytes)
    {
        buffer.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    std::optional<FramedLine> LineFramer::Next()
    {
        while (true)
        {
            const size_t newline = buffer.find('\n', scanned);

            if (newline == std::string::npos)
            {
                scanned = buffer.size();

                const bool tooLong = !discarding && std::cmp_greater(buffer.size() - consumed, maxLineBytes);

                if (discarding || tooLong)
                {
                    buffer.clear();
                    consumed = 0u;
                    scanned = 0u;
                    discarding = true;
                }
                else
                {
                    discardConsumedBytes();
                }

                if (tooLong)
                {
                    return FramedLine{ .Kind = LineKind::Oversize };
                }

                return std::nullopt;
            }

            const size_t lineStart = consumed;
            const size_t lineLength = newline - lineStart;

            consumed = newline + 1u;
            scanned = consumed;

            if (discarding)
            {
                discarding = false;
                continue;
            }

            if (std::cmp_greater(lineLength, maxLineBytes))
            {
                return FramedLine{ .Kind = LineKind::Oversize };
            }

            return FramedLine{ .Kind = LineKind::Complete, .Text = buffer.substr(lineStart, lineLength) };
        }
    }

    void LineFramer::discardConsumedBytes()
    {
        buffer.erase(0u, consumed);
        scanned -= consumed;
        consumed = 0u;
    }

    std::expected<RpcRequest, RpcResult> ParseRequest(std::string_view line)
    {
        const json document = json::parse(line, nullptr, false);

        if (document.is_discarded())
        {
            return std::unexpected(MakeRefusal(RpcCode::ParseError, "the line is not valid JSON", std::nullopt));
        }

        if (document.is_array())
        {
            return std::unexpected(MakeRefusal(RpcCode::InvalidRequest, "batches are refused", std::nullopt));
        }

        if (!document.is_object())
        {
            return std::unexpected(
                MakeRefusal(RpcCode::InvalidRequest, "a request must be a JSON object", std::nullopt));
        }

        std::expected<int64_t, RpcResult> clientId = ReadClientId(document);

        if (!clientId.has_value())
        {
            return std::unexpected(std::move(clientId.error()));
        }

        const json::const_iterator version = document.find("jsonrpc");

        if (version == document.end() || !version->is_string() || version->get_ref<const std::string&>() != "2.0")
        {
            return std::unexpected(MakeRefusal(RpcCode::InvalidRequest, "jsonrpc must be \"2.0\"", *clientId));
        }

        const json::const_iterator method = document.find("method");

        if (method == document.end() || !method->is_string())
        {
            return std::unexpected(MakeRefusal(RpcCode::InvalidRequest, "method must be a string", *clientId));
        }

        const std::string& methodName = method->get_ref<const std::string&>();

        RpcRequest request{};

        request.ClientId = *clientId;
        request.Method = FindMethod(methodName);

        if (request.Method == RpcMethod::Invalid)
        {
            return std::unexpected(MakeRefusal(RpcCode::MethodNotFound,
                                               std::format("the method '{}' does not exist", methodName),
                                               *clientId));
        }

        const json::const_iterator params = document.find("params");
        const json noParams = json::object();
        const json& paramsObject = params == document.end() ? noParams : *params;

        if (!paramsObject.is_object())
        {
            return std::unexpected(MakeRefusal(RpcCode::InvalidParams, "params must be an object", *clientId));
        }

        RpcResult parsed = ParseParams(paramsObject, request);

        if (!parsed.Succeeded())
        {
            return std::unexpected(std::move(parsed));
        }

        return request;
    }

    foundation::reflect::ScalarValue ViewOf(const OwnedScalar& value) noexcept
    {
        if (const std::string* text = std::get_if<std::string>(&value))
        {
            return foundation::reflect::ScalarValue{ std::string_view{ *text } };
        }

        if (const bool* flag = std::get_if<bool>(&value))
        {
            return foundation::reflect::ScalarValue{ *flag };
        }

        if (const int64_t* signedValue = std::get_if<int64_t>(&value))
        {
            return foundation::reflect::ScalarValue{ *signedValue };
        }

        if (const uint64_t* unsignedValue = std::get_if<uint64_t>(&value))
        {
            return foundation::reflect::ScalarValue{ *unsignedValue };
        }

        return foundation::reflect::ScalarValue{ std::get<double>(value) };
    }

    std::string FormatResult(int64_t client_id, std::string_view result_json)
    {
        return std::format("{{\"jsonrpc\":\"2.0\",\"id\":{},\"result\":{}}}\n", client_id, result_json);
    }

    std::string FormatToolFailure(int64_t client_id, std::string_view message, std::string_view failure_json)
    {
        return std::format("{{\"jsonrpc\":\"2.0\",\"id\":{},\"error\":{{\"code\":{},\"message\":{},\"data\":{}}}}}\n",
                           client_id,
                           k_ToolFailureWireCode,
                           EncodeText(message),
                           failure_json);
    }

    std::string FormatRefusal(const RpcResult& refusal)
    {
        const std::string id = refusal.ClientId.has_value() ? std::to_string(*refusal.ClientId) : "null";

        return std::format("{{\"jsonrpc\":\"2.0\",\"id\":{},\"error\":{{\"code\":{},\"message\":{}}}}}\n",
                           id,
                           WireCode(refusal.Code),
                           EncodeText(refusal.Message));
    }
}

namespace
{
    RpcResult MakeSuccess()
    {
        return RpcResult{ .Code = RpcCode::Success };
    }

    RpcResult MakeRefusal(RpcCode code, std::string message, std::optional<int64_t> client_id)
    {
        return RpcResult{ .Code = code, .Message = std::move(message), .ClientId = client_id };
    }

    std::expected<int64_t, RpcResult> ReadClientId(const json& document)
    {
        const json::const_iterator id = document.find("id");

        if (id == document.end())
        {
            return std::unexpected(
                MakeRefusal(RpcCode::InvalidRequest, "notifications are refused: add an integer id", std::nullopt));
        }

        if (!id->is_number_integer())
        {
            return std::unexpected(MakeRefusal(RpcCode::InvalidRequest, "id must be an integer", std::nullopt));
        }

        if (id->is_number_unsigned() && id->get<uint64_t>() > uint64_t{ std::numeric_limits<int64_t>::max() })
        {
            return std::unexpected(MakeRefusal(RpcCode::InvalidRequest, "id does not fit in 64 bits", std::nullopt));
        }

        return id->get<int64_t>();
    }

    RpcMethod FindMethod(std::string_view name)
    {
        if (name == "tool.describe")
        {
            return RpcMethod::Describe;
        }

        if (name == "tool.get")
        {
            return RpcMethod::Get;
        }

        if (name == "tool.set")
        {
            return RpcMethod::Set;
        }

        if (name == "tool.call")
        {
            return RpcMethod::Call;
        }

        if (name == "server.ping")
        {
            return RpcMethod::Ping;
        }

        return RpcMethod::Invalid;
    }

    RpcResult CheckParamNames(const json& params, std::span<const std::string_view> allowed, int64_t client_id)
    {
        for (const auto& [name, value] : params.items())
        {
            static_cast<void>(value);

            if (!std::ranges::contains(allowed, std::string_view{ name }))
            {
                return MakeRefusal(RpcCode::InvalidParams, std::format("unknown parameter '{}'", name), client_id);
            }
        }

        return MakeSuccess();
    }

    RpcResult ParseParams(const json& params, RpcRequest& request)
    {
        switch (request.Method)
        {
        case RpcMethod::Describe: return ParseDescribe(params, request);
        case RpcMethod::Get: return ParseGet(params, request);
        case RpcMethod::Set: return ParseSet(params, request);
        case RpcMethod::Call: return ParseCall(params, request);
        case RpcMethod::Ping: return CheckParamNames(params, k_PingParams, request.ClientId);
        case RpcMethod::Invalid: break;
        }

        return MakeRefusal(RpcCode::MethodNotFound, "the method does not exist", request.ClientId);
    }

    RpcResult ParseDescribe(const json& params, RpcRequest& request)
    {
        RpcResult names = CheckParamNames(params, k_DescribeParams, request.ClientId);

        if (!names.Succeeded() || !params.contains("path"))
        {
            return names;
        }

        return ReadRequiredString(params, "path", request.ClientId, request.Target);
    }

    RpcResult ParseGet(const json& params, RpcRequest& request)
    {
        RpcResult names = CheckParamNames(params, k_GetParams, request.ClientId);

        if (!names.Succeeded())
        {
            return names;
        }

        return ReadRequiredString(params, "path", request.ClientId, request.Target);
    }

    RpcResult ParseSet(const json& params, RpcRequest& request)
    {
        RpcResult names = CheckParamNames(params, k_SetParams, request.ClientId);

        if (!names.Succeeded())
        {
            return names;
        }

        RpcResult path = ReadRequiredString(params, "path", request.ClientId, request.Target);

        if (!path.Succeeded())
        {
            return path;
        }

        const json::const_iterator value = params.find("value");

        if (value == params.end())
        {
            return MakeRefusal(RpcCode::InvalidParams, "the parameter 'value' is missing", request.ClientId);
        }

        std::optional<OwnedScalar> scalar = ReadScalar(*value);

        if (!scalar.has_value())
        {
            return MakeRefusal(RpcCode::InvalidParams,
                               "value must be one scalar. Set the elements of a struct or an array by their paths",
                               request.ClientId);
        }

        request.Value = std::move(*scalar);

        return MakeSuccess();
    }

    RpcResult ParseCall(const json& params, RpcRequest& request)
    {
        RpcResult names = CheckParamNames(params, k_CallParams, request.ClientId);

        if (!names.Succeeded())
        {
            return names;
        }

        RpcResult name = ReadRequiredString(params, "name", request.ClientId, request.Target);

        if (!name.Succeeded())
        {
            return name;
        }

        const json::const_iterator arguments = params.find("arguments");

        if (arguments == params.end())
        {
            return MakeSuccess();
        }

        if (!arguments->is_object())
        {
            return MakeRefusal(RpcCode::InvalidParams, "arguments must be an object", request.ClientId);
        }

        if (arguments->empty())
        {
            return MakeSuccess();
        }

        std::string path;

        return FlattenObject(*arguments, request.ClientId, 0, path, request.Arguments);
    }

    RpcResult ReadRequiredString(const json& params, std::string_view name, int64_t client_id, std::string& text)
    {
        const json::const_iterator value = params.find(name);

        if (value == params.end() || !value->is_string())
        {
            return MakeRefusal(RpcCode::InvalidParams,
                               std::format("the parameter '{}' must be a string", name),
                               client_id);
        }

        text = value->get_ref<const std::string&>();

        return MakeSuccess();
    }

    std::optional<OwnedScalar> ReadScalar(const json& value)
    {
        if (value.is_boolean())
        {
            return OwnedScalar{ value.get<bool>() };
        }

        if (value.is_string())
        {
            return OwnedScalar{ value.get_ref<const std::string&>() };
        }

        if (value.is_number_float())
        {
            return OwnedScalar{ value.get<double>() };
        }

        // nlohmann stores every positive integer as unsigned. A signed value is what a signed field wants
        if (value.is_number_unsigned())
        {
            const uint64_t number = value.get<uint64_t>();

            if (number > uint64_t{ std::numeric_limits<int64_t>::max() })
            {
                return OwnedScalar{ number };
            }

            return OwnedScalar{ static_cast<int64_t>(number) };
        }

        if (value.is_number_integer())
        {
            return OwnedScalar{ value.get<int64_t>() };
        }

        return std::nullopt;
    }

    RpcResult FlattenObject(const json& object,
                            int64_t client_id,
                            int32_t depth,
                            std::string& path,
                            std::vector<OwnedArgument>& arguments)
    {
        const size_t parentLength = path.size();

        for (const auto& [name, value] : object.items())
        {
            if (parentLength != 0u)
            {
                path.push_back('.');
            }

            path.append(name);

            RpcResult flattened = FlattenValue(value, client_id, depth + 1, path, arguments);

            if (!flattened.Succeeded())
            {
                return flattened;
            }

            path.resize(parentLength);
        }

        return MakeSuccess();
    }

    RpcResult FlattenArray(const json& array,
                           int64_t client_id,
                           int32_t depth,
                           std::string& path,
                           std::vector<OwnedArgument>& arguments)
    {
        const size_t parentLength = path.size();

        for (size_t index = 0u; index < array.size(); ++index)
        {
            path.append(std::format("[{}]", index));

            RpcResult flattened = FlattenValue(array[index], client_id, depth + 1, path, arguments);

            if (!flattened.Succeeded())
            {
                return flattened;
            }

            path.resize(parentLength);
        }

        return MakeSuccess();
    }

    RpcResult FlattenValue(const json& value,
                           int64_t client_id,
                           int32_t depth,
                           std::string& path,
                           std::vector<OwnedArgument>& arguments)
    {
        if (depth > k_MaxArgumentDepth)
        {
            return MakeRefusal(RpcCode::InvalidParams,
                               std::format("the arguments nest deeper than {} levels", k_MaxArgumentDepth),
                               client_id);
        }

        if (value.is_structured() && value.empty())
        {
            return MakeRefusal(RpcCode::InvalidParams,
                               std::format("the argument '{}' is empty, so it would write nothing", path),
                               client_id);
        }

        if (value.is_object())
        {
            return FlattenObject(value, client_id, depth, path, arguments);
        }

        if (value.is_array())
        {
            return FlattenArray(value, client_id, depth, path, arguments);
        }

        std::optional<OwnedScalar> scalar = ReadScalar(value);

        if (!scalar.has_value())
        {
            return MakeRefusal(RpcCode::InvalidParams,
                               std::format("the argument '{}' is null, so it would write nothing", path),
                               client_id);
        }

        arguments.push_back(OwnedArgument{ .Path = path, .Value = std::move(*scalar) });

        return MakeSuccess();
    }

    int32_t WireCode(RpcCode code) noexcept
    {
        switch (code)
        {
        case RpcCode::ParseError: return -32700;
        case RpcCode::InvalidRequest: return -32600;
        case RpcCode::MethodNotFound: return -32601;
        case RpcCode::InvalidParams: return -32602;
        case RpcCode::ServerBusy: return -32001;
        case RpcCode::Invalid:
        case RpcCode::Success:
            break;
        }

        return -32603;
    }

    std::string EncodeText(std::string_view text)
    {
        // the replace handler keeps a bad byte from throwing. A message can quote a path from the client
        return json(text).dump(-1, ' ', false, json::error_handler_t::replace);
    }
}
