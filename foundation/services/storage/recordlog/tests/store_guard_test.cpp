// store's own guards, in both stores with no admit hook (NoAdmit): each
// write the store refuses itself, whatever its hook, followed by verify
// and, on SQLite, a reopen and verify again, the counts and the head as
// before the write. append_forget: a forget appended, forget_path;
// forget_plain: a forget given a record not a forget, forget_kind;
// forget_unheld, _twice,
// _forgotten, _scope: a forget whose target is not held, is the call's twice,
// is forgotten, or is of another scope, target; import_gap, import_parent:
// an import's line with a seq gap, seq, or a parent not its lane's head,
// parent; head_parent, head_seq: on a lane with a head, a parent not the
// head, parent, and a seq not one past it with the right parent (refused
// by the order alone), seq; other_scope, other_lane: an entry
// whose scope or lane is not the call's, parent; append_unheld: an entry
// whose target is not held, target; import_unheld: an import's forget whose
// target is not held, target; import_forgotten, import_bare: an import
// giving a forgotten entry's body, forgotten_body, or a held entry's body
// missing, body_missing. long_id, _scope, _lane, _parent, _target: an
// append whose key is past its bound (Id's capacity, the scope's or the
// lane's bytes), too_long with the key's name as its field (long_id with
// no id); body_unrecorded: a body key with no record, body_missing;
// record_unkeyed: a record with no body key, body_mismatch.
// append_forget_body, forget_body, import_forget_body: a forget with a body
// key and a record appended, given to forget or imported, forget_body;
// append_forget_bare, forget_bare, import_forget_bare: a forget with no
// target key, target. A verify or a reopen refusing is printed. import_plain:
// an import, then an append, with no admit hook over plain keys of lines no
// parser takes, the store reading no line, each verified clean, and on
// SQLite reopened and verified clean again.

#include <array>
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

#include "qiven/recordlog/keys.hpp"
#include "qiven/memory/arena.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/runner/runner.hpp"
#include "qiven/recordlog/contract.hpp"
#include "qiven/recordlog/errors.hpp"
#include "qiven/recordlog/sqlite_steps.hpp"
#include "qiven/recordlog/memory.hpp"
#include "qiven/recordlog/memory_state.hpp"
#include "qiven/recordlog/sqlite_store.hpp"
#include "qiven/platform/fake/fake_platform.hpp"
#include "support/stores/records.hpp"

namespace {

namespace fs = std::filesystem;
namespace records = qiven::test::records;
namespace state = qiven::recordlog::state;
using qiven::recordlog::Engine;
using qiven::recordlog::EntryView;
using qiven::recordlog::Expected;
using qiven::recordlog::GuardView;
using qiven::recordlog::Id;
using qiven::recordlog::LaneHead;
using qiven::recordlog::Limits;
using qiven::recordlog::Line;
using qiven::recordlog::NoAdmit;
using qiven::recordlog::Vector;
using records::id_of;
using records::lane_head;
using records::Rec;
using records::rec;
using records::seq_of;
using records::views;
using Arena = qiven::memory::Arena;
using Chars = qiven::memory::Allocator<char>;
using Fake = qiven::platform::fake::FakePlatform;

constexpr std::string_view qs = "group:primary";
constexpr std::string_view away_scope = "person";
constexpr std::string_view mine = "lane-one";
constexpr std::size_t state_bytes = std::size_t{8} << 20U;

// A StoreError's code (or full reason, or entry code), index, field, id and
// detail; none where f raised none.
struct Refusal {
    std::string code;
    std::size_t index = 0;
    std::string field;
    std::string id;
    std::string detail;
};
template <class F>
std::optional<Refusal> refused(const F& f) {
    try {
        f();
    } catch (const qiven::recordlog::StoreError& e) {
        Refusal r;
        if (const std::optional<qiven::recordlog::Code> code = e.code()) {
            r.code = qiven::recordlog::name(*code);
        } else if (const std::optional<qiven::recordlog::Full> full = e.full()) {
            r.code = qiven::recordlog::name(*full);
        } else {
            r.code = e.entry_code();
        }
        r.index = e.index();
        r.field = e.field();
        r.id = e.id();
        r.detail = e.detail();
        return r;
    }
    return std::nullopt;
}

// The records the cases write: lane one's chain A, B, D and ACC
// (targeting D), E a forget of B; C on lane two, targeting A; AWAY on
// lane one in another scope.
struct Book {
    Rec a = rec("A", qs, mine, 1, std::nullopt, "alpha");
    Rec b = rec("B", qs, mine, 2, "A", "alpha");
    Rec d = rec("D", qs, mine, 3, "B", "beta");
    Rec acc = records::targeting(rec("ACC", qs, mine, 4, "D", "epsilon"), "D");
    Rec e = records::forget("E", qs, mine, 5, "ACC", "B");
    Rec c = records::targeting(rec("C", qs, "lane-two", 1, std::nullopt, "alpha"), "A");
    Rec away = rec("AWAY", away_scope, mine, 1, std::nullopt, "alpha");
};

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
    [[nodiscard]] qiven::platform::fake::Config config() const {
        qiven::platform::fake::Config config;
        config.root = text(root);
        config.seed = 7;
        return config;
    }
    fs::path root;
};

