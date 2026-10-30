// store's view cases, the generic calls in both stores: the in-memory
// store's view over its state and the SQLite store's over its connection,
// on the fake, after one chain of fixtures on two lanes (A, B, D and ACC
// on implementer, C, M1, M2 and ACC2 on reviewer). line(id) gives a held
// record's line byte-equal, false for an id not held; targets(id, scope)
// the ids whose target is it, in the order written, none in another scope
// or for an id no record targets, and targets(id) the same in every
// scope; keyed(scope, kind) a scope's ids of a
// kind with no target, in (lane, seq) order, a revision of the kind left
// out, none in another scope; contains(bytes) whether a held line holds
// them. Each list is cleared first. order_memory, order_sqlite: the
// store's own order with no admit hook, a parent not the lane's head
// refused parent and a seq not one past it seq, the chain in order
// appended.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/memory.hpp"
#include "store/memory_state.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

namespace fs = std::filesystem;
namespace sqlite = qiven3::store::sqlite;
namespace state = qiven3::store::state;
using qiven3::store::Engine;
using qiven3::store::Id;
using qiven3::store::Limits;
using qiven3::store::Operation;
using qiven3::store::Vector;
using qiven3::test::stage_book::Book;
using qiven3::test::stage_book::Fake;
using qiven3::test::stage_book::id_of;
using qiven3::test::stage_book::Stored;
using qiven3::test::stage_book::views;
using Arena = qiven3::memory::Arena;
using Chars = qiven3::memory::Allocator<char>;

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::size_t state_bytes = std::size_t{8} << 20U;
constexpr std::size_t buffer_bytes = std::size_t{1} << 20U;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// A scratch directory of the case's own, removed at its end.
struct Scratch {
    Scratch() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        root = fs::temp_directory_path() / ("qiven3-view-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(root);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    [[nodiscard]] qiven3::platform::fake::Config config() const {
        qiven3::platform::fake::Config config;
        config.root = text(root);
        config.seed = 7;
        return config;
    }
    fs::path root;
};

