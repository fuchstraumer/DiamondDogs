#include <gtest/gtest.h>
#include "LoomDiagnostic.hpp"
#include "JsonWriter.hpp"
#include <cstdint>
#include <format>
#include <source_location>
#include <string>

namespace
{

    loom::Diagnostic MakeDiagnostic(loom::DiagnosticCode code, loom::DiagnosticSeverity severity)
    {
        loom::Diagnostic diagnostic{};
        diagnostic.Code = code;
        diagnostic.Severity = severity;
        return diagnostic;
    }

}

TEST(LoomCatalog, NamesSeveritiesAndSummaries)
{
    EXPECT_EQ(loom::ToString(loom::DiagnosticCode::LM101), "LM101");
    EXPECT_EQ(loom::ToString(loom::DiagnosticCode::LM207), "LM207");
    EXPECT_EQ(loom::DefaultSeverityOf(loom::DiagnosticCode::LM206), loom::DiagnosticSeverity::Warning);
    EXPECT_EQ(loom::DefaultSeverityOf(loom::DiagnosticCode::LM207), loom::DiagnosticSeverity::Error);
    EXPECT_EQ(loom::SummaryOf(loom::DiagnosticCode::LM108), "A graph pin is set twice.");

    // A value that is not in the catalog gets "Invalid" from every lookup.
    EXPECT_EQ(loom::ToString(loom::DiagnosticCode::Invalid), "Invalid");
    EXPECT_EQ(loom::ToString(static_cast<loom::DiagnosticCode>(150)), "Invalid");
    EXPECT_EQ(loom::SummaryOf(static_cast<loom::DiagnosticCode>(150)), "Invalid");
    EXPECT_EQ(loom::DefaultSeverityOf(static_cast<loom::DiagnosticCode>(150)), loom::DiagnosticSeverity::Invalid);
    EXPECT_EQ(loom::ToString(static_cast<loom::DiagnosticStage>(9)), "Invalid");
}

TEST(LoomDiagnosticSink, CountsBySeverity)
{
    loom::CollectingSink sink;
    sink.Report(MakeDiagnostic(loom::DiagnosticCode::LM101, loom::DiagnosticSeverity::Error));
    sink.Report(MakeDiagnostic(loom::DiagnosticCode::LM101, loom::DiagnosticSeverity::Fatal));
    sink.Report(MakeDiagnostic(loom::DiagnosticCode::LM504, loom::DiagnosticSeverity::Warning));
    sink.Report(MakeDiagnostic(loom::DiagnosticCode::LM206, loom::DiagnosticSeverity::Note));

    // A record with no severity is a Loom fault, and counts as an error.
    sink.Report(MakeDiagnostic(loom::DiagnosticCode::LM102, loom::DiagnosticSeverity::Invalid));

    EXPECT_EQ(sink.ErrorCount(), 3);
    EXPECT_EQ(sink.WarningCount(), 1);
    EXPECT_EQ(sink.NoteCount(), 1);

    ASSERT_EQ(sink.Diagnostics().size(), 5u);
    EXPECT_EQ(sink.Diagnostics()[2].Code, loom::DiagnosticCode::LM504);

    sink.Clear();
    EXPECT_TRUE(sink.Diagnostics().empty());
}

TEST(LoomDiagnosticText, FirstLineHasCompilerShape)
{
    loom::Diagnostic diagnostic = MakeDiagnostic(loom::DiagnosticCode::LM101, loom::DiagnosticSeverity::Error);
    diagnostic.Where = std::source_location::current();

    const std::string text = loom::FormatText(diagnostic);
    const std::string expected = std::format("{}({}): error LM101 [Freeze]: A required field is Invalid.\n",
        diagnostic.Where.file_name(), diagnostic.Where.line());

    EXPECT_EQ(text, expected);
}

TEST(LoomDiagnosticText, PrintsOnlyTheFieldsThatAreSet)
{
    loom::Diagnostic diagnostic = MakeDiagnostic(loom::DiagnosticCode::LM109, loom::DiagnosticSeverity::Error);
    diagnostic.Resource = loom::KeyRef{ .Name = "Ocean.Spectrum", .Hash = 1u };
    diagnostic.Expected = loom::DiagnosticValue{ .FieldName = "Layers", .Kind = loom::DiagnosticValueKind::Text, .Text = ">= 1" };
    diagnostic.Actual = loom::DiagnosticValue{ .FieldName = "Layers", .Kind = loom::DiagnosticValueKind::Integer, .Integer = 0 };
    diagnostic.Suggestion = "Ocean.SpectrumParams";

    const std::string expected =
        "error LM109 [Freeze]: A value is out of range.\n"
        "    key: Ocean.Spectrum\n"
        "    expected: Layers = >= 1\n"
        "    actual: Layers = 0\n"
        "    did you mean: Ocean.SpectrumParams?\n";

    EXPECT_EQ(loom::FormatText(diagnostic), expected);
}

