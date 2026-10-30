// store's view cases, the generic calls in both stores: the in-memory
// store's view over its state and the SQLite store's over its connection,
// on the fake, after one chain of plain records on two lanes (A, B, D and
// ACC on implementer, C, M1, M2 and ACC2 on reviewer), verified after the
// write and, on SQLite, reopened and verified again. line(id) and header(id)
// give a held record's line byte-equal, body(id) its body record, each false
// for an id not held; held_in(id, scope)
// whether a record of id is held in that scope, by its bytes (no prefix,
// no `*`), false in another or for an id not held; targets(id, scope)
// the ids whose target is it, in the order written, none in another scope
// or for an id no record targets, and targets(id) the same in every
// scope; keyed(scope, kind) a scope's ids of a
// kind with no target, in (lane, seq) order, a revision of the kind left
// out, none in another scope; contains(bytes) whether a held line holds
// them. Each list is cleared first. order_memory, order_sqlite: the
// store's own order with no admit hook, a parent not the lane's head
// refused parent and a seq not one past it seq, the chain in order
// appended, verified and, on SQLite, reopened and verified again.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
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
#include "store/errors.hpp"
#include "store/memory.hpp"
#include "store/memory_state.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/stores/records.hpp"

namespace {

namespace fs = std::filesystem;
namespace sqlite = qiven3::store::sqlite;
namespace state = qiven3::store::state;
using qiven3::store::Engine;
using qiven3::store::Id;
using qiven3::store::Limits;
using qiven3::store::Operation;
using qiven3::store::Vector;
using qiven3::test::records::id_of;
using qiven3::test::records::Rec;
using qiven3::test::records::rec;
using qiven3::test::records::targeting;
using qiven3::test::records::views;
using Arena = qiven3::memory::Arena;
using Fake = qiven3::platform::fake::FakePlatform;
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

// The chain, written by the store given: implementer's lane (claims A and
// B, direction D, ACC accepting D), then reviewer's (C a claim revising A,
// maps M1 and M2 revising it, ACC2 accepting D); every record bodied.
struct Chain {
    Chain()
        : mine{rec("A", qiven_scope, "implementer", 1, std::nullopt, "claim"),
               rec("B", qiven_scope, "implementer", 2, "A", "claim"),
               rec("D", qiven_scope, "implementer", 3, "B", "direction"),
               targeting(rec("ACC", qiven_scope, "implementer", 4, "D", "accept"), "D")},
          theirs{targeting(rec("C", qiven_scope, "reviewer", 1, std::nullopt, "claim"), "A"),
                 rec("M1", qiven_scope, "reviewer", 2, "C", "map"),
                 targeting(rec("M2", qiven_scope, "reviewer", 3, "M1", "map"), "M1"),
                 targeting(rec("ACC2", qiven_scope, "reviewer", 4, "M2", "accept"), "D")} {}
    template <class S>
    void write(S& store) const {
        (void)store.append(qiven_scope, "implementer", std::nullopt, views(mine));
        (void)store.append(qiven_scope, "reviewer", std::nullopt, views(theirs));
    }
    // The counts the chain's write holds.
    static constexpr qiven3::store::Counts held{8, 8, 0, 2};
    std::vector<Rec> mine;
    std::vector<Rec> theirs;
};

// The ids of a list, each by value.
std::vector<std::string> ids(const Vector<Id>& list) {
    std::vector<std::string> out;
    for (const Id& id : list) {
        out.emplace_back(id.view());
    }
    return out;
}
std::vector<std::string> ids_of(const std::vector<const Rec*>& list) {
    std::vector<std::string> out;
    for (const Rec* s : list) {
        out.push_back(id_of(*s));
    }
    return out;
}

// The generic calls of view V over the chain written.
template <class V>
void calls(qiven3::test::Context& qiven3_ctx, V& view, const Chain& chain, const Chars& chars) {
    const Rec& a = chain.mine[0];
    const Rec& b = chain.mine[1];
    const Rec& d = chain.mine[2];
    const Rec& acc = chain.mine[3];
    const Rec& c = chain.theirs[0];
    const Rec& m1 = chain.theirs[1];
    const Rec& m2 = chain.theirs[2];
    const Rec& acc2 = chain.theirs[3];
    const std::string none = "sha256:" + std::string(64, '0');

    qiven3::memory::String line(chars);
    CHECK(view.line(id_of(a), line) && std::string_view(line) == a.line);
    CHECK(view.line(id_of(acc2), line) && std::string_view(line) == acc2.line);
    CHECK(!view.line(none, line));
    CHECK(view.header(id_of(m1), line) && std::string_view(line) == m1.line && !view.header(none, line));
    CHECK(view.body(id_of(a), line) && std::string_view(line) == *a.record && !view.body(none, line));

    CHECK(view.held_in(id_of(a), qiven_scope) && view.held_in(id_of(acc2), qiven_scope));
    CHECK(!view.held_in(id_of(a), "person") && !view.held_in(none, qiven_scope));
    CHECK(!view.held_in(id_of(a), qiven_scope.substr(0, qiven_scope.size() - 1)));
    CHECK(!view.held_in(id_of(a), std::string(qiven_scope) + "*"));

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
    CHECK(view.contains("kind:direction"));
    CHECK(!view.contains(none));
    CHECK(!view.contains("kind:forget"));

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
// A refusal's code (or entry code), index and id; none where f raised none.
struct Refusal {
    std::string code;
    std::size_t index = 0;
    std::string id;
};
template <class F>
std::optional<Refusal> refused(const F& f) {
    try {
        f();
    } catch (const qiven3::store::StoreError& e) {
        const std::optional<qiven3::store::Code> code = e.code();
        return Refusal{std::string(code ? qiven3::store::name(*code) : e.entry_code()), e.index(), std::string(e.id())};
    }
    return std::nullopt;
}

// The counts the order's chain holds.
constexpr qiven3::store::Counts ordered{3, 3, 0, 1};

template <class S>
void order(qiven3::test::Context& qiven3_ctx, S& store) {
    using qiven3::store::NoAdmit;
    const std::vector<Rec> chain = qiven3::test::records::chain(qiven_scope, "implementer", {"A", "B", "D"});
    const std::vector<Rec> skip_first = {chain[1]};
    const std::vector<Rec> skip_second = {chain[0], chain[2]};
    const std::vector<Rec> late = {rec("A", qiven_scope, "implementer", 2)};
    const auto one = [&](const std::vector<Rec>& list) {
        const auto lines = views(list);
        return refused([&] { (void)store.append(qiven_scope, "implementer", std::nullopt, lines, NoAdmit{}); });
    };
    const auto first = one(skip_first);
    CHECK(first.has_value() && first->code == "parent" && first->index == 0U);
    const auto second = one(skip_second);
    CHECK(second.has_value() && second->code == "parent" && second->index == 1U && second->id == id_of(chain[2]));
    const auto seq = one(late);
    CHECK(seq.has_value() && seq->code == "seq" && seq->index == 0U);
    CHECK(store.verify() == qiven3::store::Counts{});
    const auto lines = views(chain);
    const qiven3::store::Written w = store.append(qiven_scope, "implementer", std::nullopt, lines, NoAdmit{});
    CHECK_EQ(w.head.id.view(), id_of(chain.back()));
    CHECK(w.head.seq == 3);
    CHECK(store.verify() == ordered);
}

}  // namespace

QIVEN3_CASE(store_view, order_memory) {
    const auto fake = std::make_unique<Fake>(qiven3::platform::fake::Config{});
    auto heap = fake->pages(state_bytes);
    const auto st = std::make_unique<state::State>(heap.bytes(), Limits{});
    qiven3::store::MemoryStore<Fake> store(*st, *fake);
    order(qiven3_ctx, store);
}

QIVEN3_CASE(store_view, order_sqlite) {
    const auto scratch = std::make_unique<Scratch>();
    const auto fake = std::make_unique<Fake>(scratch->config());
    const Engine<Fake> engine(*fake);
    using Store = qiven3::store::SQLiteStore<Fake>;
    const std::string path = text(scratch->root / "o" / "store.db");
    {
        const auto store = std::make_unique<Store>(Store::open(path, true, false, *fake));
        order(qiven3_ctx, *store);
        (void)store->close();
    }
    const auto again = std::make_unique<Store>(Store::open(path, false, false, *fake));
    CHECK(again->verify() == ordered);
    const std::optional<qiven3::store::Head> head = again->head(qiven_scope, "implementer");
    CHECK(head.has_value() && head->seq == 3 && head->id.view() == "D");
    (void)again->close();
}

QIVEN3_CASE(store_view, memory) {
    const auto fake = std::make_unique<Fake>(qiven3::platform::fake::Config{});
    auto heap = fake->pages(state_bytes);
    const auto st = std::make_unique<state::State>(heap.bytes(), Limits{});
    const Chain chain;
    {
        qiven3::store::MemoryStore<Fake> store(*st, *fake);
        chain.write(store);
        CHECK(store.verify() == Chain::held);
    }
    std::vector<std::byte> pages(buffer_bytes);
    Arena arena(pages);
    const Chars chars(arena);
    const state::Locked lock(*st, Operation::append, {});
    state::StateView view(lock.data(), chars);
    calls(qiven3_ctx, view, chain, chars);
    std::vector<std::byte> scratch_pages(buffer_bytes);
    state::ScratchView scratch(scratch_pages, nullptr);
    state::StateView over(lock.data(), chars, &scratch);
    work(qiven3_ctx, over, scratch, chars);
}

QIVEN3_CASE(store_view, sqlite) {
    const auto scratch = std::make_unique<Scratch>();
    const auto fake = std::make_unique<Fake>(scratch->config());
    const Engine<Fake> engine(*fake);
    const Chain chain;
    using Store = qiven3::store::SQLiteStore<Fake>;
    const std::string path = text(scratch->root / "v" / "store.db");
    const auto store = std::make_unique<Store>(Store::open(path, true, false, *fake));
    chain.write(*store);
    CHECK(store->verify() == Chain::held);
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
    const auto again = std::make_unique<Store>(Store::open(path, false, false, *fake));
    CHECK(again->verify() == Chain::held);
    (void)again->close();
}
