#pragma once

// stores: the factory the store cases take, and their tags. A case is a
// template on a factory F, written once and bound to each store by its
// store's case file; F::Store satisfies the contract's Store.
//
//   Site, site(seed)    the case's own ground, on the heap: a scratch
//                       directory (fresh, or the root given) removed at
//                       its end, the fake platform
//                       over it, the engine heap once a SQLite factory asks
//                       for it, the fixtures and the caller's arena
//   Memory, Sqlite<H>   a factory over one Site, for one state each:
//     make(limits)      a new empty store, its object read-write (SQLite in
//                       a directory of its own under the scratch root)
//     again()           a second object over the same state (SQLite: a new
//                       connection to its file; in-memory: one sharing its
//                       state and its lock)
//     read_only()       a read-only object over it
//     reopen()          a read-write object after the others closed, where
//                       durable; on the in-memory store not applicable
//   Needs, lacking<F>, require<F>
//                       the tags a case names (durable, files, processes,
//                       own: sqlite), never an implementation; a tag the
//                       store lacks is "not applicable: ...", the runner's
//                       skip
//
// The text's again(store), read_only(store) and reopen(store) name the
// state by a store object; here each factory holds one state, which names
// it, and a case wanting two states (an import into another store) takes
// two factories over one Site, of either kind. The state outlives every
// object made over it: a case may close an object and then read from a
// scan or an export it made, the store answering as the contract says.
//
// Allocations: the Site's pages (the fake's, the engine's, the arena's)
// and each state's, at construction and make; the stores' own. Platform
// calls: the fake's, as each store makes them. thread: one, each object.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/memory.hpp"
#include "store/memory_state.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"

