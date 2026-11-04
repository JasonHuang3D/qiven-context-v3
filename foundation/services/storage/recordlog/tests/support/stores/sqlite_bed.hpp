#pragma once

// store/sqlite's budgets, counts and fault runs, on the fake, over a scratch directory of the case's own and one
// course of steps on a store of plain records: created; A, B and D appended, then ACC; head, heads, header, body and
// forgotten of A; a scan read whole; size; B forgotten by E under the guard; purge; a second object opened and
// closed, read-write, then read-only; the guard alone; A's line, B's targets, the notes keyed and their lent walk
// stopped at the second; each case's course ends verified, then reopened read-only and
// verified. budget: each step's counted peak (the store's peak(), a scan's) within its budget, 1 MiB plus 4 times
// the lines given, and the engine's peak, its heap's and its page cache's slots', on a fresh engine heap for each
// step, the store reopened on it first, within the engine's budget for each connection the step holds.
// counts: each step's allocations, by allocator (the store's arenas, the engine heap, the caller's arena), its
// engine calls the wrapper maps and its platform calls, exactly. faults: the steps but the create and the forget,
// which store_sqlite_parts_test.cpp runs in parts, each within 30 s on every job; a step outside the case's run
// once, unfaulted: each step run with each of these failed in turn: each run MemoryError, PlatformError or the
// failed engine call's StoreLocked io, or an allocation the engine absorbs, the step completing; after a refused
// run the caller's arena and the fake's pages given back, no transaction left open, the store as its reads give it
// unchanged; a run the engine absorbed, its result as the step's; a write so completed, or a create, undone from
// the files kept before it. The engine heap given back whole at its end, which fails fast otherwise.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "qiven/memory/arena.hpp"
#include "qiven/memory/fail.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/platform/platform.hpp"
#include "qiven/recordlog/contract.hpp"
#include "qiven/recordlog/sqlite_steps.hpp"
#include "qiven/recordlog/sqlite_store.hpp"
#include "qiven/platform/fake/fake_platform.hpp"
#include "support/stores/records.hpp"

namespace qiven::test::sqlite_bed {

using records::chain, records::forget, records::id_of, records::Rec, records::views;
namespace fs = std::filesystem;
namespace sqlite = qiven::recordlog::sqlite;
using qiven::memory::String;
using qiven::memory::Vector;
using qiven::recordlog::EntryView;
using qiven::recordlog::GuardView;
using qiven::recordlog::Heads;
using qiven::recordlog::LaneHead;
using qiven::recordlog::RangeView;
using qiven::recordlog::Sizes;
using qiven::recordlog::Written;
using Chars = qiven::memory::Allocator<char>;
using Fake = qiven::platform::fake::FakePlatform;
using Store = qiven::recordlog::SQLiteStore<Fake>;
using Engine = qiven::recordlog::Engine<Fake>;

inline constexpr std::string_view group_scope = "group:primary";
inline constexpr std::string_view lane_one = "lane-one";
inline constexpr std::size_t call_bytes = std::size_t{2} << 20U;
// The ids of the lane's chain.
inline constexpr std::initializer_list<std::string_view> lane_ids = {"A", "B", "D", "ACC"};
inline constexpr std::string_view unfaulted =
    "not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers";

inline std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

inline std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The scratch, the fake over it, the fixtures, the engine heap and the
// store, its steps' lines and the caller's arena; on the heap.
// What a Bed is made from: a fresh scratch each time.
struct Scratch {};
struct Bed {
    explicit Bed(Scratch /*fresh*/)
        : root(fresh_root()), fake(config(root)), lane(chain(group_scope, lane_one, lane_ids)),
          engine(std::make_unique<Engine>(fake)), path(text(root / "s" / "store.db")), first{lane[0], lane[1], lane[2]},
          acc{lane[3]}, e{forget("E", group_scope, lane_one, 5, "ACC", "B")}, first_lines(views(first)),
          acc_lines(views(acc)), e_lines(views(e)), a_id(id_of(first[0])), d_id(id_of(first[2])), acc_id(id_of(acc[0])),
          guard{GuardView{group_scope, lane_one, acc_id}}, call_pages(call_bytes) {}
    Bed(const Bed&) = delete;
    Bed& operator=(const Bed&) = delete;
    Bed(Bed&&) = delete;
    Bed& operator=(Bed&&) = delete;
    ~Bed() {
        store.reset();
        engine.reset();
        try {
            std::error_code error;
            fs::remove_all(root, error);
        } catch (...) {
            qiven::memory::fail_fast("sqlite_bed: the scratch directory's removal threw");
        }
    }

