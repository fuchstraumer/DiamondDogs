#include "ToolRpc.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace
{
    using agent::rpc::FramedLine;
    using agent::rpc::LineFramer;
    using agent::rpc::LineKind;
    using agent::rpc::OwnedArgument;
    using agent::rpc::OwnedScalar;
    using agent::rpc::ParseRequest;
    using agent::rpc::RpcCode;
    using agent::rpc::RpcMethod;
    using agent::rpc::RpcRequest;
    using agent::rpc::RpcResult;

    [[nodiscard]] RpcResult RefusalOf(std::string_view line);
    [[nodiscard]] RpcRequest CallWithArguments(std::string_view arguments_json);
    [[nodiscard]] const OwnedArgument* FindArgument(const RpcRequest& request, std::string_view path);
}

TEST(ToolRpcParse, ReadsEachMethod)
{
    const std::expected<RpcRequest, RpcResult> describe =
        ParseRequest(R"({"jsonrpc":"2.0","id":1,"method":"tool.describe"})");
    const std::expected<RpcRequest, RpcResult> describePath =
        ParseRequest(R"({"jsonrpc":"2.0","id":2,"method":"tool.describe","params":{"path":"a.b"}})");
    const std::expected<RpcRequest, RpcResult> get =
        ParseRequest(R"({"jsonrpc":"2.0","id":3,"method":"tool.get","params":{"path":"a.b"}})");
    const std::expected<RpcRequest, RpcResult> set =
        ParseRequest(R"({"jsonrpc":"2.0","id":4,"method":"tool.set","params":{"path":"a.b","value":2.5}})");
    const std::expected<RpcRequest, RpcResult> call =
        ParseRequest(R"({"jsonrpc":"2.0","id":5,"method":"tool.call","params":{"name":"go"}})");
    const std::expected<RpcRequest, RpcResult> ping =
        ParseRequest(R"({"jsonrpc":"2.0","id":6,"method":"server.ping"})");

    ASSERT_TRUE(describe.has_value());
    EXPECT_EQ(describe->Method, RpcMethod::Describe);
    EXPECT_TRUE(describe->Target.empty());

    ASSERT_TRUE(describePath.has_value());
    EXPECT_EQ(describePath->Target, "a.b");

    ASSERT_TRUE(get.has_value());
    EXPECT_EQ(get->Method, RpcMethod::Get);
    EXPECT_EQ(get->ClientId, 3);

    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->Method, RpcMethod::Set);
    EXPECT_EQ(std::get<double>(set->Value), 2.5);

    ASSERT_TRUE(call.has_value());
    EXPECT_EQ(call->Method, RpcMethod::Call);
    EXPECT_EQ(call->Target, "go");
    EXPECT_TRUE(call->Arguments.empty());

    ASSERT_TRUE(ping.has_value());
    EXPECT_EQ(ping->Method, RpcMethod::Ping);
}

TEST(ToolRpcParse, RefusesEachMalformedEnvelope)
{
    EXPECT_EQ(RefusalOf("not json").Code, RpcCode::ParseError);
    EXPECT_EQ(RefusalOf("").Code, RpcCode::ParseError);
    EXPECT_EQ(RefusalOf(R"([{"jsonrpc":"2.0","id":1,"method":"server.ping"}])").Code, RpcCode::InvalidRequest);
    EXPECT_EQ(RefusalOf("7").Code, RpcCode::InvalidRequest);
    EXPECT_EQ(RefusalOf(R"({"jsonrpc":"2.0","method":"server.ping"})").Code, RpcCode::InvalidRequest);
    EXPECT_EQ(RefusalOf(R"({"jsonrpc":"2.0","id":"7","method":"server.ping"})").Code, RpcCode::InvalidRequest);
    EXPECT_EQ(RefusalOf(R"({"jsonrpc":"2.0","id":1.5,"method":"server.ping"})").Code, RpcCode::InvalidRequest);
    EXPECT_EQ(RefusalOf(R"({"jsonrpc":"2.0","id":18446744073709551615,"method":"server.ping"})").Code,
              RpcCode::InvalidRequest);
    EXPECT_EQ(RefusalOf(R"({"jsonrpc":"1.0","id":1,"method":"server.ping"})").Code, RpcCode::InvalidRequest);
    EXPECT_EQ(RefusalOf(R"({"jsonrpc":"2.0","id":1,"method":7})").Code, RpcCode::InvalidRequest);
    EXPECT_EQ(RefusalOf(R"({"jsonrpc":"2.0","id":1,"method":"nope"})").Code, RpcCode::MethodNotFound);
}

