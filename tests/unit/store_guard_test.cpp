// store's own guards, in both stores with no admit hook (NoAdmit): each
// write the store refuses itself, whatever its hook, followed by verify
// and, on SQLite, a reopen and verify again, the counts and the head as
// before the write. append_forget: a forget appended, forget_path;
// forget_accept: a forget given an accept, forget_kind; forget_unheld, _twice,
// _forgotten, _scope: a forget whose target is not held, is the call's twice,
// is forgotten, or is of another scope, target; import_gap, import_parent:
// an import's line with a seq gap, seq, or a parent not its lane's head,
// parent; head_parent, head_seq: on a lane with a head, a parent not the
// head, parent, and a seq not one past it with the right parent (past part
// 1, refused by the order alone), seq; other_scope, other_lane: an entry
// whose scope or lane is not the call's, parent; append_unheld: an entry
// whose target is not held, target; import_unheld: an import's forget whose
// target is not held, target; import_forgotten, import_bare: an import
// giving a forgotten entry's body, forgotten_body, or a held entry's body
// missing, body_missing.

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "entry/header.hpp"
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
namespace header = qiven3::entry::header;
namespace state = qiven3::store::state;
using qiven3::store::Engine;
using qiven3::store::Expected;
using qiven3::store::GuardView;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::store::Limits;
using qiven3::store::Line;
using qiven3::store::NoAdmit;
using qiven3::store::Vector;
using qiven3::test::stage_book::Book;
using qiven3::test::stage_book::Fake;
using qiven3::test::stage_book::id_of;
using qiven3::test::stage_book::lane_head;
using qiven3::test::stage_book::Refusal;
using qiven3::test::stage_book::refused;
using qiven3::test::stage_book::Stored;
using qiven3::test::stage_book::views;
using Arena = qiven3::memory::Arena;
using Chars = qiven3::memory::Allocator<char>;
using Slot = header::Slot;

constexpr std::string_view qs = "project:qiven";
constexpr std::string_view away_scope = "person";
constexpr std::string_view mine = "implementer";
constexpr std::size_t state_bytes = std::size_t{8} << 20U;

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
        root = fs::temp_directory_path() / ("qiven3-guard-" + std::to_string(draw()) + std::to_string(draw()));
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

// An import's source: the lines given, in order, and the heads.
struct ListSource {
    std::vector<LaneHead> given;
    std::vector<Stored> lines;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines.size()) {
            return false;
        }
        const Stored& s = lines[at++];
        line.header.assign(s.line);
        line.has_body = s.record.has_value();
        line.body.assign(s.record.value_or(std::string()));
        line.forgotten = false;
        return true;
    }
};

// Fixture E, the forget of B, with its target, parent and seq as given.
Stored forget_of(const Book& book, std::string_view target, std::string_view parent, std::int64_t seq) {
    const header::Header<> h = header::parse_line(book.stored("E").line);
    header::FieldsView f;
    f.scope = h.value(Slot::scope);
    f.lane = h.value(Slot::lane);
    f.seq = seq;
    f.parent = parent;
    f.kind = h.value(Slot::kind);
    f.writer = h.value(Slot::writer);
    f.basis = h.value(Slot::basis);
    f.at = h.value(Slot::at);
    f.target = target;
    Stored s;
    header::make(f).line(s.line);
    return s;
}

std::int64_t seq_of(const Stored& s) { return header::parse_line(s.line).seq(); }

// What a case wants refused: its code and index.
struct Want {
    std::string_view name;
    std::string_view code;
    std::size_t index;
};
constexpr std::size_t cases = 16;
constexpr Want wants[cases] = {
    {"append_forget", "forget_path", 0}, {"forget_accept", "forget_kind", 0}, {"forget_unheld", "target", 0},
    {"forget_twice", "target", 1},       {"forget_forgotten", "target", 0},   {"forget_scope", "target", 0},
    {"import_gap", "seq", 1},            {"import_parent", "parent", 1},      {"head_parent", "parent", 0},
    {"head_seq", "seq", 0},              {"other_scope", "parent", 0},        {"other_lane", "parent", 0},
    {"append_unheld", "target", 0},      {"import_unheld", "target", 4},      {"import_forgotten", "forgotten_body", 1},
    {"import_bare", "body_missing", 1}};

