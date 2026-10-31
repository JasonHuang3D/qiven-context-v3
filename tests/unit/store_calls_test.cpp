// store's generic calls on the store itself, outside any write, over plain
// records (records.hpp) on the plain factory, written once as templates on
// a factory, as stores_test.cpp's are: line, targets, keyed and each_keyed
// on a read-write object, a second one and a read-only one; their order,
// the store's (targets as written, keyed by lane name then seq, not as
// written, records with a target and without); a scope given and another scope's ids kept out; an id no
// record targets; an id absent; a forget among the targets; a key one past
// its bound finding none, never the held key it extends; each_keyed
// stopping where f gives true, and f's throw leaving the object as it was,
// its peak too; the budget: none of the object's pages, and on the
// caller's allocator the bytes' and the list's room alone (counted where
// the countdown is built); a closed object's call refused. In both stores;
// where durable, the writer reopened before the reads, which so read what
// the file holds. Each case that writes ends in verify and, where durable,
// a reopen's verify.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "keys/keys.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "support/stores/records.hpp"
#include "support/stores/stores.hpp"

namespace {

namespace stores = qiven3::test::stores;
namespace records = qiven3::test::records;
using qiven3::memory::String;
using qiven3::memory::Vector;
using qiven3::store::GuardView;
using qiven3::store::Id;
using records::Rec;

constexpr std::string_view qs = "project:qiven";
constexpr std::string_view other_scope = "project:other";

// A writing case's end: verify counts entries, the object closes, and
// where durable a reopened object's verify counts them again.
template <class F, class S>
void verified(qiven3::test::Context& qiven3_ctx, F& f, S& store, std::uint64_t entries) {
    CHECK_EQ(store.verify().entries, entries);
    (void)store.close();
    if (!stores::lacking<F>(stores::durable)) {
        const auto back = std::make_unique<typename F::Store>(f.reopen());
        CHECK_EQ(back->verify().entries, entries);
        (void)back->close();
    }
}

// The calls read what the file holds: where durable, the writer closed
// and a reopened object in its place before the reads.
template <class F>
void reopened(F& f, std::unique_ptr<typename F::Store>& store) {
    if (!stores::lacking<F>(stores::durable)) {
        (void)store->close();
        store = std::make_unique<typename F::Store>(f.reopen());
    }
}

std::string joined(const Vector<Id>& ids) {
    std::string out;
    for (const Id& id : ids) {
        out += (out.empty() ? "" : " ") + std::string(id.view());
    }
    return out;
}

// Lane b of qs written first: B1, B2, BT (targets B1), BD (a decision);
// lane a of qs: A1, A2 (targets B1), then F, a forget of A1; lane a of
// other_scope: T1, T2 (targets T1). Nine entries; A1's line kept.
template <class S>
std::string fill(S& store, const qiven3::memory::Allocator<char>& chars) {
    const std::vector<Rec> b = {records::rec("B1", qs, "b", 1), records::rec("B2", qs, "b", 2, "B1"),
                                records::targeting(records::rec("BT", qs, "b", 3, "B2"), "B1"),
                                records::rec("BD", qs, "b", 4, "BT", "decision")};
    const std::vector<Rec> a = {records::rec("A1", qs, "a", 1),
                                records::targeting(records::rec("A2", qs, "a", 2, "A1"), "B1")};
    const std::vector<Rec> t = {records::rec("T1", other_scope, "a", 1),
                                records::targeting(records::rec("T2", other_scope, "a", 2, "T1"), "T1")};
    const std::vector<Rec> forget = {records::forget("F", qs, "a", 3, "A2", "A1")};
    (void)store.append(qs, "b", std::nullopt, records::views(b));
    (void)store.append(qs, "a", std::nullopt, records::views(a));
    (void)store.append(other_scope, "a", std::nullopt, records::views(t));
    const std::vector<GuardView> guard = {{qs, "b", "BD"}, {qs, "a", "A2"}, {other_scope, "a", "T2"}};
    Vector<Id> erased{chars};
    (void)store.forget(qs, "a", std::string_view("A2"), records::views(forget), guard, erased);
    return a[0].line;
}

// Each call's answer on one object, its lists cleared first.
template <class S>
void answers(qiven3::test::Context& qiven3_ctx, S& store, const std::string& a1,
             const qiven3::memory::Allocator<char>& chars) {
    String line(chars);
    line.assign("stale");
    CHECK(store.line("A1", line));
    CHECK_EQ(std::string_view(line), std::string_view(a1));
    CHECK(!store.line("Z9", line));
    Vector<Id> out{chars};
    out.push_back(Id::of("stale"));
    const auto targets = [&](std::string_view id, std::string_view scope) {
        store.targets(id, scope, out);
        return joined(out);
    };
    const auto keyed = [&](std::string_view scope, std::string_view kind) {
        store.keyed(scope, kind, out);
        return joined(out);
    };
    CHECK_EQ(targets("B1", qs), std::string("BT A2"));
    CHECK_EQ(targets("B1", other_scope), std::string());
    CHECK_EQ(targets("T1", other_scope), std::string("T2"));
    CHECK_EQ(targets("T1", qs), std::string());
    CHECK_EQ(targets("B2", qs), std::string());
    CHECK_EQ(targets("Z9", qs), std::string());
    CHECK_EQ(targets("A1", qs), std::string("F"));
    CHECK_EQ(keyed(qs, "note"), std::string("A1 A2 B1 B2 BT"));
    CHECK_EQ(keyed(qs, "decision"), std::string("BD"));
    CHECK_EQ(keyed(qs, "forget"), std::string("F"));
    CHECK_EQ(keyed(qs, "absent"), std::string());
    CHECK_EQ(keyed(other_scope, "note"), std::string("T1 T2"));
    CHECK_EQ(keyed("project:none", "note"), std::string());
}

template <class F>
void calls_case(qiven3::test::Context& qiven3_ctx, F& f) {
    using Store = typename F::Store;
    auto store = std::make_unique<Store>(f.make());
    const std::string a1 = fill(*store, f.site().chars());
    reopened(f, store);
    const auto other = std::make_unique<Store>(f.again());
    const auto reader = std::make_unique<Store>(f.read_only());
    answers(qiven3_ctx, *store, a1, f.site().chars());
    answers(qiven3_ctx, *other, a1, f.site().chars());
    answers(qiven3_ctx, *reader, a1, f.site().chars());
    (void)reader->close();
    (void)other->close();
    String line(f.site().chars());
    const std::optional<records::Refusal> closed = records::refused([&] { (void)other->line("A1", line); });
    CHECK(closed && closed->code == "closed");
    verified(qiven3_ctx, f, *store, 9);
}

// A key one past its bound, its first bytes a held key's: none found.
template <class F>
void bounds_case(qiven3::test::Context& qiven3_ctx, F& f) {
    const std::string id(Id::capacity, 'x');
    const std::string scope = std::string(qiven3::store::scope_bytes - qs.size(), 's') + std::string(qs);
    const std::vector<Rec> l = {records::rec(id, scope, "a", 1),
                                records::targeting(records::rec("M", scope, "a", 2, id), id)};
    auto store = std::make_unique<typename F::Store>(f.make());
    (void)store->append(scope, "a", std::nullopt, records::views(l));
    reopened(f, store);
    String line(f.site().chars());
    CHECK(store->line(id, line));
    CHECK(!store->line(id + "x", line));
    Vector<Id> out{f.site().chars()};
    store->targets(id, scope, out);
    CHECK_EQ(joined(out), std::string("M"));
    store->targets(id + "x", scope, out);
    CHECK(out.empty());
    store->targets(id, scope + "s", out);
    CHECK(out.empty());
    store->keyed(scope, "note", out);
    CHECK_EQ(joined(out), id + " M");
    store->keyed(scope + "s", "note", out);
    CHECK(out.empty());
    verified(qiven3_ctx, f, *store, 2);
}

// each_keyed lent: stopped where f gives true, f's throw leaving the object
// and its peak as they were; then each call's budget.
template <class F>
void lend_case(qiven3::test::Context& qiven3_ctx, F& f) {
    auto made = std::make_unique<typename F::Store>(f.make());
    const std::string a1 = fill(*made, f.site().chars());
    reopened(f, made);
    typename F::Store& store = *made;
    const std::size_t peak = store.peak();
    std::string seen;
    const auto visit = [&seen](std::string_view stop) {
        return [&seen, stop](const Id& id) {
            seen += std::string(id.view()) + " ";
            if (stop == "throw" && id.view() == "B1") {
                throw std::runtime_error("the step");
            }
            return id.view() == stop;
        };
    };
    bool threw = false;
    try {
        (void)store.each_keyed(qs, "note", visit("throw"));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK_EQ(store.peak(), peak);
    seen.clear();
    CHECK(!store.each_keyed(qs, "note", visit("")));
    CHECK_EQ(seen, std::string("A1 A2 B1 B2 BT "));
    seen.clear();
    CHECK(store.each_keyed(qs, "note", visit("B1")));
    CHECK_EQ(seen, std::string("A1 A2 B1 "));

    // The budget: the caller's arena holds the line's bytes and the lists'
    // room reserved before; the object's peak 0.
    std::vector<std::byte> pages(std::size_t{64} << 10U);
    qiven3::memory::Arena arena(pages);
    const qiven3::memory::Allocator<char> chars(arena);
    String line(chars);
    Vector<Id> out{chars};
    out.reserve(5);
#if defined(QIVEN_FAULTS)
    qiven3::memory::Countdown countdown;
    arena.attach(&countdown);
    countdown.disarm();
#endif
    CHECK(store.line("A1", line));
    CHECK_EQ(store.peak(), 0U);
    const std::size_t used = arena.used();
    CHECK(used >= a1.size());
    store.targets("B1", qs, out);
    CHECK_EQ(store.peak(), 0U);
    store.keyed(qs, "note", out);
    CHECK_EQ(store.peak(), 0U);
    seen.clear();
    CHECK(!store.each_keyed(qs, "note", visit("")));
    CHECK_EQ(store.peak(), 0U);
    CHECK_EQ(arena.used(), used);
#if defined(QIVEN_FAULTS)
    CHECK_EQ(countdown.calls(), 1U);
    arena.attach(nullptr);
#endif
    verified(qiven3_ctx, f, store, 9);
}

}  // namespace

QIVEN3_CASE(store_calls, memory_calls) {
    const auto site = stores::site();
    stores::Memory f(*site);
    calls_case(qiven3_ctx, f);
}

QIVEN3_CASE(store_calls, memory_bounds) {
    const auto site = stores::site();
    stores::Memory f(*site);
    bounds_case(qiven3_ctx, f);
}

QIVEN3_CASE(store_calls, memory_lend) {
    const auto site = stores::site();
    stores::Memory f(*site);
    lend_case(qiven3_ctx, f);
}

QIVEN3_CASE(store_calls, sqlite_calls) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    calls_case(qiven3_ctx, f);
}

QIVEN3_CASE(store_calls, sqlite_bounds) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    bounds_case(qiven3_ctx, f);
}

QIVEN3_CASE(store_calls, sqlite_lend) {
    const auto site = stores::site();
    stores::Sqlite<> f(*site);
    lend_case(qiven3_ctx, f);
}