namespace qiven3::test::stores {

using Fake = platform::fake::FakePlatform;
using Chars = memory::Allocator<char>;

// The tags. own names the one implementation whose layout or settings a
// case reads (S16 (c), S17, S18, S19, S24, S28).
enum class Own : std::uint8_t { none, sqlite };
struct Needs {
    store::Capabilities capabilities{};
    Own own = Own::none;
};
constexpr Needs operator|(const Needs& a, const Needs& b) noexcept {
    const store::Capabilities& x = a.capabilities;
    const store::Capabilities& y = b.capabilities;
    return Needs{{x.durable || y.durable, x.files || y.files, x.processes || y.processes},
                 a.own != Own::none ? a.own : b.own};
}
inline constexpr Needs none{};
inline constexpr Needs durable{{true, false, false}, Own::none};
inline constexpr Needs files{{false, true, false}, Own::none};
inline constexpr Needs processes{{false, false, true}, Own::none};
inline constexpr Needs own_sqlite{{}, Own::sqlite};

// The reason a store of factory F cannot run a case needing n, or none.
template <class F>
std::optional<std::string> lacking(const Needs& n) {
    constexpr store::Capabilities have = F::Store::capabilities;
    std::string why;
    const auto lack = [&why](bool needed, bool held, std::string_view tag) {
        if (needed && !held) {
            why += why.empty() ? "" : ", ";
            why += tag;
        }
    };
    lack(n.capabilities.durable, have.durable, "durable");
    lack(n.capabilities.files, have.files, "files");
    lack(n.capabilities.processes, have.processes, "processes");
    lack(n.own != Own::none, n.own == F::own, "own: sqlite");
    if (why.empty()) {
        return std::nullopt;
    }
    return "not applicable: the " + std::string(F::name) + " store lacks " + why;
}

// The runner's skip where F lacks a tag of n; else nothing.
template <class F>
void require(const Needs& n) {
    if (std::optional<std::string> why = lacking<F>(n)) {
        throw Skip{std::move(*why)};
    }
}

// A path's UTF-8 text, as the platform takes it.
inline std::string text(const std::filesystem::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

inline constexpr std::size_t arena_bytes = std::size_t{1} << 20U;

class Site {
public:
    explicit Site(std::uint64_t seed) : Site(seed, fresh()) {}
    // Over the root given, made where absent, and removed at its end as a
    // fresh one is (the drive's, under its --root).
    Site(std::uint64_t seed, std::filesystem::path root)
        : root_(made(std::move(root))), fake_(config(root_, seed)), book_(fake_), pages_(arena_bytes), arena_(pages_),
          chars_(arena_) {}
    Site(const Site&) = delete;
    Site& operator=(const Site&) = delete;
    Site(Site&&) = delete;
    Site& operator=(Site&&) = delete;
    ~Site() {
        engine_.reset();
        try {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        } catch (const std::exception&) {
            static_cast<void>(std::fputs("stores: the scratch directory was not removed\n", stderr));
        }
    }

    [[nodiscard]] Fake& fake() noexcept { return fake_; }
    [[nodiscard]] const stage_book::Book& book() const noexcept { return book_; }
    [[nodiscard]] const Chars& chars() const noexcept { return chars_; }
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    // The engine heap, made at the first call and kept past every store.
    store::Engine<Fake>& engine() {
        if (!engine_) {
            engine_.emplace(fake_);
        }
        return *engine_;
    }
    // A directory under the root not used before: s1, s2, ...
    std::filesystem::path next_directory() { return root_ / ("s" + std::to_string(++made_)); }

private:
    static std::filesystem::path fresh() {
        // On the heap: libstdc++'s random_device is some 5,000 bytes.
        std::vector<std::random_device> device(1);
        std::random_device& draw = device.front();
        std::filesystem::path out =
            std::filesystem::temp_directory_path() / ("qiven3-stores-" + std::to_string(draw()) + std::to_string(draw()));
        std::filesystem::create_directories(out);
        return out;
    }
    static std::filesystem::path made(std::filesystem::path root) {
        std::filesystem::create_directories(root);
        return root;
    }
    static platform::fake::Config config(const std::filesystem::path& root, std::uint64_t seed) {
        platform::fake::Config c;
        c.root = text(root);
        c.seed = seed;
        return c;
    }

    std::filesystem::path root_;
    Fake fake_;
    std::optional<store::Engine<Fake>> engine_;  // after the fake, whose pages it holds
    stage_book::Book book_;
    std::vector<std::byte> pages_;
    memory::Arena arena_;
    Chars chars_;
    unsigned made_ = 0;
};

// A Site on the heap, its fake's seed given.
inline std::unique_ptr<Site> site(std::uint64_t seed = 7) { return std::make_unique<Site>(seed); }

// The in-memory store: its state on pages the fake gives, heap bytes of
// them (the default holds S12's 20,000 entries of 1 KiB).
class Memory {
public:
    using Store = store::MemoryStore<Fake>;
    static constexpr std::string_view name = "memory";
    static constexpr Own own = Own::none;
    static constexpr std::size_t default_heap = std::size_t{64} << 20U;

    explicit Memory(Site& site, std::size_t heap = default_heap) : site_(&site), heap_(heap) {}

    [[nodiscard]] Site& site() noexcept { return *site_; }
    // make() once a factory: a second call is a case's error.
    Store make(const store::Limits& limits = store::Limits{}) {
        if (state_) {
            throw std::logic_error("stores: a factory makes one state; take another factory");
        }
        pages_.emplace(site_->fake().pages(heap_));
        state_ = std::make_unique<store::state::State>(pages_->bytes(), limits);
        return Store(*state_, site_->fake());
    }
    Store again() { return Store(state(), site_->fake()); }
    Store read_only() { return Store(state(), site_->fake(), true); }
    [[noreturn]] static Store reopen() { throw Skip{"not applicable: the memory store is not durable"}; }

private:
    store::state::State& state() {
        if (!state_) {
            throw std::logic_error("stores: no state made yet");
        }
        return *state_;
    }

    Site* site_;
    std::size_t heap_;
    std::optional<platform::fake::FakePages> pages_;  // before the state made over them
    std::unique_ptr<store::state::State> state_;
};

// The SQLite store, its hooks H (a seam's, seams.hpp) given to each object:
// its file store.db in a directory of its own under the Site's root.
template <store::Hooks H = store::NoHooks>
class Sqlite {
public:
    using Store = store::SQLiteStore<Fake, H>;
    static constexpr std::string_view name = "sqlite";
    static constexpr Own own = Own::sqlite;

    explicit Sqlite(Site& site, H hooks = H{}) : site_(&site), hooks_(std::move(hooks)) { (void)site.engine(); }

    [[nodiscard]] Site& site() noexcept { return *site_; }
    Store make(const store::Limits& limits = store::Limits{}) {
        if (!path_.empty()) {
            throw std::logic_error("stores: a factory makes one state; take another factory");
        }
        directory_ = site_->next_directory();
        path_ = text(directory_ / "store.db");
        limits_ = limits;
        return Store::open(path_, true, false, site_->fake(), limits_, hooks_);
    }
    Store again() { return Store::open(path(), false, false, site_->fake(), limits_, hooks_); }
    Store read_only() { return Store::open(path(), false, true, site_->fake(), limits_, hooks_); }
    Store reopen() { return again(); }
    // create over the state's file, which is there: StoreError exists (S17).
    Store create() { return Store::open(path(), true, false, site_->fake(), limits_, hooks_); }
    // The store's directory and file, for a case's scan or forgery.
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
    [[nodiscard]] const std::string& path() const {
        if (path_.empty()) {
            throw std::logic_error("stores: no state made yet");
        }
        return path_;
    }

private:
    Site* site_;
    H hooks_;
    std::filesystem::path directory_;
    std::string path_;
    store::Limits limits_{};
};

}  // namespace qiven3::test::stores
