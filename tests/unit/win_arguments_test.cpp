// win/arguments' run_utf8 on the Windows adapter, wmain's body: the
// program's name not read, so a name that is not valid UTF-16 reaches the
// entry, as an empty argv[0]; an argument after it that is not, a command
// line past its bounds and a vector it cannot read each the refused status
// with the entry not run; and, under QIVEN3_FAULTS, the pages' call and
// the arena's two allocations each failed in turn, the faulted status,
// apart from the refused one, with the entry not run.

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "memory/memory.hpp"
#include "platform/win/windows.hpp"
#include "runner/runner.hpp"

namespace {

namespace win = qiven3::platform::win;

// The statuses: refused and faulted run_utf8's, ran and other the entry's,
// so that a status says whether the entry ran.
constexpr int refused = 21;
constexpr int faulted = 23;
constexpr int ran = 7;
constexpr int other = 8;
constexpr win::Utf8Statuses statuses{refused, faulted};

// The entry: ran where it sees an empty name and "location", argv[argc]
// null; other else.
int entry(int argc, const char* const* argv) noexcept {
    if (argc != 2 || argv == nullptr) {
        return other;
    }
    const std::span<const char* const> given(argv, 3);
    const std::string_view name(given[0]);
    const std::string_view first(given[1]);
    return name.empty() && first == "location" && given[2] == nullptr ? ran : other;
}

// A string with a lone high surrogate inside.
std::wstring lone(std::wstring_view before) {
    std::wstring out(before);
    out.push_back(static_cast<wchar_t>(0xD800));
    out.push_back(L'x');
    return out;
}

int run(const std::vector<std::wstring>& texts) {
    std::vector<const wchar_t*> wide;
    wide.reserve(texts.size() + 1);
    for (const std::wstring& t : texts) {
        wide.push_back(t.c_str());
    }
    wide.push_back(nullptr);
    return win::run_utf8(static_cast<int>(texts.size()), wide.data(), &entry, statuses);
}

}  // namespace

// The name is not read: one that is not valid UTF-16 runs the entry, its
// argv[0] empty.
QIVEN3_CASE(win_arguments, name_unread) {
    CHECK_EQ(run({L"qiven3", L"location"}), ran);
    CHECK_EQ(run({lone(L"qiven3"), L"location"}), ran);
}

// What it cannot read or is past a bound: refused, the entry not run.
QIVEN3_CASE(win_arguments, refused) {
    CHECK_EQ(run({L"qiven3", lone(L"location")}), refused);
    // 32,768 units after the name: past a command line's; 32,767 within.
    CHECK_EQ(run({L"qiven3", std::wstring(win::command_line_units, L'a'), L"b"}), refused);
    CHECK_EQ(run({L"", std::wstring(win::command_line_units, L'a')}), other);
    CHECK_EQ(win::run_utf8(-1, nullptr, &entry, statuses), refused);
    CHECK_EQ(win::run_utf8(1, nullptr, &entry, statuses), refused);
    const std::array<const wchar_t*, 1> none = {nullptr};
    CHECK_EQ(win::run_utf8(0, none.data(), nullptr, statuses), refused);
}

// The pages' call, then the arena's two allocations, each failed in turn:
// faulted, the entry not run; past them, the entry's status.
QIVEN3_CASE(win_arguments, faults) {
#if defined(QIVEN3_FAULTS)
    const std::wstring name = L"qiven3";
    const std::wstring first = L"location";
    const std::vector<const wchar_t*> wide = {name.c_str(), first.c_str(), nullptr};
    qiven3::memory::Countdown countdown;
    std::size_t k = 1;
    for (; k <= 5; ++k) {
        countdown.arm(k);
        const int status = win::run_utf8(2, wide.data(), &entry, statuses, &countdown);
        const bool fired = countdown.fired();
        countdown.disarm();
        if (!fired) {
            CHECK_EQ(status, ran);
            break;
        }
        CHECK_EQ(status, faulted);
    }
    std::cout << "win_arguments.faults: failed in turn " << k - 1 << "\n";
    CHECK_EQ(k, std::size_t{4});
#else
    QIVEN3_SKIP("not applicable: QIVEN3_FAULTS is off in this build");
#endif
}
