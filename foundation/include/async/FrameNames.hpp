#pragma once
#ifndef FOUNDATION_ASYNC_FRAME_NAMES_HPP
#define FOUNDATION_ASYNC_FRAME_NAMES_HPP
#include <string>

namespace foundation
{

/**@brief Turns the address of a coroutine frame into the name of its function. Uses
 * Dbghelp on Windows, elsewhere just uses the frame address in hex. */
class FrameSymbolizer
{
public:
    FrameSymbolizer() noexcept;
    ~FrameSymbolizer();
    FrameSymbolizer(const FrameSymbolizer&) = delete;
    FrameSymbolizer& operator=(const FrameSymbolizer&) = delete;
    FrameSymbolizer(FrameSymbolizer&&) = delete;
    FrameSymbolizer& operator=(FrameSymbolizer&&) = delete;

    /**@brief Returns the function name for a live frame.
     * @note The first call starts a DbgHelp session, which is slow. */
    [[nodiscard]] std::string DescribeFrame(const void* frame_address);

private:
    void initializeSession() noexcept;

    void* session{ nullptr };
    bool sessionAttempted{ false };
};

}

#endif // !FOUNDATION_ASYNC_FRAME_NAMES_HPP
