// store's plain path at a large import, on SQLite, on the bed of plain_bed.hpp: many_keys_import, body keys with no
// record past any list of them the import's pages could keep. Its own target, not of the label fast
// (tests/CMakeLists.txt).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "qiven/recordlog/contract.hpp"
#include "qiven/runner/runner.hpp"
#include "support/stores/plain_bed.hpp"

using namespace qiven::test::plain_bed;

// 10,000 entries with a body key, each with a forget of its own.
QIVEN_CASE(store_plain, many_keys_import) {
    constexpr std::size_t count = 10000;
    std::vector<std::string> ids;  // the keys' views: an even index a target, the next its forget
    for (std::size_t i = 0; i < 2 * count; ++i) {
        ids.push_back("i" + std::to_string(i));
    }
    Plan p;
    p.import = true;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t t = 2 * i;
        const auto seq = static_cast<std::int64_t>(i) + 1;
        p.lines.push_back(note(ids[t], "one", seq, i > 0 ? Parent(ids[t - 2]) : std::nullopt, true, false));
        p.lines.push_back(gone(ids[t + 1], "two", seq, i > 0 ? Parent(ids[t - 1]) : std::nullopt, ids[t]));
    }
    p.heads = {lane_head("plain", "one", ids[(2 * count) - 2], count), lane_head("plain", "two", ids.back(), count)};
    (void)on_sqlite(qiven_ctx, p, Counts{2 * count, 0, count, 2});
}