// Case k's held state, written on s.
template <class S>
void before(std::size_t k, S& s, const Book& book, Arena& arena) {
    const Stored a = book.stored("A");
    const std::vector<Stored> chain = {a, book.stored("B"), book.stored("D"), book.stored("ACC")};
    if (k == 1) {
        const std::vector<Stored> three = {a, book.stored("B"), book.stored("D")};
        (void)s.append(qs, mine, std::nullopt, views(three), NoAdmit{});
    } else if (k <= 5) {
        (void)s.append(qs, mine, std::nullopt, views(chain), NoAdmit{});
    } else if (k == 8 || k == 9 || k == 12) {
        const std::vector<Stored> first = {a};
        (void)s.append(qs, mine, std::nullopt, views(first), NoAdmit{});
    }
    if (k == 4) {
        const std::vector<Stored> e = {book.stored("E")};
        const std::string acc = id_of(book.stored("ACC"));
        const std::vector<GuardView> guard = {GuardView{qs, mine, acc}};
        Vector<Id> erased{Chars(arena)};
        (void)s.forget(qs, mine, Expected(acc), views(e), guard, erased, NoAdmit{});
    }
    if (k == 5) {
        const std::vector<Stored> away = {
            book.made("A", "A", *a.content, [](header::FieldsView& f) { f.scope = away_scope; })};
        (void)s.append(away_scope, mine, std::nullopt, views(away), NoAdmit{});
    }
}

// Case k's write on s, after before; its refusal.
template <class S>
std::optional<Refusal> write(std::size_t k, S& s, const Book& book, Arena& arena) {
    const Stored a = book.stored("A");
    const Stored e = book.stored("E");
    const std::string acc = id_of(book.stored("ACC"));
    const std::string d = id_of(book.stored("D"));
    std::vector<Stored> lines;
    const auto append = [&](std::string_view scope, std::string_view lane, Expected expected) {
        return refused([&] { (void)s.append(scope, lane, expected, views(lines), NoAdmit{}); });
    };
    const auto forget = [&](Expected expected, std::vector<GuardView> guard) {
        Vector<Id> erased{Chars(arena)};
        return refused([&] { (void)s.forget(qs, mine, expected, views(lines), guard, erased, NoAdmit{}); });
    };
    const auto import = [&] {
        ListSource src;
        src.lines = lines;
        src.given.push_back(lane_head(qs, mine, id_of(lines.back()), seq_of(lines.back())));
        Vector<LaneHead> wrote{Chars(arena)};
        return refused([&] { (void)s.import_(src, wrote, NoAdmit{}); });
    };
    const auto again = [&](std::string_view target, std::string_view parent, std::int64_t seq) {
        return forget_of(book, target, parent, seq);
    };
    switch (k) {
    case 0:
        lines = {e};
        return append(qs, mine, Expected(acc));
    case 1:
        lines = {book.stored("ACC")};
        return forget(Expected(d), {GuardView{qs, mine, d}});
    case 2:
        lines = {again(id_of(book.stored("C")), acc, seq_of(e))};
        return forget(Expected(acc), {GuardView{qs, mine, acc}});
    case 3:
        lines = {e, again(id_of(book.stored("B")), id_of(e), seq_of(e) + 1)};
        return forget(Expected(acc), {GuardView{qs, mine, acc}});
    case 4:
        lines = {again(id_of(book.stored("B")), id_of(e), seq_of(e) + 1)};
        return forget(Expected(id_of(e)), {GuardView{qs, mine, id_of(e)}});
    case 5: {
        const Stored away = book.made("A", "A", *a.content, [](header::FieldsView& f) { f.scope = away_scope; });
        lines = {again(id_of(away), acc, seq_of(e))};
        return forget(Expected(acc), {GuardView{qs, mine, acc}, GuardView{away_scope, mine, id_of(away)}});
    }
    case 6:
        lines = {a, book.made("B", "B", *book.stored("B").content, [](header::FieldsView& f) { f.seq = 3; })};
        return import();
    case 7:
        lines = {a, book.stored("D")};
        return import();
    case 8:
        lines = {book.stored("D")};
        return append(qs, mine, Expected(id_of(a)));
    case 9:
        lines = {book.made("B", "B", *book.stored("B").content, [](header::FieldsView& f) { f.seq = 5; })};
        return append(qs, mine, Expected(id_of(a)));
    case 10:
        lines = {a};
        return append(away_scope, mine, std::nullopt);
    case 11:
        lines = {book.stored("C")};
        return append(qs, mine, std::nullopt);
    case 12: {
        const std::string c = id_of(book.stored("C"));
        lines = {book.made("B", "B", *book.stored("B").content, [&](header::FieldsView& f) { f.target = c; })};
        return append(qs, mine, Expected(id_of(a)));
    }
    case 13:
        lines = {a, book.stored("B"), book.stored("D"), book.stored("ACC"),
                 again(id_of(book.stored("C")), acc, seq_of(e))};
        return import();
    case 14:
        lines = {a, book.stored("B"), book.stored("D"), book.stored("ACC"), e};
        return import();
    default: {
        Stored bare = book.stored("B");
        bare.record.reset();
        lines = {a, bare};
        return import();
    }
    }
}

