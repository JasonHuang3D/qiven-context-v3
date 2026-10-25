// support/stores' smoke cases: the factory and the tags, written once as
// a template on a factory and bound to each store as the S cases are:
// make, again and read_only over one state, a read-only object's write
// refused, close, then reopen where durable; the case template of a
// scan's end (store_scan_case.hpp) taken over the factory's objects; and
// references: a map citing A, its 24 references the most its lists hold,
// every syntax of its content among them, appended on each store, its
// counted peak within its budget, 1 MiB plus 4 times the lines given.

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "memory/memory.hpp"
#include "store/errors.hpp"
#include "support/fixtures/stage_book.hpp"
#include "support/fixtures/store_scan_case.hpp"
#include "support/stores/stores.hpp"

namespace {

namespace stores = qiven3::test::stores;
namespace stage_book = qiven3::test::stage_book;
using qiven3::store::Code;
using qiven3::store::StoreError;

constexpr std::string_view at_scope = "project:qiven";
constexpr std::string_view at_lane = "implementer";

template <class F>
void factory_case(qiven3::test::Context& qiven3_ctx, F& f) {
    const stage_book::Book& book = f.site().book();
    const std::vector<stage_book::Stored> a = {book.stored("A")};
    const std::vector<stage_book::Stored> b = {book.stored("B")};
    const std::string id = stage_book::id_of(a[0]);
    // The store objects on the heap: four SQLite objects pass the frame bound.
    using Store = typename F::Store;
    const auto store = std::make_unique<Store>(f.make());
    const auto other = std::make_unique<Store>(f.again());
    const auto reader = std::make_unique<Store>(f.read_only());
    CHECK(!store->read_only());
    CHECK(reader->read_only());
    (void)store->append(at_scope, at_lane, std::nullopt, stage_book::views(a));
    CHECK(other->head(at_scope, at_lane).has_value());
    CHECK(reader->head(at_scope, at_lane).has_value());
    CHECK_EQ(reader->head(at_scope, at_lane).value_or(qiven3::store::Head{}).seq, 1);
    std::optional<Code> refused;
    try {
        (void)reader->append(at_scope, at_lane, id, stage_book::views(b));
    } catch (const StoreError& e) {
        refused = e.code();
    }
    CHECK(refused == Code::read_only);
    (void)reader->close();
    (void)other->close();
    (void)store->close();
    if (stores::lacking<F>(stores::durable)) {
        return;
    }
    const auto back = std::make_unique<Store>(f.reopen());
    CHECK_EQ(back->head(at_scope, at_lane).value_or(qiven3::store::Head{}).seq, 1);
    (void)back->close();
}

template <class F>
void scan_case(qiven3::test::Context& qiven3_ctx, F& f) {
    auto store = f.make();
    auto other = f.again();
    qiven3::test::scan_case::scan_ends_at_start(qiven3_ctx, store, other, f.site().book(), f.site().chars());
}

template <class F>
void references_case(qiven3::test::Context& qiven3_ctx, F& f, std::string_view name) {
    const stage_book::Book& book = f.site().book();
    const std::vector<stage_book::Stored> a = {book.stored("A")};
    const std::vector<stage_book::Stored> map = {book.with_references(24, stage_book::id_of(a[0]))};
    const std::vector<qiven3::store::EntryView> lines = stage_book::views(map);
    const auto store = std::make_unique<typename F::Store>(f.make());
    (void)store->append(at_scope, at_lane, std::nullopt, stage_book::views(a));
    std::string refused = "none";
    try {
        if (const std::optional<stage_book::Refusal> r =
                stage_book::refused([&] { (void)store->append(at_scope, "reviewer", std::nullopt, lines); })) {
            refused = r->code + " " + r->field + " " + r->detail;
        }
    } catch (const qiven3::memory::MemoryError&) {
        refused = "MemoryError";
    }
    const std::size_t budget = *qiven3::store::operation_budget(*qiven3::store::given(lines));
    std::cout << name << ": content " << map[0].content->size() << " bytes, refused " << refused << ", peak "
              << store->peak() << " of " << budget << "\n";
    CHECK_EQ(refused, std::string("none"));
    CHECK(store->peak() <= budget);
    CHECK_EQ(store->head(at_scope, "reviewer").value_or(qiven3::store::Head{}).seq, 1);
    (void)store->close();
}

}  // namespace

QIVEN3_CASE(stores, tags) {
    using Memory = stores::Memory;
    using Sqlite = stores::Sqlite<>;
    CHECK(!stores::lacking<Memory>(stores::none));
    CHECK(!stores::lacking<Sqlite>(stores::none));
    CHECK(!stores::lacking<Sqlite>(stores::durable | stores::files | stores::processes | stores::own_sqlite));
    CHECK_EQ(stores::lacking<Memory>(stores::durable).value_or(""),
             std::string("not applicable: the memory store lacks durable"));
    CHECK_EQ(stores::lacking<Memory>(stores::files | stores::own_sqlite).value_or(""),
             std::string("not applicable: the memory store lacks files, own: sqlite"));
    std::string reason;
    try {
        stores::require<Memory>(stores::processes);
    } catch (const qiven3::test::Skip& skip) {
        reason = skip.reason;
    }
    CHECK_EQ(reason, std::string("not applicable: the memory store lacks processes"));
    stores::require<Sqlite>(stores::processes);
}

QIVEN3_CASE(stores, memory_factory) {
    const auto site = stores::site();
    stores::Memory f(*site);
    factory_case(qiven3_ctx, f);
    std::string reason;
    try {
        (void)stores::Memory::reopen();
    } catch (const qiven3::test::Skip& skip) {
        reason = skip.reason;
    }
    CHECK_EQ(reason.substr(0, 16), std::string("not applicable: "));
}

QIVEN3_CASE(stores, sqlite_factory) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    factory_case(qiven3_ctx, f);
    CHECK(std::filesystem::exists(f.directory() / "store.db"));
}

QIVEN3_CASE(stores, memory_scan_case) {
    const auto site = stores::site();
    stores::Memory f(*site);
    scan_case(qiven3_ctx, f);
}

QIVEN3_CASE(stores, sqlite_scan_case) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    scan_case(qiven3_ctx, f);
}

QIVEN3_CASE(stores, two_states) {
    const auto site = stores::site();
    stores::Sqlite<> first(*site);
    stores::Sqlite<> second(*site);
    stores::Memory third(*site);
    const std::vector<stage_book::Stored> a = {site->book().stored("A")};
    auto one = first.make();
    auto two = second.make();
    auto three = third.make();
    (void)one.append(at_scope, at_lane, std::nullopt, stage_book::views(a));
    CHECK(!two.head(at_scope, at_lane).has_value());
    CHECK(!three.head(at_scope, at_lane).has_value());
    CHECK(first.directory() != second.directory());
    bool twice = false;
    try {
        (void)first.make();
    } catch (const std::logic_error&) {
        twice = true;
    }
    CHECK(twice);
}

QIVEN3_CASE(stores, memory_references) {
    const auto site = stores::site();
    stores::Memory f(*site);
    references_case(qiven3_ctx, f, "stores.memory_references");
}

QIVEN3_CASE(stores, sqlite_references) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    references_case(qiven3_ctx, f, "stores.sqlite_references");
}
