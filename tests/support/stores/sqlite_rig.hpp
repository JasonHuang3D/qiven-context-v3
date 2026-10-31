#pragma once

// store/sqlite's rig for the cases on an engine heap of their own (store_sqlite_cache_test.cpp and
// store_sqlite_states_test.cpp, which describe them), on the fake, over a scratch directory of the case's own: the
// rig itself, a connection as no store opens, the error a call raised, the engine's bytes once a read.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "memory/pool.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::test::sqlite_rig {

namespace fs = std::filesystem;
namespace sqlite = qiven3::store::sqlite;
using qiven3::memory::Arena;
using qiven3::memory::String;
using qiven3::store::StoreError;
using qiven3::store::WhereView;
using Chars = qiven3::memory::Allocator<char>;
using Fake = qiven3::platform::fake::FakePlatform;
using Engine = qiven3::store::Engine<Fake>;

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
struct Rig {
    Rig() : root(fresh_root()), fake(config(root)), engine(std::make_unique<Engine>(fake)) {}
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;
    ~Rig() {
        engine.reset();
        std::error_code error;
        fs::remove_all(root, error);
    }
    static fs::path fresh_root() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        fs::path out = fs::temp_directory_path() / ("qiven3-sqlite-s-" + std::to_string(draw()) +
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
    // A fresh engine heap: every connection on the last closed first.
    void fresh_engine(std::size_t connections = sqlite::default_connections) {
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
        if (e.code()) {
            code = qiven3::store::name(*e.code());
        } else if (e.corrupt()) {
            code = qiven3::store::name(*e.corrupt());
        } else if (e.cause()) {
            code = qiven3::store::name(*e.cause());
        }
        return std::string(qiven3::store::name(e.kind())) + " " + code + ": " + std::string(e.detail());
    }
    return "none";
}

// The engine's bytes once a read: the heap's, held and their mark, and
// the page cache's slots.
struct Held {
    std::size_t used = 0;
    std::size_t peak = 0;
    sqlite::Slots slots;
    // The slots' bytes held and their mark; the connection's peak, the
    // heap's and the slots' marks together.
    [[nodiscard]] std::size_t in_slots() const { return slots.used * slots.size; }
    [[nodiscard]] std::size_t top() const { return peak + (slots.peak * slots.size); }
};
inline Held held(Rig& rig) { return {rig.engine->heap().used(), rig.engine->heap().peak(), rig.engine->slots()}; }

}  // namespace qiven3::test::sqlite_rig
