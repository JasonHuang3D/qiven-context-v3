// store/sqlite's refused create, on the fake over a scratch directory of
// the case's own. create_refused, in parts (store_sqlite_create_parts_test.cpp
// registers them, support/faults/parts.hpp), each a few runs: four passes,
// each refusing in turn every call of one kind a create makes: the fake's
// platform calls, the store's arena allocations, the engine calls the
// wrapper maps (open_v2, the settings, the schema, its TRUNCATE checkpoint,
// the probe, coherence), and the engine heap's allocations, the parts by
// their first points (that file's table). After each refused run nothing is
// left, the directory create made removed too, and a second create makes
// the store. create_unremoved: a removal itself refused, by the countdowns (the
// engine heap's first allocation refused, then one of the four files'
// removals, or the directory's by the platform's countdown) and by a
// directory in a sidecar's place: the step's error raised, its detail
// naming the refused removal; with a file's refused the file kept and the
// next create StoreError exists, with the directory's alone the directory
// left empty and the next create made; a directory there before create
// kept.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "store/codes.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"
#include "support/faults/parts.hpp"

namespace qiven3::test {
void store_sqlite_create_refused(Context& qiven3_ctx, const std::array<FaultRange, 4>& ranges);
}  // namespace qiven3::test

#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
namespace {

namespace fs = std::filesystem;
namespace sqlite = qiven3::store::sqlite;
using Fake = qiven3::platform::fake::FakePlatform;
using Store = qiven3::store::SQLiteStore<Fake>;
using qiven3::memory::Countdown;
using qiven3::memory::MemoryError;
using qiven3::platform::PlatformError;
using qiven3::store::StoreError;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// A scratch directory of the case's own, removed at its end; the fake over
// it and the engine heap; the store's directory, made by create.
struct Rig {
    Rig() : root(fresh()), fake(config(root)), engine(fake), dir(root / "c"), path(text(dir / "store.db")) {}
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;
    ~Rig() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    static fs::path fresh() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        fs::path out = fs::temp_directory_path() / ("qiven3-sqlite-c-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    static qiven3::platform::fake::Config config(const fs::path& root) {
        qiven3::platform::fake::Config c;
        c.root = text(root);
        c.seed = 7;
        return c;
    }
    // The names under the store's directory; "absent" where there is none.
    [[nodiscard]] std::string left() const {
        std::error_code error;
        if (!fs::exists(dir, error)) {
            return "absent";
        }
        std::string names;
        for (const fs::directory_entry& e : fs::directory_iterator(dir, error)) {
            names += text(e.path().filename()) + " ";
        }
        return names;
    }
    // A create, its object closed; "made" or what it raised, its detail
    // kept in detail.
    [[nodiscard]] std::string create() {
        detail.clear();
        try {
            Store s = Store::open(path, true, false, fake);
            (void)s.close();
            return "made";
        } catch (const StoreError& e) {
            detail = e.detail();
            const auto code = e.code();
            return std::string(qiven3::store::name(e.kind())) + (code ? " " + std::string(qiven3::store::name(*code)) : "");
        } catch (const MemoryError& e) {
            detail = e.detail.view();
            return "MemoryError";
        } catch (const PlatformError& e) {
            detail = e.detail.view();
            return "PlatformError";
        }
    }
    // Whether the last create's detail ends naming these removals refused.
    [[nodiscard]] bool names(std::string_view refused) const {
        const std::string tail = "removal refused: " + std::string(refused);
        return detail.size() >= tail.size() && detail.compare(detail.size() - tail.size(), tail.size(), tail) == 0;
    }
    void clear() const {
        std::error_code error;
        fs::remove_all(dir, error);
    }
    fs::path root;
    Fake fake;
    qiven3::store::Engine<Fake> engine;
    fs::path dir;
    std::string path;
    std::string detail;
};


struct Typed {};

enum class Kind : std::uint8_t { platform, arena, call, heap };
constexpr std::array<std::string_view, 4> kind_names = {"platform call", "arena allocation", "engine call",
                                                        "engine heap allocation"};

void attach(Rig& rig, Kind kind, Countdown* c) {
    if (kind == Kind::platform) {
        rig.fake.attach(c);
    } else if (kind == Kind::arena) {
        sqlite::attach_calls(nullptr, c);
    } else if (kind == Kind::call) {
        sqlite::attach_calls(c, nullptr);
    } else {
        rig.engine.heap().attach(c);
    }
}

// The steps of connect, each by its last call of kind counted from the
// connection's open: open_v2, the settings, the schema with its TRUNCATE
// checkpoint, the probe, coherence; and the checkpoint's own calls.
struct Bounds {
    std::array<std::size_t, 5> last{};
    std::size_t checkpoint = 0;
};
constexpr std::array<std::string_view, 6> step_names = {"open_v2", "settings", "schema", "checkpoint", "probe",
                                                        "coherence"};

Bounds measure(Rig& rig, Kind kind) {
    std::error_code error;
    fs::create_directories(rig.dir, error);
    rig.fake.create(rig.path, false);
    std::vector<std::byte> pages(std::size_t{1} << 16U);
    qiven3::memory::Arena arena(pages);
    const qiven3::memory::String path(rig.path, qiven3::memory::Allocator<char>(arena));
    const qiven3::store::WhereView at = sqlite::site(sqlite::open_name, rig.path);
    Bounds b;
    Countdown c;
    {
        attach(rig, kind, &c);
        sqlite::Connection conn = sqlite::Connection::open(path, false, at);
        b.last.at(0) = c.calls();
        sqlite::apply(conn, false, at);
        b.last.at(1) = c.calls();
        sqlite::create_schema(conn, at);
        b.last.at(2) = c.calls();
        sqlite::probe(conn, at);
        b.last.at(3) = c.calls();
        (void)sqlite::coherence(conn, at);
        b.last.at(4) = c.calls();
        conn.exec("PRAGMA wal_checkpoint(TRUNCATE)", at);
        b.checkpoint = c.calls() - b.last.at(4);
    }
    attach(rig, kind, nullptr);
    rig.clear();
    return b;
}

// The step of the at-th call of kind, counted from the create's start:
// the platform calls and the arena's come before the connection, the
// engine's within it.
std::string_view step_of(Kind kind, const Bounds& b, std::size_t at) {
    if (kind == Kind::platform || kind == Kind::arena) {
        return "before the connection";
    }
    for (std::size_t i = 0; i < b.last.size(); ++i) {
        if (at <= b.last.at(i)) {
            if (i == 2 && kind == Kind::call && at + b.checkpoint > b.last.at(2)) {
                return step_names.at(3);
            }
            return step_names.at(i < 3 ? i : i + 1);
        }
    }
    return "past coherence";
}

// One pass: each call of kind refused in turn, the runs of the part's
// range r executing (support/faults/parts.hpp's PartRuns).
void pass(qiven3::test::Context& qiven3_ctx, Rig& rig, Kind kind, qiven3::test::FaultRange r) {
    const Bounds b = measure(rig, kind);
    qiven3::test::PartRuns part(r);
    std::map<std::string, std::size_t> by_step;
    std::map<std::string, std::size_t> raised;
    std::size_t left = 0;
    std::size_t again = 0;
    std::size_t emptied = 0;
    std::size_t named = 0;
    std::optional<Store> made;
    bool done = false;
    const std::size_t lent = rig.fake.lent();
    Countdown c;
    const auto op = [&] {
        done = false;
        part.enter<Typed>(c);
        rig.detail.clear();
        try {
            made.emplace(Store::open(rig.path, true, false, rig.fake));
        } catch (const StoreError& e) {
            ++raised[std::string(qiven3::store::name(e.kind()))];
            rig.detail = e.detail();
            throw Typed{};
        } catch (const MemoryError& e) {
            ++raised["MemoryError"];
            rig.detail = e.detail.view();
            throw Typed{};
        } catch (const PlatformError& e) {
            ++raised["PlatformError"];
            rig.detail = e.detail.view();
            throw Typed{};
        }
        done = true;
        if (c.fired()) {
            ++raised["absorbed"];
            throw Typed{};
        }
    };
    const auto check = [&](std::size_t k) {
        if (part.skipped()) {
            return true;
        }
        made.reset();
        bool held = rig.fake.lent() == lent;
        if (!done) {
            const std::string names = rig.left();
            const bool clean = names == "absent";
            left += clean ? 0U : 1U;
            emptied += names.empty() ? 1U : 0U;
            named += rig.detail.find("removal refused") != std::string::npos ? 1U : 0U;
            if (!clean) {
                std::cout << "store_sqlite.create_refused: " << kind_names.at(static_cast<std::size_t>(kind)) << " " << k
                          << " left " << names << "\n";
            }
            ++by_step[std::string(step_of(kind, b, k))];
            const std::string second = rig.create();
            again += second == "made" ? 1U : 0U;
            held = held && clean && second == "made";
        }
        rig.clear();
        return held;
    };
    attach(rig, kind, &c);
    const qiven3::test::FaultRuns runs = qiven3::test::run_faults<Typed>(qiven3_ctx.log, "store_sqlite.create", c, op, check);
    attach(rig, kind, nullptr);
    std::cout << "store_sqlite.create_refused: " << kind_names.at(static_cast<std::size_t>(kind)) << ": "
              << runs.typed << " runs refused, completed at " << runs.completed_at << " (executing " << r.from
              << " to " << part.last(runs) << ", " << part.executed() << " runs); files left " << left
              << ", its directory left empty " << emptied << ", a removal named " << named
              << ", a second create made " << again << "; untyped "
              << runs.untyped << ", unchecked " << runs.unchecked << "\n  by step:";
    for (const auto& [step, n] : by_step) {
        std::cout << " " << step << " " << n << ";";
    }
    std::cout << "\n  raised:";
    for (const auto& [name, n] : raised) {
        std::cout << " " << name << " " << n << ";";
    }
    std::cout << "\n";
    CHECK(runs.typed > 0);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK_EQ(left, 0U);
    CHECK_EQ(emptied, 0U);
    CHECK_EQ(named, 0U);
    // Each run of the part's range executed, up to the completing one.
    CHECK(part.held(runs));
    if (kind == Kind::call) {
        CHECK(!part.completed_here(runs) || runs.completed_at == b.last.at(4) + 1);
        // Each step of the calls the part refused has its refused runs, by
        // step_of; the checkpoint's each.
        std::map<std::string, std::size_t> want;
        const std::size_t last = runs.completed_at == 0 ? 0 : std::min(runs.completed_at - 1, r.to);
        for (std::size_t k = r.from; k <= last; ++k) {
            ++want[std::string(step_of(kind, b, k))];
        }
        for (const auto& [step, n] : want) {
            CHECK(by_step[step] > 0);
        }
        CHECK(b.checkpoint > 0 && by_step["checkpoint"] == want["checkpoint"]);
    }
}

}  // namespace

