#pragma once
#ifndef DIAMOND_DOGS_PLANET_PASS_CHECKS_HPP
#define DIAMOND_DOGS_PLANET_PASS_CHECKS_HPP
#include <span>
#include <string_view>

namespace planet
{
    struct CheckContext;

    using CheckFn = bool (*)(CheckContext&);

    struct NamedCheck
    {
        std::string_view Name;
        CheckFn Run{ nullptr };
    };

    [[nodiscard]] std::span<const std::string_view> AllCheckEntryPoints() noexcept;
    [[nodiscard]] std::span<const NamedCheck> AllChecks() noexcept;

    [[nodiscard]] bool CheckMap(CheckContext& context);
    [[nodiscard]] bool CheckOwners(CheckContext& context);
    [[nodiscard]] bool CheckFront(CheckContext& context);
    [[nodiscard]] bool CheckCrust(CheckContext& context);
    [[nodiscard]] bool CheckLiveCells(CheckContext& context);
    [[nodiscard]] bool CheckFrontDistance(CheckContext& context);
    [[nodiscard]] bool CheckUplift(CheckContext& context);
    [[nodiscard]] bool CheckTorqueSums(CheckContext& context);
    [[nodiscard]] bool CheckChain(CheckContext& context);
}

#endif // !DIAMOND_DOGS_PLANET_PASS_CHECKS_HPP
