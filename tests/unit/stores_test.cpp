// support/stores' smoke cases: the factory and the tags, written once as
// a template on a factory and bound to each store as the S cases are:
// make, again and read_only over one state, a read-only object's write
// refused, close, then reopen where durable; the case template of a
// scan's end (store_scan_case.hpp) taken over the factory's objects; and
// references: a map citing A, its 24 references the most its lists hold,
// every syntax of its content among them, appended on each store, its
// counted peak within its budget, 1 MiB plus 4 times the lines given; and
// a scope set's entries ending in `*`, matched by prefix in heads and in
// an export's heads on each store, one of them one past the scope's bound;
// and the reads each store lends a step, on its own pages.

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "memory/memory.hpp"
#include "store/errors.hpp"
#include "store/keys.hpp"
#include "support/checked/stores.hpp"
#include "support/fixtures/stage_book.hpp"
#include "support/fixtures/store_scan_case.hpp"

namespace {

namespace stores = qiven3::test::ruled;
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

// One entry of a scope set: one ending in `*` holds the scopes its bytes
// before the `*` begin, `*` alone every scope, any other the scope it
// equals; a scope set holds what one of its entries holds.
static_assert(qiven3::store::in_scope("project:q*", "project:qiven"));
static_assert(qiven3::store::in_scope("project:qiven*", "project:qiven"));
static_assert(!qiven3::store::in_scope("project:q", "project:qiven"));
static_assert(!qiven3::store::in_scope("project:qiven", "project:q"));
static_assert(qiven3::store::in_scope("*", "") && qiven3::store::in_scope("*", "person"));
static_assert(qiven3::store::in_scope("a*", "a*") && !qiven3::store::in_scope("a*", "b*"));
static_assert(qiven3::store::in_scope("person", "person") && !qiven3::store::in_scope("person", "persons"));
static_assert(qiven3::store::in_scopes(std::nullopt, "life"));

// A scope set's prefix entries, through heads and an export's heads alike:
// one lane in each of four project scopes, the last of the scope's bound,
// and each set holding the lanes its entries match.
template <class F>
void prefix_case(qiven3::test::Context& qiven3_ctx, F& f) {
    const stage_book::Book& book = f.site().book();
    const std::string longest = "project:q" + std::string(qiven3::store::scope_bytes - 9, 'a');
    const std::vector<std::string> scopes = {"project:qiven", "project:quill", "project:other", longest};
    const std::string content = *book.stored("A").content;
    const auto store = std::make_unique<typename F::Store>(f.make());
    for (const std::string& scope : scopes) {
        const std::vector<stage_book::Stored> one = {
            book.made("A", "A", content, [&](qiven3::entry::header::FieldsView& v) { v.scope = scope; })};
        (void)store->append(scope, at_lane, std::nullopt, stage_book::views(one));
    }
    const std::string past = longest + "*";
    const std::string beyond = longest + "a*";
    struct Want {
        std::vector<std::string_view> set;
        std::size_t lanes;
    };
    const std::vector<Want> wants = {
        {{"project:q*"}, 3},     {{"project:qiven*"}, 1}, {{"project:qiven"}, 1},
        {{"project:q"}, 0},      {{"*"}, 4},              {{"project:*"}, 4},
        {{"project:other", "project:qu*"}, 2},            {{"project:qiven-*"}, 0},
        {{past}, 1},             {{beyond}, 0},           {{}, 0},
    };
    qiven3::memory::Vector<qiven3::store::LaneHead> rows(f.site().chars());
    for (const Want& want : wants) {
        const qiven3::store::Scopes set = std::span<const std::string_view>(want.set);
        (void)store->heads(set, rows);
        const std::size_t held = rows.size();
        const auto out = std::make_unique<decltype(store->export_(set, {}))>(store->export_(set, {}));
        (void)out->heads(rows);
        std::string named;
        for (const std::string_view s : want.set) {
            named += std::string(s) + " ";
        }
        CHECK_EQ(named + std::to_string(held), named + std::to_string(want.lanes));
        CHECK_EQ(named + std::to_string(rows.size()), named + std::to_string(want.lanes));
    }
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

// The reads lent to a step, on the store's own: each step given the bytes
// the plain read gives and the store's pages, three quarters of them taken
// by each step, so that a second step fits only on pages reset after the
// first; a step's throw passed on, the store as it was (its peak, head and
// bytes) and a scan at the record it was at.
template <class F>
void lend_case(qiven3::test::Context& qiven3_ctx, F& f) {
    using Chars = qiven3::memory::Allocator<char>;
    constexpr std::size_t most = qiven3::store::operation_base / 4 * 3;
    const stage_book::Book& book = f.site().book();
    const std::vector<stage_book::Stored> a = {book.stored("A")};
    const std::string id = stage_book::id_of(a[0]);
    const auto ruled = std::make_unique<typename F::Store>(f.make());
    (void)ruled->append(at_scope, at_lane, std::nullopt, stage_book::views(a));
    typename F::Store::Plain& store = *ruled;
    qiven3::memory::String line(f.site().chars());
    qiven3::memory::String record(f.site().chars());
    store.header(id, line);
    const bool has = store.body(id, record);
    CHECK_EQ(has, a[0].record.has_value());
    const auto take = [](const Chars& chars) {
        qiven3::memory::String room(chars);
        room.resize(most);
    };
    for (int round = 0; round < 2; ++round) {
        std::string seen;
        store.header(id, [&](std::string_view held, const Chars& chars) {
            take(chars);
            seen = held;
        });
        CHECK_EQ(seen, std::string(std::string_view(line)));
        CHECK(store.peak() >= most && store.peak() <= qiven3::store::operation_base);
        std::optional<std::string> body;
        CHECK_EQ(store.body(id,
                            [&](std::string_view, const std::string_view* held, const Chars& chars) {
                                take(chars);
                                body = held != nullptr ? std::optional<std::string>(*held) : std::nullopt;
                            }),
                 has);
        CHECK(body == (has ? std::optional<std::string>(std::string_view(record)) : std::nullopt));
    }
    const std::vector<stage_book::Stored> b = {book.stored("B")};
    (void)ruled->append(at_scope, at_lane, id, stage_book::views(b));
    const std::size_t peak = store.peak();
    // Each throws on a line it is given, held by every entry.
    const auto fail = [](std::string_view held, auto&&...) {
        if (!held.empty()) {
            throw std::runtime_error("the step");
        }
    };
    const auto fail_next = [](const qiven3::store::Record& r, const Chars&) {
        if (!r.line.empty()) {
            throw std::runtime_error("the step");
        }
    };
    const auto threw = [](const auto& call) {
        try {
            call();
        } catch (const std::runtime_error& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    CHECK_EQ(threw([&] { store.header(id, fail); }), std::string("the step"));
    CHECK_EQ(threw([&] { (void)store.body(id, fail); }), std::string("the step"));
    CHECK_EQ(store.peak(), peak);
    const auto head = store.head(at_scope, at_lane);
    CHECK(head && head->seq == 2 && head->id.view() == stage_book::id_of(b[0]));
    qiven3::memory::String again(f.site().chars());
    store.header(id, again);
    CHECK_EQ(std::string_view(again), std::string_view(line));

    // Two records, each next's step given the pages afresh; a throwing
    // step leaves the scan at A.
    auto scan = store.scan({});
    CHECK_EQ(threw([&] { (void)scan.next(fail_next); }), std::string("the step"));
    std::string ids;
    for (int round = 0; round < 3; ++round) {
        CHECK(scan.next([&](const qiven3::store::Record& r, const Chars& chars) {
            take(chars);
            ids += std::string(r.id.view()) + " ";
        }) == (round < 2));
    }
    CHECK_EQ(ids, id + " " + stage_book::id_of(b[0]) + " ");
    CHECK(scan.peak() >= most && scan.peak() <= qiven3::store::operation_base);
}

QIVEN3_CASE(stores, memory_lend) {
    const auto site = stores::site();
    stores::Memory f(*site);
    lend_case(qiven3_ctx, f);
}

QIVEN3_CASE(stores, sqlite_lend) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    lend_case(qiven3_ctx, f);
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

QIVEN3_CASE(stores, memory_prefix) {
    const auto site = stores::site();
    stores::Memory f(*site);
    prefix_case(qiven3_ctx, f);
}

QIVEN3_CASE(stores, sqlite_prefix) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    prefix_case(qiven3_ctx, f);
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

namespace {

// What the runner wrote of a case of body fn that a StoreError ends, named
// by the store's namer (support/stores/named.hpp).
std::string named_run(qiven3::test::Context& ctx, qiven3::test::CaseFn fn) {
    std::ostringstream out;
    const int status = qiven3::test::run_case(qiven3::test::Case{"sample.case", nullptr, fn, false}, out, ctx.log);
    return status == qiven3::test::exit_fail ? out.str() : std::string("not failed");
}

}  // namespace

QIVEN3_CASE(stores, store_error_named) {
    // A StoreError has no base: its kind, its kind's code and its detail
    // are named, each kind's code as message() names it, and none for a
    // kind with none.
    using qiven3::test::Context;
    const std::string own = named_run(qiven3_ctx, [](Context&) {
        throw qiven3::store::StoreError(qiven3::store::Code::internal, "open", "a.db", "no engine heap installed");
    });
    CHECK(own.find("store error StoreError internal: no engine heap installed\n") != std::string::npos);
    CHECK(own.find("unknown type") == std::string::npos);
    CHECK(own.find("FAIL sample.case: 1 failed\n") != std::string::npos);
    const std::string locked = named_run(qiven3_ctx, [](Context&) {
        throw qiven3::store::StoreError::locked("open", "a.db", qiven3::store::Cause::held, "another writer");
    });
    CHECK(locked.find("store error StoreLocked held: another writer\n") != std::string::npos);
    const std::string none =
        named_run(qiven3_ctx, [](Context&) { throw qiven3::store::StoreError::not_found("get", "a.db", "e1"); });
    CHECK(none.find("store error NotFound: \n") != std::string::npos);
}