// An import's source: the lines given, in order, the heads, and a change to the last line's keys.
struct ListSource {
    using Change = void (*)(qiven::recordlog::KeysView&);
    std::vector<LaneHead> given;
    std::vector<Rec> lines;
    Change last = nullptr;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines.size()) {
            return false;
        }
        const Rec& s = lines[at++];
        line.header.assign(s.line);
        line.has_body = s.record.has_value();
        line.body.assign(s.record.value_or(std::string()));
        line.forgotten = false;
        qiven::recordlog::KeysView k = records::view(s.keys);
        if (at == lines.size() && last != nullptr) {
            last(k);
        }
        return line.keys.assign(k);
    }
};

// A forget on lane one like E, with its target, parent and seq
// as given, its id of them.
Rec forget_of(std::string_view target, std::string_view parent, std::int64_t seq) {
    const std::string id = "F-" + std::string(target) + "-" + std::to_string(seq);
    return records::forget(id, qs, mine, seq, parent, target);
}

// What a case wants refused: its code and index.
struct Want {
    std::string_view name;
    std::string_view code;
    std::size_t index;
};
constexpr std::size_t cases = 29;
constexpr Want wants[cases] = {
    {"append_forget", "forget_path", 0}, {"forget_plain", "forget_kind", 0}, {"forget_unheld", "target", 0},
    {"forget_twice", "target", 1},       {"forget_forgotten", "target", 0},   {"forget_scope", "target", 0},
    {"import_gap", "seq", 1},            {"import_parent", "parent", 1},      {"head_parent", "parent", 0},
    {"head_seq", "seq", 0},              {"other_scope", "parent", 0},        {"other_lane", "parent", 0},
    {"append_unheld", "target", 0},      {"import_unheld", "target", 4},      {"import_forgotten", "forgotten_body", 1},
    {"import_bare", "body_missing", 1},  {"long_id", "too_long", 0},          {"long_scope", "too_long", 0},
    {"long_lane", "too_long", 0},        {"long_parent", "too_long", 0},      {"long_target", "too_long", 0},
    {"body_unrecorded", "body_missing", 0}, {"record_unkeyed", "body_mismatch", 0},
    {"append_forget_body", "forget_body", 0}, {"forget_body", "forget_body", 0},
    {"append_forget_bare", "target", 0},      {"forget_bare", "target", 0},
    {"import_forget_body", "forget_body", 4}, {"import_forget_bare", "target", 4}};