// The chain, written by the store given: implementer's lane, then
// reviewer's.
struct Chain {
    explicit Chain(const Book& book)
        : mine{book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")},
          theirs{book.stored("C"), book.stored("M1"), book.stored("M2"), book.stored("ACC2")} {}
    template <class S>
    void write(S& store) const {
        (void)store.append(qiven_scope, "implementer", std::nullopt, views(mine));
        (void)store.append(qiven_scope, "reviewer", std::nullopt, views(theirs));
    }
    std::vector<Stored> mine;
    std::vector<Stored> theirs;
};

// The ids of a list, each by value.
std::vector<std::string> ids(const Vector<Id>& list) {
    std::vector<std::string> out;
    for (const Id& id : list) {
        out.emplace_back(id.view());
    }
    return out;
}
std::vector<std::string> ids_of(const std::vector<const Stored*>& list) {
    std::vector<std::string> out;
    for (const Stored* s : list) {
        out.push_back(id_of(*s));
    }
    return out;
}

// The generic calls of view V over the chain written.
template <class V>
void calls(qiven3::test::Context& qiven3_ctx, V& view, const Chain& chain, const Chars& chars) {
    const Stored& a = chain.mine[0];
    const Stored& b = chain.mine[1];
    const Stored& d = chain.mine[2];
    const Stored& acc = chain.mine[3];
    const Stored& c = chain.theirs[0];
    const Stored& m1 = chain.theirs[1];
    const Stored& m2 = chain.theirs[2];
    const Stored& acc2 = chain.theirs[3];
    const std::string none = "sha256:" + std::string(64, '0');

    qiven3::memory::String line(chars);
    CHECK(view.line(id_of(a), line) && std::string_view(line) == a.line);
    CHECK(view.line(id_of(acc2), line) && std::string_view(line) == acc2.line);
    CHECK(!view.line(none, line));

    Vector<Id> out(chars);
    out.push_back(Id::of(none));
    view.targets(id_of(a), qiven_scope, out);
    CHECK(ids(out) == ids_of({&c}));
    view.targets(id_of(d), qiven_scope, out);
    CHECK(ids(out) == ids_of({&acc, &acc2}));
    view.targets(id_of(m1), qiven_scope, out);
    CHECK(ids(out) == ids_of({&m2}));
    view.targets(id_of(b), qiven_scope, out);
    CHECK(out.empty());
    view.targets(id_of(a), "person", out);
    CHECK(out.empty());
    view.targets(none, qiven_scope, out);
    CHECK(out.empty());

    out.push_back(Id::of(none));
    view.targets(id_of(d), out);
    CHECK(ids(out) == ids_of({&acc, &acc2}));
    view.targets(id_of(a), out);
    CHECK(ids(out) == ids_of({&c}));
    view.targets(id_of(m1), out);
    CHECK(ids(out) == ids_of({&m2}));
    view.targets(id_of(b), out);
    CHECK(out.empty());
    view.targets(none, out);
    CHECK(out.empty());

    view.keyed(qiven_scope, "claim", out);
    CHECK(ids(out) == ids_of({&a, &b}));
    view.keyed(qiven_scope, "direction", out);
    CHECK(ids(out) == ids_of({&d}));
    view.keyed(qiven_scope, "map", out);
    CHECK(ids(out) == ids_of({&m1}));
    view.keyed(qiven_scope, "check", out);
    CHECK(out.empty());
    view.keyed(qiven_scope, "accept", out);
    CHECK(out.empty());
    view.keyed("person", "claim", out);
    CHECK(out.empty());

    CHECK(view.contains(id_of(m2)));
    CHECK(view.contains(R"("kind":"direction")"));
    CHECK(!view.contains(none));
    CHECK(!view.contains(R"("kind":"forget")"));

    // each_keyed: keyed's ids in its order, all walked when f never stops,
    // the walk ended at the first f stops at.
    for (const std::string_view kind : {"claim", "direction", "map", "check", "accept"}) {
        view.keyed(qiven_scope, kind, out);
        std::vector<std::string> walked;
        CHECK(!view.each_keyed(qiven_scope, kind, [&](const Id& id) {
            walked.emplace_back(id.view());
            return false;
        }));
        CHECK(walked == ids(out));
    }
    std::vector<std::string> first;
    CHECK(view.each_keyed(qiven_scope, "claim", [&](const Id& id) {
        first.emplace_back(id.view());
        return true;
    }));
    CHECK(first == ids_of({&a}));
    std::vector<std::string> second;
    CHECK(view.each_keyed(qiven_scope, "claim", [&](const Id& id) {
        second.emplace_back(id.view());
        return second.size() == 2;
    }));
    CHECK(second == ids_of({&a, &b}));
    CHECK(!view.each_keyed("person", "claim", [](const Id&) { return true; }));

    // work with no scratch: the view's allocator.
    CHECK(&view.work().arena() == &chars.arena());
}

// work over a scratch: each call a fresh arena over the scratch's pages,
// not the view's allocator, its peak counted, the last given back at the
// next.
template <class V, class S>
void work(qiven3::test::Context& qiven3_ctx, V& view, const S& scratch, const Chars& chars) {
    {
        const Chars w = view.work();
        CHECK(&w.arena() != &chars.arena());
        qiven3::memory::String s(w);
        s.assign(200, 'x');
    }
    const std::size_t peak = scratch.peak();
    CHECK(peak >= 200U);
    {
        const Chars w = view.work();
        qiven3::memory::String s(w);
        s.assign(100, 'x');
    }
    CHECK_EQ(scratch.peak(), peak);
}

// The store's own order with no admit hook, on an empty store: a call
// whose first parent is not the lane's head refused parent at 0; one whose
// second parent is not the first refused parent at 1; one whose seq is
// not one past the head refused seq at 0; each refusal leaving the lane
// empty; then the chain in order appended, its head the last.
template <class S>
void order(qiven3::test::Context& qiven3_ctx, S& store, const Book& book) {
    using qiven3::store::NoAdmit;
    using qiven3::test::stage_book::refused;
    const Stored a = book.stored("A");
    const std::vector<Stored> skip_first = {book.stored("B")};
    const std::vector<Stored> skip_second = {a, book.stored("D")};
    const std::vector<Stored> late = {
        book.made("A", "A", *a.content, [](qiven3::entry::header::FieldsView& f) { f.seq = 2; })};
    const std::vector<Stored> chain = {a, book.stored("B"), book.stored("D")};
    const auto one = [&](const std::vector<Stored>& list) {
        const auto lines = views(list);
        return refused([&] { (void)store.append(qiven_scope, "implementer", std::nullopt, lines, NoAdmit{}); });
    };
    const auto first = one(skip_first);
    CHECK(first.has_value() && first->code == "parent" && first->index == 0U);
    const auto second = one(skip_second);
    CHECK(second.has_value() && second->code == "parent" && second->index == 1U &&
          second->id == id_of(book.stored("D")));
    const auto seq = one(late);
    CHECK(seq.has_value() && seq->code == "seq" && seq->index == 0U);
    const auto lines = views(chain);
    const qiven3::store::Written w = store.append(qiven_scope, "implementer", std::nullopt, lines, NoAdmit{});
    CHECK_EQ(w.head.id.view(), id_of(chain.back()));
    CHECK(w.head.seq == 3);
}

}  // namespace

