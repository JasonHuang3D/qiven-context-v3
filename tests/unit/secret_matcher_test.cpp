// The matcher over its callers' patterns: the syntaxes' vectors, copies of
// the entry's, evaluated by the compiler on std::allocator, here until the
// entry is rebuilt; and secret's, the secret check's twelve patterns, each
// compiled alone, on their tables' fixtures and the clearing fixtures
// (support/fixtures), each built when the case runs so that no file holds a
// live-looking token. The matcher's own guarantees are shown by its own
// cases (foundation/core/matcher/tests/).

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "entry/secret.hpp"
#include "qiven/matcher/matcher.hpp"
#include "qiven/runner/runner.hpp"
#include "support/fixtures/patterns.hpp"

namespace {

namespace matcher = qiven::matcher;
namespace secret = qiven3::entry::secret;
namespace fixtures = qiven::test::fixtures;
using matcher::Fault;

constexpr bool hits(std::string_view pattern, std::string_view text) {
    return matcher::hit(matcher::compile(pattern), text);
}

constexpr std::optional<Fault> fault_of(std::string_view pattern) {
    matcher::Basic<> out;
    const std::optional<matcher::PatternError> fault = matcher::try_compile(pattern, out);
    return fault ? std::optional<Fault>(fault->code) : std::nullopt;
}

// The syntaxes, whole-value.
constexpr std::string_view lane = "^[a-z][a-z0-9-]{0,31}$";
constexpr std::string_view number = "^[1-9][0-9]{0,8}$";
constexpr std::string_view key_syntax = "^[a-z][a-z0-9]*(_[a-z0-9]+)*$";
constexpr std::string_view case_id = "^[A-Z][A-Z0-9]*(-[A-Z0-9]+)*$";
constexpr std::string_view symbol = "^[A-Za-z_][A-Za-z0-9_]*((\\.|::|/)[A-Za-z_][A-Za-z0-9_]*)*$";
static_assert(hits(lane, "writer") && hits(lane, "a-") && !hits(lane, "Writer") && !hits(lane, ""));
static_assert(hits(lane, "a234567890123456789012345678901b") && !hits(lane, "a234567890123456789012345678901bc"));
static_assert(!hits(lane, "writer\n") && !hits(lane, "1writer") && !hits(lane, "wr ter"));
static_assert(hits(number, "1") && hits(number, "123456789") && !hits(number, "1234567890") && !hits(number, "0"));
static_assert(hits(key_syntax, "a_b") && hits(key_syntax, "ab1_c2_d") && !hits(key_syntax, "a__b") && !hits(key_syntax, "a_") && !hits(key_syntax, "_a"));
static_assert(hits(case_id, "FA2") && hits(case_id, "ACC-2") && !hits(case_id, "FA--2") && !hits(case_id, "fa2"));
static_assert(hits(symbol, "a::b/c.d") && hits(symbol, "_x") && !hits(symbol, "a:b") && !hits(symbol, "a..b"));

static_assert(!fault_of(symbol));

// The secret check's twelve patterns, in its table's order.
std::vector<std::string_view> patterns() {
    std::vector<std::string_view> out;
    for (const secret::Pattern& p : secret::PATTERNS) {
        out.push_back(p.text.view());
    }
    return out;
}

}  // namespace

QIVEN_CASE(secret, pattern_fixtures) {
    std::vector<matcher::Basic<>> compiled;
    for (const std::string_view p : patterns()) {
        compiled.push_back(matcher::compile(p));
        CHECK(!compiled.back().whole());
    }
    std::size_t checked = 0;
    for (const fixtures::RecipeView& f : fixtures::recipes) {
        const bool got = matcher::hit(compiled[f.pattern], fixtures::text(f));
        if (got != f.hit) {
            std::cout << "secret.pattern_fixtures: pattern " << f.pattern << " gave " << got << " on fixture " << checked
                      << "\n";
        }
        CHECK_EQ(got, f.hit);
        ++checked;
    }
    std::cout << "secret.pattern_fixtures: " << compiled.size() << " patterns, " << checked << " fixtures\n";
    CHECK_EQ(compiled.size(), 12U);
}