// Case k's held state, written on s.
template <class S>
void before(std::size_t k, S& s, const Book& book, Arena& arena) {
    const Rec& a = book.a;
    const std::vector<Rec> chain = {a, book.b, book.d, book.acc};
    if (k == 1) {
        const std::vector<Rec> three = {a, book.b, book.d};
        (void)s.append(qs, mine, std::nullopt, views(three), NoAdmit{});
    } else if (k <= 5 || (k >= 23 && k <= 26)) {
        (void)s.append(qs, mine, std::nullopt, views(chain), NoAdmit{});
    } else if (k == 8 || k == 9 || k == 12) {
        const std::vector<Rec> first = {a};
        (void)s.append(qs, mine, std::nullopt, views(first), NoAdmit{});
    }
    if (k == 4) {
        const std::vector<Rec> e = {book.e};
        const std::string& acc = id_of(book.acc);
        const std::vector<GuardView> guard = {GuardView{qs, mine, acc}};
        Vector<Id> erased{Chars(arena)};
        (void)s.forget(qs, mine, Expected(acc), views(e), guard, erased, NoAdmit{});
    }
    if (k == 5) {
        const std::vector<Rec> away = {book.away};
        (void)s.append(away_scope, mine, std::nullopt, views(away), NoAdmit{});
    }
}

// Case k's write on s, after before; its refusal.
template <class S>
std::optional<Refusal> write(std::size_t k, S& s, const Book& book, Arena& arena) {
    const Rec& a = book.a;
    const Rec& e = book.e;
    const std::string& acc = id_of(book.acc);
    const std::string& d = id_of(book.d);
    std::vector<Rec> lines;
    const auto append = [&](std::string_view scope, std::string_view lane, Expected expected) {
        return refused([&] { (void)s.append(scope, lane, expected, views(lines), NoAdmit{}); });
    };
    const auto forget = [&](Expected expected, std::vector<GuardView> guard) {
        Vector<Id> erased{Chars(arena)};
        return refused([&] { (void)s.forget(qs, mine, expected, views(lines), guard, erased, NoAdmit{}); });
    };
    const auto import = [&](ListSource::Change last = nullptr) {
        ListSource src;
        src.lines = lines;
        src.last = last;
        src.given.push_back(lane_head(qs, mine, id_of(lines.back()), seq_of(lines.back())));
        Vector<LaneHead> wrote{Chars(arena)};
        return refused([&] { (void)s.import_(src, wrote, NoAdmit{}); });
    };
    const auto again = [](std::string_view target, std::string_view parent, std::int64_t seq) {
        return forget_of(target, parent, seq);
    };
    // A's view, its keys or record changed by f, appended on an empty lane.
    const std::string past_id(Id::capacity + 1, 'x');
    const std::string past_scope(qiven::recordlog::scope_bytes + 1, 's');
    const std::string past_lane(qiven::recordlog::lane_bytes + 1, 'l');
    const auto keyed = [&](auto&& f) {
        lines = {a};
        std::vector<EntryView> given = views(lines);
        f(given[0]);
        return refused([&] { (void)s.append(qs, mine, std::nullopt, given, NoAdmit{}); });
    };
    // E, its keys changed by f, with A's record where bodied: appended (op
    // 'a') or given to forget ('f') on A, B, D and ACC, or imported after
    // them ('i'), B's body not given where E forgets it.
    const ListSource::Change bodied = [](qiven::recordlog::KeysView& v) { v.body = true; };
    const ListSource::Change untargeted = [](qiven::recordlog::KeysView& v) { v.target.reset(); };
    const auto changed = [&](ListSource::Change f, char op) {
        Rec given = e;
        Rec b = book.b;
        if (f == bodied) {
            given.record = a.record;
            b.record.reset();
        }
        if (op == 'i') {
            lines = {a, b, book.d, book.acc, given};
            return import(f);
        }
        lines = {given};
        std::vector<EntryView> v = views(lines);
        f(v[0].keys);
        const std::vector<GuardView> guard = {GuardView{qs, mine, acc}};
        Vector<Id> erased{Chars(arena)};
        return refused([&] {
            if (op == 'a') {
                (void)s.append(qs, mine, Expected(acc), v, NoAdmit{});
            } else {
                (void)s.forget(qs, mine, Expected(acc), v, guard, erased, NoAdmit{});
            }
        });
    };
    if (k >= 23) {
        return changed(k == 23 || k == 24 || k == 27 ? bodied : untargeted, "afafii"[k - 23]);
    }
    switch (k) {
    case 15: {
        Rec bare = book.b;
        bare.record.reset();
        lines = {a, bare};
        return import();
    }
    case 16:
        return keyed([&](EntryView& v) { v.keys.id = past_id; });
    case 17:
        return keyed([&](EntryView& v) { v.keys.scope = past_scope; });
    case 18:
        return keyed([&](EntryView& v) { v.keys.lane = past_lane; });
    case 19:
        return keyed([&](EntryView& v) { v.keys.parent = past_id; });
    case 20:
        return keyed([&](EntryView& v) { v.keys.target = past_id; });
    case 21:
        return keyed([&](EntryView& v) { v.body.reset(); });
    case 22:
        return keyed([&](EntryView& v) { v.keys.body = false; });
    case 0:
        lines = {e};
        return append(qs, mine, Expected(acc));
    case 1:
        lines = {book.acc};
        return forget(Expected(d), {GuardView{qs, mine, d}});
    case 2:
        lines = {again(id_of(book.c), acc, seq_of(e))};
        return forget(Expected(acc), {GuardView{qs, mine, acc}});
    case 3:
        lines = {e, again(id_of(book.b), id_of(e), seq_of(e) + 1)};
        return forget(Expected(acc), {GuardView{qs, mine, acc}});
    case 4:
        lines = {again(id_of(book.b), id_of(e), seq_of(e) + 1)};
        return forget(Expected(id_of(e)), {GuardView{qs, mine, id_of(e)}});
    case 5:
        lines = {again(id_of(book.away), acc, seq_of(e))};
        return forget(Expected(acc), {GuardView{qs, mine, acc}, GuardView{away_scope, mine, id_of(book.away)}});
    case 6:
        lines = {a, rec("B", qs, mine, 3, "A", "alpha")};
        return import();
    case 7:
        lines = {a, book.d};
        return import();
    case 8:
        lines = {book.d};
        return append(qs, mine, Expected(id_of(a)));
    case 9:
        lines = {rec("B", qs, mine, 5, "A", "alpha")};
        return append(qs, mine, Expected(id_of(a)));
    case 10:
        lines = {a};
        return append(away_scope, mine, std::nullopt);
    case 11:
        lines = {book.c};
        return append(qs, mine, std::nullopt);
    case 12:
        lines = {records::targeting(book.b, id_of(book.c))};
        return append(qs, mine, Expected(id_of(a)));
    case 13:
        lines = {a, book.b, book.d, book.acc, again(id_of(book.c), acc, seq_of(e))};
        return import();
    case 14:
        lines = {a, book.b, book.d, book.acc, e};
        return import();
    default:
        return std::nullopt;
    }
}