QIVEN3_CASE(store_view, order_memory) {
    const auto fake = std::make_unique<Fake>(qiven3::platform::fake::Config{});
    const auto book = std::make_unique<const Book>(*fake);
    auto heap = fake->pages(state_bytes);
    const auto st = std::make_unique<state::State>(heap.bytes(), Limits{});
    qiven3::store::MemoryStore<Fake> store(*st, *fake);
    order(qiven3_ctx, store, *book);
}

QIVEN3_CASE(store_view, order_sqlite) {
    const auto scratch = std::make_unique<Scratch>();
    const auto fake = std::make_unique<Fake>(scratch->config());
    const Engine<Fake> engine(*fake);
    const auto book = std::make_unique<const Book>(*fake);
    const std::string path = text(scratch->root / "o" / "store.db");
    const auto store = std::make_unique<qiven3::store::SQLiteStore<Fake>>(
        qiven3::store::SQLiteStore<Fake>::open(path, true, false, *fake));
    order(qiven3_ctx, *store, *book);
    (void)store->close();
}

QIVEN3_CASE(store_view, memory) {
    const auto fake = std::make_unique<Fake>(qiven3::platform::fake::Config{});
    const auto book = std::make_unique<const Book>(*fake);
    auto heap = fake->pages(state_bytes);
    const auto st = std::make_unique<state::State>(heap.bytes(), Limits{});
    const Chain chain(*book);
    {
        qiven3::store::MemoryStore<Fake> store(*st, *fake);
        chain.write(store);
    }
    std::vector<std::byte> pages(buffer_bytes);
    Arena arena(pages);
    const Chars chars(arena);
    const state::Locked lock(*st, Operation::append, {});
    state::StateView view(lock.data(), chars, Operation::append, {});
    calls(qiven3_ctx, view, chain, chars);
    std::vector<std::byte> scratch_pages(buffer_bytes);
    state::ScratchView scratch(scratch_pages, nullptr);
    state::StateView over(lock.data(), chars, Operation::append, {}, &scratch);
    work(qiven3_ctx, over, scratch, chars);
}

QIVEN3_CASE(store_view, sqlite) {
    const auto scratch = std::make_unique<Scratch>();
    const auto fake = std::make_unique<Fake>(scratch->config());
    const Engine<Fake> engine(*fake);
    const auto book = std::make_unique<const Book>(*fake);
    const Chain chain(*book);
    const std::string path = text(scratch->root / "v" / "store.db");
    const auto store = std::make_unique<qiven3::store::SQLiteStore<Fake>>(
        qiven3::store::SQLiteStore<Fake>::open(path, true, false, *fake));
    chain.write(*store);
    std::vector<std::byte> pages(buffer_bytes);
    Arena arena(pages);
    const Chars chars(arena);
    sqlite::Connection& c = store->connection();
    const qiven3::store::WhereView at = sqlite::site("append", path);
    const sqlite::Totals held = sqlite::totals(c, at);
    sqlite::WriteView view(c, chars, nullptr, at, held);
    calls(qiven3_ctx, view, chain, chars);
    std::vector<std::byte> scratch_pages(buffer_bytes);
    sqlite::ScratchView arenas(scratch_pages, nullptr);
    sqlite::WriteView over(c, chars, &arenas, at, held);
    work(qiven3_ctx, over, arenas, chars);
    (void)store->close();
}
