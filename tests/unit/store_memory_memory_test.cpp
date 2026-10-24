// store/memory's budgets, counts and fault runs, on the fake, over one
// course of steps on a state: an object made; A, B and D appended, then
// ACC; head, heads, header, body and forgotten of A; a scan read whole;
// size; B forgotten by E under the guard; purge; an export read whole and
// closed; that export imported into a second, empty state; verify; an
// object made and closed. budget: each step's counted peak (the store's
// peak(), a scan's) within its budget, 1 MiB plus 4 times the lines given,
// an export's lines on the caller's arena within 2 MiB. counts: each
// step's allocations, by allocator (the store's arenas, the states' heaps,
// the caller's arena) and its platform calls, exactly. faults: each step
// run with each allocation failed in turn, then each platform call: each
// run MemoryError, or PlatformError for a platform call; after it the
// caller's arena and the fake's pages given back, both states as the reads
// give them unchanged (heads, size, count, forget count, a scan's ids,
// verify's counts), and each heap holding no more than once the step
// completes: a container's room grown before a later allocation failed
// stays, as a vector's reserve does, and saves the next run one allocation.
// The run completes past the last failure. The runner's count after each
// case holds too.
// maximum: a module of 64 KiB content appended, imported into the second
// state and verified, each peak within its operation's budget.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/memory.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

using namespace qiven3::test::stage_book;  // NOLINT(google-build-using-namespace)
namespace state = qiven3::store::state;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
using qiven3::memory::Vector;
using qiven3::platform::PlatformError;
using qiven3::store::Counts;
using qiven3::store::GuardView;
using qiven3::store::Header;
using qiven3::store::Heads;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using qiven3::store::MemoryStore;
using qiven3::store::RangeView;
using qiven3::store::Sizes;
using Store = MemoryStore<Fake>;

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";
constexpr std::size_t state_bytes = std::size_t{8} << 20U;
constexpr std::size_t call_bytes = std::size_t{2} << 20U;
// An import grows the state's rows as its lines arrive, unreserved, so its
// heap's count follows the library's growth: by half (MSVC's) or double.
#if defined(_MSVC_STL_VERSION)
constexpr std::size_t import_heap = 29;
#else
constexpr std::size_t import_heap = 26;
#endif

// The fake, the fixtures, two states with their objects, and the caller's
// arena, fresh for each run.
struct Bed {
    Bed()
        : fake(qiven3::platform::fake::Config{}), book(fake), heap(fake.pages(state_bytes)),
          state(heap.bytes(), Limits{}), heap2(fake.pages(state_bytes)), state2(heap2.bytes(), Limits{}),
          store(state, fake), other(state2, fake), first{book.stored("A"), book.stored("B"), book.stored("D")},
          acc{book.stored("ACC")}, e{book.stored("E")}, first_lines(views(first)), acc_lines(views(acc)),
          e_lines(views(e)), a_id(id_of(first[0])), d_id(id_of(first[2])), acc_id(id_of(acc[0])),
          guard{GuardView{qiven_scope, implementer_lane, acc_id}}, call_pages(call_bytes) {}

    // A fresh caller's arena.
    Chars caller() {
        call.emplace(call_pages);
#if defined(QIVEN3_FAULTS)
        call->attach(call_countdown);
#endif
        return Chars(*call);
    }

    Fake fake;
    Book book;
    qiven3::platform::fake::FakePages heap;
    state::State state;
    qiven3::platform::fake::FakePages heap2;
    state::State state2;  // the import's
    Store store;
    Store other;
    std::vector<Stored> first;
    std::vector<Stored> acc;
    std::vector<Stored> e;
    std::vector<EntryView> first_lines;
    std::vector<EntryView> acc_lines;
    std::vector<EntryView> e_lines;
    std::string a_id;
    std::string d_id;
    std::string acc_id;
    std::vector<GuardView> guard;
    std::vector<std::byte> call_pages;
    std::optional<Arena> call;
#if defined(QIVEN3_FAULTS)
    qiven3::memory::Countdown* call_countdown = nullptr;
#endif
};

// A step's allocations by allocator, and its platform calls.
struct Count {
    std::size_t arenas = 0;
    std::size_t heap = 0;
    std::size_t caller = 0;
    std::size_t platform = 0;
    friend bool operator==(const Count&, const Count&) = default;
};

// A step: its name, its budget, what it writes, its counts, and its run,
// which gives its counted peak and its caller's arena's.
struct Peaks {
    std::size_t store = 0;
    std::size_t caller = 0;
};
struct Step {
    std::string name;
    std::size_t budget;
    bool writes;
    Count count;
    std::function<Peaks(Bed&, const Chars&)> run;
};

std::size_t lines_budget(const std::vector<EntryView>& lines) { return state::budget_of(lines); }

