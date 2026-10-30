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
// served again once restored, a body record's digit body_mismatch; a
// held line given another held entry's, header's id_mismatch. forged: a state forged under the lock in one way
// each, every other rule kept, refused by verify with its code: a line
// whose parent, or seq, is not its place's; a target index not its
// header's; a forgotten entry's body kept (forgotten_body); a content's
// body dropped; a lane's head not its last; the size one off; the ids out
// of order; the store's own verify, which reads no line, refusing each of
// the keys' forgeries with the same code and finding the target index
// coherent.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "checked/stores.hpp"
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
using qiven3::store::Head;
using qiven3::checked::Header;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using qiven3::store::MemoryStore;
using qiven3::store::RangeView;
using Store = qiven3::checked::MemoryStore<Fake>;

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

// The StoreCorrupt code a call raised, "none", or "not corrupt".
template <class F>
std::string corrupt_code(const F& f) {
    try {
        f();
    } catch (const qiven3::store::StoreError& e) {
        return e.corrupt() ? std::string(qiven3::store::name(*e.corrupt())) : std::string("not corrupt");
    }
    return "none";
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
    CHECK_EQ(corrupt_code([&] { (void)store.body(a, record); }), std::string("body_mismatch"));
    CHECK(corrupt([&] { (void)store.verify(); }));
    (void)set(true, digit, was);
    CHECK(store.body(a, record));
    CHECK_EQ(store.verify().entries, 4U);
    // A's line B's: a header line whole, of another id.
    const std::string line = b->lane[0].line;
    {
        const state::Locked lock(b->state, Operation::size, {});
        lock.data().held[0].line.assign(b->lane[1].line);
    }
    CHECK_EQ(corrupt_code([&] { store.header(a, h); }), std::string("id_mismatch"));
    {
        const state::Locked lock(b->state, Operation::size, {});
        lock.data().held[0].line.assign(line);
    }
    store.header(a, h);
}

namespace {

// A fresh bed's state, E's forget of B first where asked, forged under its
// lock by change; then the verify's refusal code, or "none": the ruled
// store's, or, by Plain, the store's own, which reads no line.
template <class S = Store, class F>
std::string forged(bool forget, const F& change) {
    const auto b = std::make_unique<Bed>();
    Store store(b->state, b->fake);
    if (forget) {
        Vector<Id> erased(b->chars);
        (void)store.forget(qiven_scope, implementer_lane, b->acc, b->e_lines, b->guard, erased);
    }
    {
        const state::Locked lock(b->state, Operation::size, {});
        change(*b, lock.data());
    }
    S verifier(b->state, b->fake);
    return corrupt_code([&] { (void)verifier.verify(); });
}

// Entry i made s: its line, body record and id; the ids sorted again.
void replace(state::Data& d, std::uint32_t i, const Stored& s) {
    state::Held& e = d.held[i];
    e.line.assign(s.line);
    e.body.assign(*s.record);
    e.id = Id::of(id_of(s));
    state::sort_ids(d);
}

}  // namespace

QIVEN3_CASE(store_memory, forged) {
    CHECK_EQ(forged(false, [](Bed&, state::Data&) {}), std::string("none"));
    // B's line with D for its parent; ACC's, the lane's last, at seq 5,
    // the head with it: each its own id and body.
    CHECK_EQ(forged(false,
                    [](Bed& b, state::Data& d) {
                        const std::string parent = id_of(b.lane[2]);
                        replace(d, 1, b.book.made("B", "B", *b.lane[1].content,
                                                  [&](header::FieldsView& f) { f.parent = parent; }));
                    }),
             std::string("incoherent"));
    CHECK_EQ(forged(false,
                    [](Bed& b, state::Data& d) {
                        replace(d, 3,
                                b.book.made("ACC", "ACC", *b.lane[3].content,
                                            [](header::FieldsView& f) { f.seq = 5; }));
                        d.held[3].seq = 5;
                        d.lanes[0].head = Head{d.held[3].id, 5};
                    }),
             std::string("incoherent"));
    // ACC's target index A's, its header naming D: the store's own verify
    // reads no line, so finds its keys coherent.
    const auto aimed = [](Bed&, state::Data& d) { d.held[3].target = 0; };
    CHECK_EQ(forged(false, aimed), std::string("incoherent"));
    CHECK_EQ(forged<Store::Plain>(false, aimed), std::string("none"));
    // B forgotten, its body kept, the size counting it.
    const auto kept = [](Bed& b, state::Data& d) {
        d.held[1].body.assign(*b.lane[1].record);
        d.held[1].has_body = true;
        d.size += b.lane[1].record->size() + 1U;
    };
    CHECK_EQ(forged(true, kept), std::string("forgotten_body"));
    CHECK_EQ(forged<Store::Plain>(true, kept), std::string("forgotten_body"));
    // A's body dropped, the size without it; ACC's seq 5; a head not its
    // lane's last; the size one off; the ids out of order: the store's
    // own verify refusing each, as the ruled one does.
    const std::array<std::function<void(Bed&, state::Data&)>, 5> own = {
        [](Bed&, state::Data& d) {
            d.size -= d.held[0].body.size() + 1U;
            d.held[0].body.clear();
            d.held[0].has_body = false;
        },
        [](Bed&, state::Data& d) { d.held[3].seq = 5; },
        [](Bed&, state::Data& d) { d.lanes[0].head = Head{d.held[0].id, 1}; },
        [](Bed&, state::Data& d) { d.size += 1U; },
        [](Bed&, state::Data& d) { std::swap(d.by_id[0], d.by_id[1]); }};
    for (const auto& change : own) {
        CHECK_EQ(forged(false, change), std::string("incoherent"));
        CHECK_EQ(forged<Store::Plain>(false, change), std::string("incoherent"));
    }
}
