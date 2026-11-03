// The store's scan case: a SQLite store's directory scanned while open and
// after close, the marker held in a plain record's body found, the store
// verified before its close and after a reopen. The scan's own cases are
// its module's (foundation/devkit/test/scan/tests/).

#include <optional>
#include <string>
#include <vector>

#include "qiven/runner/runner.hpp"
#include "qiven/scan/scan.hpp"
#include "support/stores/records.hpp"
#include "support/stores/stores.hpp"

namespace {

namespace scan = qiven::test::scan;
namespace stores = qiven::test::stores;
namespace records = qiven::test::records;

}  // namespace

QIVEN_CASE(scan, store_directory) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    const std::vector<records::Rec> a = records::chain("group:primary", "lane-one", {"A"});
    const std::string content = a[0].record.value_or("");
    REQUIRE(content.size() >= 12U);
    const std::vector<scan::Pattern> patterns = scan::marker("content", content.substr(0, 12));
    {
        auto store = f.make();
        (void)store.append("group:primary", "lane-one", std::nullopt, records::views(a));
        const scan::Report open = scan::scan(f.directory(), patterns);
        CHECK(open.lists("store.db"));
        CHECK(!open.hits.empty());
        CHECK_EQ(store.verify().entries, 1U);
        (void)store.close();
    }
    const scan::Report closed = scan::scan(f.directory(), patterns);
    CHECK(closed.lists("store.db"));
    CHECK(!closed.hits.empty());
    auto back = f.reopen();
    CHECK_EQ(back.verify().entries, 1U);
    (void)back.close();
}