// The refusal as case k wants it: a key past its bound naming the key as
// its field, and no id where the id is the one past it.
bool as_wanted(std::size_t k, const std::optional<Refusal>& r) {
    const std::string_view name = wants[k].name;
    const bool bound = name.starts_with("long_");
    return r.has_value() && r->code == wants[k].code &&
           (wants[k].code == "forget_kind" ? r->detail == "entry 0" : r->index == wants[k].index) &&
           (!bound || r->field == name.substr(5)) && (name != "long_id" || r->id.empty());
}

// What a StoreError f raised says: its kind, its corrupt code where it has
// one, and its detail; empty where f raised none.
template <class F>
std::string said(const F& f) {
    try {
        f();
    } catch (const qiven::recordlog::StoreError& e) {
        const std::optional<qiven::recordlog::Corrupt> c = e.corrupt();
        return std::string(qiven::recordlog::name(e.kind())) + " " +
               std::string(c ? qiven::recordlog::name(*c) : std::string_view()) + " [" + std::string(e.detail()) + "]";
    }
    return {};
}

// s's verify the counts held; a refusal printed, the case's name at.
template <class S>
bool verifies(S& s, const qiven::recordlog::Counts& held, std::string_view at) {
    qiven::recordlog::Counts got;
    if (const std::string r = said([&] { got = s.verify(); }); !r.empty()) {
        std::cout << at << ": verify " << r << "\n";
        return false;
    }
    return got == held;
}

