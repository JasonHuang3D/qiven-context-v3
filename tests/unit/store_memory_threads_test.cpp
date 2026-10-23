// store/memory's lock, thread gate, read-only object and checked reads, on
// the fake. busy: while the test holds the state's lock, append, forget,
// head, a scan's making, export, import and verify, each from an object of
// its own thread, wait 5 s and raise StoreBusy, all at once; once it is
// released each serves. gate: an object, a scan and an export used from
// another thread than their maker's raise StoreError thread, and serve
// their own. read_only: a read-only object refuses append, forget, purge
// and import with StoreError read_only, writing nothing, and serves every
// read, an export, verify and close. corrupt: a held line or body record
// changed under the lock is StoreCorrupt at header, body and verify, and
// served again once restored.

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/memory.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

using namespace qiven3::test::stage_book;  // NOLINT(google-build-using-namespace)
namespace state = qiven3::store::state;
using qiven3::memory::String;
using qiven3::memory::Vector;
using qiven3::store::GuardView;
using qiven3::store::Header;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using qiven3::store::MemoryStore;
using qiven3::store::RangeView;
using Store = MemoryStore<Fake>;

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";
constexpr std::size_t state_bytes = std::size_t{8} << 20U;

// An empty source.
struct NoSource {
    static void heads(Vector<LaneHead>& rows) { rows.clear(); }
    static bool next(Line& /*line*/) { return false; }
};

// The fake, the fixtures, a state holding A, B, D and ACC, and an arena.
struct Bed {
    Bed()
        : fake(qiven3::platform::fake::Config{}), book(fake), heap(fake.pages(state_bytes)),
          state(heap.bytes(), Limits{}), lane{book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")},
          e{book.stored("E")}, c{book.stored("C")}, e_lines(views(e)), c_lines(views(c)), pages(std::size_t{1} << 20U), arena(pages), chars(arena) {
        Store store(state, fake);
        const std::vector<EntryView> lines = views(lane);
        (void)store.append(qiven_scope, implementer_lane, std::nullopt, lines);
        acc = id_of(lane[3]);
        guard.push_back(GuardView{qiven_scope, implementer_lane, acc});
    }
    Fake fake;
    Book book;
    qiven3::platform::fake::FakePages heap;
    state::State state;
    std::vector<Stored> lane;
    std::vector<Stored> e;
    std::vector<Stored> c;  // the reviewer's first
    std::vector<EntryView> e_lines;
    std::vector<EntryView> c_lines;
    std::string acc;  // ACC's id, the guard's view's
    std::vector<GuardView> guard;
    std::vector<std::byte> pages;
    Arena arena;
    Chars chars;
};

// The error a call raised, as "Kind code", or "none".
template <class F>
std::string raised(const F& f) {
    const std::optional<Refusal> r = refused(f);
    return r ? std::string(qiven3::store::name(r->kind)) + " " + r->code : std::string("none");
}

// The operations each busy thread runs on an object of its own, over a
// fake of its own; their lines made before.
using Call = std::function<void(Store&, Bed&)>;
std::vector<std::pair<std::string, Call>> calls() {
    std::vector<std::pair<std::string, Call>> out;
    out.emplace_back("forget", [](Store& s, Bed& b) {
        std::vector<std::byte> pages(std::size_t{1} << 16U);
        Arena arena(pages);
        Vector<Id> erased{Chars(arena)};
        (void)s.forget(qiven_scope, implementer_lane, b.acc, b.e_lines, b.guard, erased);
    });
    out.emplace_back("append", [](Store& s, Bed& b) {
        (void)s.append(qiven_scope, "reviewer", std::nullopt, b.c_lines);
    });
    out.emplace_back("head", [](Store& s, Bed&) { (void)s.head(qiven_scope, implementer_lane); });
    out.emplace_back("scan", [](Store& s, Bed&) { (void)s.scan(RangeView{}); });
    out.emplace_back("export", [](Store& s, Bed&) { (void)s.export_(std::nullopt, {}); });
    out.emplace_back("import", [](Store& s, Bed&) {
        std::vector<std::byte> pages(std::size_t{1} << 16U);
        Arena arena(pages);
        Vector<LaneHead> heads{Chars(arena)};
        NoSource source;
        (void)s.import_(source, heads);
    });
    out.emplace_back("verify", [](Store& s, Bed&) { (void)s.verify(); });
    return out;
}

}  // namespace

