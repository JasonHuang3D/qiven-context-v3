#pragma once

// store/exchange's budgets, counts and fault runs, on the fake, over a scratch directory of the case's own: a
// source store with two lanes and a forget (A, B, D and ACC on implementer's lane, C and M1 on reviewer's, B
// forgotten by E), its lines held as an import's source, and an empty store to import them into. The steps:
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
// and the caller's heads cleared; an import that completed undone.

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

#include "checked/stores.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/exchange.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"

namespace qiven3::test::exchange_bed {

using stage_book::Arena, stage_book::Book, stage_book::Chars, stage_book::Fake, stage_book::id_of;
using stage_book::Slot, stage_book::Stored, stage_book::views;
namespace fs = std::filesystem;
namespace sqlite = qiven3::store::sqlite;
using qiven3::memory::Vector;
using qiven3::store::AfterView;
using qiven3::store::Counts;
using qiven3::store::GuardView;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using Store = qiven3::checked::SQLiteStore<Fake>;
using Export = qiven3::checked::Export<sqlite::SQLiteExport<Fake>>;
using Engine = qiven3::store::Engine<Fake>;

inline constexpr std::string_view qiven_scope = "project:qiven";
inline constexpr std::string_view implementer_lane = "implementer";
inline constexpr std::string_view reviewer_lane = "reviewer";
inline constexpr std::size_t call_bytes = std::size_t{2} << 20U;
inline constexpr std::string_view unfaulted =
    "not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers";

inline std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// A source of lines held: the heads given, then each line.
struct Held {
    std::vector<LaneHead> given;
    std::vector<std::pair<std::string, std::optional<std::string>>> lines;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines.size()) {
            return false;
        }
        line.header.assign(lines[at].first);
        line.has_body = lines[at].second.has_value();
        line.body.assign(lines[at].second.value_or(std::string()));
        ++at;
        return true;
    }
};

// The scratch, the fake over it, the fixtures, the engine heap, the source
// and the store to import into, the held lines, the export in hand and the
// caller's arena; on the heap.
struct Bed {
    Bed()
        : root(fresh_root()), fake(config(root)), book(fake), engine(std::make_unique<Engine>(fake)),
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
            held.lines.emplace_back(std::string(line.header),
                                    line.has_body ? std::optional<std::string>(std::string(line.body)) : std::nullopt);
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
        const std::vector<Stored> mine = {book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")};
        const std::vector<Stored> theirs = {book.stored("C"), book.stored("M1")};
        const std::vector<Stored> forgets = {book.stored("E")};
        (void)source->append(qiven_scope, implementer_lane, std::nullopt, views(mine));
        (void)source->append(qiven_scope, reviewer_lane, std::nullopt, views(theirs));
        const std::string acc = id_of(mine[3]);
        const std::string m1 = id_of(theirs[1]);
        const std::vector<GuardView> guard = {GuardView{qiven_scope, implementer_lane, acc},
                                              GuardView{qiven_scope, reviewer_lane, m1}};
        std::vector<std::byte> pages(std::size_t{1} << 16U);
        Arena arena(pages);
        Vector<Id> erased{Chars(arena)};
        (void)source->forget(qiven_scope, implementer_lane, acc, views(forgets), guard, erased);
    }

    // A fresh caller's arena.
    Chars caller() {
        call.emplace(call_pages);
#if defined(QIVEN3_FAULTS)
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
#if defined(QIVEN3_FAULTS)
        target->attach(attached);
#endif
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
    Book book;
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
#if defined(QIVEN3_FAULTS)
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
inline constexpr std::string_view next_ids = "7f5af8 b  014325 b  e84b77 -  e721d4 b  cdc1cc b  ";

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
                           const std::string_view h(line.header);
                           ids.append(h.substr(h.find("\"id\":") + 13, 6)).append(line.has_body ? " b" : " -");
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