std::vector<Step> steps() {
    constexpr std::size_t base = qiven3::store::operation_base;
    std::vector<Step> out;
    out.push_back({"make", base, false, {0, 0, 0, 1}, [](Bed& b, const Chars&) {
                       Store s(b.state, b.fake);
                       return Peaks{};
                   }});
    out.push_back({"append", 0, true, {204, 17, 0, 1}, [](Bed& b, const Chars&) {
                       (void)b.store.append(qiven_scope, implementer_lane, std::nullopt, b.first_lines);
                       return Peaks{b.store.peak(), 0};
                   }});
    out.push_back({"append_acc", 0, true, {106, 7, 0, 1}, [](Bed& b, const Chars&) {
                       (void)b.store.append(qiven_scope, implementer_lane, b.d_id, b.acc_lines);
                       return Peaks{b.store.peak(), 0};
                   }});
    out.push_back({"head", base, false, {}, [](Bed& b, const Chars&) {
                       (void)b.store.head(qiven_scope, implementer_lane);
                       return Peaks{};
                   }});
    out.push_back({"heads", base, false, {0, 0, 1, 0}, [](Bed& b, const Chars& c) {
                       Vector<LaneHead> rows(c);
                       (void)b.store.heads(std::nullopt, rows);
                       return Peaks{0, b.call->peak()};
                   }});
    out.push_back({"header", base, false, {15, 0, 1, 0}, [](Bed& b, const Chars& c) {
                       Header h(c);
                       b.store.header(b.a_id, h);
                       return Peaks{b.store.peak(), b.call->peak()};
                   }});
    out.push_back({"body", base, false, {20, 0, 1, 0}, [](Bed& b, const Chars& c) {
                       String record(c);
                       (void)b.store.body(b.a_id, record);
                       return Peaks{b.store.peak(), b.call->peak()};
                   }});
    out.push_back({"forgotten", base, false, {}, [](Bed& b, const Chars&) {
                       (void)b.store.forgotten(b.a_id);
                       return Peaks{};
                   }});
    out.push_back({"scan", base, false, {60, 1, 4, 1}, [](Bed& b, const Chars& c) {
                       auto scan = b.store.scan(RangeView{});
                       Header h(c);
                       while (scan.next(h)) {
                       }
                       return Peaks{scan.peak(), b.call->peak()};
                   }});
    out.push_back({"size", base, false, {}, [](Bed& b, const Chars&) {
                       (void)b.store.size();
                       return Peaks{};
                   }});
    out.push_back({"forget", 0, true, {56, 5, 1, 1}, [](Bed& b, const Chars& c) {
                       Vector<Id> erased(c);
                       (void)b.store.forget(qiven_scope, implementer_lane, b.acc_id, b.e_lines, b.guard, erased);
                       return Peaks{b.store.peak(), b.call->peak()};
                   }});
    out.push_back({"purge", base, false, {}, [](Bed& b, const Chars&) {
                       (void)b.store.purge();
                       return Peaks{};
                   }});
    out.push_back({"export", qiven3::store::export_budget, false, {0, 2, 4, 0}, [](Bed& b, const Chars& c) {
                       auto x = b.store.export_(std::nullopt, {});
                       Vector<LaneHead> rows(c);
                       (void)x.heads(rows);
                       Line line(*b.call);
                       while (x.next(line)) {
                       }
                       x.close();
                       return Peaks{0, b.call->peak()};
                   }});
    out.push_back({"import", base, true, {399, import_heap, 1, 0}, [](Bed& b, const Chars& c) {
                       auto x = b.store.export_(std::nullopt, {});
                       Vector<LaneHead> wrote(c);
                       (void)b.other.import_(x, wrote);
                       return Peaks{b.other.peak(), b.call->peak()};
                   }});
    out.push_back({"verify", base, false, {230, 0, 0, 0}, [](Bed& b, const Chars&) {
                       (void)b.store.verify();
                       return Peaks{b.store.peak(), 0};
                   }});
    out.push_back({"close", base, false, {0, 0, 0, 1}, [](Bed& b, const Chars&) {
                       Store s(b.state, b.fake);
                       (void)s.close();
                       return Peaks{};
                   }});
    return out;
}

// A step's budget: given, else its lines'.
std::size_t budget(const Bed& b, const Step& s) {
    if (s.budget != 0) {
        return s.budget;
    }
    return lines_budget(s.name == "append" ? b.first_lines : s.name == "append_acc" ? b.acc_lines : b.e_lines);
}

// A fault run's typed end: MemoryError or PlatformError.
struct Typed {};

// The bytes a state's heap holds.
std::size_t held(state::State& s) {
    const state::Locked lock(s, Operation::size, {});
    return lock.data().heap.used();
}

