#pragma once

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace sloptest {

inline int& failureCount() {
    static int count = 0;
    return count;
}

inline void fail(const char* file, int line, const std::string& message) {
    std::cerr << file << ':' << line << ": CHECK failed: " << message << '\n';
    ++failureCount();
}

} // namespace sloptest

#define CHECK(cond)                                                                              \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            sloptest::fail(__FILE__, __LINE__, #cond);                                            \
        }                                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                           \
    do {                                                                                         \
        const auto& _slop_a = (a);                                                               \
        const auto& _slop_b = (b);                                                               \
        if (!(_slop_a == _slop_b)) {                                                              \
            std::ostringstream _slop_msg;                                                         \
            _slop_msg << #a " == " #b << " (" << _slop_a << " vs " << _slop_b << ')';             \
            sloptest::fail(__FILE__, __LINE__, _slop_msg.str());                                  \
        }                                                                                        \
    } while (0)

#define CHECK_TRUE(cond) CHECK(cond)
#define CHECK_FALSE(cond) CHECK(!(cond))
