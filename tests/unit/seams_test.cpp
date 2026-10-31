// support/seams' smoke cases: the SQLite store under the hooks reporting
// each step, through the factory; an incoherent row written through the
// store's connection, found at open; the hooks without the deferral as the
// store reads them; the naive file store's unlocked compare-and-swap. All
// over plain records (records.hpp); each SQLite store written verified,
// and reopened.

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "qiven/runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/exchange.hpp"
#include "support/seams/seams.hpp"
#include "support/stores/records.hpp"
#include "support/stores/stores.hpp"

namespace {

namespace seams = qiven3::test::seams;
namespace stores = qiven3::test::stores;
namespace records = qiven3::test::records;
using qiven3::store::Kind;
using qiven3::store::StoreError;

constexpr std::string_view at_scope = "project:qiven";
constexpr std::string_view at_lane = "implementer";

struct Recorder {
    std::string steps;
    void step(std::string_view name) {
        steps += name;
        steps += ' ';
    }
};

static_assert(qiven3::store::Hooks<seams::Steps<Recorder>>);
static_assert(qiven3::store::Hooks<seams::NoDeferral>);
static_assert(!qiven3::store::sqlite::defers<seams::NoDeferral>());
static_assert(qiven3::store::sqlite::defers<qiven3::store::NoHooks>());

}  // namespace

QIVEN_CASE(seams, steps) {
    const auto site = stores::site();
    Recorder recorder;
    stores::Sqlite<seams::Steps<Recorder>> f(*site, seams::Steps<Recorder>(recorder));
    const std::vector<records::Rec> a = records::chain(at_scope, at_lane, {"A"});
    auto store = f.make();
    recorder.steps.clear();
    (void)store.append(at_scope, at_lane, std::nullopt, records::views(a));
    CHECK_EQ(recorder.steps, std::string("begun written committed "));
    auto other = f.again();
    CHECK(other.head(at_scope, at_lane).has_value());
    CHECK_EQ(other.verify().entries, 1U);
    (void)other.close();
    (void)store.close();
}

QIVEN_CASE(seams, incoherent_row) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    const std::vector<records::Rec> a = records::chain(at_scope, at_lane, {"A"});
    {
        auto store = f.make();
        (void)store.append(at_scope, at_lane, std::nullopt, records::views(a));
        CHECK_EQ(store.verify().entries, 1U);
        CHECK_EQ(seams::write(store, seams::purged_above_vacuumed), 0);
        (void)store.close();
    }
    std::optional<qiven3::store::Corrupt> found;
    try {
        (void)f.reopen();
    } catch (const StoreError& e) {
        found = e.corrupt();
    }
    CHECK(found == qiven3::store::Corrupt::incoherent);
}

QIVEN_CASE(seams, vacuum_and_checkpoint) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    const std::vector<records::Rec> a = records::chain(at_scope, at_lane, {"A"});
    auto store = f.make();
    (void)store.append(at_scope, at_lane, std::nullopt, records::views(a));
    CHECK_EQ(seams::vacuum_and_checkpoint(store), 0);
    CHECK_EQ(store.verify().entries, 1U);
    (void)store.close();
    auto back = f.reopen();
    CHECK_EQ(back.verify().entries, 1U);
    (void)back.close();
}

QIVEN_CASE(seams, naive_store) {
    const auto site = stores::site();
    const std::vector<records::Rec> a = records::chain(at_scope, at_lane, {"A"});
    const std::vector<records::Rec> b = {records::rec("B", at_scope, at_lane, 2, "A")};
    seams::NaiveStore store(site->root() / "naive");
    seams::NaiveStore other(site->root() / "naive");
    CHECK(!store.head(at_scope, at_lane).has_value());
    const std::string first = store.append(at_scope, at_lane, std::nullopt, records::views(a));
    CHECK_EQ(first, records::id_of(a[0]));
    CHECK_EQ(other.head(at_scope, at_lane).value_or(""), first);
    std::optional<Kind> conflict;
    try {
        (void)other.append(at_scope, at_lane, std::nullopt, records::views(b));
    } catch (const StoreError& e) {
        conflict = e.kind();
    }
    CHECK(conflict == Kind::head_conflict);
    CHECK_EQ(other.append(at_scope, at_lane, first, records::views(b)), records::id_of(b[0]));
    const std::vector<std::string> lines = store.lines(at_scope, at_lane);
    REQUIRE(lines.size() == 2U);
    CHECK_EQ(lines[0].substr(0, a[0].line.size()), a[0].line);
    CHECK(store.lines(at_scope, "reviewer").empty());
}
