#pragma once
#ifndef DIAMOND_DOGS_RHI_COMMAND_BUFFER_HPP
#define DIAMOND_DOGS_RHI_COMMAND_BUFFER_HPP
#include "RhiHandle.hpp"
#include "RhiResult.hpp"

namespace rhi
{

    /** @brief An RAII wrapper for CommandBuffers, mostly ensuring that Begin/End semantics stay the same regardless of backend API.
     *  Note that CommandBuffers themselves are not copyable or movable, as they represent a unique recording context. If a CommandBuffer
     *  exits scope while still in recording mode in Graphics debug builds, it will throw an assertion failure to help catch bugs early.
     *
     *  This class is mostly required because on DX12, command buffers are actually command lists, which have different rules and lifetimes relative
     *  to Vulkan command buffers, which are more independent objects that belong to a parent pool (almost entirely unlike DX12 command lists). If
     *  we didn't use this wrapper, users would have to deal with the differences in semantics themselves, which would be error-prone and tedious.
     */
    struct CommandBuffer
    {
        CommandBuffer(CommandBufferHandle handle) noexcept;
        ~CommandBuffer();
        CommandBuffer(const CommandBuffer&) = delete;
        CommandBuffer& operator=(const CommandBuffer&) = delete;
        CommandBuffer(CommandBuffer&& other) noexcept;
        CommandBuffer& operator=(CommandBuffer&& other) noexcept;

        CommandBufferHandle Handle() const noexcept;
        Result Begin() noexcept;
        Result End() noexcept;

    private:
        bool isRecording = false;
        CommandBufferHandle handle;
    };

}

#endif // !DIAMOND_DOGS_RHI_COMMAND_BUFFER_HPP
