// store/exchange's budgets, counts and fault runs, on the fake, over a scratch directory of the case's own: a
// source store with two lanes and a forget (A, B, D and ACC on implementer's lane, C and M1 on reviewer's, B
// forgotten by E), its lines held as an import's source, and an empty store to import them into. The steps:
// export, an export of the scope set project:* from seq 2 of implementer's lane made and closed; heads, its
// heads read; next, its lines read whole; import, the held lines into the empty store; verify, of the source.
// budget: each step's counted peak within 1 MiB (an export's pages 1 MiB plus 57 bytes a scope and 98 a seq),
// and the engine's peak, its heap's and its page cache's slots', on a fresh engine heap for each step, within
// the engine's budget for each connection the step holds. counts: each step's allocations, by allocator (the
// store's and the export's arenas, the engine heap, the caller's arena), its engine calls the wrapper maps and
// its platform calls, exactly. faults_export, faults_import and faults_verify: each step run with each of these
// failed in turn: each run MemoryError, PlatformError or the failed engine call's StoreLocked io, or a failure
// the engine absorbs, the step completing with its own result; after each run the export closed, the caller's
// arena and the fake's pages given back (an export's engine heap too), no transaction open, the source as its
// reads give it unchanged; after an import refused its store empty (verify's counts none), its -wal 0 bytes
// and the caller's heads cleared; an import that completed undone.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
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
#include "store/exchange.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

using namespace qiven3::test::stage_book;  // NOLINT(google-build-using-namespace)
namespace fs = std::filesystem;
namespace sqlite = qiven3::store::sqlite;
using qiven3::memory::MemoryError;
using qiven3::memory::Vector;
using qiven3::platform::PlatformError;
using qiven3::store::AfterView;
using qiven3::store::Counts;
using qiven3::store::GuardView;
using qiven3::store::Header;
using qiven3::store::Heads;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using qiven3::store::RangeView;
using qiven3::store::Sizes;
using Store = qiven3::checked::SQLiteStore<Fake>;
using Export = sqlite::SQLiteExport<Fake>;
using Engine = qiven3::store::Engine<Fake>;

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";
constexpr std::string_view reviewer_lane = "reviewer";
constexpr std::size_t call_bytes = std::size_t{2} << 20U;
constexpr std::size_t own_bytes = qiven3::store::operation_base;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::uintmax_t wal_bytes(const std::string& path) {
    std::error_code error;
    const std::uintmax_t n = fs::file_size(fs::path(path + "-wal"), error);
    return error ? 0 : n;
}

// A source of lines held: the heads given, then each line.
struct Held {
    std::vector<LaneHead> given;
    std::vector<std::pair<std::string, std::optional<std::string>>> lines;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines.size()) {
            return false;
        }
        line.header.assign(lines[at].first);
        line.has_body = lines[at].second.has_value();
        line.body.assign(lines[at].second.value_or(std::string()));
        ++at;
        return true;
    }
};

// The scratch, the fake over it, the fixtures, the engine heap, the source
// and the store to import into, the held lines, the export in hand and the
// caller's arena; on the heap.
struct Bed {
    Bed()
        : root(fresh_root()), fake(config(root)), book(fake), engine(std::make_unique<Engine>(fake)),
          from(text(root / "from" / "store.db")), into(text(root / "into" / "store.db")), call_pages(call_bytes) {
        source = std::make_unique<Store>(Store::open(from, true, false, fake));
        fill();
        target = std::make_unique<Store>(Store::open(into, true, false, fake));
        const Chars c = caller();
        auto whole = std::make_unique<Export>(source->export_(std::nullopt, {}));
        Vector<LaneHead> rows(c);
        (void)whole->heads(rows);
        held.given.assign(rows.begin(), rows.end());
        std::vector<std::byte> pages(std::size_t{1} << 18U);
        for (;;) {
            Arena arena(pages);
            Line line(arena);
            if (!whole->next(line)) {
                break;
            }
            held.lines.emplace_back(std::string(line.header),
                                    line.has_body ? std::optional<std::string>(std::string(line.body)) : std::nullopt);
        }
    }
    Bed(const Bed&) = delete;
    Bed& operator=(const Bed&) = delete;
    Bed(Bed&&) = delete;
    Bed& operator=(Bed&&) = delete;
    ~Bed() {
        x.reset();
        source.reset();
        target.reset();
        engine.reset();
        std::error_code error;
        fs::remove_all(root, error);
    }

