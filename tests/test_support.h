#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace test_support {

using TestCase = std::pair<std::string, std::function<void()>>;

inline void Fail(const char* expression, const char* file, int line) {
    throw std::runtime_error(
        std::string(file) + ":" + std::to_string(line) +
        " check failed: " + expression);
}

inline int Run(const std::vector<TestCase>& tests) {
    std::size_t passed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            ++passed;
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << test.first << ": "
                      << error.what() << '\n';
        } catch (...) {
            std::cerr << "[FAIL] " << test.first
                      << ": unknown exception\n";
        }
    }
    std::cout << passed << "/" << tests.size() << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}

}  // namespace test_support

#define CHECK_TRUE(expression)                                                \
    do {                                                                      \
        if (!(expression)) {                                                  \
            ::test_support::Fail(#expression, __FILE__, __LINE__);            \
        }                                                                     \
    } while (false)

#define CHECK_FALSE(expression) CHECK_TRUE(!(expression))

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        const auto& check_actual = (actual);                                  \
        const auto& check_expected = (expected);                              \
        if (!(check_actual == check_expected)) {                              \
            ::test_support::Fail(                                             \
                #actual " == " #expected, __FILE__, __LINE__);               \
        }                                                                     \
    } while (false)