// A state as the reads give it.
std::string fingerprint(Store& store) {
    std::vector<std::byte> pages(std::size_t{1} << 20U);
    Arena arena(pages);
    const Chars chars(arena);
    std::string out;
    {
        Vector<LaneHead> rows(chars);
        const Heads hs = store.heads(std::nullopt, rows);
        for (const LaneHead& r : rows) {
            out.append(r.scope.view()).append("/").append(r.lane.view()).append("=").append(r.head.id.view());
            out.append("@").append(std::to_string(r.head.seq)).append(";");
        }
        out.append("forgets ").append(std::to_string(hs.forgets));
        const auto sizes = std::make_unique<Sizes>(store.size());
        out.append(" size ").append(std::to_string(sizes->volume.size));
        out.append(" count ").append(std::to_string(sizes->volume.count)).append(" ids");
        auto scan = store.scan(RangeView{});
        Header h(chars);
        while (scan.next(h)) {
            out.append(" ").append(h.value(Slot::id));
        }
        const Counts c = store.verify();
        out.append(" verify ").append(std::to_string(c.entries)).append(",").append(std::to_string(c.bodies));
        out.append(",").append(std::to_string(c.forgets)).append(",").append(std::to_string(c.lanes));
    }
    return out;
}

}  // namespace

QIVEN3_CASE(store_memory, budget) {
    const auto b = std::make_unique<Bed>();
    for (const Step& s : steps()) {
        const Chars c = b->caller();
        const Peaks p = s.run(*b, c);
        const std::size_t within = budget(*b, s);
        CHECK(p.store <= within);
        CHECK(p.caller <= (s.name == "export" ? within : call_bytes));
        CHECK_EQ(b->call->used(), 0U);
        std::cout << "store_memory.budget: " << s.name << ": " << p.store << " bytes counted, " << p.caller
                  << " on the caller's arena, within " << within << "\n";
    }
    CHECK_EQ(fingerprint(b->other), fingerprint(b->store));
}

QIVEN3_CASE(store_memory, counts) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const auto b = std::make_unique<Bed>();
    qiven3::memory::Countdown arenas;
    qiven3::memory::Countdown heap;
    qiven3::memory::Countdown caller;
    qiven3::memory::Countdown platform;
    b->store.attach(&arenas);
    b->other.attach(&arenas);
    state::attach(b->state, &heap);
    state::attach(b->state2, &heap);
    b->call_countdown = &caller;
    b->fake.attach(&platform);
    for (const Step& s : steps()) {
        const Chars c = b->caller();
        const std::size_t calls = b->fake.calls().size();
        arenas.disarm();
        heap.disarm();
        caller.disarm();
        platform.disarm();
        (void)s.run(*b, c);
        const Count got{arenas.calls(), heap.calls(), caller.calls(), platform.calls()};
        std::size_t wipes = 0;
        for (std::size_t i = calls; i < b->fake.calls().size(); ++i) {
            wipes += b->fake.calls()[i] == "wipe" ? 1U : 0U;
        }
        std::cout << "store_memory.counts: " << s.name << ": arenas " << got.arenas << ", heap " << got.heap
                  << ", caller " << got.caller << ", platform " << got.platform << " and " << wipes << " wipes\n";
        CHECK(got == s.count);
    }
    b->fake.attach(nullptr);
    state::attach(b->state, nullptr);
    state::attach(b->state2, nullptr);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}