// An import's source of lines with their keys as given.
struct PlainSource {
    struct Given {
        std::string line;
        std::optional<std::string> record;
        qiven::recordlog::KeysView keys;
    };
    std::vector<LaneHead> given;
    std::vector<Given> lines;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines.size()) {
            return false;
        }
        const Given& g = lines[at++];
        line.header.assign(g.line);
        line.has_body = g.record.has_value();
        line.body.assign(g.record.value_or(std::string()));
        line.forgotten = false;
        return line.keys.assign(g.keys);
    }
};

// import_plain on s: lines with no admit hook, taken by their keys alone, the
// store reading nothing of a line or a record: A's line under keys not its
// own with a record no body parser takes, a body key whose body a forget on
// another lane forgets, and that forget; the counts, the heads, then a
// scan's keys and lines as given.
template <class S>
bool imports_plain(S& s, const Book& book, Arena& arena) {
    using qiven::recordlog::KeysView;
    PlainSource src;
    src.lines = {{book.a.line, "a record", KeysView{"p1", "plain", "one", "note", 1, {}, {}, true}},
                 {"line two", std::nullopt, KeysView{"p2", "plain", "one", "note", 2, "p1", {}, true}},
                 {"line three", std::nullopt, KeysView{"p3", "plain", "two", "forget", 1, {}, "p2", false}}};
    src.given = {lane_head("plain", "one", "p2", 2), lane_head("plain", "two", "p3", 1)};
    Vector<LaneHead> wrote{Chars(arena)};
    const qiven::recordlog::Imported in = s.import_(src, wrote, NoAdmit{});
    const std::optional<qiven::recordlog::Head> one = s.head("plain", "one");
    const std::optional<qiven::recordlog::Head> two = s.head("plain", "two");
    bool ok = in.counts == qiven::recordlog::Counts{3, 1, 1, 2} && wrote.size() == 2 && one && one->id.view() == "p2" &&
              two && two->seq == 1;
    {
        auto scan = s.scan({});
        qiven::recordlog::Record r{Chars(arena)};
        for (const PlainSource::Given& g : src.lines) {
            ok = ok && scan.next(r);
            const KeysView k = r.keys();
            ok = ok && std::string_view(r.line) == g.line && k.id == g.keys.id && k.scope == g.keys.scope &&
                 k.lane == g.keys.lane && k.kind == g.keys.kind && k.seq == g.keys.seq &&
                 k.parent == g.keys.parent && k.target == g.keys.target && k.body == g.keys.body;
        }
        ok = ok && !scan.next(r);
    }
    return ok && s.verify() == in.counts;
}

// appends_plain on s, after imports_plain: an append with no admit hook of
// a line and a record no parser takes on a lane of its own; the store's
// verify reading neither, clean.
template <class S>
bool appends_plain(S& s) {
    const std::array<EntryView, 1> lines = {
        EntryView{"line four", "record four", qiven::recordlog::KeysView{"p4", "plain", "three", "note", 1, {}, {}, true}}};
    (void)s.append("plain", "three", std::nullopt, lines, NoAdmit{});
    return s.verify() == qiven::recordlog::Counts{4, 2, 1, 3};
}

}  // namespace