TEST(ToolRpcParse, ARefusalKeepsTheIdWhenTheIdIsUsable)
{
    EXPECT_EQ(RefusalOf(R"({"jsonrpc":"2.0","id":9,"method":"nope"})").ClientId, 9);
    EXPECT_EQ(RefusalOf(R"({"jsonrpc":"2.0","id":-9,"method":"nope"})").ClientId, -9);
    EXPECT_EQ(RefusalOf("not json").ClientId, std::nullopt);
}

TEST(ToolRpcParse, RefusesBadParams)
{
    const std::string_view lines[]
    {
        R"({"jsonrpc":"2.0","id":1,"method":"tool.get"})",
        R"({"jsonrpc":"2.0","id":1,"method":"tool.get","params":[1]})",
        R"({"jsonrpc":"2.0","id":1,"method":"tool.get","params":{"path":7}})",
        R"({"jsonrpc":"2.0","id":1,"method":"tool.get","params":{"path":"a","extra":1}})",
        R"({"jsonrpc":"2.0","id":1,"method":"tool.set","params":{"path":"a"}})",
        R"({"jsonrpc":"2.0","id":1,"method":"tool.set","params":{"path":"a","value":null}})",
        R"({"jsonrpc":"2.0","id":1,"method":"tool.set","params":{"path":"a","value":[1]}})",
        R"({"jsonrpc":"2.0","id":1,"method":"tool.set","params":{"path":"a","value":{"b":1}}})",
        R"({"jsonrpc":"2.0","id":1,"method":"tool.call","params":{}})",
        R"({"jsonrpc":"2.0","id":1,"method":"tool.call","params":{"name":"go","arguments":[1]}})",
        R"({"jsonrpc":"2.0","id":1,"method":"server.ping","params":{"x":1}})",
    };

    for (const std::string_view line : lines)
    {
        EXPECT_EQ(RefusalOf(line).Code, RpcCode::InvalidParams) << line;
    }
}

TEST(ToolRpcFlatten, ObjectsBecomeDotsAndArraysBecomeIndices)
{
    const RpcRequest request = CallWithArguments(R"({"Eye":[1,2],"Lens":{"Fov":60.5,"Tags":[{"On":true}]}})");

    ASSERT_EQ(request.Arguments.size(), 4u);

    const OwnedArgument* first = FindArgument(request, "Eye[0]");
    const OwnedArgument* second = FindArgument(request, "Eye[1]");
    const OwnedArgument* fov = FindArgument(request, "Lens.Fov");
    const OwnedArgument* tag = FindArgument(request, "Lens.Tags[0].On");

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(fov, nullptr);
    ASSERT_NE(tag, nullptr);
    EXPECT_EQ(std::get<int64_t>(first->Value), 1);
    EXPECT_EQ(std::get<int64_t>(second->Value), 2);
    EXPECT_EQ(std::get<double>(fov->Value), 60.5);
    EXPECT_TRUE(std::get<bool>(tag->Value));
}

