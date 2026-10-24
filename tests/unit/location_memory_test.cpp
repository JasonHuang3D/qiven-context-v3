// location's budgets, counts and fault runs, on the fake over a scratch
// directory of the case's own. The steps, each from its own state: init
// into a new directory (the store created), into one holding a store
// (opened), init with a record there (chosen), init warned (exit 4); show;
// change to the recorded directory, to a new one (created), to one holding
// a store (opened), and with the old one holding map-marks.json
// (not_moved). budget: each step's command arena peak within its 1 MiB,
// every page given back; store_dir's peak on the caller's arena. counts:
// each step's command arena allocations, the store's arenas, the engine
// calls the wrapper maps and the platform calls, exactly; the engine heap's
// allocations with an engine installed; the platform's operations in
// order. faults_*: each step run with each of these failed in turn, and
// the engine heap's allocations with an engine installed: each run exit 1,
// or the step's own status where the failure came after the record or was
// absorbed; after each run every page given back, nothing but the record
// in the configuration directory, the record as before or, at exit 0,
// whole; after a refused run the next run, failing nothing, the step's
// own status.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/location.hpp"
#include "runner/runner.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace fs = std::filesystem;
namespace location = qiven3::location;
namespace sqlite = qiven3::store::sqlite;
using Fake = qiven3::platform::fake::FakePlatform;
using qiven3::memory::Countdown;
using qiven3::platform::State;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string bytes_of(const fs::path& path) {
    std::error_code error;
    if (!fs::exists(path, error)) {
        return "absent";
    }
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void put(const fs::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string record_of(std::string_view dir) {
    std::string d;
    for (const char c : dir) {
        if (c == '\\' || c == '"') {
            d.push_back('\\');
        }
        d.push_back(c);
    }
    return "{\"at\":\"2026-10-24T08:09:10.123Z\",\"dir\":\"" + d + "\"}\n";
}

enum class Step : std::uint8_t { init_new, init_held, init_chosen, init_warned, show, change_same, change_new, change_held, change_left };
constexpr std::array<Step, 9> steps = {Step::init_new,    Step::init_held,  Step::init_chosen,
                                       Step::init_warned, Step::show,       Step::change_same,
                                       Step::change_new,  Step::change_held, Step::change_left};
constexpr std::array<std::string_view, 9> step_names = {"init new",    "init held",  "init chosen",
                                                        "init warned", "show",       "change same",
                                                        "change new",  "change held", "change left"};
std::string_view name_of(Step s) { return step_names.at(static_cast<std::size_t>(s)); }

// The scratch directory, its fake and its directories: old, the recorded
// one a change leaves; held, holding a store made once; fresh, made by a
// step; warm, where the fake's BitLocker is unknown.
struct Bed {
    Bed() : root(fresh_root()), config_dir(root / "config"), record(config_dir / "location.json") {
        old = root / "old";
        held = root / "held";
        fresh = root / "new";
        warm = root / "warm";
        qiven3::platform::fake::Config c;
        c.root = text(root);
        c.config_dir = text(config_dir);
        c.seed = 5;
        c.clock = std::chrono::time_point_cast<std::chrono::microseconds>(
            std::chrono::sys_days{std::chrono::year{2026} / 10 / 24} + std::chrono::hours{8} + std::chrono::minutes{9} +
            std::chrono::seconds{10} + std::chrono::microseconds{123456});
        c.default_facts.volume = "V:";
        c.default_facts.protection.state = State::off;
        c.default_facts.file_history.state = State::off;
        c.default_facts.bitlocker.state = State::off;
        qiven3::platform::fake::FakeFacts w = c.default_facts;
        w.bitlocker.state = State::unknown;
        c.facts = {{text(warm), w}};
        fake = std::make_unique<Fake>(c);
        {
            const qiven3::store::Engine<Fake> maker(*fake, 1);
            auto s = qiven3::store::SQLiteStore<Fake>::open(text(held / "store.db"), true, false, *fake);
            static_cast<void>(s.close());
        }
        fs::create_directories(config_dir);
    }
    Bed(const Bed&) = delete;
    Bed& operator=(const Bed&) = delete;
    Bed(Bed&&) = delete;
    Bed& operator=(Bed&&) = delete;
    ~Bed() {
        engine.reset();
        fake.reset();
        std::error_code error;
        fs::remove_all(root, error);
    }
    static fs::path fresh_root() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        fs::path out = fs::temp_directory_path() / ("qiven3-location-m-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    // The step's state, made again from what any run left.
    void prepare(Step s) {
        std::error_code error;
        fs::remove(record, error);
        fs::remove_all(fresh, error);
        fs::remove_all(old, error);
        fs::remove_all(warm, error);
        if (s == Step::init_chosen || s == Step::change_new || s == Step::change_held || s == Step::change_left) {
            put(record, record_of(text(old)));
        } else if (s == Step::show || s == Step::change_same) {
            put(record, record_of(text(held)));
        }
        if (s == Step::change_left) {
            fs::create_directories(old);
            put(old / "map-marks.json", "{}");
        }
        before = bytes_of(record);
    }
    [[nodiscard]] fs::path target(Step s) const {
        if (s == Step::init_held || s == Step::change_held || s == Step::change_same) {
            return held;
        }
        return s == Step::init_warned ? warm / "s" : fresh;
    }
    [[nodiscard]] static int expected(Step s) {
        if (s == Step::init_chosen || s == Step::change_left) {
            return location::refused;
        }
        return s == Step::init_warned ? location::not_accepted : location::recorded;
    }
    // Whether the step writes a record.
    [[nodiscard]] static bool records(Step s) {
        return s == Step::init_new || s == Step::init_held || s == Step::change_new || s == Step::change_held;
    }
    // main on the step's arguments, its arena under arenas.
    int run(Step s, Countdown* arenas = nullptr, std::size_t* peak = nullptr) {
        const std::string dir = text(target(s));
        location::Hooks hooks;
        hooks.peak = peak;
#if defined(QIVEN3_FAULTS)
        hooks.arenas = arenas;
#else
        static_cast<void>(arenas);
#endif
        const std::string_view op = s <= Step::init_warned ? "init" : (s == Step::show ? "show" : "change");
        std::array<std::string_view, 3> args = {op, dir, "--accept"};
        const std::size_t n = s == Step::show ? 1 : (s == Step::init_warned ? 2 : 3);
        return location::main(std::span<const std::string_view>(args.data(), n), *fake, hooks);
    }
    // The record's state after a run that ended in code: as before, or the
    // step's own at its status 0.
    [[nodiscard]] bool record_held(Step s, int code) const {
        const std::string now = bytes_of(record);
        if (code == location::recorded && records(s)) {
            return now == record_of(text(target(s)));
        }
        return now == before;
    }
    // Nothing but the record in the configuration directory.
    [[nodiscard]] bool config_clean() const {
        std::error_code error;
        for (const fs::directory_entry& e : fs::directory_iterator(config_dir, error)) {
            if (e.path().filename() != "location.json") {
                return false;
            }
        }
        return !error;
    }

    fs::path root;
    fs::path config_dir;
    fs::path record;
    fs::path old;
    fs::path held;
    fs::path fresh;
    fs::path warm;
    std::unique_ptr<Fake> fake;
    std::optional<qiven3::store::Engine<Fake>> engine;
    std::string before;
};

}  // namespace

QIVEN3_CASE(location, budget) {
    const auto b = std::make_unique<Bed>();
    for (const Step s : steps) {
        b->prepare(s);
        std::size_t peak = 0;
        const int code = b->run(s, nullptr, &peak);
        std::cout << "location.budget: " << name_of(s) << ": exit " << code << ", peak " << peak << " of "
                  << location::budget << " bytes; pages lent after " << b->fake->lent() << "\n";
        CHECK_EQ(code, Bed::expected(s));
        CHECK(peak > 0 && peak <= location::budget);
        CHECK_EQ(b->fake->lent(), std::size_t{0});
        CHECK(b->record_held(s, code));
    }
    // store_dir on the caller's arena: its strings, the record's bytes and
    // the dir, each at most twice its length with its terminator.
    b->prepare(Step::show);
    std::vector<std::byte> pages(std::size_t{1} << 16U);
    qiven3::memory::Arena arena(pages);
    {
        qiven3::memory::String got{qiven3::memory::Allocator<char>(arena)};
        location::store_dir(*b->fake, got);
        CHECK_EQ(std::string(got), text(b->held));
    }
    const std::size_t lengths = text(b->config_dir).size() + text(b->record).size() + b->before.size() +
                                text(b->held).size() + 4;
    std::cout << "location.budget: store_dir: peak " << arena.peak() << " on the caller's arena, its strings "
              << lengths << " bytes\n";
    CHECK(arena.peak() >= lengths && arena.peak() <= 2 * lengths + 64);
}

#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
namespace {

struct Count {
    std::size_t arenas = 0;
    std::size_t store = 0;
    std::size_t calls = 0;
    std::size_t platform = 0;
    bool operator==(const Count&) const = default;
};

struct Typed {};
struct Untyped {};

enum class Kind : std::uint8_t { platform, arena, store, call, heap };
constexpr std::array<std::string_view, 5> kind_names = {"platform call", "command arena allocation",
                                                        "store arena allocation", "engine call",
                                                        "engine heap allocation"};

struct Tally {
    std::size_t refused = 0;
    std::size_t kept = 0;  // the step's own status: after the record, or absorbed
    std::size_t next = 0;  // after a refused run, the next run's own status
};

// One step, each call of kind failed in turn.
// from and to bound the runs that execute: a run k before from fails at
// once by its own k calls, executing nothing; a run past to completes at
// once, failing nothing, and ends the pass.
void pass(qiven3::test::Context& qiven3_ctx, Bed& b, Step s, Kind kind, std::size_t from = 1,
          std::size_t to = SIZE_MAX) {
    Countdown c;
    std::size_t n = 0;
    bool skipped = false;
    const std::size_t lent = b.fake->lent();
    Tally t;
    int code = 0;
    bool fired = false;
    const auto attach = [&](Countdown* on) {
        if (kind == Kind::platform) {
            b.fake->attach(on);
        } else if (kind == Kind::store) {
            sqlite::attach_calls(nullptr, on);
        } else if (kind == Kind::call) {
            sqlite::attach_calls(on, nullptr);
        } else if (kind == Kind::heap) {
            b.engine->heap().attach(on);
        }
    };
    const auto op = [&] {
        skipped = ++n < from || n > to;
        if (n < from) {
            for (std::size_t i = 0; i < n; ++i) {
                static_cast<void>(c.fail());
            }
            throw Typed{};
        }
        if (n > to) {
            return;
        }
        b.prepare(s);
        attach(&c);
        code = b.run(s, kind == Kind::arena ? &c : nullptr);
        attach(nullptr);
        fired = c.fired();
        if (!fired) {
            if (code != Bed::expected(s)) {
                throw Untyped{};
            }
            return;
        }
        if (code == location::refused || code == Bed::expected(s)) {
            throw Typed{};
        }
        throw Untyped{};
    };
    const auto check = [&](std::size_t k) {
        if (skipped) {
            return true;
        }
        bool held = b.fake->lent() == lent && b.config_clean() && b.record_held(s, code);
        if (fired && code == location::refused && Bed::expected(s) != location::refused) {
            ++t.refused;
            const int again = b.run(s);
            t.next += again == Bed::expected(s) ? 1U : 0U;
            held = held && again == Bed::expected(s) && b.record_held(s, again) && b.fake->lent() == lent;
        } else if (fired) {
            ++(code == location::refused ? t.refused : t.kept);
        }
        if (!held) {
            std::cout << "location.faults: " << name_of(s) << ", " << kind_names.at(static_cast<std::size_t>(kind))
                      << " " << k << ": exit " << code << ", not as before\n";
        }
        return held;
    };
    const qiven3::test::FaultRuns runs = qiven3::test::run_faults<Typed>(qiven3_ctx.log, "location.main", c, op, check, 5000);
    attach(nullptr);
    std::cout << "location.faults: " << name_of(s) << ", " << kind_names.at(static_cast<std::size_t>(kind)) << ": "
              << runs.typed << " runs, completed at " << runs.completed_at << " (executing " << from << " to "
              << (to == SIZE_MAX ? runs.completed_at : to) << "); refused " << t.refused
              << ", its own status " << t.kept << ", the next run its own " << t.next << "; untyped " << runs.untyped
              << ", unchecked " << runs.unchecked << "\n";
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK_EQ(runs.completed_at, runs.typed + 1);
    CHECK(kind != Kind::platform || runs.typed > 0);
    CHECK(to == SIZE_MAX || runs.completed_at == to + 1);
}

void passes(qiven3::test::Context& qiven3_ctx, std::span<const Step> chosen, std::span<const Kind> kinds,
            std::size_t from = 1, std::size_t to = SIZE_MAX) {
    const auto b = std::make_unique<Bed>();
    for (const Kind kind : kinds) {
        if (kind == Kind::heap) {
            b->engine.emplace(*b->fake, 1);
        }
        for (const Step s : chosen) {
            pass(qiven3_ctx, *b, s, kind, from, to);
        }
        b->engine.reset();
    }
}

// store_dir with each platform call, then each allocation on the caller's
// arena, failed in turn: each run its typed error, the arena's blocks and
// the fake's pages given back, the record unchanged.
void store_dir_faults(qiven3::test::Context& qiven3_ctx, Bed& b) {
    b.prepare(Step::show);
    std::vector<std::byte> pages(std::size_t{1} << 16U);
    for (const bool on_arena : {false, true}) {
        Countdown c;
        std::size_t used = 0;
        bool fired = false;
        std::string got;
        const auto op = [&] {
            qiven3::memory::Arena arena(pages);
            got.clear();
            if (on_arena) {
                arena.attach(&c);
            } else {
                b.fake->attach(&c);
            }
            try {
                qiven3::memory::String out{qiven3::memory::Allocator<char>(arena)};
                location::store_dir(*b.fake, out);
                got = std::string(out);
            } catch (const qiven3::store::StoreError&) {
                got = "StoreError";
            } catch (const qiven3::platform::PlatformError&) {
                got = "PlatformError";
            } catch (const qiven3::memory::MemoryError&) {
                got = "MemoryError";
            }
            b.fake->attach(nullptr);
            used = arena.used();
            fired = c.fired();
            if (fired) {
                if (got == "StoreError" || got == "PlatformError" || got == "MemoryError") {
                    throw Typed{};
                }
                throw Untyped{};
            }
        };
        const auto check = [&](std::size_t) {
            return used == 0 && b.fake->lent() == 0 && bytes_of(b.record) == b.before &&
                   (fired || got == text(b.held));
        };
        const qiven3::test::FaultRuns runs =
            qiven3::test::run_faults<Typed>(qiven3_ctx.log, "location.store_dir", c, op, check, 1000);
        std::cout << "location.faults: store_dir, " << (on_arena ? "caller arena allocation" : "platform call") << ": "
                  << runs.typed << " runs, completed at " << runs.completed_at << "; untyped " << runs.untyped
                  << ", unchecked " << runs.unchecked << "\n";
        CHECK(runs.typed > 0);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK_EQ(runs.completed_at, runs.typed + 1);
    }
}

constexpr std::array<Kind, 4> own_kinds = {Kind::platform, Kind::arena, Kind::store, Kind::call};
constexpr std::array<Step, 4> init_steps = {Step::init_new, Step::init_held, Step::init_chosen, Step::init_warned};
constexpr std::array<Step, 5> change_steps = {Step::show, Step::change_same, Step::change_new, Step::change_held,
                                              Step::change_left};
constexpr std::array<Step, 1> heap_create = {Step::init_new};
constexpr std::array<Step, 1> heap_open = {Step::change_held};
constexpr std::array<Kind, 1> heap_kind = {Kind::heap};


// The counts each step makes, pinned as measured, and the platform's
// operations in order. The command arena's count is its standard
// library's (a string's growth): measured with MSVC's on Windows and with
// libstdc++ and libc++ on Linux; the engine heap's its file layer's, on
// Windows and Linux; elsewhere not compared.
struct Pinned {
    Step step;
    Count windows;
    Count libstdcxx;
    Count libcxx;
    std::string_view operations;
};
#if defined(_WIN32) || (defined(__linux__) && (defined(__GLIBCXX__) || defined(_LIBCPP_VERSION)))
constexpr bool counts_pinned = true;
#else
constexpr bool counts_pinned = false;
#endif
constexpr std::string_view check_ops = "pages config_dir lock resolve pages resolve synced facts standard_output "
                                       "standard_output standard_output";
constexpr std::array<Pinned, 9> pinned = {{
    {Step::init_new, {26, 3, 98, 29}, {27, 3, 98, 29}, {25, 3, 98, 29},
     "now pages pages resolve synced pages resolve synced create create replace random_bytes standard_output "
     "standard_output"},
    {Step::init_held, {26, 2, 77, 24}, {27, 2, 77, 24}, {24, 2, 77, 24},
     "now pages pages resolve synced replace random_bytes standard_output standard_output"},
    {Step::init_chosen, {5, 0, 0, 6}, {5, 0, 0, 6}, {5, 0, 0, 6}, "pages config_dir lock standard_output"},
    {Step::init_warned, {11, 1, 0, 14}, {10, 1, 0, 14}, {10, 1, 0, 14}, "standard_output"},
    {Step::show, {16, 0, 0, 12}, {17, 0, 0, 12}, {15, 0, 0, 12},
     "pages config_dir lock read facts standard_output standard_output standard_output standard_output "
     "standard_output"},
    {Step::change_same, {25, 1, 0, 18}, {24, 1, 0, 18}, {20, 1, 0, 18}, "normcase normcase standard_output standard_output"},
    {Step::change_new, {40, 3, 98, 33}, {39, 3, 98, 33}, {35, 3, 98, 33},
     "normcase normcase now pages pages resolve synced pages resolve synced create create standard_output replace "
     "random_bytes standard_output standard_output"},
    {Step::change_held, {40, 2, 77, 28}, {39, 2, 77, 28}, {34, 2, 77, 28},
     "normcase normcase now pages pages resolve synced standard_output replace random_bytes standard_output "
     "standard_output"},
    {Step::change_left, {24, 1, 0, 17}, {22, 1, 0, 17}, {19, 1, 0, 17}, "normcase normcase standard_output"},
}};

// The step's operations as pinned: init's and change's after the check's,
// change's with read after lock.
std::string operations_of(const Pinned& p) {
    const std::string_view op = p.step <= Step::init_warned ? "init" : (p.step == Step::show ? "show" : "change");
    if (op == "show" || p.step == Step::init_chosen) {
        return std::string(p.operations);
    }
    std::string check(check_ops);
    if (op == "change") {
        check.replace(check.find("lock") + 4, 0, " read");
    }
    return check + " " + std::string(p.operations);
}

// The engine heap's allocations, an engine installed, on Windows and Linux.
struct HeapPinned {
    Step step;
    std::size_t windows;
    std::size_t on_linux;
};
constexpr std::array<HeapPinned, 2> heap_pinned = {{{Step::init_new, 651, 639}, {Step::change_held, 212, 206}}};

}  // namespace

QIVEN3_CASE(location, counts) {
    const auto b = std::make_unique<Bed>();
    Countdown arenas;
    Countdown store;
    Countdown calls;
    Countdown platform;
    for (const Pinned& p : pinned) {
        b->prepare(p.step);
        const std::size_t first = b->fake->calls().size();
        b->fake->attach(&platform);
        sqlite::attach_calls(&calls, &store);
        arenas.disarm();
        store.disarm();
        calls.disarm();
        platform.disarm();
        const int code = b->run(p.step, &arenas);
        b->fake->attach(nullptr);
        sqlite::attach_calls(nullptr, nullptr);
        const Count got{arenas.calls(), store.calls(), calls.calls(), platform.calls()};
        std::string ops;
        for (std::size_t i = first; i < b->fake->calls().size(); ++i) {
            ops += (ops.empty() ? "" : " ") + b->fake->calls().at(i);
        }
        std::cout << "location.counts: " << name_of(p.step) << ": exit " << code << "; command arena " << got.arenas
                  << ", store arenas " << got.store << ", engine calls " << got.calls << ", platform " << got.platform
                  << "\n  operations: " << ops << "\n";
        CHECK_EQ(code, Bed::expected(p.step));
        CHECK_EQ(ops, operations_of(p));
#if defined(_WIN32)
        const Count want = p.windows;
#elif defined(_LIBCPP_VERSION)
        const Count want = p.libcxx;
#else
        const Count want = p.libstdcxx;
#endif
        CHECK(!counts_pinned || got == want);
    }
    b->engine.emplace(*b->fake, 1);
    Countdown heap;
    for (const HeapPinned& h : heap_pinned) {
        b->prepare(h.step);
        b->engine->heap().attach(&heap);
        heap.disarm();
        const int code = b->run(h.step);
        b->engine->heap().attach(nullptr);
        std::cout << "location.counts: " << name_of(h.step) << ", an engine installed: exit " << code
                  << "; engine heap " << heap.calls() << "\n";
        CHECK_EQ(code, Bed::expected(h.step));
#if defined(_WIN32)
        CHECK(!counts_pinned || heap.calls() == h.windows);
#else
        CHECK(!counts_pinned || heap.calls() == h.on_linux);
#endif
    }
    b->engine.reset();
}

QIVEN3_CASE(location, faults_init) { passes(qiven3_ctx, init_steps, own_kinds); }
QIVEN3_CASE(location, faults_change) {
    passes(qiven3_ctx, change_steps, own_kinds);
    const auto b = std::make_unique<Bed>();
    store_dir_faults(qiven3_ctx, *b);
}
// init's create, its engine heap's allocations failed in two halves, each
// within the fast group's time under TSan.
QIVEN3_CASE(location, faults_heap_create) { passes(qiven3_ctx, heap_create, heap_kind, 1, 320); }
QIVEN3_CASE(location, faults_heap_create_rest) { passes(qiven3_ctx, heap_create, heap_kind, 321); }
QIVEN3_CASE(location, faults_heap_open) { passes(qiven3_ctx, heap_open, heap_kind); }
#else
namespace {
constexpr std::string_view unfaulted = "not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers";
}  // namespace
QIVEN3_CASE(location, counts) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(location, faults_init) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(location, faults_change) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(location, faults_heap_create) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(location, faults_heap_create_rest) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(location, faults_heap_open) { QIVEN3_SKIP(unfaulted); }
#endif