    static fs::path fresh_root() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        fs::path out = fs::temp_directory_path() / ("qiven3-exchange-m-" + std::to_string(draw()) +
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

    void fill() {
        const std::vector<Stored> mine = {book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")};
        const std::vector<Stored> theirs = {book.stored("C"), book.stored("M1")};
        const std::vector<Stored> forgets = {book.stored("E")};
        (void)source->append(qiven_scope, implementer_lane, std::nullopt, views(mine));
        (void)source->append(qiven_scope, reviewer_lane, std::nullopt, views(theirs));
        const std::string acc = id_of(mine[3]);
        const std::string m1 = id_of(theirs[1]);
        const std::vector<GuardView> guard = {GuardView{qiven_scope, implementer_lane, acc},
                                              GuardView{qiven_scope, reviewer_lane, m1}};
        std::vector<std::byte> pages(std::size_t{1} << 16U);
        Arena arena(pages);
        Vector<Id> erased{Chars(arena)};
        (void)source->forget(qiven_scope, implementer_lane, acc, views(forgets), guard, erased);
    }

    // A fresh caller's arena.
    Chars caller() {
        call.emplace(call_pages);
#if defined(QIVEN3_FAULTS)
        call->attach(call_countdown);
#endif
        return Chars(*call);
    }
    // The export of the step's set and seq, made.
    void make_export() { x = std::make_unique<Export>(source->export_(scopes, after)); }
    // The store to import into, emptied: its files removed, made again.
    void empty_target() {
        target.reset();
        std::error_code error;
        fs::remove_all(root / "into", error);
        target = std::make_unique<Store>(Store::open(into, true, false, fake));
#if defined(QIVEN3_FAULTS)
        target->attach(attached);
#endif
    }
    // A fresh engine heap, the stores closed before; the source or the
    // target reopened after.
    void fresh_engine(bool open_source, bool open_target) {
        x.reset();
        source.reset();
        target.reset();
        engine.reset();
        engine = std::make_unique<Engine>(fake);
        if (open_source) {
            source = std::make_unique<Store>(Store::open(from, false, false, fake));
        }
        if (open_target) {
            target = std::make_unique<Store>(Store::open(into, false, false, fake));
        }
    }

    fs::path root;
    Fake fake;
    Book book;
    std::unique_ptr<Engine> engine;  // before the stores, which end first
    std::string from;
    std::string into;
    std::unique_ptr<Store> source;
    std::unique_ptr<Store> target;
    std::unique_ptr<Export> x;
    Held held;
    std::vector<std::string_view> set = {"project:*"};
    qiven3::store::Scopes scopes{set};
    std::vector<AfterView> after = {AfterView{qiven_scope, implementer_lane, 2}};
    std::vector<std::byte> call_pages;
    std::optional<Arena> call;
    std::size_t heads_left = 0;  // the caller's heads after a refused import
#if defined(QIVEN3_FAULTS)
    qiven3::memory::Countdown* call_countdown = nullptr;
    qiven3::memory::Countdown* attached = nullptr;  // the stores' arenas'
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
    std::size_t own = 0;
    std::size_t caller = 0;
    std::string result;
};
enum class Side : std::uint8_t { exporting, importing, verifying };
struct Step {
    std::string name;
    Side side;
    bool made;              // an export made before it
    std::string_view want;  // its result
    std::function<Ran(Bed&, const Chars&)> run;
};
constexpr std::string_view next_ids = "7f5af8 b  014325 b  e84b77 -  e721d4 b  cdc1cc b  ";

std::vector<Step> steps() {
    std::vector<Step> out;
    out.push_back({"export", Side::exporting, false, "made", [](Bed& b, const Chars&) {
                       b.make_export();
                       const std::size_t peak = b.x->peak();
                       b.x->close();
                       return Ran{peak, 0, "made"};
                   }});
    out.push_back({"heads", Side::exporting, true, "2 1 7", [](Bed& b, const Chars& c) {
                       Vector<LaneHead> rows(c);
                       const qiven3::store::Snapshot s = b.x->heads(rows);
                       return Ran{0, b.call->peak(),
                                  std::to_string(rows.size()) + " " + std::to_string(s.forgets) + " " +
                                      std::to_string(s.volume.count)};
                   }});
    out.push_back({"next", Side::exporting, true, next_ids, [](Bed& b, const Chars&) {
                       std::string ids;
                       std::size_t most = 0;
                       for (;;) {
                           (void)b.caller();
                           Line line(*b.call);
                           if (!b.x->next(line)) {
                               break;
                           }
                           most = std::max(most, b.call->peak());
                           const std::string_view h(line.header);
                           ids.append(h.substr(h.find("\"id\":") + 13, 6)).append(line.has_body ? " b" : " -");
                           ids.append(line.forgotten ? "f " : "  ");
                       }
                       return Ran{b.x->peak(), most, ids};
                   }});
    out.push_back({"import", Side::importing, false, "7 2", [](Bed& b, const Chars& c) {
                       b.held.at = 0;
                       Vector<LaneHead> heads(c);
                       b.heads_left = 0;
                       try {
                           const qiven3::store::Imported i = b.target->import_(b.held, heads);
                           return Ran{b.target->peak(), b.call->peak(),
                                      std::to_string(i.counts.entries) + " " + std::to_string(heads.size())};
                       } catch (...) {
                           b.heads_left = heads.size();
                           throw;
                       }
                   }});
    out.push_back({"verify", Side::verifying, false, "7 5 1 2", [](Bed& b, const Chars&) {
                       const Counts n = b.source->verify();
                       return Ran{b.source->peak(), 0,
                                  std::to_string(n.entries) + " " + std::to_string(n.bodies) + " " +
                                      std::to_string(n.forgets) + " " + std::to_string(n.lanes)};
                   }});
    return out;
}

}  // namespace

QIVEN3_CASE(store_exchange, budget) {
    const auto b = std::make_unique<Bed>();
    CHECK_EQ(sqlite::Reader::pages_for(b->scopes, b->after), own_bytes + 57U + 98U);
    CHECK_EQ(sqlite::Reader::pages_for(std::nullopt, {}), own_bytes);
    const std::vector<std::string_view> three = {"project:a", "project:b", "project:c"};
    const std::vector<AfterView> two = {AfterView{"project:a", "x", 1}, AfterView{"project:b", "y", 2}};
    CHECK_EQ(sqlite::Reader::pages_for(three, two), own_bytes + (3U * 57U) + (2U * 98U));
    for (const Step& s : steps()) {
        b->fresh_engine(s.side != Side::importing, s.side == Side::importing);
        if (s.side == Side::importing) {
            b->empty_target();
        }
        const std::size_t opened = b->engine->heap().peak();
        if (s.made) {
            b->make_export();
        }
        const Chars c = b->caller();
        const Ran r = s.run(*b, c);
        const sqlite::Slots slots = b->engine->slots();
        const std::size_t engine = b->engine->heap().peak() + (slots.peak * slots.size);
        const std::size_t engine_within = qiven3::store::engine_budget * (s.side == Side::exporting ? 2U : 1U);
        CHECK(r.own <= own_bytes);
        CHECK(r.own > 0 || s.name == "heads");
        CHECK_EQ(r.result, std::string(s.want));
        CHECK(r.caller <= call_bytes);
        CHECK(engine <= engine_within);
        std::cout << "store_exchange.budget: " << s.name << ": " << r.own << " counted within " << own_bytes << ", "
                  << r.caller << " on the caller's arena; engine peak " << engine << ", " << slots.peak
                  << " slots of it (reopened, the heap's " << opened << ") within " << engine_within << "; "
                  << r.result << "\n";
    }
}

#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
namespace {

// The counts each step makes, pinned as measured; the engine heap's count
// is the engine's own, by its system's file layer: measured on Windows and
// Linux; elsewhere not compared.
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
constexpr std::array<Pinned, 5> pinned = {{
    {"export", {1, 208, 50, 0, 1}, {1, 201, 50, 0, 1}},
    {"heads", {0, 0, 4, 2, 0}, {0, 0, 4, 2, 0}},
    {"next", {73, 0, 16, 9, 0}, {73, 0, 16, 9, 0}},
    {"import", {472, 74, 223, 2, 0}, {472, 74, 223, 2, 0}},
    {"verify", {336, 17, 28, 0, 0}, {336, 17, 28, 0, 0}},
}};

Count pinned_of(std::string_view name) {
    for (const Pinned& p : pinned) {
        if (p.name == name) {
#if defined(_WIN32)
            return p.windows;
#else
            return p.on_linux;
#endif
        }
    }
    return {};
}

// The source as its reads give it: its heads, forget count, size, count
// and a scan's ids; or what a read raised.
std::string fingerprint(Store& store) {
    try {
        std::vector<std::byte> pages(std::size_t{1} << 20U);
        Arena arena(pages);
        const Chars chars(arena);
        std::string out;
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
        return out;
    } catch (const qiven3::store::StoreError& e) {
        return "raised " + std::string(qiven3::store::name(e.kind()));
    } catch (const MemoryError&) {
        return "raised MemoryError";
    }
}

struct Typed {};  // a fault run's typed end
struct Tally {  // a step's fault runs' tallies
    std::size_t memory = 0;
    std::size_t platform = 0;
    std::size_t engine = 0;    // StoreLocked io, a failed engine call's
    std::size_t absorbed = 0;  // the step completing past a failure
    std::size_t differed = 0;  // of those, a result not the step's
};

void attach(Bed& b, qiven3::memory::Countdown* c) {
    b.attached = c;
    b.source->attach(c);
    b.target->attach(c);
    b.engine->heap().attach(c);
    sqlite::attach_calls(c, c);
    b.call_countdown = c;
    b.fake.attach(c);
}

bool injected_io(const qiven3::store::StoreError& e) {
    return qiven3::store::name(e.kind()) == "StoreLocked" && e.cause() && *e.cause() == qiven3::store::Cause::io;
}

struct After {
    std::string source;
    std::size_t lent = 0;
    std::size_t heap = 0;
};

bool held_after(Bed& b, const Step& s, const After& before, bool done) {
    b.x.reset();
    bool good = b.call->used() == 0 && b.fake.lent() == before.lent;
    if (s.side == Side::importing) {
        good = good && !b.target->connection().in_transaction() && b.heads_left == 0;
        if (done) {
            b.empty_target();
        }
        return good && b.target->verify() == Counts{} && wal_bytes(b.into) == 0;
    }
    good = good && (s.side != Side::exporting || b.engine->heap().used() == before.heap);
    good = good && !b.source->connection().in_transaction();
    return good && fingerprint(*b.source) == before.source;
}

// One step's fault runs.
void fault_step(qiven3::test::Context& qiven3_ctx, Bed& b, const Step& s, qiven3::memory::Countdown& countdown) {
    if (s.side == Side::importing) {
        b.empty_target();
    }
    After before{fingerprint(*b.source), b.fake.lent(), 0};
    before.heap = b.engine->heap().used();
    Tally t;
    bool done = false;
    std::size_t k = 0;
    std::string result;
    std::vector<std::string> results;
    const auto op = [&] {
        done = false;
        ++k;
        countdown.disarm();
        const Chars c = b.caller();
        if (s.made) {
            b.make_export();
        }
        countdown.arm(k);
        Ran r;
        try {
            r = s.run(b, c);
        } catch (const MemoryError&) {
            ++t.memory;
            throw Typed{};
        } catch (const PlatformError&) {
            ++t.platform;
            throw Typed{};
        } catch (const qiven3::store::StoreError& e) {
            if (!injected_io(e)) {
                std::cout << "store_exchange.faults: " << s.name << ": " << qiven3::store::name(e.kind()) << " "
                          << e.detail() << "\n";
                throw;
            }
            ++t.engine;
            throw Typed{};
        }
        done = true;
        result = r.result;
        if (countdown.fired()) {
            ++t.absorbed;
            results.push_back(r.result);
            throw Typed{};
        }
    };
    const auto check = [&](std::size_t) {
        const bool good = held_after(b, s, before, done);
        if (!good) {
            std::cout << "store_exchange.faults: " << s.name << ": run " << k << " left the state changed\n";
        }
        return good;
    };
    attach(b, &countdown);
    const qiven3::test::FaultRuns runs =
        qiven3::test::run_faults<Typed>(qiven3_ctx.log, "store_exchange.step", countdown, op, check);
    attach(b, nullptr);
    for (const std::string& r : results) {
        t.differed += r == result ? 0U : 1U;
    }
    std::cout << "store_exchange.faults: " << s.name << ": " << runs.typed << " runs refused or absorbed: memory "
              << t.memory << ", platform " << t.platform << ", engine call " << t.engine << ", absorbed "
              << t.absorbed << " (" << t.differed << " of another result); untyped " << runs.untyped
              << ", unchecked " << runs.unchecked << "; completed: " << result << "\n";
    CHECK(done);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK_EQ(runs.completed_at, runs.typed + 1);
    CHECK_EQ(t.memory + t.platform + t.engine + t.absorbed, runs.typed);
    CHECK_EQ(t.differed, 0U);
}
void fault_steps(qiven3::test::Context& qiven3_ctx, Side side) {
    const auto b = std::make_unique<Bed>();
    qiven3::memory::Countdown countdown;
    for (const Step& s : steps()) {
        if (s.side == side) {
            fault_step(qiven3_ctx, *b, s, countdown);
        }
    }
}

}  // namespace

QIVEN3_CASE(store_exchange, counts) {
    const auto b = std::make_unique<Bed>();
    qiven3::memory::Countdown arenas;
    qiven3::memory::Countdown heap;
    qiven3::memory::Countdown calls;
    qiven3::memory::Countdown caller;
    qiven3::memory::Countdown platform;
    b->attached = &arenas;
    b->source->attach(&arenas);
    b->target->attach(&arenas);
    b->engine->heap().attach(&heap);
    sqlite::attach_calls(&calls, &arenas);
    b->call_countdown = &caller;
    b->fake.attach(&platform);
    for (const Step& s : steps()) {
        if (s.side == Side::importing) {
            b->empty_target();
        }
        if (s.made) {
            b->make_export();
        }
        const Chars c = b->caller();
        arenas.disarm();
        heap.disarm();
        calls.disarm();
        caller.disarm();
        platform.disarm();
        const Ran r = s.run(*b, c);
        const Count got{arenas.calls(), heap.calls(), calls.calls(), caller.calls(), platform.calls()};
        std::cout << "store_exchange.counts: " << s.name << ": arenas " << got.arenas << ", engine heap " << got.heap
                  << ", engine calls " << got.calls << ", caller " << got.caller << ", platform " << got.platform
                  << "; " << r.result << "\n";
        Count want = pinned_of(s.name);
        want.heap = heap_pinned ? want.heap : got.heap;
        CHECK(got == want);
        b->x.reset();
    }
    b->fake.attach(nullptr);
    sqlite::attach_calls(nullptr, nullptr);
    b->engine->heap().attach(nullptr);
    b->call_countdown = nullptr;
    b->attached = nullptr;
    b->source->attach(nullptr);
    b->target->attach(nullptr);
}

QIVEN3_CASE(store_exchange, faults_export) { fault_steps(qiven3_ctx, Side::exporting); }
QIVEN3_CASE(store_exchange, faults_import) { fault_steps(qiven3_ctx, Side::importing); }
QIVEN3_CASE(store_exchange, faults_verify) { fault_steps(qiven3_ctx, Side::verifying); }
#else
namespace {
constexpr std::string_view unfaulted = "not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers";
}  // namespace
QIVEN3_CASE(store_exchange, counts) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(store_exchange, faults_export) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(store_exchange, faults_import) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(store_exchange, faults_verify) { QIVEN3_SKIP(unfaulted); }
#endif
