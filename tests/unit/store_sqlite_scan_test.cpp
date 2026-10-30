// store/sqlite's binding of the case a scan's end (scan_case.hpp) over
// plain records with no admit hook: a store created on the fake over a
// scratch directory of the case's own, a second object opened on it, the
// case run on the two, then verify on a reopen. The forget's purge runs
// VACUUM between a scan's start and its end. And the scan's
// bound on the pinned engine: entry's rowids kept by a purge's VACUUM
// between two steps of a scan.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/seams/seams.hpp"
#include "support/stores/records.hpp"
#include "support/stores/scan_case.hpp"

namespace {

namespace fs = std::filesystem;
using Fake = qiven3::platform::fake::FakePlatform;
using Store = qiven3::store::SQLiteStore<Fake>;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// A scratch directory of the case's own, removed at its end; the fake
// over it, the engine heap and the caller's arena.
struct Rig {
    Rig()
        : root(fresh()), fake(config(root)), engine(fake), pages(std::size_t{1} << 20U), arena(pages), chars(arena) {}
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;
    ~Rig() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    static fs::path fresh() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        fs::path out = fs::temp_directory_path() / ("qiven3-sqlite-s-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    static qiven3::platform::fake::Config config(const fs::path& root) {
        qiven3::platform::fake::Config c;
        c.root = text(root);
        c.seed = 7;
        return c;
    }
    fs::path root;
    Fake fake;
    qiven3::store::Engine<Fake> engine;
    std::vector<std::byte> pages;
    qiven3::memory::Arena arena;
    qiven3::memory::Allocator<char> chars;
};

// Each id's rowid in entry, read through the seam, a space after each.
std::string rowids(Store& store, const std::vector<std::string>& ids) {
    std::string out;
    for (const std::string& id : ids) {
        const std::optional<std::int64_t> rowid =
            qiven3::test::seams::read(store, "SELECT rowid FROM entry WHERE id = '" + id + "'");
        out += (rowid ? std::to_string(*rowid) : std::string("none")) + " ";
    }
    return out;
}

}  // namespace

QIVEN3_CASE(store_sqlite, scan_at_start) {
    const auto rig = std::make_unique<Rig>();
    const std::string path = text(rig->root / "s" / "store.db");
    Store store = Store::open(path, true, false, rig->fake);
    Store other = Store::open(path, false, false, rig->fake);
    qiven3::test::scan_case::scan_ends_at_start(qiven3_ctx, store, other, rig->chars);
    CHECK(store.close().done);
    CHECK(other.close().done);
    Store reopened = Store::open(path, false, false, rig->fake);
    CHECK(reopened.verify() == qiven3::test::scan_case::written);
}

// The scan's bound, rowid at most entry's greatest at its start, holds
// while a VACUUM keeps entry's rowids: the pinned engine keeps a table's
// rowids when it has an index, and entry has its keys'. Three lanes, then
// a gap below all but A's rowid, as rows deleted would leave (entry rows
// never are, so a fresh numbering differs from the store's only past such
// a gap); a scan stepped once; B forgotten by E through the scanning
// object, its purge's VACUUM on the connection the scan reads; the scan
// stepped to its end. Each rowid is as before, and the scan gives what it
// would have without the VACUUM. An engine renumbering entry fails both:
// the rowids close the gap, and E, appended after the scan began, falls
// under its bound.
QIVEN3_CASE(store_sqlite, scan_vacuum_keeps_rowids) {
    using qiven3::test::records::Rec;
    namespace scan_case = qiven3::test::scan_case;
    namespace records = qiven3::test::records;
    const auto rig = std::make_unique<Rig>();
    const std::string path = text(rig->root / "v" / "store.db");
    Store store = Store::open(path, true, false, rig->fake);
    const scan_case::Lanes lanes;
    const std::vector<Rec>& mine = lanes.mine;
    const std::vector<Rec>& theirs = lanes.theirs;
    const std::vector<Rec>& tester = lanes.tester;
    const std::vector<Rec>& forgets = lanes.forgets;
    (void)store.append(scan_case::scope, scan_case::implementer, std::nullopt, records::views(mine));
    (void)store.append(scan_case::scope, scan_case::reviewer, std::nullopt, records::views(theirs));
    (void)store.append(scan_case::scope, "tester", std::nullopt, records::views(tester));
    CHECK_EQ(qiven3::test::seams::write(store, "UPDATE entry SET rowid = rowid + 1000 WHERE rowid > 1"), 0);
    std::vector<std::string> ids;
    for (const std::vector<Rec>* lane : {&mine, &theirs, &tester}) {
        for (const Rec& r : *lane) {
            ids.push_back(records::id_of(r));
        }
    }
    const std::string before = rowids(store, ids);
    CHECK_EQ(before, std::string("1 1002 1003 1004 1005 1006 1007 "));

    auto whole = store.scan(qiven3::store::RangeView{});
    CHECK_EQ(scan_case::rest(whole, rig->chars, 1), scan_case::cut({mine[0]}));
    // The guard's views over ids held for the call.
    const std::string& acc = records::id_of(mine[3]);
    const std::string& m1 = records::id_of(theirs[1]);
    const std::vector<qiven3::store::GuardView> guard = {
        qiven3::store::GuardView{scan_case::scope, scan_case::implementer, acc},
        qiven3::store::GuardView{scan_case::scope, scan_case::reviewer, m1},
        qiven3::store::GuardView{scan_case::scope, "tester", ids.back()}};
    qiven3::store::Vector<qiven3::store::Id> erased(rig->chars);
    CHECK(store.forget(scan_case::scope, scan_case::implementer, acc, records::views(forgets), guard, erased)
              .purge.done);
    CHECK_EQ(qiven3::test::seams::read(store, "SELECT value FROM meta WHERE name = 'vacuumed'").value_or(-1),
             std::int64_t{1});
    CHECK_EQ(rowids(store, ids), before);
    CHECK_EQ(scan_case::rest(whole, rig->chars), scan_case::cut({mine[1], mine[2], mine[3]}) +
                                                     scan_case::cut(theirs) + scan_case::cut(tester));
    CHECK(store.close().done);
    Store reopened = Store::open(path, false, false, rig->fake);
    CHECK(reopened.verify() == (qiven3::store::Counts{8, 6, 1, 3}));
}
