// store/sqlite's budgets, counts and fault runs, on the fake, over a
// scratch directory of the case's own and one course of steps on a store:
// created; A, B and D appended, then ACC; head, heads, header, body and
// forgotten of A; a scan read whole; size; B forgotten by E under the
// guard; purge; a second object opened and closed, read-write, then
// read-only; the guard alone. budget: each step's counted peak (the
// store's peak(), a scan's) within its budget, 1 MiB plus 4 times the
// lines given, and the engine's peak, its heap's and its page cache's
// slots', on a fresh engine heap for each step, the store reopened on it
// first, within the engine's budget for each connection the step holds.
// counts: each step's allocations, by allocator (the store's arenas, the engine heap, the caller's arena), its
// engine calls the wrapper maps and its platform calls, exactly. faults: the steps but the create and the forget,
// which store_sqlite_parts_test.cpp runs in parts, each within 30 s on every job; a step outside the case's run
// once, unfaulted: each step run with each of these failed in turn: each run MemoryError,
// PlatformError or the failed engine call's StoreLocked io, or an
// allocation the engine absorbs, the step completing; after a refused run
// the caller's arena and the fake's pages given back, no transaction left
// open, the store as its reads give it unchanged; a run the engine
// absorbed, its result as the step's; a write so completed, or a create,
// undone from the files kept before it. The engine heap given back whole
// at its end, which fails fast otherwise.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "checked/stores.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"
#include "support/faults/parts.hpp"
#include "support/fixtures/stage_book.hpp"

namespace qiven3::test {  // A group's fault runs, a part's range of them, by store_sqlite_parts_test.cpp.
void store_sqlite_fault_steps(Context& qiven3_ctx, std::string_view group, FaultRange range); }

namespace {

using namespace qiven3::test::stage_book;  // NOLINT(google-build-using-namespace)
namespace fs = std::filesystem;
namespace sqlite = qiven3::store::sqlite;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
using qiven3::memory::Vector;
using qiven3::platform::PlatformError;
using qiven3::store::GuardView;
using qiven3::store::Header;
using qiven3::store::Heads;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::store::RangeView;
using qiven3::store::Sizes;
using qiven3::store::StoreError;
using qiven3::store::Written;
using Store = qiven3::checked::SQLiteStore<Fake>;
using Engine = qiven3::store::Engine<Fake>;

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";
constexpr std::size_t call_bytes = std::size_t{2} << 20U;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The scratch, the fake over it, the fixtures, the engine heap and the
// store, its steps' lines and the caller's arena; on the heap.
struct Bed {
    Bed()
        : root(fresh_root()), fake(config(root)), book(fake), engine(std::make_unique<Engine>(fake)),
          path(text(root / "s" / "store.db")), first{book.stored("A"), book.stored("B"), book.stored("D")},
          acc{book.stored("ACC")}, e{book.stored("E")}, first_lines(views(first)), acc_lines(views(acc)),
          e_lines(views(e)), a_id(id_of(first[0])), d_id(id_of(first[2])), acc_id(id_of(acc[0])),
          guard{GuardView{qiven_scope, implementer_lane, acc_id}}, call_pages(call_bytes) {}
    Bed(const Bed&) = delete;
    Bed& operator=(const Bed&) = delete;
    Bed(Bed&&) = delete;
    Bed& operator=(Bed&&) = delete;
    ~Bed() {
        store.reset();
        engine.reset();
        std::error_code error;
        fs::remove_all(root, error);
    }

