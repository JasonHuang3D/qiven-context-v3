// win/facts' cases that read no machine fact, on the Windows adapter: the
// rules platform/win/facts.hpp states, evaluated by the compiler; an
// HRESULT's names and the error table's names win/facts added; System
// Protection's key unopened, unknown with its reason; each
// member's refusal of a path before any read, its out cleared; and, under
// QIVEN_FAULTS, each member's first call failed, a typed error before any
// machine fact is read. The reads of this machine are
// win_facts_machine_test.cpp's.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "memory/arena.hpp"
#include "platform/win/facts.hpp"
#include "platform/win/windows.hpp"
#include "runner/runner.hpp"

namespace {

namespace readings = qiven3::platform::win::readings;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::platform::Code;
using qiven3::platform::Detail;
using qiven3::platform::Facts;
using qiven3::platform::PlatformError;
using qiven3::platform::Row;
using qiven3::platform::State;
using qiven3::platform::String;
using qiven3::platform::Vector;
using qiven3::platform::win::WindowsPlatform;
using readings::Applies;

// BitLocker's values: 2 off; 6 locked; 1, 3, 4, 5 and 8 on; another unknown.
static_assert(readings::bitlocker(2) == State::off);
static_assert(readings::bitlocker(6) == State::locked);
static_assert(readings::bitlocker(1) == State::on && readings::bitlocker(3) == State::on &&
              readings::bitlocker(4) == State::on && readings::bitlocker(5) == State::on &&
              readings::bitlocker(8) == State::on);
static_assert(readings::bitlocker(0) == State::unknown && readings::bitlocker(7) == State::unknown &&
              readings::bitlocker(9) == State::unknown && readings::bitlocker(0xFFFFFFFFU) == State::unknown);

// applies over the paths: yes where any is, else unknown where any is, else
// no; the same either way round.
constexpr bool folds() {
    constexpr std::array<Applies, 3> all = {Applies::no, Applies::yes, Applies::unknown};
    constexpr std::array<std::array<Applies, 3>, 3> expected = {{
        {Applies::no, Applies::yes, Applies::unknown},
        {Applies::yes, Applies::yes, Applies::yes},
        {Applies::unknown, Applies::yes, Applies::unknown},
    }};
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = 0; j < all.size(); ++j) {
            if (readings::fold(all.at(i), all.at(j)) != expected.at(i).at(j) ||
                readings::fold(all.at(j), all.at(i)) != expected.at(i).at(j)) {
                return false;
            }
        }
    }
    return true;
}
static_assert(folds());
static_assert(readings::text(Applies::yes) == "yes" && readings::text(Applies::no) == "no" &&
              readings::text(Applies::unknown) == "unknown");

// restore_points' and file_history's: on yes, off no, locked and unknown
// unknown; encryption's: off yes, on and locked no, unknown unknown.
static_assert(readings::kept_by(State::on) == Applies::yes && readings::kept_by(State::off) == Applies::no &&
              readings::kept_by(State::locked) == Applies::unknown &&
              readings::kept_by(State::unknown) == Applies::unknown);
static_assert(readings::unencrypted(State::off) == Applies::yes && readings::unencrypted(State::on) == Applies::no &&
              readings::unencrypted(State::locked) == Applies::no &&
              readings::unencrypted(State::unknown) == Applies::unknown);

// A sync root's client: its key's name up to its first '!'.
static_assert(readings::client(L"OneDrive!S-1-5-21-1!Personal") == L"OneDrive");
static_assert(readings::client(L"Dropbox") == L"Dropbox");
static_assert(readings::client(L"!lead") == L"" && readings::client(L"").empty());

// The six rows in their order, each with what is kept, how long and what
// the owner can do.
constexpr bool rows_whole() {
    constexpr std::array<std::string_view, 6> names = {"restore_points", "file_history", "pagefile",
                                                       "hibernation",    "crash_dumps",  "encryption"};
    for (std::size_t i = 0; i < names.size(); ++i) {
        const readings::RowView& row = readings::rows.at(i);
        if (row.name != names.at(i) || row.kept.empty() || row.how_long.empty() || row.owner_can.empty()) {
            return false;
        }
    }
    return true;
}
static_assert(rows_whole());

