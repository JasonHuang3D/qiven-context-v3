// support/seams' smoke cases: the SQLite store under the hooks reporting
// each step, through the factory; an incoherent row written through the
// store's connection, found at open; the hooks without the deferral as the
// store reads them; the naive file store's unlocked compare-and-swap.

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/exchange.hpp"
#include "support/fixtures/stage_book.hpp"
#include "support/seams/seams.hpp"
#include "support/stores/stores.hpp"

namespace {

namespace seams = qiven3::test::seams;
namespace stores = qiven3::test::stores;
namespace stage_book = qiven3::test::stage_book;
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

QIVEN3_CASE(seams, steps) {
    const auto site = stores::site();
    Recorder recorder;
    stores::Sqlite<seams::Steps<Recorder>> f(*site, seams::Steps<Recorder>(recorder));
    const std::vector<stage_book::Stored> a = {site->book().stored("A")};
    auto store = f.make();
    recorder.steps.clear();
    (void)store.append(at_scope, at_lane, std::nullopt, stage_book::views(a));
    CHECK_EQ(recorder.steps, std::string("begun written committed "));
    auto other = f.again();
    CHECK(other.head(at_scope, at_lane).has_value());
    (void)other.close();
    (void)store.close();
}

QIVEN3_CASE(seams, incoherent_row) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    const std::vector<stage_book::Stored> a = {site->book().stored("A")};
    {
        auto store = f.make();
        (void)store.append(at_scope, at_lane, std::nullopt, stage_book::views(a));
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

QIVEN3_CASE(seams, vacuum_and_checkpoint) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    const std::vector<stage_book::Stored> a = {site->book().stored("A")};
    auto store = f.make();
    (void)store.append(at_scope, at_lane, std::nullopt, stage_book::views(a));
    CHECK_EQ(seams::vacuum_and_checkpoint(store), 0);
    CHECK_EQ(store.verify().entries, 1U);
    (void)store.close();
}

QIVEN3_CASE(seams, naive_store) {
    const auto site = stores::site();
    const stage_book::Book& book = site->book();
    const std::vector<stage_book::Stored> a = {book.stored("A")};
    const std::vector<stage_book::Stored> b = {book.stored("B")};
    seams::NaiveStore store(site->root() / "naive");
    seams::NaiveStore other(site->root() / "naive");
    CHECK(!store.head(at_scope, at_lane).has_value());
    const std::string first = store.append(at_scope, at_lane, std::nullopt, stage_book::views(a));
    CHECK_EQ(first, stage_book::id_of(a[0]));
    CHECK_EQ(other.head(at_scope, at_lane).value_or(""), first);
    std::optional<Kind> conflict;
    try {
        (void)other.append(at_scope, at_lane, std::nullopt, stage_book::views(b));
    } catch (const StoreError& e) {
        conflict = e.kind();
    }
    CHECK(conflict == Kind::head_conflict);
    CHECK_EQ(other.append(at_scope, at_lane, first, stage_book::views(b)), stage_book::id_of(b[0]));
    const std::vector<std::string> lines = store.lines(at_scope, at_lane);
    REQUIRE(lines.size() == 2U);
    CHECK_EQ(lines[0].substr(0, a[0].line.size()), a[0].line);
    CHECK(store.lines(at_scope, "reviewer").empty());
}
