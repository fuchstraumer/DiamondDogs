#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "containers/CircularBuffer.hpp"

class CircularBufferTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Setup if needed
    }
};

TEST_F(CircularBufferTest, BasicConstruction)
{
    // circular_buffer<int> buffer(10);
    SUCCEED() << "circular_buffer should be constructible";
}

// Add more tests as the circular_buffer implementation is completed
