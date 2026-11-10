#pragma once
#ifndef DIAMOND_DOGS_AGENT_TOOLS_TOOL_LINE_HPP
#define DIAMOND_DOGS_AGENT_TOOLS_TOOL_LINE_HPP
#include <cstdint>
#include <string_view>

namespace agent
{
    class ToolRegistry;
    class ReplySink;
    /** @brief Runs one request from a line of text, so that a script or a console can reach the registry. The
     *  answer arrives at the sink as JSON.
     *
     *    describe [path or command]
     *    get path
     *    set path value
     *    call command [argument=value ...]
     *
     *  A value is true, false, a number, or otherwise a string with no spaces. */
    void RunToolLine(ToolRegistry& registry, std::string_view line, ReplySink& sink, int64_t request_id);
}

#endif // !DIAMOND_DOGS_AGENT_TOOLS_TOOL_LINE_HPP