// A System Protection list names a volume where an entry starts with its
// name, letters without case; a list of none, or the volume a part of an
// entry's tail, names none.
constexpr std::wstring_view volume_a = L"\\\\?\\Volume{0a1b2c3d-0000-0000-0000-00000000000a}\\";
static_assert(readings::names(std::wstring_view(L"\\\\?\\Volume{0A1B2C3D-0000-0000-0000-00000000000A}\\:(C%3A)"),
                              volume_a));
constexpr wchar_t two[] =
    L"\\\\?\\Volume{ffff}\\:(D%3A)\0\\\\?\\Volume{0a1b2c3d-0000-0000-0000-00000000000a}\\:(C%3A)";
static_assert(readings::names(std::wstring_view(two, std::size(two) - 1), volume_a));
static_assert(!readings::names(std::wstring_view(two, 25), volume_a));
static_assert(!readings::names(std::wstring_view(L"\\\\?\\Volume{ffff}\\:(D%3A)"), volume_a));
static_assert(!readings::names(std::wstring_view(L"x\\\\?\\Volume{0a1b2c3d-0000-0000-0000-00000000000a}\\"), volume_a));
static_assert(!readings::names(std::wstring_view(), volume_a));
static_assert(!readings::names(std::wstring_view(L"\\\\?\\Volume{0a1b2c3d-0000-0000-0000-00000000000a}"), volume_a));
static_assert(!readings::names(std::wstring_view(L"anything"), std::wstring_view()));

std::string result_text(long result) {
    Detail detail;
    readings::add_result(detail, result);
    return std::string(detail.view());
}

std::string error_text(unsigned long error) {
    Detail detail;
    qiven3::platform::win::add_error(detail, error);
    return std::string(detail.view());
}

// The code, operation and detail of the PlatformError op throws; io, "" and
// "none thrown" when it returns.
struct Thrown {
    Code code = Code::io;
    std::string operation;
    std::string detail = "none thrown";
};

template <class Op>
Thrown thrown(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const PlatformError& error) {
        return {error.code, std::string(error.operation.view()), std::string(error.detail.view())};
    }
    return {};
}

// Pages for an arena, off the stack.
struct Pages {
    std::array<std::byte, 1U << 16U> bytes{};
};

// An absolute path no member reads here: each case refuses or fails first.
constexpr std::string_view nowhere = "C:\\qiven3-win-facts-never-read";

}  // namespace

QIVEN3_CASE(win_facts, names) {
    // A Win32 error's HRESULT, then COM's own names, then one unnamed.
    const std::array<std::pair<std::uint32_t, std::string_view>, 8> results = {{
        {0x8007001FU, "HRESULT 0x8007001f, ERROR_GEN_FAILURE (31)"},
        {0x80070005U, "HRESULT 0x80070005, ERROR_ACCESS_DENIED (5)"},
        {0x80004005U, "HRESULT 0x80004005, E_FAIL"},
        {0x80004002U, "HRESULT 0x80004002, E_NOINTERFACE"},
        {0x80010106U, "HRESULT 0x80010106, RPC_E_CHANGED_MODE"},
        {0x800401F0U, "HRESULT 0x800401f0, CO_E_NOTINITIALIZED"},
        {0x8002802BU, "HRESULT 0x8002802b, TYPE_E_ELEMENTNOTFOUND"},
        {0x80041001U, "HRESULT 0x80041001"},
    }};
    for (const auto& [result, named] : results) {
        CHECK_EQ(result_text(static_cast<long>(result)), std::string(named));
    }
    // The table's names win/facts' reads give.
    CHECK_EQ(error_text(14), std::string("ERROR_OUTOFMEMORY (14)"));
    CHECK_EQ(error_text(203), std::string("ERROR_ENVVAR_NOT_FOUND (203)"));
    CHECK_EQ(error_text(234), std::string("ERROR_MORE_DATA (234)"));
    CHECK_EQ(error_text(259), std::string("ERROR_NO_MORE_ITEMS (259)"));
    CHECK_EQ(error_text(1630), std::string("ERROR_UNSUPPORTED_TYPE (1630)"));
}