// The refusal as case k wants it.
bool as_wanted(std::size_t k, const std::optional<Refusal>& r) {
    return r.has_value() && r->code == wants[k].code &&
           (wants[k].code == "forget_kind" ? r->detail == "entry 0" : r->index == wants[k].index);
}

}  // namespace

QIVEN3_CASE(store_guard, memory) {
    const auto fake = std::make_unique<Fake>(qiven3::platform::fake::Config{});
    const auto book = std::make_unique<const Book>(*fake);
    for (std::size_t k = 0; k < cases; ++k) {
        auto heap = fake->pages(state_bytes);
        const auto st = std::make_unique<state::State>(heap.bytes(), Limits{});
        std::vector<std::byte> pages(std::size_t{1} << 20U);
        Arena arena(pages);
        qiven3::store::MemoryStore<Fake> s(*st, *fake);
        before(k, s, *book, arena);
        const qiven3::store::Counts held = s.verify();
        const std::optional<qiven3::store::Head> head = s.head(qs, mine);
        const std::optional<Refusal> r = write(k, s, *book, arena);
        std::cout << "store_guard.memory: " << wants[k].name << ": " << (r ? r->code : "none") << "\n";
        CHECK(as_wanted(k, r));
        CHECK(s.verify() == held);
        CHECK(s.head(qs, mine).has_value() == head.has_value());
        CHECK(!head || s.head(qs, mine)->id == head->id);
    }
}

QIVEN3_CASE(store_guard, sqlite) {
    const auto scratch = std::make_unique<Scratch>();
    const auto fake = std::make_unique<Fake>(scratch->config());
    const Engine<Fake> engine(*fake);
    const auto book = std::make_unique<const Book>(*fake);
    using Store = qiven3::store::SQLiteStore<Fake>;
    for (std::size_t k = 0; k < cases; ++k) {
        const std::string path = text(scratch->root / ("g" + std::to_string(k)) / "store.db");
        std::vector<std::byte> pages(std::size_t{1} << 20U);
        Arena arena(pages);
        qiven3::store::Counts held;
        std::optional<qiven3::store::Head> head;
        {
            const auto s = std::make_unique<Store>(Store::open(path, true, false, *fake));
            before(k, *s, *book, arena);
            held = s->verify();
            head = s->head(qs, mine);
            const std::optional<Refusal> r = write(k, *s, *book, arena);
            std::cout << "store_guard.sqlite: " << wants[k].name << ": " << (r ? r->code : "none") << "\n";
            CHECK(as_wanted(k, r));
            CHECK(s->verify() == held);
            (void)s->close();
        }
        const auto again = std::make_unique<Store>(Store::open(path, false, false, *fake));
        CHECK(again->verify() == held);
        CHECK(again->head(qs, mine).has_value() == head.has_value());
        CHECK(!head || again->head(qs, mine)->id == head->id);
        (void)again->close();
    }
}