    static fs::path fresh_root() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        fs::path out = fs::temp_directory_path() / ("qiven3-sqlite-m-" + std::to_string(draw()) +
                                                    std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    static qiven3::platform::fake::Config config(const fs::path& root) {
        qiven3::platform::fake::Config c;
        c.root = text(root);
        c.seed = 7;
        return c;
    }

    // A fresh caller's arena.
    Chars caller() {
        call.emplace(call_pages);
#if defined(QIVEN3_FAULTS)
        call->attach(call_countdown);
#endif
        return Chars(*call);
    }
    void reopen() { store = std::make_unique<Store>(Store::open(path, false, false, fake)); }
    // A fresh engine heap, the store closed before and reopened after.
    void fresh_engine(bool open) {
        store.reset();
        engine.reset();
        engine = std::make_unique<Engine>(fake);
        if (open) {
            reopen();
        }
    }
    // The store's files kept, its object closed and made again; then put
    // back, as they were kept.
    void keep() {
        store.reset();
        kept_db = bytes_of(path);
        reopen();
    }
    void put_back() {
        store.reset();
        std::error_code error;
        fs::remove(path + "-wal", error);
        fs::remove(path + "-shm", error);
        std::ofstream(path, std::ios::binary | std::ios::trunc) << kept_db;
        reopen();
    }

    fs::path root;
    Fake fake;
    Book book;
    std::unique_ptr<Engine> engine;  // before the store, which ends first
    std::string path;
    std::unique_ptr<Store> store;
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
    std::string kept_db;
#if defined(QIVEN3_FAULTS)
    qiven3::memory::Countdown* call_countdown = nullptr;
#endif
};

// A step's allocations by allocator, its engine calls and platform calls.
struct Count {
    std::size_t arenas = 0;
    std::size_t heap = 0;
    std::size_t calls = 0;
    std::size_t caller = 0;
    std::size_t platform = 0;
    friend bool operator==(const Count&, const Count&) = default;
};

// What a step gave: its counted peak, its caller's arena's, its result.
struct Ran {
    std::size_t store = 0;
    std::size_t caller = 0;
    std::string result;
};
enum class Sort : std::uint8_t { create, write, read };
struct Step {
    std::string name;
    std::size_t budget;  // 0: the lines'
    Sort sort;
    std::size_t connections;
    Count count;
    std::function<Ran(Bed&, const Chars&)> run;
};

std::string written(const Written& w) {
    return std::string(w.head.id.view()) + "@" + std::to_string(w.head.seq) + " " + std::to_string(w.volume.count);
}

void add_steps(std::vector<Step>& out);
void add_reads(std::vector<Step>& out);

std::vector<Step> steps() {
    std::vector<Step> out;
    add_steps(out);
    add_reads(out);
    return out;
}

void add_steps(std::vector<Step>& out) {
    constexpr std::size_t base = qiven3::store::operation_base;
    out.push_back({"create", base, Sort::create, 1, {}, [](Bed& b, const Chars&) {
                       b.store = std::make_unique<Store>(Store::open(b.path, true, false, b.fake));
                       return Ran{b.store->peak(), 0, "made"};
                   }});
    out.push_back({"append", 0, Sort::write, 1, {}, [](Bed& b, const Chars&) {
                       const Written w =
                           b.store->append(qiven_scope, implementer_lane, std::nullopt, b.first_lines);
                       return Ran{b.store->peak(), 0, written(w)};
                   }});
    out.push_back({"append_acc", 0, Sort::write, 1, {}, [](Bed& b, const Chars&) {
                       const Written w = b.store->append(qiven_scope, implementer_lane, b.d_id, b.acc_lines);
                       return Ran{b.store->peak(), 0, written(w)};
                   }});
}

void add_reads(std::vector<Step>& out) {
    constexpr std::size_t base = qiven3::store::operation_base;
    out.push_back({"head", base, Sort::read, 1, {}, [](Bed& b, const Chars&) {
                       const auto h = b.store->head(qiven_scope, implementer_lane);
                       return Ran{0, 0, h ? std::string(h->id.view()) : "none"};
                   }});
    out.push_back({"heads", base, Sort::read, 1, {}, [](Bed& b, const Chars& c) {
                       Vector<LaneHead> rows(c);
                       const Heads hs = b.store->heads(std::nullopt, rows);
                       return Ran{0, b.call->peak(), std::to_string(rows.size()) + " " + std::to_string(hs.forgets)};
                   }});
    out.push_back({"header", base, Sort::read, 1, {}, [](Bed& b, const Chars& c) {
                       Header h(c);
                       b.store->header(b.a_id, h);
                       return Ran{b.store->peak(), b.call->peak(), std::string(h.value(Slot::id))};
                   }});
    out.push_back({"body", base, Sort::read, 1, {}, [](Bed& b, const Chars& c) {
                       String record(c);
                       const bool held = b.store->body(b.a_id, record);
                       return Ran{b.store->peak(), b.call->peak(), std::to_string(held) + std::to_string(record.size())};
                   }});
    out.push_back({"forgotten", base, Sort::read, 1, {}, [](Bed& b, const Chars&) {
                       return Ran{0, 0, std::to_string(b.store->forgotten(b.a_id))};
                   }});
    out.push_back({"scan", base, Sort::read, 1, {}, [](Bed& b, const Chars& c) {
                       auto scan = b.store->scan(RangeView{});
                       Header h(c);
                       std::string ids;
                       while (scan.next(h)) {
                           ids.append(h.value(Slot::id).substr(0, 8)).append(" ");
                       }
                       return Ran{scan.peak(), b.call->peak(), ids};
                   }});
    out.push_back({"size", base, Sort::read, 1, {}, [](Bed& b, const Chars&) {
                       const auto sizes = std::make_unique<Sizes>(b.store->size());
                       return Ran{0, 0, std::to_string(sizes->volume.size) + " " + std::to_string(sizes->volume.count)};
                   }});
    out.push_back({"forget", 0, Sort::write, 1, {}, [](Bed& b, const Chars& c) {
                       Vector<Id> erased(c);
                       const Written w =
                           b.store->forget(qiven_scope, implementer_lane, b.acc_id, b.e_lines, b.guard, erased);
                       return Ran{b.store->peak(), b.call->peak(), written(w) + " " + std::to_string(erased.size())};
                   }});
    out.push_back({"purge", base, Sort::read, 1, {}, [](Bed& b, const Chars&) {
                       return Ran{0, 0, std::to_string(b.store->purge().done)};
                   }});
    out.push_back({"open", base, Sort::read, 2, {}, [](Bed& b, const Chars&) {
                       Store s = Store::open(b.path, false, false, b.fake);
                       const bool done = s.close().done;
                       return Ran{s.peak(), 0, std::to_string(done)};
                   }});
    out.push_back({"read_only", base, Sort::read, 2, {}, [](Bed& b, const Chars&) {
                       Store s = Store::open(b.path, false, true, b.fake);
                       const bool done = s.close().done;
                       return Ran{s.peak(), 0, std::to_string(done)};
                   }});
    out.push_back({"guard", base, Sort::read, 0, {}, [](Bed& b, const Chars&) {
                       Store::guard(b.path, b.fake);
                       return Ran{0, 0, "guarded"};
                   }});
}

// A step's budget: given, else its lines'.
std::size_t budget(const Bed& b, const Step& s) {
    if (s.budget != 0) {
        return s.budget;
    }
    const std::vector<EntryView>& lines =
        s.name == "append" ? b.first_lines : s.name == "append_acc" ? b.acc_lines : b.e_lines;
    return qiven3::store::operation_budget(qiven3::store::given(lines).value_or(0)).value_or(0);
}

}  // namespace

QIVEN3_CASE(store_sqlite, budget) {
    const auto b = std::make_unique<Bed>();
    for (const Step& s : steps()) {
        b->fresh_engine(s.sort != Sort::create);
        const std::size_t opened = b->engine->heap().peak();
        const Chars c = b->caller();
        const Ran r = s.run(*b, c);
        const qiven3::store::sqlite::Slots slots = b->engine->slots();
        const std::size_t engine = b->engine->heap().peak() + (slots.peak * slots.size);
        const std::size_t within = budget(*b, s);
        const std::size_t engine_within = qiven3::store::engine_budget * (s.connections == 0 ? 1 : s.connections);
        CHECK(r.store <= within);
        CHECK(r.caller <= call_bytes);
        CHECK(engine <= engine_within);
        CHECK_EQ(b->call->used(), 0U);
        std::cout << "store_sqlite.budget: " << s.name << ": " << r.store << " bytes counted, " << r.caller
                  << " on the caller's arena, within " << within << "; engine peak " << engine << ", "
                  << slots.peak << " slots of it (reopened, the heap's " << opened << ") within " << engine_within
                  << "\n";
    }
}

#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
namespace {

// The counts each step makes, pinned as measured; the engine's own and the platform calls differ by
// system. The engine heap's, by its system's file layer: measured on Windows and Linux; elsewhere not compared.
struct Pinned {
    std::string_view name;
    Count windows;
    Count on_linux;
};
#if defined(_WIN32) || defined(__linux__)
constexpr bool heap_pinned = true;
#else
constexpr bool heap_pinned = false;
#endif
constexpr std::array<Pinned, 15> pinned = {{
    {"create", {1, 652, 98, 0, 5}, {1, 642, 98, 0, 5}},
    {"append", {206, 14, 78, 0, 1}, {206, 14, 78, 0, 1}},
    {"append_acc", {106, 21, 57, 0, 1}, {106, 21, 57, 0, 1}},
    {"head", {0, 0, 4, 0, 0}, {0, 0, 4, 0, 0}},
    {"heads", {0, 0, 11, 1, 0}, {0, 0, 11, 1, 0}},
    {"header", {16, 0, 3, 1, 0}, {16, 0, 3, 1, 0}},
    {"body", {22, 0, 3, 1, 0}, {22, 0, 3, 1, 0}},
    {"forgotten", {0, 0, 3, 0, 0}, {0, 0, 3, 0, 0}},
    {"scan", {64, 10, 47, 4, 0}, {64, 10, 47, 4, 0}},
    {"size", {0, 0, 8, 0, 0}, {0, 0, 8, 0, 0}},
    {"forget", {59, 719, 80, 1, 1}, {59, 722, 80, 1, 1}},
    {"purge", {0, 0, 3, 0, 0}, {0, 0, 3, 0, 0}},
    {"open", {1, 209, 80, 0, 3}, {1, 203, 80, 0, 3}},
    {"read_only", {1, 208, 68, 0, 3}, {1, 202, 68, 0, 3}},
    {"guard", {1, 0, 0, 0, 3}, {1, 0, 0, 0, 3}},
}};

void pin(std::vector<Step>& all) {
    for (Step& s : all) {
        for (const Pinned& p : pinned) {
            if (p.name == s.name) {
#if defined(_WIN32)
                s.count = p.windows;
#else
                s.count = p.on_linux;
#endif
            }
        }
    }
}

// The store as its reads give it: its heads, forget count, size, count and
// a scan's ids; or what a read raised.
std::string fingerprint(Store* store) {
    if (store == nullptr) {
        return "none";
    }
    try {
        std::vector<std::byte> pages(std::size_t{1} << 20U);
        Arena arena(pages);
        const Chars chars(arena);
        std::string out;
        Vector<LaneHead> rows(chars);
        const Heads hs = store->heads(std::nullopt, rows);
        for (const LaneHead& r : rows) {
            out.append(r.scope.view()).append("/").append(r.lane.view()).append("=").append(r.head.id.view());
            out.append("@").append(std::to_string(r.head.seq)).append(";");
        }
        out.append("forgets ").append(std::to_string(hs.forgets));
        const auto sizes = std::make_unique<Sizes>(store->size());
        out.append(" size ").append(std::to_string(sizes->volume.size));
        out.append(" count ").append(std::to_string(sizes->volume.count)).append(" ids");
        auto scan = store->scan(RangeView{});
        Header h(chars);
        while (scan.next(h)) {
            out.append(" ").append(h.value(Slot::id));
        }
        return out;
    } catch (const StoreError& e) {
        return "raised " + std::string(qiven3::store::name(e.kind()));
    } catch (const MemoryError&) {
        return "raised MemoryError";
    }
}

struct Typed {};  // a fault run's typed end
struct Tally {    // a step's fault runs' tallies
    std::size_t memory = 0;
    std::size_t platform = 0;
    std::size_t engine = 0;    // StoreLocked io, a failed engine call's
    std::size_t absorbed = 0;  // the step completing past a failed allocation
    std::size_t differed = 0;  // of those, a result not the step's
    std::size_t left = 0;      // a create's refused runs leaving its file
};

void attach(Bed& b, qiven3::memory::Countdown* c) {
    if (b.store) {
        b.store->attach(c);
    }
    b.engine->heap().attach(c);
    sqlite::attach_calls(c, c);
    b.call_countdown = c;
    b.fake.attach(c);
}

bool injected_io(const StoreError& e) {
    return qiven3::store::name(e.kind()) == "StoreLocked" && e.cause() &&
           *e.cause() == qiven3::store::Cause::io;
}

// One step's fault runs, those of the part's range; its tallies, and
// whether each run held.
bool fault_step(qiven3::test::Context& qiven3_ctx, Bed& b, const Step& s, qiven3::memory::Countdown& countdown,
                qiven3::test::FaultRange range) {
    const std::string before = fingerprint(b.store.get());
    if (s.sort == Sort::write) {
        b.keep();
    }
    const std::size_t lent = b.fake.lent();
    Tally t;
    qiven3::test::PartRuns part(range);
    bool done = false;
    bool absorbed = false;
    std::string result;
    std::vector<std::string> results;
    const auto op = [&] {
        done = false;
        absorbed = false;
        part.enter<Typed>(countdown);
        const Chars c = b.caller();
        Ran r;
        try {
            r = s.run(b, c);
        } catch (const MemoryError&) {
            ++t.memory;
            throw Typed{};
        } catch (const PlatformError&) {
            ++t.platform;
            throw Typed{};
        } catch (const StoreError& e) {
            if (!injected_io(e)) {
                std::cout << "store_sqlite.faults: " << s.name << ": " << qiven3::store::name(e.kind()) << " "
                          << e.detail() << "\n";
                throw;
            }
            ++t.engine;
            throw Typed{};
        }
        done = true;
        result = r.result;
        if (countdown.fired()) {
            absorbed = true;
            ++t.absorbed;
            results.push_back(r.result);
            throw Typed{};
        }
    };
    const auto check = [&](std::size_t) {
        if (part.skipped()) {
            return true;
        }
        if (s.sort == Sort::create) {
            if (!done) {
                std::error_code error;
                t.left += fs::exists(b.path, error) ? 1U : 0U;
            }
            if (!done || absorbed) {
                b.store.reset();
                std::error_code error;
                fs::remove_all(b.root / "s", error);
            }
            return b.call->used() == 0 && (done && !absorbed ? true : b.fake.lent() == lent);
        }
        if (done && !absorbed) {
            return b.call->used() == 0 && b.fake.lent() == lent;
        }
        if (absorbed && s.sort == Sort::write) {
            b.put_back();
        }
        const bool back = b.call->used() == 0 && b.fake.lent() == lent;
        return back && !b.store->connection().in_transaction() && fingerprint(b.store.get()) == before;
    };
    attach(b, &countdown);
    const qiven3::test::FaultRuns runs =
        qiven3::test::run_faults<Typed>(qiven3_ctx.log, "store_sqlite.step", countdown, op, check);
    attach(b, nullptr);
    for (const std::string& r : results) {
        t.differed += r == result ? 0U : 1U;
    }
    std::cout << "store_sqlite.faults: " << s.name << ": " << runs.typed << " runs refused or absorbed: memory "
              << t.memory << ", platform " << t.platform << ", engine call " << t.engine << ", absorbed "
              << t.absorbed << " (" << t.differed << " of another result), a create's file left " << t.left
              << "; untyped " << runs.untyped << ", unchecked " << runs.unchecked << "; executing " << range.from
              << " to " << part.last(runs) << ", " << part.executed() << " runs\n";
    CHECK(done);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK_EQ(runs.completed_at, runs.typed + 1);
    CHECK(part.held(runs));
    CHECK_EQ(t.memory + t.platform + t.engine + t.absorbed, runs.typed - part.before());
    CHECK_EQ(t.left, 0U);  // a refused create removes its file
    return runs.untyped == 0 && runs.unchecked == 0;
}

}  // namespace

// The steps in order on one store: those of the case's group ("create", "forget" or "rest", the other
// steps) faulted, the runs of the part's range alone, the others run once (store_sqlite_parts_test.cpp).
void qiven3::test::store_sqlite_fault_steps(Context& qiven3_ctx, std::string_view group, FaultRange range) {
    const auto b = std::make_unique<Bed>();
    qiven3::memory::Countdown countdown;
    for (const Step& s : steps()) {
        if ((s.name == "create" || s.name == "forget" ? std::string_view(s.name) : "rest") != group) {
            const Chars c = b->caller();
            (void)s.run(*b, c);
        } else if (!fault_step(qiven3_ctx, *b, s, countdown, range)) {
            std::cout << "store_sqlite.faults: " << s.name << " failed\n";
        }
    }
}

QIVEN3_CASE(store_sqlite, counts) {
    const auto b = std::make_unique<Bed>();
    std::vector<Step> all = steps();
    pin(all);
    qiven3::memory::Countdown arenas;
    qiven3::memory::Countdown heap;
    qiven3::memory::Countdown calls;
    qiven3::memory::Countdown caller;
    qiven3::memory::Countdown platform;
    b->engine->heap().attach(&heap);
    sqlite::attach_calls(&calls, &arenas);
    b->call_countdown = &caller;
    b->fake.attach(&platform);
    for (const Step& s : all) {
        const Chars c = b->caller();
        arenas.disarm();
        heap.disarm();
        calls.disarm();
        caller.disarm();
        platform.disarm();
        (void)s.run(*b, c);
        const Count got{arenas.calls(), heap.calls(), calls.calls(), caller.calls(), platform.calls()};
        std::cout << "store_sqlite.counts: " << s.name << ": arenas " << got.arenas << ", engine heap " << got.heap
                  << ", engine calls " << got.calls << ", caller " << got.caller << ", platform " << got.platform
                  << "\n";
        Count want = s.count;
        want.heap = heap_pinned ? want.heap : got.heap;
        CHECK(got == want);
    }
    b->fake.attach(nullptr);
    sqlite::attach_calls(nullptr, nullptr);
    b->engine->heap().attach(nullptr);
    b->store->attach(nullptr);
}
#else
void qiven3::test::store_sqlite_fault_steps(Context& /*qiven3_ctx*/, std::string_view /*group*/, FaultRange) {
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
}
QIVEN3_CASE(store_sqlite, counts) {
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
}
#endif
QIVEN3_CASE(store_sqlite, faults) { qiven3::test::store_sqlite_fault_steps(qiven3_ctx, "rest", {}); }
