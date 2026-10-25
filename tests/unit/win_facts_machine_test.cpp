// win/facts' cases on this machine's facts, on the Windows adapter: facts
// of the case's scratch directory and of C:'s root, each reading as
// measured here; residual's rows against facts, the others as measured
// here; synced of a folder no client syncs and of the OneDrive variable's
// folder; each member's allocations counted; residual's allocations failed
// in turn. The platform calls failed in turn are win_facts_faults_test's.
// The values and sequences pinned are this machine's: System Protection's
// key refused, the OneDrive variable alone, no account and no sync root.
// No Windows header here (B5): the drive's cases read the registry. machine:
// they read this machine's facts, so the full and asan presets run them.

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "platform/win/facts.hpp"
#include "platform/win/windows.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace fs = std::filesystem;
namespace readings = qiven3::platform::win::readings;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::platform::Code;
using qiven3::platform::Facts;
using qiven3::platform::PlatformError;
using qiven3::platform::Reading;
using qiven3::platform::Row;
using qiven3::platform::State;
using qiven3::platform::String;
using qiven3::platform::Vector;
using qiven3::platform::win::WindowsPlatform;
using readings::Applies;

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
        root = fs::temp_directory_path() / ("qiven3-win-facts-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(root);
        root = fs::canonical(root);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    fs::path root;
};

// Pages for an arena, off the stack.
struct Pages {
    std::array<std::byte, 1U << 16U> bytes{};
};

// An environment variable through the C runtime, none where unset.
std::optional<std::wstring> variable(const wchar_t* name) {
    wchar_t* value = nullptr;
    std::size_t n = 0;
    if (_wdupenv_s(&value, &n, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<wchar_t, decltype(&std::free)> owned(value, &std::free);
    return std::wstring(owned.get());
}

std::string_view named(State state) {
    return state == State::on ? "on" : state == State::off ? "off" : state == State::locked ? "locked" : "unknown";
}

// A reading's form: a known state with no reason, unknown with one.
bool formed(const Reading& reading) { return (reading.state == State::unknown) != reading.reason.empty(); }

// The strings past the library's own 15 bytes: each one allocation.
std::size_t past_inline(std::initializer_list<std::string_view> texts) {
    std::size_t n = 0;
    for (const std::string_view t : texts) {
        n += t.size() > 15 ? 1 : 0;
    }
    return n;
}

// The facts as measured here: System Protection's key refused this token,
// unknown, admin_only; File History on where its configuration directory
// is in the local application data, else off (no policy here); BitLocker
// off.
void read_here(qiven3::test::Context& qiven3_ctx, const Facts& facts) {
    CHECK(facts.protection.state == State::unknown);
    CHECK_EQ(std::string(facts.protection.reason), std::string("admin_only"));
    const std::optional<std::wstring> local = variable(L"LOCALAPPDATA");
    REQUIRE(local.has_value());
    const bool configured = fs::is_directory(fs::path(*local) / "Microsoft/Windows/FileHistory/Configuration");
    CHECK(facts.file_history.state == (configured ? State::on : State::off));
    CHECK(facts.bitlocker.state == State::off);
}

// A row's applies and texts as residual's rules give them.
void row_is(qiven3::test::Context& qiven3_ctx, const Row& row, std::size_t i, Applies applies) {
    const readings::RowView& expected = readings::rows.at(i);
    CHECK_EQ(std::string(row.name), std::string(expected.name));
    CHECK_EQ(std::string(row.applies), std::string(readings::text(applies)));
    CHECK_EQ(std::string(row.kept), std::string(expected.kept));
    CHECK_EQ(std::string(row.how_long), std::string(expected.how_long));
    CHECK_EQ(std::string(row.owner_can), std::string(expected.owner_can));
}

}  // namespace

QIVEN3_CASE(win_facts_machine, facts_here) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    const std::array<std::string, 2> paths = {text(scratch.root), "C:\\"};
#if defined(QIVEN3_FAULTS)
    qiven3::memory::Countdown allocations;
    arena.attach(&allocations);
#endif
    for (const std::string& path : paths) {
        Facts facts(arena);
#if defined(QIVEN3_FAULTS)
        allocations.disarm();
#endif
        platform.facts(path, facts);
        std::cout << "win_facts_machine.facts_here: " << path << ": volume " << facts.volume << ", protection "
                  << named(facts.protection.state) << " (" << facts.protection.reason << "), file_history "
                  << named(facts.file_history.state) << " (" << facts.file_history.reason << "), bitlocker "
                  << named(facts.bitlocker.state) << " (" << facts.bitlocker.reason << ")\n";
        CHECK_EQ(std::string(facts.volume), path.substr(0, 2));
        CHECK(formed(facts.protection) && formed(facts.file_history) && formed(facts.bitlocker));
        read_here(qiven3_ctx, facts);
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
        CHECK_EQ(allocations.calls(), past_inline({facts.volume, facts.protection.reason, facts.file_history.reason,
                                                   facts.bitlocker.reason}));
#endif
    }
#if defined(QIVEN3_FAULTS)
    arena.attach(nullptr);
#endif
    CHECK_EQ(arena.used(), 0U);
}

