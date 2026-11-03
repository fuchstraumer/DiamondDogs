#pragma once
#ifndef DIAMOND_DOGS_INPUT_SYSTEM_INPUT_LOG_HPP
#define DIAMOND_DOGS_INPUT_SYSTEM_INPUT_LOG_HPP
#include "InputTypes.hpp"
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace input
{
    /** @brief Why ReadInputLog rejected a line. */
    enum class InputLogError : uint8_t
    {
        Invalid = 0,
        Success = 1,
        /** @brief A line is not a JSON object on one line. */
        MalformedLine,
        /** @brief A line lacks one of the keys of a record. */
        MissingKey,
        UnknownControl,
        UnknownKind,
        UnknownDevice,
        /** @brief A number is not valid text, or its value does not fit the field. */
        MalformedNumber,
    };

    template<typename T>
    using InputLogResult = std::expected<T, InputLogError>;

    [[nodiscard]] std::string_view ToString(InputLogError error) noexcept;

    /** @brief Returns the events as JSON Lines text, one record per event. The caller writes it in one operation. */
    [[nodiscard]] std::string WriteInputLog(std::span<const InputEvent> events);

    /** @brief Parses JSON Lines text that WriteInputLog wrote. Blank lines are skipped. */
    [[nodiscard]] InputLogResult<std::vector<InputEvent>> ReadInputLog(std::string_view text);
}

#endif // !DIAMOND_DOGS_INPUT_SYSTEM_INPUT_LOG_HPP
