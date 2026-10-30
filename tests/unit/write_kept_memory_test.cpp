// write/kept's fault runs over the fake platform's real files in a scratch
// store and a fake store's ids (every scope, writer, id and text invented).
// Each allocation failed in turn, the arena's and each header line's work
// arena's, is a MemoryError; each platform call failed in turn is the
// platform's io; neither ever a refusal. Over three operations: a write
// whose step 1 meets a kept file, a pending file the store holds, one it
// holds none of and a torn .tmp; the same write with its append refused;
// and the import. After each run the store is appended once at most, and a
// write with no fault settles what the run left: no pending or temporary
// name kept, every kept file whole with all its ids held, and every id the
// store holds kept once, so no fault point loses a kept file or keeps one
// torn. A refusal's removal of its pending file failing is dropped, the
// two runs that end in the refusal with the failure swallowed. The runs
// complete past the last, which counts them exactly: allocations by MSVC's
// library, libstdc++ and libc++, a write and the refused write 44, 42 and
// 37, the import 4 on each; the platform's fault points, the fake's and its
// files' beneath it, a write 24, the refused write 22 and its removal's 2,
// the import 9.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/kept_bed.hpp"

using namespace qiven3::test::kept_bed;
using qiven3::memory::MemoryError;

#if defined(QIVEN3_FAULTS)
namespace {

constexpr std::string_view writer = "implementer/s7";

// The allocations' count follows the library's growth: MSVC's,
// libstdc++'s, libc++'s.
constexpr std::size_t grown(std::size_t msvc, std::size_t libstdcxx, std::size_t libcxx) {
    static_cast<void>(msvc);
    static_cast<void>(libstdcxx);
    static_cast<void>(libcxx);
#if defined(_MSVC_STL_VERSION)
    return msvc;
#elif defined(_LIBCPP_VERSION)
    return libcxx;
#else
    return libstdcxx;
#endif
}

enum class Op : std::uint8_t { write, refused, import };

// The runs' store: its scratch directory, the fake, the entries on an
// arena of their own, and the fault run's arena on pages of its own.
struct Bed {
    Scratch scratch{fresh};
    FakePlatform fake{scratch.config()};
    std::vector<std::byte> fixed = std::vector<std::byte>(2 * mib);
    Arena arena{fixed};
    std::vector<std::byte> pages = std::vector<std::byte>(2 * mib);
    std::optional<Arena> run;
    Store store;
    Entries kept_one{arena, writer, 2, 1};
    Entries held{arena, writer, 2, 3};
    Entries none{arena, writer, 1, 5};
    Entries entries{arena, writer, 3, 6, true};
    Entries later{arena, writer, 1, 9};
    Entries imported{arena, "import", 3, 1};

    [[nodiscard]] std::string file(std::uint64_t n, const Entries& e) {
        String out{Allocator<char>(arena)};
        kept::bytes(kept::Head{n, kept::Command::write, Inline<64>(writer), 0}, e.view(), out);
        return std::string(out);
    }
    void plant(std::string_view name, std::string_view bytes) const {
        write_bytes(scratch.kept() / std::string(name), bytes);
    }
    // The store as the run starts: none for the import; for a write, a kept
    // file, a pending file held, one held none of, and a torn .tmp.
    void reset(Op op) {
        std::error_code error;
        fs::remove_all(scratch.kept(), error);
        store = Store{};
        if (op == Op::import) {
            return;
        }
        fs::create_directories(scratch.kept());
        plant("0000000001.jsonl", file(1, kept_one));
        plant("0000000002.pending", file(2, held));
        plant("0000000003.pending", file(3, none));
        plant("0000000004.tmp", "{\"kept\":\"qiven3-ke");
        for (const Entries* e : {&kept_one, &held}) {
            for (const Built& b : e->built) {
                store.ids.insert(std::string(b.id.view()));
            }
        }
    }
    // The run's operation on the run's arena, countdown attached to it.
    void operate(Op op, qiven3::memory::Countdown* countdown) {
        run.emplace(pages);
        run->attach(countdown);
        if (op == Op::import) {
            kept::import(fake, scratch.store(), imported.view(), appends(store, imported), *run);
            return;
        }
        if (op == Op::refused) {
            kept::write(fake, scratch.store(), kept::Command::write, writer, entries.view(), store,
                        [] { throw Refusal{rc::Code::head_conflict, {}}; }, *run, hooks);
            return;
        }
        kept::write(fake, scratch.store(), kept::Command::write, writer, entries.view(), store,
                    appends(store, entries), *run, hooks);
    }
    // After a run: appended once at most; then, for a write, a write with no
    // fault settles it, every name left kept and whole, its ids all held,
    // each id the store holds kept once; for the import, no settle step, a
    // kept or pending 0000000001 whole, kept only once appended.
    [[nodiscard]] bool settled(Op op) {
        try {
            return settles(op);
        } catch (...) {
            return false;
        }
    }
    [[nodiscard]] bool settles(Op op) {
        const int appended = store.appends;
        if (appended > 1) {
            return false;
        }
        if (op != Op::import) {
            run.emplace(pages);
            if (kept::write(fake, scratch.store(), kept::Command::write, writer, later.view(), store,
                            appends(store, later), *run) == 0) {
                return false;
            }
        }
        if (!fs::exists(scratch.kept())) {
            return op == Op::import && appended == 0;  // the import failed making kept/
        }
        std::set<std::string> ids;
        std::size_t lines = 0;
        std::size_t files = 0;
        for (const auto& e : fs::directory_iterator(scratch.kept())) {
            const std::string name = text(e.path().filename());
            const std::optional<kept::Name> got = kept::parse_name(name);
            if (!got || got->form == kept::Form::tmp || got->form == kept::Form::temporary) {
                if (op != Op::import && got) {
                    return false;
                }
                continue;
            }
            if (got->form == kept::Form::pending && op != Op::import) {
                return false;
            }
            const std::string bytes = bytes_of(e.path());
            run.emplace(pages);
            kept::Head head;
            Vector<kept::LineView> parsed{Allocator<kept::LineView>(*run)};
            kept::parse(bytes, name, *run, head, parsed);
            ++files;
            bool all = true;
            bool any = false;
            for (const kept::LineView& line : parsed) {
                const bool holds = store(line.id.view());
                all = all && holds;
                any = any || holds;
                ids.insert(std::string(line.id.view()));
                ++lines;
            }
            if (got->form == kept::Form::kept ? !all : (any != (appended == 1) || (any && !all))) {
                return false;
            }
        }
        if (op == Op::import) {
            return files <= 1 && (appended == 0 || files == 1);
        }
        const std::size_t want = op == Op::write ? 4U - (appended == 0 ? 1U : 0U) : 3U;
        return files == want && lines == ids.size() && ids.size() == store.ids.size();
    }
    kept::Hooks hooks{};
};

struct Case {
    const char* name;
    Op op;
    std::size_t points;  // its fault points failed, each typed
};

}  // namespace
#endif