// One part of create_refused: the passes in order, the platform's calls,
// the arena's allocations, the engine calls, the engine heap's allocations,
// each over the part's range of it.
void qiven3::test::store_sqlite_create_refused(Context& qiven3_ctx, const std::array<FaultRange, 4>& ranges) {
    constexpr std::array<Kind, 4> kinds = {Kind::platform, Kind::arena, Kind::call, Kind::heap};
    const auto rig = std::make_unique<Rig>();
    for (std::size_t p = 0; p < kinds.size(); ++p) {
        if (!ranges.at(p).none()) {
            pass(qiven3_ctx, *rig, kinds.at(p), ranges.at(p));
        }
    }
}

QIVEN3_CASE(store_sqlite, create_unremoved) {
    const auto rig = std::make_unique<Rig>();
    // The engine heap's first allocation refused, at open_v2: then the
    // removals, the engine calls 2 to 5 (-journal, -wal, -shm, the file).
    for (std::size_t removal = 2; removal <= 5; ++removal) {
        Countdown heap;
        Countdown calls;
        rig->engine.heap().attach(&heap);
        sqlite::attach_calls(&calls, nullptr);
        heap.arm(1);
        calls.arm(removal);
        const std::string first = rig->create();
        const bool fired = heap.fired() && calls.fired();
        rig->engine.heap().attach(nullptr);
        sqlite::attach_calls(nullptr, nullptr);
        const std::string_view name = sqlite::removal_names.at(removal - 2);
        const bool named = rig->names(name);
        const std::string said = rig->detail;
        const std::string names = rig->left();
        const std::string second = rig->create();
        std::cout << "store_sqlite.create_unremoved: removal " << removal - 1 << " refused: " << first << " ("
                  << said << "); left " << names << "; the next create " << second << "\n";
        CHECK(fired);
        CHECK(named);
        CHECK_EQ(first, std::string("MemoryError"));
        CHECK_EQ(names, std::string("store.db "));
        CHECK_EQ(second, std::string("StoreError exists"));
        rig->clear();
    }
    // The directory's removal refused, the platform's sixth call (pages,
    // resolve, synced, the directory's create, the file's, then it): the
    // directory left empty, the next create made in it.
    {
        Countdown heap;
        Countdown platform;
        rig->engine.heap().attach(&heap);
        rig->fake.attach(&platform);
        heap.arm(1);
        platform.arm(6);
        const std::string first = rig->create();
        const bool fired = heap.fired() && platform.fired() && rig->fake.calls().back() == "remove_directory";
        rig->engine.heap().attach(nullptr);
        rig->fake.attach(nullptr);
        const bool named = rig->names("the directory");
        const std::string said = rig->detail;
        const std::string names = rig->left();
        const std::string second = rig->create();
        std::cout << "store_sqlite.create_unremoved: the directory's removal refused: " << first << " ("
                  << said << "); left [" << names << "]; the next create " << second << "\n";
        CHECK(fired);
        CHECK(named);
        CHECK_EQ(first, std::string("MemoryError"));
        CHECK_EQ(names, std::string());
        CHECK_EQ(second, std::string("made"));
        rig->clear();
    }
    // A directory there before create: kept, empty, by a refused create,
    // its detail naming no removal.
    {
        std::error_code error;
        fs::create_directories(rig->dir, error);
        Countdown heap;
        rig->engine.heap().attach(&heap);
        heap.arm(1);
        const std::string first = rig->create();
        rig->engine.heap().attach(nullptr);
        const std::string names = rig->left();
        std::cout << "store_sqlite.create_unremoved: a directory there before: " << first << " (" << rig->detail
                  << "); left [" << names << "]\n";
        CHECK_EQ(first, std::string("MemoryError"));
        CHECK(heap.fired());
        CHECK(rig->detail.find("removal refused") == std::string::npos);
        CHECK_EQ(names, std::string());
        rig->clear();
    }
    // A directory where a sidecar goes: a step refused, that sidecar's
    // removal too, the file kept.
    for (const std::string_view sidecar : {"-wal", "-shm", "-journal"}) {
        std::error_code error;
        fs::create_directories(rig->dir / ("store.db" + std::string(sidecar)), error);
        const std::string first = rig->create();
        const bool named = rig->detail.find("removal refused: ") != std::string::npos &&
                           rig->detail.find(sidecar) != std::string::npos;
        const std::string said = rig->detail;
        const std::string names = rig->left();
        const std::string second = rig->create();
        std::cout << "store_sqlite.create_unremoved: a directory at " << sidecar << ": " << first << " ("
                  << said << "); left " << names << "; the next create " << second << "\n";
        CHECK(first != "made");
        CHECK(named);
        CHECK(names.find("store.db ") != std::string::npos);
        CHECK_EQ(second, std::string("StoreError exists"));
        rig->clear();
    }
    CHECK_EQ(rig->create(), std::string("made"));
}
#else
void qiven3::test::store_sqlite_create_refused(Context& /*qiven3_ctx*/,
                                               const std::array<FaultRange, 4>& /*ranges*/) {
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
}
QIVEN3_CASE(store_sqlite, create_unremoved) {
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
}
#endif
