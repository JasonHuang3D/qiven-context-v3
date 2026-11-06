#pragma once

// store's plain path, its bed, on the fake with no admit hook (store_plain_test.cpp and store_plain_many_test.cpp):
// plain keys of lines no parser takes (Given, note, gone), an import's source of them, a case's writes (Plan, write)
// and what a store gives after them, by a scan and an export and by id; on_sqlite, a plan on the SQLite store, each
// write followed by verify, a reopen and verify again, then its own export imported into a fresh file, verified,
// reopened and compared.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "qiven/memory/arena.hpp"
#include "qiven/memory/fail.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/recordlog/contract.hpp"
#include "qiven/recordlog/errors.hpp"
#include "qiven/recordlog/keys.hpp"
#include "qiven/recordlog/sqlite_steps.hpp"
#include "qiven/recordlog/sqlite_store.hpp"
#include "qiven/platform/fake/fake_platform.hpp"
#include "qiven/runner/runner.hpp"
#include "support/stores/records.hpp"

namespace qiven::test::plain_bed {

namespace fs = std::filesystem;
namespace sqlite = qiven::recordlog::sqlite;
using qiven::recordlog::Counts;
using qiven::recordlog::EntryView;
using qiven::recordlog::KeysView;
using qiven::recordlog::LaneHead;
using qiven::recordlog::Line;
using qiven::recordlog::Vector;
using qiven::test::records::lane_head;
using Fake = qiven::platform::fake::FakePlatform;
using Arena = qiven::memory::Arena;
using Chars = qiven::memory::Allocator<char>;
using String = qiven::memory::String;
using Store = qiven::recordlog::SQLiteStore<Fake>;
using Parent = std::optional<std::string_view>;

inline constexpr std::size_t page_bytes = std::size_t{1} << 20U;
inline constexpr qiven::recordlog::WhereView test_at = sqlite::site("test", "scratch");

inline std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// A scratch directory of the case's own, removed at its end, a fake on it.
// What a Site is made from: a fresh scratch each time.
struct Scratch {};
struct Site {
    static qiven::platform::fake::Config config(const fs::path& root) {
        qiven::platform::fake::Config out;
        out.root = text(root);
        out.seed = 7;
        return out;
    }
    static fs::path fresh() {
        // On the heap: libstdc++'s random_device is some 5,000 bytes.
        std::vector<std::random_device> device(1);
        std::random_device& draw = device.front();
        fs::path out = fs::temp_directory_path() / ("qiven-plain-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    explicit Site(Scratch /*fresh*/) : root(fresh()), fake(config(root)), engine(fake) {}
    Site(const Site&) = delete;
    Site& operator=(const Site&) = delete;
    Site(Site&&) = delete;
    Site& operator=(Site&&) = delete;
    ~Site() {
        try {
            std::error_code error;
            fs::remove_all(root, error);
        } catch (...) {
            qiven::memory::fail_fast("plain_bed: the scratch directory's removal threw");
        }
    }
    fs::path root;
    Fake fake;
    qiven::recordlog::Engine<Fake> engine;
};

// The store at path on fake: the suite's identity, the suite's limits or those given.
inline Store opened(std::string_view path, bool create, bool read_only, Fake& fake,
                    const qiven::recordlog::Limits& limits = records::suite_limits) {
    return Store::open(path, create, read_only, fake, limits, records::suite_identity);
}

struct Given {
    std::string line;
    std::optional<std::string> record;
    KeysView keys;
};

// A plain record on the scope "plain", its body key and its record as given.
inline Given note(std::string_view id, std::string_view lane, std::int64_t seq, Parent parent, bool key, bool record) {
    return Given{std::string("line ").append(id), record ? std::optional<std::string>("record") : std::nullopt,
                 KeysView{id, "plain", lane, "note", seq, parent, {}, key}};
}
inline Given gone(std::string_view id, std::string_view lane, std::int64_t seq, Parent parent,
                  std::string_view target) {
    return Given{std::string("line ").append(id), std::nullopt,
                 KeysView{id, "plain", lane, "forget", seq, parent, target, false}};
}

// An import's source of lines with their keys as given.
struct PlainSource {
    std::vector<LaneHead> given;
    const std::vector<Given>* lines = nullptr;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines->size()) {
            return false;
        }
        const Given& g = (*lines)[at++];
        line.header.assign(g.line);
        line.has_body = g.record.has_value();
        line.body.assign(g.record.value_or(std::string()));
        line.forgotten = false;
        return line.keys.assign(g.keys);
    }
};

// The writes of a case: lines imported or appended each alone, then a first
// call of lines together, then a forget given; the last refused "code index id".
struct Plan {
    bool import = false;
    std::vector<Given> lines;
    std::vector<LaneHead> heads;
    std::optional<Given> forget;
    std::vector<Given> together;
    std::string refused;
    bool own = false;  // last, its own export imported
    qiven::recordlog::Limits limits = records::suite_limits;
};

inline std::array<EntryView, 1> one(const Given& g) {
    return {EntryView{g.line, g.record ? std::optional<std::string_view>(*g.record) : std::nullopt, g.keys}};
}

template <class S>
void own_import(S& s, Arena& arena) {
    auto source = s.export_(std::nullopt, {});
    Vector<LaneHead> rows{Chars(arena)};
    (void)s.import_(source, rows);
}

template <class S>
void write(S& s, const Plan& p, Arena& arena) {
    if (p.import) {
        PlainSource source{p.heads, &p.lines};
        Vector<LaneHead> rows{Chars(arena)};
        (void)s.import_(source, rows);
    } else {
        for (const Given& g : p.lines) {
            (void)s.append(g.keys.scope, g.keys.lane, g.keys.parent, one(g));
        }
    }
    if (!p.together.empty()) {
        std::vector<EntryView> call;
        for (const Given& g : p.together) {
            call.push_back(one(g)[0]);
        }
        (void)s.append("plain", "one", {}, std::span<const EntryView>(call));
    }
    if (p.forget) {
        const KeysView& k = p.forget->keys;
        const KeysView& last = p.lines.back().keys;  // the one lane held
        const std::array<qiven::recordlog::GuardView, 1> guard = {{{last.scope, last.lane, last.id}}};
        Vector<qiven::recordlog::Id> erased{Chars(arena)};
        (void)s.forget(k.scope, k.lane, k.parent, one(*p.forget), guard, erased);
    }
    if (p.own) {
        own_import(s, arena);
    }
}

inline std::string keys_text(const KeysView& k) {
    std::string out(k.id);
    out.append(" ").append(k.scope).append(" ").append(k.lane).append(" ").append(k.kind);
    out.append(" ").append(std::to_string(k.seq)).append(" ").append(k.parent.value_or("-"));
    out.append(" ").append(k.target.value_or("-")).append(k.body ? " body\n" : " none\n");
    return out;
}

// Each entry's keys by a scan and by an export.
template <class S>
std::string scanned(S& s, Arena& arena) {
    std::string out;
    qiven::recordlog::Record r{Chars(arena)};
    for (auto scan = s.scan({}); scan.next(r);) {
        out += keys_text(r.keys());
    }
    return out;
}
template <class S>
std::string exported(S& s, Arena& arena) {
    std::string out;
    auto source = s.export_(std::nullopt, {});
    Vector<LaneHead> rows{Chars(arena)};
    (void)source.heads(rows);
    Line line(arena);
    while (source.next(line)) {
        out += keys_text(line.keys.view());
    }
    source.close();
    return out;
}

// What s gives: its keys, by a scan and an export alike, then by id, through
// the plan's lines where all were written, whether it holds a record.
template <class S>
std::string given(S& s, const Plan& p, Arena& arena) {
    const std::string keys = scanned(s, arena);
    std::string out = keys == exported(s, arena) ? keys : std::string("a scan and an export differ\n");
    String record{Chars(arena)};
    for (const Given& g : p.refused.empty() ? p.lines : std::vector<Given>{}) {
        out.append(g.keys.id).append(s.body(g.keys.id, record) ? " record\n" : " no record\n");
    }
    return out;
}

// What a StoreError f raised says, its kind, its corrupt code and its detail.
template <class F>
std::string said(const F& f) {
    try {
        f();
    } catch (const qiven::recordlog::StoreError& e) {
        if (e.kind() == qiven::recordlog::Kind::entry_invalid) {
            return std::string(e.entry_code()) + " " + std::to_string(e.index()) + " " + std::string(e.id());
        }
        const std::optional<qiven::recordlog::Corrupt> c = e.corrupt();
        return std::string(qiven::recordlog::name(e.kind())) + " " +
               std::string(c ? qiven::recordlog::name(*c) : std::string_view()) + " [" + std::string(e.detail()) + "]";
    }
    return {};
}

// p's writes on s, refused as it names, if it names a refusal.
template <class S>
void attempt(qiven::runner::Context& qiven_ctx, S& s, const Plan& p, Arena& arena) {
    if (p.refused.empty()) {
        write(s, p, arena);
    } else {
        CHECK_EQ(said([&] { write(s, p, arena); }), p.refused);
    }
}

// s's own export imported into t, a fresh store of its type, verified to
// counts and giving gave.
template <class S, class T>
void moved(qiven::runner::Context& qiven_ctx, S& s, T& t, const Plan& p, const Counts& counts,
           const std::string& gave) {
    std::vector<std::byte> pages(page_bytes);
    Arena arena(pages);
    auto source = s.export_(std::nullopt, {});
    Vector<LaneHead> rows{Chars(arena)};
    (void)t.import_(source, rows);
    CHECK(t.verify() == counts);
    CHECK_EQ(given(t, p, arena), gave);
}

// p on SQLite, verified to counts, reopened and verified to them again, its
// own export moved into a fresh file, reopened; what it gave, the same after.
inline std::string on_sqlite(qiven::runner::Context& qiven_ctx, const Plan& p, const Counts& counts) {
    std::vector<std::byte> pages(page_bytes);
    auto site = std::make_unique<Site>(Scratch{});
    const std::string path = text(site->root / "p" / "store.db");
    const std::string to = text(site->root / "q" / "store.db");
    std::string gave;
    {
        auto s = std::make_unique<Store>(opened(path, true, false, site->fake, p.limits));
        Arena arena(pages);
        attempt(qiven_ctx, *s, p, arena);
        CHECK(s->verify() == counts);
        gave = given(*s, p, arena);
        (void)s->close();
        s.reset();
    }
    auto again = std::make_unique<Store>(opened(path, false, false, site->fake));
    Arena arena(pages);
    CHECK(again->verify() == counts);
    CHECK(given(*again, p, arena) == gave);
    sqlite::WriteView view(again->connection(), Chars(arena), nullptr, test_at, sqlite::Totals{});
    String held{Chars(arena)};
    for (const Given& g : p.refused.empty() ? p.lines : std::vector<Given>{}) {
        CHECK(view.header(g.keys.id, held) && std::string_view(held) == g.line);  // parsed by none
    }
    {
        auto t = std::make_unique<Store>(opened(to, true, false, site->fake));
        moved(qiven_ctx, *again, *t, p, counts, gave);
        (void)t->close();
        t.reset();
    }
    (void)again->close();
    again.reset();
    auto t = std::make_unique<Store>(opened(to, false, false, site->fake));
    std::vector<std::byte> more(page_bytes);
    Arena last(more);
    CHECK(t->verify() == counts);
    CHECK(given(*t, p, last) == gave);
    (void)t->close();
    t.reset();
    site.reset();
    return gave;
}

}  // namespace qiven::test::plain_bed
