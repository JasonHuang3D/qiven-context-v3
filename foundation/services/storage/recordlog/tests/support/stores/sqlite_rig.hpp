#pragma once

// store/sqlite's rig for the cases on an engine heap of their own (store_sqlite_states_test.cpp, which describes
// them), on the fake, over a scratch directory of the case's own: the rig itself, a connection as no store opens, the
// error a call raised, the engine's bytes once a read.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "qiven/memory/arena.hpp"
#include "qiven/memory/fail.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/memory/pool.hpp"
#include "qiven/recordlog/contract.hpp"
#include "qiven/recordlog/sqlite_steps.hpp"
#include "qiven/recordlog/errors.hpp"
#include "qiven/recordlog/sqlite_store.hpp"
#include "qiven/platform/fake/fake_platform.hpp"

namespace qiven::test::sqlite_rig {

namespace fs = std::filesystem;
namespace sqlite = qiven::recordlog::sqlite;
namespace storage_sqlite = qiven::storage_sqlite;
using qiven::memory::Arena;
using qiven::memory::String;
using qiven::recordlog::StoreError;
using qiven::recordlog::WhereView;
using Chars = qiven::memory::Allocator<char>;
using Fake = qiven::platform::fake::FakePlatform;
using Engine = qiven::recordlog::Engine<Fake>;

inline constexpr WhereView test_at = sqlite::site("test", "scratch");

inline std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// The scratch, the fake over it and the engine heap, made again at will;
// on the heap.
// What a Rig is made from: a fresh scratch each time.
struct Scratch {};
struct Rig {
    explicit Rig(Scratch /*fresh*/) : root(fresh_root()), fake(config(root)), engine(std::make_unique<Engine>(fake)) {}
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;
    ~Rig() {
        engine.reset();
        try {
            std::error_code error;
            fs::remove_all(root, error);
        } catch (...) {
            qiven::memory::fail_fast("sqlite_rig: the scratch directory's removal threw");
        }
    }
    static fs::path fresh_root() {
        // On the heap: libstdc++'s random_device is some 5,000 bytes.
        std::vector<std::random_device> device(1);
        std::random_device& draw = device.front();
        fs::path out = fs::temp_directory_path() / ("qiven-sqlite-s-" + std::to_string(draw()) +
                                                    std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    static qiven::platform::fake::Config config(const fs::path& root) {
        qiven::platform::fake::Config c;
        c.root = text(root);
        c.seed = 7;
        return c;
    }
    // A fresh engine heap: every connection on the last closed first.
    void fresh_engine(std::size_t connections = storage_sqlite::default_connections) {
        engine.reset();
        engine = std::make_unique<Engine>(fake, connections);
    }

    fs::path root;
    Fake fake;
    std::unique_ptr<Engine> engine;
};

// A connection of the case's own through the wrapper, as no store opens.
inline sqlite::Connection raw(const std::string& path) {
    std::vector<std::byte> bytes(4096);
    Arena arena(bytes);
    const String name(path, Chars(arena));
    return sqlite::Connection::open(name, false, test_at);
}

// The error a call raised, as "Kind code: detail", or "none".
template <class F>
std::string raised(const F& f) {
    try {
        f();
    } catch (const StoreError& e) {
        std::string code;
        if (const auto given = e.code()) {
            code = qiven::recordlog::name(*given);
        } else if (const auto corrupt = e.corrupt()) {
            code = qiven::recordlog::name(*corrupt);
        } else if (const auto cause = e.cause()) {
            code = qiven::recordlog::name(*cause);
        }
        return std::string(qiven::recordlog::name(e.kind())) + " " + code + ": " + std::string(e.detail());
    }
    return "none";
}

// The engine's bytes once a read: the heap's, held and their mark, and
// the page cache's slots.
struct Held {
    std::size_t used = 0;
    std::size_t peak = 0;
    storage_sqlite::Slots slots;
};
inline Held held(const Rig& rig) { return {rig.engine->heap().used(), rig.engine->heap().peak(), rig.engine->slots()}; }

}  // namespace qiven::test::sqlite_rig