QIVEN3_CASE(win_facts_machine, residual_here) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    const std::array<std::string, 2> given = {text(scratch.root), "C:\\"};
    const std::array<std::string_view, 2> paths = {given[0], given[1]};
    {
        Arena arena(pages->bytes);
        Applies restore = Applies::no;
        Applies unencrypted = Applies::no;
        State history = State::unknown;
        for (const std::string& path : given) {
            Facts facts(arena);
            platform.facts(path, facts);
            restore = readings::fold(restore, readings::kept_by(facts.protection.state));
            unencrypted = readings::fold(unencrypted, readings::unencrypted(facts.bitlocker.state));
            history = facts.file_history.state;
        }
        // pagefile, hibernation and crash_dumps as measured here:
        // ClearPageFileAtShutdown 0, HibernateEnabled 0, a LocalDumps key.
        const std::array<Applies, 6> expected = {restore,     readings::kept_by(history), Applies::yes,
                                                 Applies::no, Applies::yes,               unencrypted};
        Vector<Row> rows{Allocator<Row>(arena)};
        platform.residual(paths, rows);
        REQUIRE(rows.size() == expected.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            std::cout << "win_facts_machine.residual_here: " << rows.at(i).name << " " << rows.at(i).applies << "\n";
            row_is(qiven3_ctx, rows.at(i), i, expected.at(i));
        }
        // No path: no volume, so restore_points and encryption no.
        platform.residual({}, rows);
        REQUIRE(rows.size() == expected.size());
        row_is(qiven3_ctx, rows.at(0), 0, Applies::no);
        row_is(qiven3_ctx, rows.at(5), 5, Applies::no);
    }
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    // The six rows reserved at once, then each text past 15 bytes.
    std::size_t texts = 1;
    for (const readings::RowView& row : readings::rows) {
        texts += past_inline({row.name, row.kept, row.how_long, row.owner_can});
    }
    Arena arena(pages->bytes);
    qiven3::memory::Countdown allocations;
    arena.attach(&allocations);
    {
        Vector<Row> rows{Allocator<Row>(arena)};
        allocations.disarm();
        platform.residual(paths, rows);
        std::cout << "win_facts_machine.residual_here: allocations " << allocations.calls() << ", peak "
                  << arena.peak() << "\n";
        CHECK_EQ(allocations.calls(), texts);
        CHECK_EQ(texts, 15U);
#if defined(_MSC_VER)
        CHECK_EQ(arena.peak(), 2064U);  // 6 rows of 200 B, 14 texts by MSVC's capacity of 16n - 1
#endif
        // Each allocation failed in turn: MemoryError, no row left; then
        // the six rows.
        std::size_t emptied = 0;
        // Each run on a fresh vector, so that its reserve is counted too.
        const auto fresh = [&] {
            Vector<Row>(Allocator<Row>(arena)).swap(rows);
            platform.residual(paths, rows);
        };
        const qiven3::test::FaultRuns r = qiven3::test::run_faults<qiven3::memory::MemoryError>(
            qiven3_ctx.log, "win_facts_machine.residual", allocations, fresh,
            [&](std::size_t) {
                emptied += rows.empty() ? 1 : 0;
                return rows.empty() || rows.size() == readings::rows.size();
            });
        CHECK_EQ(r.completed_at, texts + 1);
        CHECK_EQ(r.typed, texts);
        CHECK_EQ(emptied, texts);
        CHECK_EQ(r.untyped + r.unchecked, 0U);
    }
    arena.attach(nullptr);
    CHECK_EQ(arena.used(), 0U);
#endif
}

QIVEN3_CASE(win_facts_machine, synced_here) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        String out{Allocator<char>(arena)};
        out.assign("stale");
        CHECK(!platform.synced(text(scratch.root), out));
        CHECK(out.empty());
        const std::optional<std::wstring> folder = variable(L"OneDrive");
        if (!folder.has_value()) {
            QIVEN3_SKIP("not applicable: no OneDrive variable here, so no synced folder");
        }
        const std::string cloud = text(fs::path(*folder));
        std::string upper = cloud;
        for (char& c : upper) {
            c = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        }
        for (const std::string& path : {cloud, cloud + "\\inner", upper + "\\INNER"}) {
            out.clear();
            CHECK(platform.synced(path, out));
            CHECK_EQ(std::string(out), std::string("OneDrive"));
        }
        CHECK(!platform.synced(cloud + "X", out));
        CHECK(!platform.synced(cloud + "X\\inner", out));
    }
    CHECK_EQ(arena.used(), 0U);
}
