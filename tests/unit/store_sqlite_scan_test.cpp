// store/sqlite's binding of the case a scan's end (store_scan_case.hpp):
// a store created on the fake over a scratch directory of the case's own,
// a second object opened on it, the case run on the two. The forget's
// purge runs VACUUM between a scan's start and its end.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"
#include "support/fixtures/store_scan_case.hpp"

namespace {

namespace fs = std::filesystem;
using Fake = qiven3::platform::fake::FakePlatform;
using Store = qiven3::store::SQLiteStore<Fake>;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// A scratch directory of the case's own, removed at its end; the fake
// over it, the engine heap, the fixtures and the caller's arena.
struct Rig {
    Rig()
        : root(fresh()), fake(config(root)), engine(fake), book(fake), pages(std::size_t{1} << 20U), arena(pages),
          chars(arena) {}
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
        fs::path out = fs::temp_directory_path() / ("qiven3-sqlite-s-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    static qiven3::platform::fake::Config config(const fs::path& root) {
        qiven3::platform::fake::Config c;
        c.root = text(root);
        c.seed = 7;
        return c;
    }
    fs::path root;
    Fake fake;
    qiven3::store::Engine<Fake> engine;
    qiven3::test::stage_book::Book book;
    std::vector<std::byte> pages;
    qiven3::memory::Arena arena;
    qiven3::memory::Allocator<char> chars;
};

}  // namespace

QIVEN3_CASE(store_sqlite, scan_at_start) {
    const auto rig = std::make_unique<Rig>();
    const std::string path = text(rig->root / "s" / "store.db");
    Store store = Store::open(path, true, false, rig->fake);
    Store other = Store::open(path, false, false, rig->fake);
    qiven3::test::scan_case::scan_ends_at_start(qiven3_ctx, store, other, rig->book, rig->chars);
}