QIVEN3_CASE(store_memory, busy) {
    const auto b = std::make_unique<Bed>();
    const std::vector<std::pair<std::string, Call>> all = calls();
    std::vector<std::string> got(all.size());
    {
        const state::Locked held(b->state, Operation::size, {});
        std::vector<std::thread> threads;
        threads.reserve(all.size());
        for (std::size_t i = 0; i < all.size(); ++i) {
            threads.emplace_back([&, i] {
                Fake fake(qiven3::platform::fake::Config{});
                Store s(b->state, fake);
                got[i] = raised([&] { all[i].second(s, *b); });
            });
        }
        for (std::thread& t : threads) {
            t.join();
        }
    }
    for (std::size_t i = 0; i < all.size(); ++i) {
        CHECK_EQ(all[i].first + " " + got[i], all[i].first + " StoreBusy ");
    }
    // Released: each serves (import refused for a state not empty).
    Store s(b->state, b->fake);
    for (const auto& [name, call] : all) {
        const std::string r = raised([&] { call(s, *b); });
        CHECK_EQ(name + " " + r, name + (name == "import" ? " StoreNotEmpty " : " none"));
    }
}

QIVEN3_CASE(store_memory, gate) {
    const auto b = std::make_unique<Bed>();
    Store store(b->state, b->fake);
    auto scan = store.scan(RangeView{});
    auto x = store.export_(std::nullopt, {});
    std::string object;
    std::string scanned;
    std::string exported;
    std::thread other([&] {
        std::vector<std::byte> pages(std::size_t{1} << 16U);
        Arena arena(pages);
        object = raised([&] { (void)store.head(qiven_scope, implementer_lane); });
        Header h{Chars(arena)};
        scanned = raised([&] { (void)scan.next(h); });
        Vector<LaneHead> rows{Chars(arena)};
        exported = raised([&] { (void)x.heads(rows); });
    });
    other.join();
    CHECK_EQ(object, std::string("StoreError thread"));
    CHECK_EQ(scanned, std::string("StoreError thread"));
    CHECK_EQ(exported, std::string("StoreError thread"));
    Header h(b->chars);
    CHECK(scan.next(h));
    Vector<LaneHead> rows(b->chars);
    CHECK_EQ(x.heads(rows).volume.count, 4U);
    CHECK_EQ(store.head(qiven_scope, implementer_lane)->seq, 4);
}

QIVEN3_CASE(store_memory, read_only) {
    const auto b = std::make_unique<Bed>();
    Store reader(b->state, b->fake, true);
    for (const auto& [name, call] : calls()) {
        const bool write = name == "append" || name == "forget" || name == "import";
        const std::string r = raised([&] { call(reader, *b); });
        CHECK_EQ(name + " " + r, name + (write ? " StoreError read_only" : " none"));
    }
    CHECK_EQ(raised([&] { (void)reader.purge(); }), std::string("StoreError read_only"));
    CHECK_EQ(reader.size().volume.count, 4U);
    CHECK_EQ(reader.head(qiven_scope, implementer_lane)->seq, 4);
    Header h(b->chars);
    reader.header(id_of(b->lane[0]), h);
    String record(b->chars);
    CHECK(reader.body(id_of(b->lane[0]), record));
    CHECK(!reader.forgotten(id_of(b->lane[1])));
    CHECK(reader.close().done);
}

QIVEN3_CASE(store_memory, corrupt) {
    const auto b = std::make_unique<Bed>();
    Store store(b->state, b->fake);
    const std::string a = id_of(b->lane[0]);
    Header h(b->chars);
    String record(b->chars);
    const auto corrupt = [](const auto& f) {
        const std::optional<Refusal> r = refused(f);
        return r.has_value() && r->kind == Kind::corrupt;
    };
    // One character of A's line or body record set, the one it was given.
    const auto set = [&](bool body, std::size_t at, char to) {
        const state::Locked lock(b->state, Operation::size, {});
        state::Held& e = lock.data().held[0];
        auto& text = body ? e.body : e.line;
        const char from = text[at];
        text[at] = to;
        return from;
    };
    // A's writer changed: its id no longer its own.
    const std::size_t writer = b->lane[0].line.find("implementer/") + 1;
    const char m = set(false, writer, 'n');
    CHECK(corrupt([&] { store.header(a, h); }));
    CHECK(corrupt([&] { (void)store.body(a, record); }));
    CHECK(corrupt([&] { (void)store.verify(); }));
    (void)set(false, writer, m);
    store.header(a, h);
    // A digit of A's body record changed.
    const std::size_t digit = b->lane[0].record->find_last_of("0123456789");
    const char was = set(true, digit, b->lane[0].record->at(digit) == '0' ? '1' : '0');
    CHECK(corrupt([&] { (void)store.body(a, record); }));
    CHECK(corrupt([&] { (void)store.verify(); }));
    (void)set(true, digit, was);
    CHECK(store.body(a, record));
    CHECK_EQ(store.verify().entries, 4U);
}