TEST(LoomDiagnosticText, FieldWithNoValue)
{
    loom::Diagnostic diagnostic = MakeDiagnostic(loom::DiagnosticCode::LM101, loom::DiagnosticSeverity::Error);
    diagnostic.Expected = loom::DiagnosticValue{ .FieldName = "Lifetime" };

    EXPECT_EQ(loom::FormatText(diagnostic), "error LM101 [Freeze]: A required field is Invalid.\n    expected: Lifetime\n");
}

TEST(LoomDiagnosticJson, OneCompactLine)
{
    loom::Diagnostic diagnostic = MakeDiagnostic(loom::DiagnosticCode::LM305, loom::DiagnosticSeverity::Error);
    diagnostic.Pass = loom::KeyRef{ .Name = "Ocean.SpectrumInit", .Hash = 0xABu };
    diagnostic.Binding = "Params";
    diagnostic.Expected = loom::DiagnosticValue{ .FieldName = "WindSpeed", .Kind = loom::DiagnosticValueKind::Integer, .Integer = 8 };
    diagnostic.Actual = loom::DiagnosticValue{ .FieldName = "WindSpeed", .Kind = loom::DiagnosticValueKind::Integer, .Integer = 52 };

    const std::string expected =
        R"({"code":"LM305","stage":"Bind","severity":"Error",)"
        R"("summary":"A params struct field differs from the reflected member in name, offset, or size.",)"
        R"("pass":{"name":"Ocean.SpectrumInit","hash":"00000000000000ab"},"binding":"Params",)"
        R"("expected":{"field":"WindSpeed","integer":8},"actual":{"field":"WindSpeed","integer":52}})"
        "\n";

    EXPECT_EQ(loom::FormatJson(diagnostic), expected);
}

TEST(LoomDiagnosticJson, SinkWritesOneLinePerRecord)
{
    loom::JsonSink sink;
    sink.Report(MakeDiagnostic(loom::DiagnosticCode::LM504, loom::DiagnosticSeverity::Warning));
    sink.Report(MakeDiagnostic(loom::DiagnosticCode::LM505, loom::DiagnosticSeverity::Warning));

    const std::string_view text = sink.Text();
    int32_t lineCount = 0;

    for (char c : text)
    {
        if (c == '\n')
        {
            ++lineCount;
        }

    }

    EXPECT_EQ(lineCount, 2);
    EXPECT_EQ(sink.WarningCount(), 2);
}

TEST(LoomJsonWriter, EscapesControlCharactersWithTwoHexDigits)
{
    loom::detail::JsonWriter writer{ loom::detail::JsonStyle::Compact };
    writer.String(std::string_view{ "a\x01" "b\x1f" "c\\d\"e", 9 });

    const loom::detail::JsonResult<std::string> document = writer.Finish();
    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(*document, R"("a\u0001b\u001fc\\d\"e")");
}

TEST(LoomJsonWriter, UnclosedContainerFails)
{
    loom::detail::JsonWriter writer{ loom::detail::JsonStyle::Compact };
    writer.BeginObject();
    writer.KeyInt("a", 1);

    const loom::detail::JsonResult<std::string> document = writer.Finish();
    ASSERT_FALSE(document.has_value());
    EXPECT_EQ(document.error(), loom::detail::JsonWriterError::UnbalancedContainers);
}

TEST(LoomJsonWriter, PrettyStyleIndents)
{
    loom::detail::JsonWriter writer{ loom::detail::JsonStyle::Pretty };
    writer.BeginObject();
    writer.KeyInt("a", 1);
    writer.Key("b");
    writer.BeginArray();
    writer.Bool(true);
    writer.EndArray();
    writer.EndObject();

    const loom::detail::JsonResult<std::string> document = writer.Finish();
    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(*document, "{\n    \"a\": 1,\n    \"b\": [\n        true\n    ]\n}");
}