// System Protection's key unopened: unknown, never off; missing, the key
// named first; refused, admin_only; another error, the call and the error.
QIVEN3_CASE(win_facts, unopened) {
    const std::array<std::pair<long, std::string_view>, 3> statuses = {{
        {2, "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\SPP\\Clients: RegOpenKeyExW: "
            "ERROR_FILE_NOT_FOUND (2)"},
        {5, "admin_only"},
        {31, "RegOpenKeyExW: ERROR_GEN_FAILURE (31)"},
    }};
    for (const auto& [status, named] : statuses) {
        Detail reason;
        CHECK(readings::unopened(reason, status) == State::unknown);
        CHECK_EQ(std::string(reason.view()), std::string(named));
    }
}

// A relative path and a text no path holds: path, before any read, each
// member's out cleared.
QIVEN3_CASE(win_facts, refusals) {
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        Facts facts(arena);
        String synced{Allocator<char>(arena)};
        Vector<Row> rows{Allocator<Row>(arena)};
        for (const std::string_view given : {std::string_view("relative\\a"), std::string_view("C:\\a\xff")}) {
            const std::string expected = given.starts_with("C:") ? "not UTF-8" : "not absolute";
            facts.volume.assign("stale");
            facts.protection.state = State::on;
            facts.protection.reason.assign("stale");
            const Thrown f = thrown([&] { platform.facts(given, facts); });
            CHECK(f.code == Code::path);
            CHECK_EQ(f.operation, std::string("facts"));
            CHECK_EQ(f.detail, expected);
            CHECK(facts.volume.empty());
            CHECK(facts.protection.state == State::unknown && facts.protection.reason.empty());
            synced.assign("stale");
            const Thrown s = thrown([&] { (void)platform.synced(given, synced); });
            CHECK(s.code == Code::path);
            CHECK_EQ(s.operation, std::string("synced"));
            CHECK_EQ(s.detail, expected);
            CHECK(synced.empty());
            rows.emplace_back(arena);
            const std::array<std::string_view, 1> paths = {given};
            const Thrown r = thrown([&] { platform.residual(paths, rows); });
            CHECK(r.code == Code::path);
            CHECK_EQ(r.operation, std::string("residual"));
            CHECK_EQ(r.detail, expected);
            CHECK(rows.empty());
        }
    }
    CHECK_EQ(arena.used(), 0U);
}

// Each member's first call failed: facts' and residual's GetVolumePathNameW,
// synced's GetEnvironmentVariableW, each path naming the call and 31; the
// failed call runs nothing, so no machine fact is read.
QIVEN3_CASE(win_facts, first_fault) {
#if defined(QIVEN_FAULTS)
    WindowsPlatform platform(std::nullopt);
    qiven3::memory::Countdown countdown;
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    platform.attach(&countdown);
    {
        Facts facts(arena);
        String synced{Allocator<char>(arena)};
        Vector<Row> rows{Allocator<Row>(arena)};
        const std::array<std::string_view, 1> paths = {nowhere};
        countdown.arm(1);
        const Thrown f = thrown([&] { platform.facts(nowhere, facts); });
        CHECK(countdown.fired());
        countdown.arm(1);
        const Thrown s = thrown([&] { (void)platform.synced(nowhere, synced); });
        CHECK(countdown.fired());
        countdown.arm(1);
        const Thrown r = thrown([&] { platform.residual(paths, rows); });
        CHECK(countdown.fired());
        countdown.disarm();
        for (const auto& [got, operation, call] :
             {std::tuple{f, "facts", "GetVolumePathNameW"}, std::tuple{s, "synced", "GetEnvironmentVariableW"},
              std::tuple{r, "residual", "GetVolumePathNameW"}}) {
            CHECK(got.code == Code::path);
            CHECK_EQ(got.operation, std::string(operation));
            CHECK_EQ(got.detail, std::string(call) + ": ERROR_GEN_FAILURE (31)");
        }
        CHECK(facts.volume.empty() && synced.empty() && rows.empty());
    }
    platform.attach(nullptr);
    CHECK_EQ(arena.used(), 0U);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}
