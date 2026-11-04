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
#include <memory>

#include "qiven3/entry/cognition.hpp"
#include "checked/stores.hpp"
#include "qiven/recordlog/contract.hpp"
#include "support/fixtures/stage_book.hpp"
#include "support/stores/stores.hpp"

namespace qiven::test::ruled {

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

// The store's Site with the fixtures' book over its fake, its factories'
// stores taking cognition's limits and file identity.
class Site : public stores::Site {
public:
    static constexpr qiven::recordlog::Limits limits = qiven3::entry::cognition_limits;
    static constexpr qiven::recordlog::Identity identity = qiven3::entry::cognition_identity;

    using stores::Site::Site;

    [[nodiscard]] const stage_book::Book& book() const noexcept { return book_; }

private:
    stage_book::Book book_{fake()};
};

// A Site on the heap, its fake's seed given.
inline std::unique_ptr<Site> site(std::uint64_t seed = 7) { return std::make_unique<Site>(seed); }

using Memory = stores::MemoryOn<qiven3::checked::MemoryStore<Fake>, Site>;
template <qiven::recordlog::Hooks H = qiven::recordlog::NoHooks>
using Sqlite = stores::SqliteOn<qiven3::checked::SQLiteStore<Fake, H>, Site, H>;

}  // namespace qiven::test::ruled
