// utc over the entry's fixture: L1's `when`, 05:00 for a message stamped
// 13:00 at +08:00, read from tests/fixtures/ (the 8-hour shift's fixture).
// utc's own cases are time's (foundation/core/time/tests/utc_test.cpp).

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "qiven/runner/runner.hpp"
#include "qiven/text/canon.hpp"
#include "qiven/time/utc.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace canon = qiven::text::canon;
namespace fixtures = qiven::test::fixtures;
namespace utc = qiven::time::utc;
using utc::Millis;

constexpr Millis ms(std::int64_t n) { return Millis{std::chrono::milliseconds{n}}; }

}  // namespace

// L1's `when` holds 05:00 for a message stamped 13:00 at +08:00.
QIVEN_CASE(utc, shift) {
    canon::Basic<> entries;
    fixtures::load_entries(entries);
    canon::Basic<> content;
    canon::parse(*fixtures::find_entry(entries, "L1")->content, content);
    const std::optional<qiven::text::json::Index> when = content.find(0, "when");
    CHECK(when.has_value());
    const std::optional<qiven::text::json::Index> time = content.find(*when, "time");
    CHECK(time.has_value());
    const std::string_view text = content.text(*time);
    CHECK_EQ(std::string(text), std::string("2026-10-17T05:00:00.000Z"));
    CHECK(utc::parse_at(text) == ms(1'792'213'200'000));
    CHECK(*utc::parse_at("2026-10-17T13:00:00.000Z") - *utc::parse_at(text) == std::chrono::hours{8});
    std::string out;
    CHECK(utc::print_at(*utc::parse_at(text), out) && out == text);
}
