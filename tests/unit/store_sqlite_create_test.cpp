// store/sqlite's refused create, on the fake over a scratch directory of
// the case's own. create_refused: four passes, each refusing in turn every
// call of one kind a create makes: the fake's platform calls, the store's
// arena allocations, the engine calls the wrapper maps (open_v2, the
// settings, the schema, its TRUNCATE checkpoint, the probe, coherence) and
// the engine heap's allocations. After each refused run no file is left
// (the directory create made, if it made one, is left empty), and a second
// create makes the store. create_unremoved: a removal itself refused, by
// the countdowns (the engine heap's first allocation refused, then one of
// the four removals) and by a directory in the -wal's place: the step's
// error raised, the file kept, the next create StoreError exists.

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

#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
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
    // A create, its object closed; "made" or what it raised.
    [[nodiscard]] std::string create() {
        try {
            Store s = Store::open(path, true, false, fake);
            (void)s.close();
            return "made";
        } catch (const StoreError& e) {
            const auto code = e.code();
            return std::string(qiven3::store::name(e.kind())) + (code ? " " + std::string(qiven3::store::name(*code)) : "");
        } catch (const MemoryError&) {
            return "MemoryError";
        } catch (const PlatformError&) {
            return "PlatformError";
        }
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

// One pass: each call of kind refused in turn.
void pass(qiven3::test::Context& qiven3_ctx, Rig& rig, Kind kind) {
    const Bounds b = measure(rig, kind);
    std::map<std::string, std::size_t> by_step;
    std::map<std::string, std::size_t> raised;
    std::size_t left = 0;
    std::size_t again = 0;
    std::size_t emptied = 0;
    std::optional<Store> made;
    bool done = false;
    const std::size_t lent = rig.fake.lent();
    Countdown c;
    const auto op = [&] {
        done = false;
        try {
            made.emplace(Store::open(rig.path, true, false, rig.fake));
        } catch (const StoreError& e) {
            ++raised[std::string(qiven3::store::name(e.kind()))];
            throw Typed{};
        } catch (const MemoryError&) {
            ++raised["MemoryError"];
            throw Typed{};
        } catch (const PlatformError&) {
            ++raised["PlatformError"];
            throw Typed{};
        }
        done = true;
        if (c.fired()) {
            ++raised["absorbed"];
            throw Typed{};
        }
    };
    const auto check = [&](std::size_t k) {
        made.reset();
        bool held = rig.fake.lent() == lent;
        if (!done) {
            const std::string names = rig.left();
            const bool clean = names == "absent" || names.empty();
            left += clean ? 0U : 1U;
            emptied += names.empty() ? 1U : 0U;
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
              << runs.typed << " runs refused, completed at " << runs.completed_at << "; files left " << left
              << ", its directory left empty " << emptied << ", a second create made " << again << "; untyped "
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
    if (kind == Kind::call) {
        CHECK_EQ(runs.completed_at, b.last.at(4) + 1);
        CHECK(b.checkpoint > 0 && by_step["checkpoint"] == b.checkpoint);
        for (const std::string_view step : step_names) {
            CHECK(by_step[std::string(step)] > 0);
        }
    }
}

}  // namespace

QIVEN3_CASE(store_sqlite, create_refused) {
    const auto rig = std::make_unique<Rig>();
    for (const Kind kind : {Kind::platform, Kind::arena, Kind::call, Kind::heap}) {
        pass(qiven3_ctx, *rig, kind);
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
        const std::string names = rig->left();
        const std::string second = rig->create();
        std::cout << "store_sqlite.create_unremoved: removal " << removal - 1 << " refused: " << first << "; left "
                  << names << "; the next create " << second << "\n";
        CHECK(fired);
        CHECK_EQ(first, std::string("MemoryError"));
        CHECK_EQ(names, std::string("store.db "));
        CHECK_EQ(second, std::string("StoreError exists"));
        rig->clear();
    }
    // A directory where a sidecar goes: a step refused, that sidecar's
    // removal too, the file kept.
    for (const std::string_view sidecar : {"-wal", "-shm", "-journal"}) {
        std::error_code error;
        fs::create_directories(rig->dir / ("store.db" + std::string(sidecar)), error);
        const std::string first = rig->create();
        const std::string names = rig->left();
        const std::string second = rig->create();
        std::cout << "store_sqlite.create_unremoved: a directory at " << sidecar << ": " << first << "; left "
                  << names << "; the next create " << second << "\n";
        CHECK(first != "made");
        CHECK(names.find("store.db ") != std::string::npos);
        CHECK_EQ(second, std::string("StoreError exists"));
        rig->clear();
    }
    CHECK_EQ(rig->create(), std::string("made"));
}
#else
QIVEN3_CASE(store_sqlite, create_refused) {
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
}
QIVEN3_CASE(store_sqlite, create_unremoved) {
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
}
#endif
