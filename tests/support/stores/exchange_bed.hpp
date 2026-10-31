#pragma once

// store/exchange's budgets, counts and fault runs, on the fake, over a scratch directory of the case's own, the
// plain SQLite store over plain records (records.hpp): a source store with two lanes and a forget (A, B, D and
// ACC on implementer's lane, C and M1 on reviewer's, B forgotten by E), its lines held with their keys as an
// import's source, and an empty store to import them into. The steps:
// export, an export of the scope set project:* from seq 2 of implementer's lane made and closed; heads, its
// heads read; next, its lines read whole; import, the held lines into the empty store; verify, of the source.
// budget: each step's counted peak within 1 MiB (an export's pages 1 MiB plus 57 bytes a scope and 98 a seq),
// and the engine's peak, its heap's and its page cache's slots', on a fresh engine heap for each step, within
// the engine's budget for each connection the step holds. counts: each step's allocations, by allocator (the
// store's and the export's arenas, the engine heap, the caller's arena), its engine calls the wrapper maps and
// its platform calls, exactly. faults_export, faults_import and faults_verify: each step run with each of these
// failed in turn: each run MemoryError, PlatformError or the failed engine call's StoreLocked io, or a failure
// the engine absorbs, the step completing with its own result; after each run the export closed, the caller's
// arena and the fake's pages given back (an export's engine heap too), no transaction open, the source as its
// reads give it unchanged; after an import refused its store empty (verify's counts none), its -wal 0 bytes
// and the caller's heads cleared; an import that completed undone. An import, refused or not, verified over a
// reopen of its store.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/exchange.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/stores/records.hpp"

namespace qiven3::test::exchange_bed {

using records::Rec, records::views;
namespace fs = std::filesystem;
namespace sqlite = qiven3::store::sqlite;
using Fake = qiven3::platform::fake::FakePlatform;
using Chars = qiven3::memory::Allocator<char>;
using qiven3::memory::Arena;
using qiven3::memory::Vector;
using qiven3::store::AfterView;
using qiven3::store::Counts;
using qiven3::store::GuardView;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using Store = qiven3::store::SQLiteStore<Fake>;
using Export = sqlite::SQLiteExport<Fake>;
using Engine = qiven3::store::Engine<Fake>;

inline constexpr std::string_view qiven_scope = "project:qiven";
inline constexpr std::string_view implementer_lane = "implementer";
inline constexpr std::string_view reviewer_lane = "reviewer";
inline constexpr std::size_t call_bytes = std::size_t{2} << 20U;
inline constexpr Counts source_counts{7, 5, 1, 2};  // verify's, of the source and an import of it
inline constexpr std::string_view unfaulted =
    "not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers";

inline std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// A source of lines held: the heads given, then each line with its keys.
struct Held {
    std::vector<LaneHead> given;
    std::vector<Rec> lines;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines.size()) {
            return false;
        }
        const Rec& r = lines[at++];
        line.header.assign(r.line);
        line.has_body = r.record.has_value();
        line.body.assign(r.record.value_or(std::string()));
        return line.keys.assign(view(r.keys));
    }
};

// The scratch, the fake over it, the engine heap, the source and the store
// to import into, the held lines, the export in hand and the caller's
// arena; on the heap.
struct Bed {
    Bed()
        : root(fresh_root()), fake(config(root)), engine(std::make_unique<Engine>(fake)),
          from(text(root / "from" / "store.db")), into(text(root / "into" / "store.db")), call_pages(call_bytes) {
        source = std::make_unique<Store>(Store::open(from, true, false, fake));
        fill();
        target = std::make_unique<Store>(Store::open(into, true, false, fake));
        const Chars c = caller();
        auto whole = std::make_unique<Export>(source->export_(std::nullopt, {}));
        Vector<LaneHead> rows(c);
        (void)whole->heads(rows);
        held.given.assign(rows.begin(), rows.end());
        std::vector<std::byte> pages(std::size_t{1} << 18U);
        for (;;) {
            Arena arena(pages);
            Line line(arena);
            if (!whole->next(line)) {
                break;
            }
            held.lines.push_back(records::owned(line));
        }
    }
    Bed(const Bed&) = delete;
    Bed& operator=(const Bed&) = delete;
    Bed(Bed&&) = delete;
    Bed& operator=(Bed&&) = delete;
    ~Bed() {
        x.reset();
        source.reset();
        target.reset();
        engine.reset();
        std::error_code error;
        fs::remove_all(root, error);
    }

