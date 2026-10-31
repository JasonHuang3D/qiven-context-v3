// fuzz/entries' kept inputs, each read and its outcome checked against the
// one its maker named: the example and an import file read, and each of
// the reader's refusals but too_large given by one (a placeholder by two,
// a later name and a forward one). too_large's inputs pass 4 MiB, kept
// out of the regression binary: built here, a text piece one byte past its
// bound and a file one byte past its own.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "qiven/runner/runner.hpp"
#include "write/entries.hpp"

namespace qiven3::fuzz {
std::size_t kept_count() noexcept;
std::string_view kept_target(std::size_t i) noexcept;
std::string_view kept_name(std::size_t i) noexcept;
std::span<const std::uint8_t> kept_bytes(std::size_t i) noexcept;
}  // namespace qiven3::fuzz

namespace {

namespace we = qiven3::write::entries;

// Each kept input by its digest's first eight digits, and its outcome.
struct Seed {
    std::string_view name;
    std::string_view outcome;
};
constexpr std::array<Seed, 16> seeds = {{
    {"34d742cc", "ok"},                         // the example
    {"c3a6625f", "ok"},                         // an import file
    {"3ef3df37", "form/json"},
    {"d1ad5316", "form/type"},
    {"b6b32f5b", "form/unknown_member"},
    {"7f05005a", "form/not_allowed"},
    {"7ef97f13", "form/missing"},
    {"e41a7aba", "form/value"},
    {"fea654ae", "form/count"},
    {"b98567d9", "form/syntax"},
    {"34c5d2b4", "form/repeated"},
    {"cf92ed0e", "form/placeholder"},           // an @name: of no entry
    {"25c58464", "form/placeholder"},           // an @name: of its own entry, not an earlier one
    {"a1c5a931", "form/piece"},
    {"60fd4091", "form/unused_piece"},
    {"7df3e99d", "invalid/content"},
}};

std::string outcome(std::string_view bytes) {
    std::vector<std::byte> pages(std::size_t{24} << 20U);
    qiven3::memory::Arena arena(pages);
    try {
        static_cast<void>(we::read(bytes, arena));
        return "ok";
    } catch (const we::EntriesError& e) {
        return std::string(we::name(e.code)) + "/" + std::string(we::name(e.why));
    }
}

}  // namespace

QIVEN_CASE(fuzz_regress, entries_seeds_give_their_refusals) {
    namespace fuzz = qiven3::fuzz;
    std::array<int, seeds.size()> met{};
    std::size_t ran = 0;
    for (std::size_t i = 0; i < fuzz::kept_count(); ++i) {
        if (fuzz::kept_target(i) != "entries") {
            continue;
        }
        ++ran;
        const std::span<const std::uint8_t> b = fuzz::kept_bytes(i);
        const std::string_view bytes(reinterpret_cast<const char*>(b.data()), b.size());
        const std::string_view name = fuzz::kept_name(i);
        const std::string got = outcome(bytes);
        bool known = false;
        for (std::size_t s = 0; s < seeds.size(); ++s) {
            if (name.starts_with(seeds[s].name)) {
                known = true;
                ++met[s];
                CHECK_EQ(got, std::string(seeds[s].outcome));
            }
        }
        std::cout << "fuzz/entries: " << name.substr(0, 8) << " " << got << "\n";
        CHECK(known);
    }
    CHECK_EQ(ran, seeds.size());
    for (const int m : met) {
        CHECK_EQ(m, 1);
    }
    // too_large: a text piece one byte past its bound, a file one past its.
    const std::string head = R"({"form": "qiven3-entries/1", "writer": "import", "scope": "project:example",)"
                             R"( "pieces": {"big": {"text": ")";
    const std::string piece = head + std::string(we::max_piece_bytes + 1, 'x') + R"("}}, "entries": []})";
    CHECK_EQ(outcome(piece), std::string("form/too_large"));
    const std::string bound = head + std::string(we::max_piece_bytes, 'x') + R"("}}, "entries": []})";
    CHECK_EQ(outcome(bound), std::string("form/count"));  // at its bound, read on to the entries
    CHECK_EQ(outcome(std::string(we::max_bytes + 1, ' ')), std::string("form/too_large"));
}