    static fs::path fresh_root() {
        // On the heap: libstdc++'s random_device is some 5,000 bytes.
        std::vector<std::random_device> device(1);
        std::random_device& draw = device.front();
        fs::path out = fs::temp_directory_path() / ("qiven-sqlite-m-" + std::to_string(draw()) +
                                                    std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    static qiven::platform::fake::Config config(const fs::path& root) {
        qiven::platform::fake::Config c;
        c.root = text(root);
        c.seed = 7;
        return c;
    }

    // A fresh caller's arena.
    Chars caller() {
        call.emplace(call_pages);
#if defined(QIVEN_FAULTS)
        call->attach(call_countdown);
#endif
        return Chars(*call);
    }
    void reopen() { store = std::make_unique<Store>(Store::open(path, false, false, fake,
                                                                records::suite_limits, records::suite_identity)); }
    // A fresh engine heap, the store closed before and reopened after.
    void fresh_engine(bool open) {
        store.reset();
        engine.reset();
        engine = std::make_unique<Engine>(fake);
        if (open) {
            reopen();
        }
    }
    // The store's files kept, its object closed and made again; then put
    // back, as they were kept.
    void keep() {
        store.reset();
        kept_db = bytes_of(path);
        reopen();
    }
    void put_back() {
        store.reset();
        std::error_code error;
        fs::remove(path + "-wal", error);
        fs::remove(path + "-shm", error);
        std::ofstream(path, std::ios::binary) << kept_db;  // out added by the stream: "wb", truncating
        reopen();
    }

    fs::path root;
    Fake fake;
    std::vector<Rec> lane;
    std::unique_ptr<Engine> engine;  // before the store, which ends first
    std::string path;
    std::unique_ptr<Store> store;
    std::vector<Rec> first;
    std::vector<Rec> acc;
    std::vector<Rec> e;
    std::vector<EntryView> first_lines;
    std::vector<EntryView> acc_lines;
    std::vector<EntryView> e_lines;
    std::string a_id;
    std::string d_id;
    std::string acc_id;
    std::vector<GuardView> guard;
    std::vector<std::byte> call_pages;
    std::optional<qiven::memory::Arena> call;
    std::string kept_db;
#if defined(QIVEN_FAULTS)
    qiven::memory::Countdown* call_countdown = nullptr;
#endif
};

// A step's allocations by allocator, its engine calls and platform calls.
struct Count {
    std::size_t arenas = 0;
    std::size_t heap = 0;
    std::size_t calls = 0;
    std::size_t caller = 0;
    std::size_t platform = 0;
    friend bool operator==(const Count&, const Count&) = default;
};

// What a step gave: its counted peak, its caller's arena's, its result.
struct Ran {
    std::size_t store = 0;
    std::size_t caller = 0;
    std::string result;
};
enum class Sort : std::uint8_t { create, write, read };
struct Step {
    std::string name;
    std::size_t budget;  // 0: the lines'
    Sort sort;
    std::size_t connections;
    Count count;
    std::function<Ran(Bed&, const Chars&)> run;
};

inline std::string written(const Written& w) {
    return std::string(w.head.id.view()) + "@" + std::to_string(w.head.seq) + " " + std::to_string(w.volume.count);
}

inline void add_steps(std::vector<Step>& out);
inline void add_reads(std::vector<Step>& out);
inline void add_calls(std::vector<Step>& out);

inline std::vector<Step> steps() {
    std::vector<Step> out;
    add_steps(out);
    add_reads(out);
    return out;
}

inline void add_steps(std::vector<Step>& out) {
    constexpr std::size_t base = qiven::recordlog::operation_base;
    out.push_back({"create", base, Sort::create, 1, {}, [](Bed& b, const Chars&) {
                       b.store = std::make_unique<Store>(Store::open(b.path, true, false, b.fake,
                                                                     records::suite_limits, records::suite_identity));
                       return Ran{b.store->peak(), 0, "made"};
                   }});
    out.push_back({"append", 0, Sort::write, 1, {}, [](Bed& b, const Chars&) {
                       const Written w =
                           b.store->append(group_scope, lane_one, std::nullopt, b.first_lines);
                       return Ran{b.store->peak(), 0, written(w)};
                   }});
    out.push_back({"append_acc", 0, Sort::write, 1, {}, [](Bed& b, const Chars&) {
                       const Written w = b.store->append(group_scope, lane_one, b.d_id, b.acc_lines);
                       return Ran{b.store->peak(), 0, written(w)};
                   }});
}

inline void add_reads(std::vector<Step>& out) {
    constexpr std::size_t base = qiven::recordlog::operation_base;
    out.push_back({"head", base, Sort::read, 1, {}, [](Bed& b, const Chars&) {
                       const auto h = b.store->head(group_scope, lane_one);
                       return Ran{0, 0, h ? std::string(h->id.view()) : "none"};
                   }});
    out.push_back({"heads", base, Sort::read, 1, {}, [](Bed& b, const Chars& c) {
                       Vector<LaneHead> rows(c);
                       const Heads hs = b.store->heads(std::nullopt, rows);
                       return Ran{0, b.call->peak(), std::to_string(rows.size()) + " " + std::to_string(hs.forgets)};
                   }});
    out.push_back({"header", base, Sort::read, 1, {}, [](Bed& b, const Chars& c) {
                       String line(c);
                       b.store->header(b.a_id, line);
                       return Ran{b.store->peak(), b.call->peak(), std::string(line.begin(), line.end())};
                   }});
    out.push_back({"body", base, Sort::read, 1, {}, [](Bed& b, const Chars& c) {
                       String record(c);
                       const bool held = b.store->body(b.a_id, record);
                       return Ran{b.store->peak(), b.call->peak(), std::to_string(held) + std::to_string(record.size())};
                   }});
    out.push_back({"forgotten", base, Sort::read, 1, {}, [](Bed& b, const Chars&) {
                       return Ran{0, 0, std::to_string(b.store->forgotten(b.a_id))};
                   }});
    out.push_back({"scan", base, Sort::read, 1, {}, [](Bed& b, const Chars& c) {
                       auto scan = b.store->scan(RangeView{});
                       qiven::recordlog::Record h(c);
                       std::string ids;
                       while (scan.next(h)) {
                           ids.append(h.id.view().substr(0, 8)).append(" ");
                       }
                       return Ran{scan.peak(), b.call->peak(), ids};
                   }});
    out.push_back({"size", base, Sort::read, 1, {}, [](Bed& b, const Chars&) {
                       const auto sizes = std::make_unique<Sizes>(b.store->size());
                       return Ran{0, 0, std::to_string(sizes->volume.size) + " " + std::to_string(sizes->volume.count)};
                   }});
    out.push_back({"forget", 0, Sort::write, 1, {}, [](Bed& b, const Chars& c) {
                       Vector<qiven::recordlog::Id> erased(c);
                       const Written w =
                           b.store->forget(group_scope, lane_one, b.acc_id, b.e_lines, b.guard, erased);
                       return Ran{b.store->peak(), b.call->peak(), written(w) + " " + std::to_string(erased.size())};
                   }});
    out.push_back({"purge", base, Sort::read, 1, {}, [](Bed& b, const Chars&) {
                       return Ran{0, 0, std::to_string(b.store->purge().done)};
                   }});
    out.push_back({"open", base, Sort::read, 2, {}, [](Bed& b, const Chars&) {
                       Store s = Store::open(b.path, false, false, b.fake,
                                             records::suite_limits, records::suite_identity);
                       const bool done = s.close().done;
                       return Ran{s.peak(), 0, std::to_string(done)};
                   }});
    out.push_back({"read_only", base, Sort::read, 2, {}, [](Bed& b, const Chars&) {
                       Store s = Store::open(b.path, false, true, b.fake,
                                             records::suite_limits, records::suite_identity);
                       const bool done = s.close().done;
                       return Ran{s.peak(), 0, std::to_string(done)};
                   }});
    out.push_back({"guard", base, Sort::read, 0, {}, [](Bed& b, const Chars&) {
                       Store::guard(b.path, b.fake);
                       return Ran{0, 0, "guarded"};
                   }});
    add_calls(out);
}

// The generic calls, after the course's forget: A's line, B's targets (E),
// the scope's notes with no target, and their lent walk.
inline void add_calls(std::vector<Step>& out) {
    constexpr std::size_t base = qiven::recordlog::operation_base;
    const auto ids = [](const Vector<qiven::recordlog::Id>& list) {
        std::string s;
        for (const qiven::recordlog::Id& id : list) {
            s.append(id.view().substr(0, 8)).append(" ");
        }
        return s;
    };
    out.push_back({"line", base, Sort::read, 1, {}, [](Bed& b, const Chars& c) {
                       String line(c);
                       const bool held = b.store->line(b.a_id, line);
                       return Ran{b.store->peak(), b.call->peak(), std::to_string(held) + std::to_string(line.size())};
                   }});
    out.push_back({"targets", base, Sort::read, 1, {}, [ids](Bed& b, const Chars& c) {
                       Vector<qiven::recordlog::Id> list(c);
                       b.store->targets(id_of(b.first[1]), group_scope, list);
                       return Ran{b.store->peak(), b.call->peak(), ids(list)};
                   }});
    out.push_back({"keyed", base, Sort::read, 1, {}, [ids](Bed& b, const Chars& c) {
                       Vector<qiven::recordlog::Id> list(c);
                       b.store->keyed(group_scope, "note", list);
                       return Ran{b.store->peak(), b.call->peak(), ids(list)};
                   }});
    out.push_back({"each_keyed", base, Sort::read, 1, {}, [](Bed& b, const Chars&) {
                       std::size_t n = 0;
                       const bool stopped = b.store->each_keyed(group_scope, "note", [&n](const qiven::recordlog::Id&) {
                           return ++n == 2;
                       });
                       return Ran{b.store->peak(), 0, std::to_string(stopped) + std::to_string(n)};
                   }});
}

// A step's budget: given, else its lines'.
inline std::size_t budget(const Bed& b, const Step& s) {
    if (s.budget != 0) {
        return s.budget;
    }
    const std::vector<EntryView>& lines =
        s.name == "append" ? b.first_lines : s.name == "append_acc" ? b.acc_lines : b.e_lines;
    return qiven::recordlog::operation_budget(qiven::recordlog::given(lines).value_or(0)).value_or(0);
}

}  // namespace qiven::test::sqlite_bed