QIVEN3_CASE(write_kept_memory, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const auto bed = std::make_unique<Bed>();
    qiven3::memory::Countdown countdown;
    bed->hooks.work = &countdown;
    for (const Case c : {Case{"a write", Op::write, grown(44, 42, 37)},
                         Case{"a refused write", Op::refused, grown(44, 42, 37)},
                         Case{"the import", Op::import, grown(4, 4, 4)}}) {
        std::size_t refused = 0;
        const auto op = [&] {
            bed->reset(c.op);
            try {
                bed->operate(c.op, &countdown);
            } catch (const Refusal& r) {
                refused += c.op == Op::refused && r.code == rc::Code::head_conflict ? 1U : 0U;
            }
        };
        const qiven3::test::FaultRuns runs = qiven3::test::run_faults<MemoryError>(
            qiven3_ctx.log, "write_kept.write", countdown, op, [&](std::size_t) { return bed->settled(c.op); });
        std::cout << "write_kept_memory.faults: " << c.name << "'s allocations " << runs.typed << ", completed at "
                  << runs.completed_at << ", untyped " << runs.untyped << ", unchecked " << runs.unchecked << "\n";
        CHECK_EQ(runs.typed, c.points);
        CHECK_EQ(runs.completed_at, c.points + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK_EQ(refused, c.op == Op::refused ? 1U : 0U);
    }
    bed->run.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}

QIVEN3_CASE(write_kept_memory, platform_faults) {
#if defined(QIVEN3_FAULTS)
    const auto bed = std::make_unique<Bed>();
    qiven3::memory::Countdown countdown;
    // Each call's fault points: the fake's and its files', the refusal's
    // removal's two dropped.
    for (const Case c : {Case{"a write", Op::write, 24}, Case{"a refused write", Op::refused, 22},
                         Case{"the import", Op::import, 9}}) {
        std::size_t refused = 0;
        bool io = true;
        const auto op = [&] {
            bed->reset(c.op);
            bed->fake.attach(&countdown);
            try {
                bed->operate(c.op, nullptr);
            } catch (const Refusal& r) {
                refused += c.op == Op::refused && r.code == rc::Code::head_conflict ? 1U : 0U;
            } catch (const PlatformError& e) {
                io = io && e.code == Code::io;
                bed->fake.attach(nullptr);
                throw;
            }
            bed->fake.attach(nullptr);
        };
        const qiven3::test::FaultRuns runs = qiven3::test::run_faults<PlatformError>(
            qiven3_ctx.log, "write_kept.platform", countdown, op, [&](std::size_t) { return bed->settled(c.op); });
        std::cout << "write_kept_memory.platform_faults: " << c.name << "'s calls failed " << runs.typed
                  << ", completed at " << runs.completed_at << ", untyped " << runs.untyped << ", unchecked "
                  << runs.unchecked << ", refused " << refused << "\n";
        const std::size_t dropped = c.op == Op::refused ? 2U : 0U;
        CHECK(io);
        CHECK_EQ(runs.typed, c.points);
        CHECK_EQ(runs.completed_at, c.points + dropped + 1);
        CHECK_EQ(runs.untyped, dropped);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK_EQ(refused, c.op == Op::refused ? dropped + 1 : 0U);
    }
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS");
#endif
}