QIVEN_CASE(store_guard, import_plain) {
    const auto fake = std::make_unique<Fake>(qiven::platform::fake::Config{});
    const auto book = std::make_unique<const Book>();
    std::vector<std::byte> pages(std::size_t{1} << 20U);
    Arena arena(pages);
    {
        auto heap = fake->pages(state_bytes);
        const auto st = std::make_unique<state::State>(heap.bytes(), records::suite_limits);
        qiven::recordlog::MemoryStore<Fake> s(*st, *fake);
        CHECK(imports_plain(s, *book, arena));
        CHECK(appends_plain(s));
    }
    const auto scratch = std::make_unique<Scratch>();
    const auto disk = std::make_unique<Fake>(scratch->config());
    const Engine<Fake> engine(*disk);
    using Store = qiven::recordlog::SQLiteStore<Fake>;
    const std::string path = text(scratch->root / "plain" / "store.db");
    {
        const auto s = std::make_unique<Store>(Store::open(path, true, false, *disk,
                                                           records::suite_limits, records::suite_identity));
        CHECK(imports_plain(*s, *book, arena));
        CHECK(appends_plain(*s));
        (void)s->close();
    }
    const auto again = std::make_unique<Store>(Store::open(path, false, false, *disk,
                                                           records::suite_limits, records::suite_identity));
    CHECK(again->verify() == (qiven::recordlog::Counts{4, 2, 1, 3}));
    (void)again->close();
}

QIVEN_CASE(store_guard, memory) {
    const auto fake = std::make_unique<Fake>(qiven::platform::fake::Config{});
    const auto book = std::make_unique<const Book>();
    for (std::size_t k = 0; k < cases; ++k) {
        auto heap = fake->pages(state_bytes);
        const auto st = std::make_unique<state::State>(heap.bytes(), records::suite_limits);
        std::vector<std::byte> pages(std::size_t{1} << 20U);
        Arena arena(pages);
        qiven::recordlog::MemoryStore<Fake> s(*st, *fake);
        before(k, s, *book, arena);
        const qiven::recordlog::Counts held = s.verify();
        const std::optional<qiven::recordlog::Head> head = s.head(qs, mine);
        const std::optional<Refusal> r = write(k, s, *book, arena);
        std::cout << "store_guard.memory: " << wants[k].name << ": " << (r ? r->code : "none") << "\n";
        CHECK(as_wanted(k, r));
        CHECK(verifies(s, held, wants[k].name));
        CHECK(s.head(qs, mine).has_value() == head.has_value());
        CHECK(!head || s.head(qs, mine)->id == head->id);
    }
}

QIVEN_CASE(store_guard, sqlite) {
    const auto scratch = std::make_unique<Scratch>();
    const auto fake = std::make_unique<Fake>(scratch->config());
    const Engine<Fake> engine(*fake);
    const auto book = std::make_unique<const Book>();
    using Store = qiven::recordlog::SQLiteStore<Fake>;
    for (std::size_t k = 0; k < cases; ++k) {
        const std::string path = text(scratch->root / ("g" + std::to_string(k)) / "store.db");
        std::vector<std::byte> pages(std::size_t{1} << 20U);
        Arena arena(pages);
        qiven::recordlog::Counts held;
        std::optional<qiven::recordlog::Head> head;
        {
            const auto s = std::make_unique<Store>(Store::open(path, true, false, *fake,
                                                               records::suite_limits, records::suite_identity));
            before(k, *s, *book, arena);
            held = s->verify();
            head = s->head(qs, mine);
            const std::optional<Refusal> r = write(k, *s, *book, arena);
            std::cout << "store_guard.sqlite: " << wants[k].name << ": " << (r ? r->code : "none") << "\n";
            CHECK(as_wanted(k, r));
            CHECK(verifies(*s, held, wants[k].name));
            (void)s->close();
        }
        std::unique_ptr<Store> again;
        if (const std::string o = said([&] { again = std::make_unique<Store>(Store::open(path, false, false, *fake,
                records::suite_limits, records::suite_identity)); });
            !o.empty()) {
            std::cout << wants[k].name << ": reopen " << o << "\n";
        }
        CHECK(again != nullptr);
        if (!again) {
            continue;
        }
        CHECK(verifies(*again, held, wants[k].name));
        CHECK(again->head(qs, mine).has_value() == head.has_value());
        CHECK(!head || again->head(qs, mine)->id == head->id);
        (void)again->close();
    }
}
