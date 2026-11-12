#pragma once
#ifndef DIAMOND_DOGS_TESTS_BOOTSTRAP_TEST_MODULES_HPP
#define DIAMOND_DOGS_TESTS_BOOTSTRAP_TEST_MODULES_HPP
#include "ModuleDescriptor.hpp"
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bootstrap_tests
{
    struct Alpha
    {
        Alpha() noexcept = default;
        ~Alpha();
        Alpha(const Alpha&) = delete;
        Alpha& operator=(const Alpha&) = delete;

        int32_t Value{ 7 };
    };

    struct Beta
    {
        Beta() noexcept = default;
        ~Beta();
        Beta(const Beta&) = delete;
        Beta& operator=(const Beta&) = delete;

        Alpha* Dependency{ nullptr };
    };

    extern const bootstrap::ModuleKey<Alpha> k_AlphaKey;
    extern const bootstrap::ModuleKey<Beta> k_BetaKey;

    // core: owns an Alpha
    extern const bootstrap::ModuleDescriptor k_AlphaModule;
    // core: depends on alpha, and owns a Beta that points to it
    extern const bootstrap::ModuleDescriptor k_BetaModule;
    // gpu: never started by these tests
    extern const bootstrap::ModuleDescriptor k_GammaModule;
    // core: requires alpha without listing it in DependsOn
    extern const bootstrap::ModuleDescriptor k_UndeclaredModule;
    // core: returns ModuleStartFailed
    extern const bootstrap::ModuleDescriptor k_FailingModule;
    // core: succeeds without calling Own
    extern const bootstrap::ModuleDescriptor k_OwnsNothingModule;
    // core: depends on gamma, which starts later
    extern const bootstrap::ModuleDescriptor k_NeedsGammaModule;

    /** @brief The names of the destroyed module objects, in destruction order. */
    [[nodiscard]] std::vector<std::string>& DestroyLog();

    /** An argv for LaunchStage::Start. The first argument is the executable name, as in a real argv. */
    class TestArguments
    {
    public:
        explicit TestArguments(std::initializer_list<std::string_view> values);
        TestArguments(const TestArguments&) = delete;
        TestArguments& operator=(const TestArguments&) = delete;

        [[nodiscard]] std::span<char* const> Span() const noexcept;

    private:
        std::vector<std::string> storage;
        std::vector<char*> pointers;
    };

    /** @brief Writes `text` to a file in the temp folder, and returns its path. */
    [[nodiscard]] std::filesystem::path WriteConfig(std::string_view file_name, std::string_view text);
}

#endif // !DIAMOND_DOGS_TESTS_BOOTSTRAP_TEST_MODULES_HPP
