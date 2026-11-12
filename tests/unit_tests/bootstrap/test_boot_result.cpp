#include "BootResult.hpp"
#include <gtest/gtest.h>
#include <set>
#include <string>

using bootstrap::BootResult;
using bootstrap::BootResultCode;
using bootstrap::Stage;

TEST(BootResult, SuccessInSucceeds)
{
    const BootResult result = bootstrap::SuccessIn(Stage::Core);

    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.FailedStage, Stage::Core);
    EXPECT_FALSE(BootResult{}.Succeeded());
}

TEST(BootResult, EveryCodeHasItsOwnName)
{
    std::set<std::string_view> names;
    const int32_t lastCode = static_cast<int32_t>(BootResultCode::ResourceContextFailed);

    for (int32_t code = 0; code <= lastCode; ++code)
    {
        const std::string_view name = bootstrap::ToString(static_cast<BootResultCode>(code));

        EXPECT_TRUE(names.insert(name).second) << name;
    }
}

TEST(BootResult, FormatFailureListsEveryField)
{
    const BootResult failure
    {
        .Code = BootResultCode::UnknownModule,
        .FailedStage = Stage::Launch,
        .Module = "beta",
        .Subject = "alpah",
        .Source = "DependsOn",
        .Suggestions = { "alpha" },
    };
    const std::string text = bootstrap::FormatFailure(failure);

    EXPECT_TRUE(text.starts_with("boot failed in Launch: UnknownModule")) << text;
    EXPECT_NE(text.find("beta"), std::string::npos) << text;
    EXPECT_NE(text.find("alpah"), std::string::npos) << text;
    EXPECT_NE(text.find("DependsOn"), std::string::npos) << text;
    EXPECT_NE(text.find("did you mean: alpha"), std::string::npos) << text;
}

TEST(BootResult, FormatFailureSkipsEmptyFields)
{
    const BootResult failure{ .Code = BootResultCode::ProfileWithoutConfig, .FailedStage = Stage::Launch };
    const std::string text = bootstrap::FormatFailure(failure);

    EXPECT_EQ(text, "boot failed in Launch: ProfileWithoutConfig");
}