TEST(ToolRpcFlatten, EachScalarKeepsItsType)
{
    const RpcRequest request =
        CallWithArguments(R"({"A":-3,"B":3,"C":18446744073709551615,"D":0.5,"E":false,"F":"Calm"})");

    EXPECT_EQ(std::get<int64_t>(FindArgument(request, "A")->Value), -3);
    EXPECT_EQ(std::get<int64_t>(FindArgument(request, "B")->Value), 3);
    EXPECT_EQ(std::get<uint64_t>(FindArgument(request, "C")->Value), 18446744073709551615ull);
    EXPECT_EQ(std::get<double>(FindArgument(request, "D")->Value), 0.5);
    EXPECT_FALSE(std::get<bool>(FindArgument(request, "E")->Value));
    EXPECT_EQ(std::get<std::string>(FindArgument(request, "F")->Value), "Calm");
}

TEST(ToolRpcFlatten, RefusesAnythingThatWouldWriteNothing)
{
    const std::string_view prefix{ R"({"jsonrpc":"2.0","id":1,"method":"tool.call","params":{"name":"go","arguments":)" };
    const std::string_view argumentSets[]
    {
        R"({"A":null})",
        R"({"A":[]})",
        R"({"A":{}})",
        R"({"A":[1,null]})",
        R"({"A":{"B":[]}})",
    };

    for (const std::string_view arguments : argumentSets)
    {
        const std::string line = std::string{ prefix } + std::string{ arguments } + "}}";

        const RpcResult refusal = RefusalOf(line);

        EXPECT_EQ(refusal.Code, RpcCode::InvalidParams) << arguments;
        EXPECT_NE(refusal.Message.find('A'), std::string::npos) << refusal.Message;
    }
}

TEST(ToolRpcFlatten, AnEmptyArgumentObjectMeansNoArguments)
{
    EXPECT_TRUE(CallWithArguments("{}").Arguments.empty());
}

// the walk recurses, so a depth limit is what stops a small line from overflowing the socket thread's stack
TEST(ToolRpcFlatten, RefusesAHugeNestingWithoutACrash)
{
    constexpr int32_t k_Depth = 200000;

    std::string line{ R"({"jsonrpc":"2.0","id":1,"method":"tool.call","params":{"name":"go","arguments":{"A":)" };

    line.append(k_Depth, '[');
    line.push_back('1');
    line.append(k_Depth, ']');
    line.append("}}}");

    EXPECT_EQ(RefusalOf(line).Code, RpcCode::InvalidParams);
}

TEST(ToolRpcFormat, ProducesValidJsonOnOneLine)
{
    const std::string result = agent::rpc::FormatResult(7, R"({"Length":5})");
    const std::string failure =
        agent::rpc::FormatToolFailure(8, "a \"quoted\"\nmessage", R"({"Code":"ValueRefused"})");
    const std::string refusal = agent::rpc::FormatRefusal(RpcResult
    {
        .Code = RpcCode::ParseError,
        .Message = "bad byte \xFF here",
    });

    for (const std::string* line : { &result, &failure, &refusal })
    {
        ASSERT_EQ(line->back(), '\n');
        EXPECT_EQ(std::ranges::count(*line, '\n'), 1);
        EXPECT_FALSE(nlohmann::json::parse(*line, nullptr, false).is_discarded()) << *line;
    }

    const nlohmann::json resultJson = nlohmann::json::parse(result);
    const nlohmann::json failureJson = nlohmann::json::parse(failure);
    const nlohmann::json refusalJson = nlohmann::json::parse(refusal);

    EXPECT_EQ(resultJson["jsonrpc"], "2.0");
    EXPECT_EQ(resultJson["id"], 7);
    EXPECT_EQ(resultJson["result"]["Length"], 5);
    EXPECT_EQ(failureJson["id"], 8);
    EXPECT_EQ(failureJson["error"]["code"], -32000);
    EXPECT_EQ(failureJson["error"]["message"], "a \"quoted\"\nmessage");
    EXPECT_EQ(failureJson["error"]["data"]["Code"], "ValueRefused");
    EXPECT_TRUE(refusalJson["id"].is_null());
    EXPECT_EQ(refusalJson["error"]["code"], -32700);
}

