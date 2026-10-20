#pragma once
#ifndef DIAMOND_DOGS_INTEGRATION_TESTS_FRAME_LIMIT_HPP
#define DIAMOND_DOGS_INTEGRATION_TESTS_FRAME_LIMIT_HPP
#include <cstdint>

/** @brief Returns N from a `--frames=N` argument, or -1 (run until the window closes) when there is none or it is
 *  malformed. Windowed tests take it so that an unattended run (an agent, or CI) can use them as smoke tests. */
[[nodiscard]] int32_t ParseFrameLimit(int argc, char* argv[]);

#endif // !DIAMOND_DOGS_INTEGRATION_TESTS_FRAME_LIMIT_HPP
