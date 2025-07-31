#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Main entry point for foundation unit tests
// GTest will automatically discover and run all tests
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
