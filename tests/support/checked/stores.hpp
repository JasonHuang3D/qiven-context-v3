#pragma once

// ruled stores: the factory checked's cases and the driver take, the
// store's factories (support/stores/stores.hpp) bound to the ruled stores
// (checked/stores.hpp), each with the rules as its admit hook, and a Site
// holding the fixtures' book of entries.
//
//   Site, site(seed)    the store's Site and the book over its fake
//   Memory, Sqlite<H>   the store's factories over the ruled stores
//   the tags, text      the store's own (Needs, lacking, require, ...)
//
// Allocations: the store's Site's and the book's, at construction; the
// stores' own. thread: one, each object.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>

#include "checked/stores.hpp"
#include "store/contract.hpp"
#include "support/fixtures/stage_book.hpp"
#include "support/stores/stores.hpp"

namespace qiven3::test::ruled {

using stores::arena_bytes;
using stores::Chars;
using stores::durable;
using stores::Fake;
using stores::files;
using stores::lacking;
using stores::Needs;
using stores::none;
using stores::Own;
using stores::own_sqlite;
using stores::processes;
using stores::require;
using stores::text;

// The store's Site with the fixtures' book over its fake.
class Site : public stores::Site {
public:
    explicit Site(std::uint64_t seed) : stores::Site(seed), book_(fake()) {}
    Site(std::uint64_t seed, std::filesystem::path root) : stores::Site(seed, std::move(root)), book_(fake()) {}

    [[nodiscard]] const stage_book::Book& book() const noexcept { return book_; }

private:
    stage_book::Book book_;
};

// A Site on the heap, its fake's seed given.
inline std::unique_ptr<Site> site(std::uint64_t seed = 7) { return std::make_unique<Site>(seed); }

using Memory = stores::MemoryOn<checked::MemoryStore<Fake>, Site>;
template <store::Hooks H = store::NoHooks>
using Sqlite = stores::SqliteOn<checked::SQLiteStore<Fake, H>, Site, H>;

}  // namespace qiven3::test::ruled
