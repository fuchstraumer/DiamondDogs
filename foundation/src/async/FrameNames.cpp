#include "async/FrameNames.hpp"
#include <cstdint>
#include <format>
#include <string>

#ifndef FOUNDATION_ASYNC_SYMBOLS
#define FOUNDATION_ASYNC_SYMBOLS 0
#endif // !FOUNDATION_ASYNC_SYMBOLS

#if FOUNDATION_ASYNC_SYMBOLS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#endif // FOUNDATION_ASYNC_SYMBOLS

namespace
{
#if FOUNDATION_ASYNC_SYMBOLS
    // the first word of a coroutine frame is the pointer to the resume function
    const void* ReadResumeAddress(const void* frame_address) noexcept;
    std::string LookUpSymbol(void* session, const void* address);
#endif // FOUNDATION_ASYNC_SYMBOLS
}

namespace foundation
{

FrameSymbolizer::FrameSymbolizer() noexcept
{
}

FrameSymbolizer::~FrameSymbolizer()
{
#if FOUNDATION_ASYNC_SYMBOLS
    if (session != nullptr)
    {
        SymCleanup(static_cast<HANDLE>(session));
        CloseHandle(static_cast<HANDLE>(session));
    }
#endif // FOUNDATION_ASYNC_SYMBOLS
}

std::string FrameSymbolizer::DescribeFrame(const void* frame_address)
{
#if FOUNDATION_ASYNC_SYMBOLS
    if (!sessionAttempted)
    {
        initializeSession();
    }

    if (session != nullptr && frame_address != nullptr)
    {
        const void* resumeAddress = ReadResumeAddress(frame_address);
        std::string name = LookUpSymbol(session, resumeAddress);

        if (!name.empty())
        {
            return name;
        }
    }
#endif // FOUNDATION_ASYNC_SYMBOLS

    return std::format("{}", frame_address);
}

void FrameSymbolizer::initializeSession() noexcept
{
    sessionAttempted = true;

#if FOUNDATION_ASYNC_SYMBOLS
    // Another library may already have called SymInitialize for the pseudo handle of this process, and a second call
    // for the same handle fails. A duplicate of the handle is a different key, so this session is ours alone.
    HANDLE self = GetCurrentProcess();
    HANDLE duplicate = nullptr;

    if (!DuplicateHandle(self, self, self, &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        return;
    }

    SymSetOptions(SymGetOptions() | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);

    if (!SymInitialize(duplicate, nullptr, TRUE))
    {
        CloseHandle(duplicate);
        return;
    }

    session = duplicate;
#endif // FOUNDATION_ASYNC_SYMBOLS
}

}

#if FOUNDATION_ASYNC_SYMBOLS
namespace
{
    const void* ReadResumeAddress(const void* frame_address) noexcept
    {
        const void* const* firstWord = static_cast<const void* const*>(frame_address);
        return *firstWord;
    }

    std::string LookUpSymbol(void* session, const void* address)
    {
        struct SymbolBuffer
        {
            SYMBOL_INFO Info;
            char NameStorage[MAX_SYM_NAME];
        };

        SymbolBuffer buffer{};
        buffer.Info.SizeOfStruct = sizeof(SYMBOL_INFO);
        buffer.Info.MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement = 0;

        if (!SymFromAddr(static_cast<HANDLE>(session), reinterpret_cast<DWORD64>(address), &displacement,
                         &buffer.Info))
        {
            return {};
        }

        std::string name(buffer.Info.Name, buffer.Info.NameLen);

        if (displacement != 0)
        {
            name += std::format("+{:#x}", static_cast<uint64_t>(displacement));
        }

        return name;
    }
}
#endif // FOUNDATION_ASYNC_SYMBOLS