TEST(ToolRpcValue, ViewOfPointsAtTheOwnedString)
{
    const OwnedScalar owned{ std::string{ "a string that is too long for the small string buffer" } };

    const foundation::reflect::ScalarValue view = agent::rpc::ViewOf(owned);

    EXPECT_EQ(std::get<std::string_view>(view).data(), std::get<std::string>(owned).data());
    EXPECT_EQ(std::get<double>(agent::rpc::ViewOf(OwnedScalar{ 2.5 })), 2.5);
    EXPECT_EQ(std::get<int64_t>(agent::rpc::ViewOf(OwnedScalar{ int64_t{ -2 } })), -2);
    EXPECT_EQ(std::get<uint64_t>(agent::rpc::ViewOf(OwnedScalar{ uint64_t{ 2u } })), 2u);
    EXPECT_TRUE(std::get<bool>(agent::rpc::ViewOf(OwnedScalar{ true })));
}

TEST(LineFramerTest, SplitsLinesAcrossAndWithinAppends)
{
    LineFramer framer{ 100 };

    const std::string_view first{ "one\ntw" };
    const std::string_view second{ "o\nthree\n" };

    framer.Append(std::as_bytes(std::span{ first }));

    std::optional<FramedLine> line = framer.Next();

    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(line->Text, "one");
    EXPECT_FALSE(framer.Next().has_value());

    framer.Append(std::as_bytes(std::span{ second }));

    line = framer.Next();

    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(line->Text, "two");

    line = framer.Next();

    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(line->Text, "three");
    EXPECT_FALSE(framer.Next().has_value());
}

TEST(LineFramerTest, TheLimitIsInclusive)
{
    LineFramer framer{ 5 };

    const std::string_view atLimit{ "12345\n" };
    const std::string_view overLimit{ "123456\n" };

    framer.Append(std::as_bytes(std::span{ atLimit }));

    std::optional<FramedLine> line = framer.Next();

    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(line->Kind, LineKind::Complete);
    EXPECT_EQ(line->Text, "12345");

    framer.Append(std::as_bytes(std::span{ overLimit }));

    line = framer.Next();

    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(line->Kind, LineKind::Oversize);
}

TEST(LineFramerTest, AnOversizeLineReportsOnceAndTheNextLineSurvives)
{
    LineFramer framer{ 4 };

    const std::string_view start{ "123456" };
    const std::string_view middle{ "789" };
    const std::string_view end{ "0\nok\n" };

    framer.Append(std::as_bytes(std::span{ start }));

    std::optional<FramedLine> line = framer.Next();

    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(line->Kind, LineKind::Oversize);
    EXPECT_FALSE(framer.Next().has_value());

    framer.Append(std::as_bytes(std::span{ middle }));
    EXPECT_FALSE(framer.Next().has_value());

    framer.Append(std::as_bytes(std::span{ end }));

    line = framer.Next();

    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(line->Kind, LineKind::Complete);
    EXPECT_EQ(line->Text, "ok");
    EXPECT_FALSE(framer.Next().has_value());
}

namespace
{
    RpcResult RefusalOf(std::string_view line)
    {
        std::expected<RpcRequest, RpcResult> request = ParseRequest(line);

        if (request.has_value())
        {
            return RpcResult{ .Code = RpcCode::Success, .Message = "the line was accepted" };
        }

        return request.error();
    }

    RpcRequest CallWithArguments(std::string_view arguments_json)
    {
        const std::string line =
            std::string{ R"({"jsonrpc":"2.0","id":1,"method":"tool.call","params":{"name":"go","arguments":)" } +
            std::string{ arguments_json } + "}}";

        std::expected<RpcRequest, RpcResult> request = ParseRequest(line);

        if (!request.has_value())
        {
            ADD_FAILURE() << request.error().Message;
            return RpcRequest{};
        }

        return *request;
    }

    const OwnedArgument* FindArgument(const RpcRequest& request, std::string_view path)
    {
        const auto matchesPath = [path](const OwnedArgument& argument) { return argument.Path == path; };
        const auto found = std::ranges::find_if(request.Arguments, matchesPath);

        return found == request.Arguments.end() ? nullptr : &*found;
    }
}
