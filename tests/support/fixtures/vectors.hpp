#pragma once

// The vectors case's readers (tests/unit/vectors_test.cpp; vectors_read.cpp there
// defines them): each group's reader of a line of tests/vectors/ids.json,
// recomputing its vector by the code built so far, and the members'
// accessors they share. A reader returns empty when its vector holds,
// else what differs; a member of another type, or a value the code
// refuses, is thrown.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "qiven3/canon.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::test::vectors {

namespace canon = qiven3::canon;
using Fake = qiven3::platform::fake::FakePlatform;
using Value = canon::Basic<>;

std::string_view text(const Value& v, std::string_view key);
std::optional<std::string_view> maybe_text(const Value& v, std::string_view key);
std::int64_t integer(const Value& v, std::string_view key);
std::size_t size(const Value& v, std::string_view key);
std::string from_hex(std::string_view hex);
std::string hex_of(std::string_view bytes);
std::string same(std::string_view what, std::string_view got, std::string_view want);

// The vectors' entries and the mapped file's versions, which other vectors
// name, and the fixtures' entries.
struct Entry {
    std::string name;
    std::string header;
    std::string key;
    std::string content;
};
struct Context {
    Fake fake{qiven3::platform::fake::Config{}};
    Value fixtures;
    std::vector<Entry> entries;
    std::vector<std::pair<std::int64_t, std::string>> files;

    const Entry& entry(std::string_view name) const {
        for (const Entry& e : entries) {
            if (e.name == name && !e.key.empty()) {
                return e;
            }
        }
        throw std::runtime_error("no entry with a body named " + std::string(name));
    }
    const std::string& file(std::int64_t version) const {
        for (const auto& [n, bytes] : files) {
            if (n == version) {
                return bytes;
            }
        }
        throw std::runtime_error("no file of version " + std::to_string(version));
    }
};

std::string framing(Context& c, const Value& v);
std::string framed_header(Context& c, const Value& v);
std::string entry(Context& c, const Value& v);
std::string content_digest(Context& c, const Value& v);
std::string evidence(Context& c, const Value& v);
std::string parse(Context& c, const Value& v);
std::string check_line(Context& c, const Value& v);
std::string usage(Context& c, const Value& v);
std::string file(Context& c, const Value& v);
std::string map(Context& c, const Value& v);
std::string block(Context& c, const Value& v);

}  // namespace qiven3::test::vectors