    static fs::path fresh_root() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        fs::path out = fs::temp_directory_path() / ("qiven3-exchange-m-" + std::to_string(draw()) +
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

    void fill() {
        const std::vector<Rec> mine = records::chain(qiven_scope, implementer_lane, {"A", "B", "D", "ACC"});
        const std::vector<Rec> theirs = records::chain(qiven_scope, reviewer_lane, {"C", "M1"});
        const std::vector<Rec> forgets = {records::forget("E", qiven_scope, implementer_lane, 5, "ACC", "B")};
        (void)source->append(qiven_scope, implementer_lane, std::nullopt, views(mine));
        (void)source->append(qiven_scope, reviewer_lane, std::nullopt, views(theirs));
        const std::vector<GuardView> guard = {GuardView{qiven_scope, implementer_lane, "ACC"},
                                              GuardView{qiven_scope, reviewer_lane, "M1"}};
        std::vector<std::byte> pages(std::size_t{1} << 16U);
        Arena arena(pages);
        Vector<Id> erased{Chars(arena)};
        (void)source->forget(qiven_scope, implementer_lane, "ACC", views(forgets), guard, erased);
    }

    // A fresh caller's arena.
    Chars caller() {
        call.emplace(call_pages);
#if defined(QIVEN_FAULTS)
        call->attach(call_countdown);
#endif
        return Chars(*call);
    }
    // The export of the step's set and seq, made.
    void make_export() { x = std::make_unique<Export>(source->export_(scopes, after)); }
    // The store to import into, emptied: its files removed, made again.
    void empty_target() {
        target.reset();
        std::error_code error;
        fs::remove_all(root / "into", error);
        target = std::make_unique<Store>(Store::open(into, true, false, fake));
#if defined(QIVEN_FAULTS)
        target->attach(attached);
#endif
    }
    // The store imported into closed and opened again: its verify's counts.
    Counts reopened_target() {
        target.reset();
        target = std::make_unique<Store>(Store::open(into, false, false, fake));
#if defined(QIVEN_FAULTS)
        target->attach(attached);
#endif
        return target->verify();
    }
    // A fresh engine heap, the stores closed before; the source or the
    // target reopened after.
    void fresh_engine(bool open_source, bool open_target) {
        x.reset();
        source.reset();
        target.reset();
        engine.reset();
        engine = std::make_unique<Engine>(fake);
        if (open_source) {
            source = std::make_unique<Store>(Store::open(from, false, false, fake));
        }
        if (open_target) {
            target = std::make_unique<Store>(Store::open(into, false, false, fake));
        }
    }

    fs::path root;
    Fake fake;
    std::unique_ptr<Engine> engine;  // before the stores, which end first
    std::string from;
    std::string into;
    std::unique_ptr<Store> source;
    std::unique_ptr<Store> target;
    std::unique_ptr<Export> x;
    Held held;
    static constexpr std::array<std::string_view, 1> set = {"project:*"};
    qiven3::store::Scopes scopes{set};
    std::vector<AfterView> after = {AfterView{qiven_scope, implementer_lane, 2}};
    std::vector<std::byte> call_pages;
    std::optional<Arena> call;
    std::size_t heads_left = 0;  // the caller's heads after a refused import
#if defined(QIVEN_FAULTS)
    qiven3::memory::Countdown* call_countdown = nullptr;
    qiven3::memory::Countdown* attached = nullptr;  // the stores' arenas'
#endif
};

// What a step gave: its counted peak, its caller's arena's, its result.
struct Ran {
    std::size_t own = 0;
    std::size_t caller = 0;
    std::string result;
};
enum class Side : std::uint8_t { exporting, importing, verifying };
struct Step {
    std::string name;
    Side side;
    bool made;              // an export made before it
    std::string want;       // its result
    std::function<Ran(Bed&, const Chars&)> run;
};
inline constexpr std::string_view next_ids = "D b  ACC b  E -  C b  M1 b  ";

inline std::vector<Step> steps() {
    std::vector<Step> out;
    out.push_back({"export", Side::exporting, false, "made", [](Bed& b, const Chars&) {
                       b.make_export();
                       const std::size_t peak = b.x->peak();
                       b.x->close();
                       return Ran{peak, 0, "made"};
                   }});
    out.push_back({"heads", Side::exporting, true, "2 1 7", [](Bed& b, const Chars& c) {
                       Vector<LaneHead> rows(c);
                       const qiven3::store::Snapshot s = b.x->heads(rows);
                       return Ran{0, b.call->peak(),
                                  std::to_string(rows.size()) + " " + std::to_string(s.forgets) + " " +
                                      std::to_string(s.volume.count)};
                   }});
    out.push_back({"next", Side::exporting, true, std::string(next_ids), [](Bed& b, const Chars&) {
                       std::string ids;
                       std::size_t most = 0;
                       for (;;) {
                           (void)b.caller();
                           Line line(*b.call);
                           if (!b.x->next(line)) {
                               break;
                           }
                           most = std::max(most, b.call->peak());
                           ids.append(line.keys.id.view()).append(line.has_body ? " b" : " -");
                           ids.append(line.forgotten ? "f " : "  ");
                       }
                       return Ran{b.x->peak(), most, ids};
                   }});
    out.push_back({"import", Side::importing, false, "7 2", [](Bed& b, const Chars& c) {
                       b.held.at = 0;
                       Vector<LaneHead> heads(c);
                       b.heads_left = 0;
                       try {
                           const qiven3::store::Imported i = b.target->import_(b.held, heads);
                           return Ran{b.target->peak(), b.call->peak(),
                                      std::to_string(i.counts.entries) + " " + std::to_string(heads.size())};
                       } catch (...) {
                           b.heads_left = heads.size();
                           throw;
                       }
                   }});
    out.push_back({"verify", Side::verifying, false, "7 5 1 2", [](Bed& b, const Chars&) {
                       const Counts n = b.source->verify();
                       return Ran{b.source->peak(), 0,
                                  std::to_string(n.entries) + " " + std::to_string(n.bodies) + " " +
                                      std::to_string(n.forgets) + " " + std::to_string(n.lanes)};
                   }});
    return out;
}

}  // namespace qiven3::test::exchange_bed
