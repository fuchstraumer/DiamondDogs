#include <gtest/gtest.h>
#include <gmock/gmock.h>

#if defined(_MSC_VER)
#include <crtdbg.h>
#include <cstdlib>
#endif

int main(int argc, char** argv)
{
#if defined(_MSC_VER)
    // a Debug assert prints to stderr and aborts: a dialog would block an unattended run
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
#endif

    ::testing::InitGoogleTest(&argc, argv);
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
