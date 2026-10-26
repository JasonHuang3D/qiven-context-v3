#pragma once

// filled: a text of n copies of a byte, built by append so that constant
// evaluation takes it on each standard library the cases build with:
// clang 18 on libstdc++ 13 cannot evaluate std::string(n, c), whose
// constructor's helper it finds undefined there.
//
// thread: none

#include <cstddef>
#include <string>

namespace qiven3::test::fixtures {

constexpr std::string filled(std::size_t n, char c) {
    std::string text;
    text.append(n, c);
    return text;
}

}  // namespace qiven3::test::fixtures