QIVEN3_CASE(store_memory, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const auto b = std::make_unique<Bed>();
    qiven3::memory::Countdown countdown;
    const auto attach = [&](qiven3::memory::Countdown* c) {
        b->store.attach(c);
        b->other.attach(c);
        state::attach(b->state, c);
        state::attach(b->state2, c);
        b->call_countdown = c;
        b->fake.attach(c);
    };
    std::size_t total = 0;
    for (const Step& s : steps()) {
        const std::string before = fingerprint(b->store) + " | " + fingerprint(b->other);
        const std::size_t held1 = held(b->state);
        const std::size_t held2 = held(b->state2);
        const std::size_t lent = b->fake.lent();
        bool done = false;
        // Each heap's bytes after the last refused run, and the times
        // they rose: once a container at most.
        std::size_t last1 = held1;
        std::size_t last2 = held2;
        std::size_t rises = 0;
        std::size_t memory = 0;
        std::size_t platform = 0;
        // A failed allocation MemoryError, a failed platform call
        // PlatformError: each typed, counted apart; any other untyped.
        const auto op = [&] {
            const Chars c = b->caller();
            try {
                (void)s.run(*b, c);
            } catch (const MemoryError&) {
                ++memory;
                throw Typed{};
            } catch (const PlatformError&) {
                ++platform;
                throw Typed{};
            }
            done = true;
        };
        // Given back, and the states as they were, unless the step wrote
        // and completed.
        const auto check = [&](std::size_t) {
            const bool back = b->call->used() == 0 && b->fake.lent() == lent;
            if (done && s.writes) {
                return back;
            }
            const std::size_t now1 = held(b->state);
            const std::size_t now2 = held(b->state2);
            rises += (now1 > last1 ? 1U : 0U) + (now2 > last2 ? 1U : 0U);
            const bool kept = now1 >= last1 && now2 >= last2;
            last1 = now1;
            last2 = now2;
            if (!kept) {
                return false;
            }
            return back && fingerprint(b->store) + " | " + fingerprint(b->other) == before;
        };
        attach(&countdown);
        const qiven3::test::FaultRuns runs =
            qiven3::test::run_faults<Typed>(qiven3_ctx.log, "store_memory.step", countdown, op, check);
        attach(nullptr);
        CHECK(done);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK_EQ(runs.completed_at, runs.typed + 1);
        CHECK_EQ(memory + platform, runs.typed);
        // A refused run's grown room on a heap, kept, never given again:
        // no more rises than the containers a call grows (held, by_id,
        // lanes, a lane's ids, the exports), and the run after it one
        // allocation fewer.
        CHECK(rises <= 5U);
        const std::size_t calls = s.count.arenas + s.count.heap + s.count.caller + s.count.platform;
        CHECK(runs.typed == calls || runs.typed + 1 == calls);
        total += runs.typed;
        std::cout << "store_memory.faults: " << s.name << ": " << memory << " allocations and " << platform
                  << " platform calls failed, each typed, the state as it was, the heaps' room grown " << rises
                  << " times\n";
    }
    std::cout << "store_memory.faults: " << total << " runs\n";
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}

namespace {

// The content's maximum, 64 KiB.
constexpr std::size_t max_content = std::size_t{64} << 10U;

// MX's content grown to the maximum: letters of its paths and interface
// names made four-byte characters, three bytes more each, the last one or
// two bytes more by a two- or three-byte one; its characters' count kept.
std::string maximum_content(const Book& book) {
    const std::string mx = *book.stored("MX").content;
    const std::size_t paths = mx.find('[', mx.find("\"paths\":["));
    const std::size_t paths_end = mx.find(']', paths);
    const std::size_t names = mx.find('[', mx.find("\"interface\":["));
    const std::size_t names_end = mx.find(']', names);
    std::size_t grow = max_content - mx.size();
    std::string out;
    out.reserve(max_content);
    for (std::size_t i = 0; i < mx.size(); ++i) {
        const bool letter =
            (i > paths && i < paths_end && mx[i] == 'a') || (i > names && i < names_end && mx[i] == 'b');
        if (!letter || grow == 0) {
            out.push_back(mx[i]);
        } else if (grow >= 3) {
            out.append("\xF0\x9F\x98\x80");
            grow -= 3;
        } else {
            out.append(grow == 2 ? "\xE2\x82\xAC" : "\xC3\xA9");
            grow = 0;
        }
    }
    return out;
}

}  // namespace

// A maximum entry, a module of 64 KiB content on reviewer's lane: its
// append within 1 MiB plus 4 times its lines, its import into the second
// state and verify within 1 MiB.
QIVEN3_CASE(store_memory, maximum) {
    const auto b = std::make_unique<Bed>();
    const std::string content = maximum_content(b->book);
    REQUIRE(content.size() == max_content);
    const std::vector<Stored> mx = {b->book.made("MX", "MX", content, [](header::FieldsView& f) {
        f.seq = 1;
        f.parent = std::nullopt;
    })};
    const std::vector<EntryView> lines = views(mx);
    const std::optional<Refusal> r =
        refused([&] { (void)b->store.append(qiven_scope, "reviewer", std::nullopt, lines); });
    if (r) {
        std::cout << "store_memory.maximum: refused " << r->code << " " << r->field << " " << r->detail << "\n";
    }
    REQUIRE(!r.has_value());
    const std::size_t append = b->store.peak();
    CHECK(append <= lines_budget(lines));
    const Chars c = b->caller();
    {
        auto x = b->store.export_(std::nullopt, {});
        Vector<LaneHead> wrote(c);
        (void)b->other.import_(x, wrote);
        x.close();
    }
    const std::size_t imported = b->other.peak();
    CHECK(imported <= qiven3::store::operation_base);
    const Counts counts = b->store.verify();
    const std::size_t verified = b->store.peak();
    CHECK(verified <= qiven3::store::operation_base);
    CHECK_EQ(counts.entries, 1U);
    CHECK_EQ(counts.bodies, 1U);
    CHECK(b->other.verify() == counts);
    std::cout << "store_memory.maximum: content " << content.size() << " bytes, lines " << mx[0].line.size() << " and "
              << mx[0].record->size() << "; append " << append << " of " << lines_budget(lines) << ", import "
              << imported << " and verify " << verified << " of " << qiven3::store::operation_base << "\n";
}
