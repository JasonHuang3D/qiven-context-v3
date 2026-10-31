// win/facts' fault runs on this machine, under QIVEN_FAULTS: each
// platform call of facts, residual and synced failed in turn, until a run
// completes. facts' failed read is unknown, its reason naming the call and
// error 31, the other readings as completed; GetVolumePathNameW's is path.
// residual's rows are each as completed or unknown, at most one unlike; a
// row holds no reason, so a read already unknown here (System Protection's)
// shows none. synced's every failure is path, naming the call. The
// sequences are this machine's (win_facts_machine_test.cpp's note).

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "memory/arena.hpp"
#include "platform/win/windows.hpp"
#include "runner/runner.hpp"

#if defined(QIVEN_FAULTS)

namespace {

namespace fs = std::filesystem;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::platform::Code;
using qiven3::platform::Facts;
using qiven3::platform::PlatformError;
using qiven3::platform::Reading;
using qiven3::platform::Row;
using qiven3::platform::String;
using qiven3::platform::Vector;
using qiven3::platform::win::WindowsPlatform;

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

struct Pages {
    std::array<std::byte, 1U << 16U> bytes{};
};

// The PlatformError op throws, its detail "none thrown" when it returns.
struct Thrown {
    Code code = Code::io;
    std::string detail = "none thrown";
};

template <class Op>
Thrown thrown(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const PlatformError& error) {
        return {error.code, std::string(error.detail.view())};
    }
    return {};
}

// The call a fault's detail or reason names, "" where it names no call
// failed with the fault's error 31.
std::string call_of(std::string_view reason) {
    for (const std::string_view tail : {": ERROR_GEN_FAILURE (31)", ": HRESULT 0x8007001f, ERROR_GEN_FAILURE (31)"}) {
        if (reason.ends_with(tail) && reason.size() > tail.size()) {
            return std::string(reason.substr(0, reason.size() - tail.size()));
        }
    }
    return "";
}

// A fault's facts against the completed: "<reading> <call>" where the
// volume is the same and one reading alone is unlike, unknown, naming the
// failed call; "wrong" otherwise.
std::string unknown_read(const Facts& got, const Facts& done) {
    const std::array<std::pair<std::string_view, std::pair<const Reading*, const Reading*>>, 3> readings = {{
        {"protection", {&got.protection, &done.protection}},
        {"bitlocker", {&got.bitlocker, &done.bitlocker}},
        {"file_history", {&got.file_history, &done.file_history}},
    }};
    std::string found = "wrong";
    std::size_t unlike = 0;
    for (const auto& [name, pair] : readings) {
        const auto& [g, d] = pair;
        if (g->state != d->state || g->reason != d->reason) {
            ++unlike;
            const std::string call = call_of(g->reason);
            if (g->state == qiven3::platform::State::unknown && !call.empty()) {
                found = std::string(name) + " " + call;
            }
        }
    }
    return unlike == 1 && got.volume == done.volume ? found : "wrong";
}

// A fault's rows against the completed: "-" where they are the same, the
// one row unlike where it is unknown, "wrong" otherwise.
std::string unknown_row(const Vector<Row>& got, const Vector<Row>& done) {
    if (got.size() != done.size()) {
        return "wrong";
    }
    std::string found = "-";
    for (std::size_t i = 0; i < got.size(); ++i) {
        const Row& g = got.at(i);
        const Row& d = done.at(i);
        if (g.name != d.name || g.kept != d.kept || g.how_long != d.how_long || g.owner_can != d.owner_can) {
            return "wrong";
        }
        if (g.applies != d.applies) {
            found = found == "-" && g.applies == "unknown" ? std::string(g.name) : "wrong";
        }
    }
    return found;
}

std::string joined(const std::vector<std::string>& seen) {
    std::string out;
    for (const std::string& s : seen) {
        out += (out.empty() ? "" : "; ") + s;
    }
    return out;
}

// Each platform call of op failed in turn: what each run gave, by
// answered(k) where op answered and "path <call>" where it threw path.
template <class Op, class Answered>
std::vector<std::string> sweep(WindowsPlatform& platform, Op op, Answered answered) {
    qiven3::memory::Countdown countdown;
    platform.attach(&countdown);
    std::vector<std::string> seen;
    for (std::size_t k = 1; k <= 100; ++k) {
        countdown.arm(k);
        const Thrown t = thrown(op);
        const bool fired = countdown.fired();
        countdown.disarm();
        if (!fired) {
            seen.push_back(t.detail == "none thrown" ? "completed" : "thrown when no call failed: " + t.detail);
            break;
        }
        if (t.detail == "none thrown") {
            seen.push_back(answered());
        } else {
            seen.push_back((t.code == Code::path ? "path " : "not path ") + call_of(t.detail));
        }
    }
    platform.attach(nullptr);
    return seen;
}

}  // namespace

#endif

QIVEN3_CASE(win_facts_machine, facts_faults) {
#if defined(QIVEN_FAULTS)
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    const std::string path = text(scratch.root);
    {
        Facts done(arena);
        Facts got(arena);
        platform.facts(path, done);
        const std::vector<std::string> seen =
            sweep(platform, [&] { platform.facts(path, got); }, [&] { return unknown_read(got, done); });
        std::cout << "win_facts_machine.facts_faults: " << joined(seen) << "\n";
        CHECK_EQ(joined(seen), std::string("path GetVolumePathNameW; protection RegGetValueW; protection "
                                           "RegOpenKeyExW; bitlocker CoInitializeEx; bitlocker CoCreateInstance; "
                                           "bitlocker IPropertySystem::GetPropertyDescriptionByName; bitlocker "
                                           "IPropertyDescription::GetPropertyKey; bitlocker "
                                           "SHGetPropertyStoreFromParsingName; bitlocker IPropertyStore::GetValue; "
                                           "file_history RegGetValueW; file_history SHGetKnownFolderPath; "
                                           "file_history GetFileAttributesW; completed"));
    }
    CHECK_EQ(arena.used(), 0U);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}

QIVEN3_CASE(win_facts_machine, residual_faults) {
#if defined(QIVEN_FAULTS)
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    const std::array<std::string, 2> given = {text(scratch.root), "C:\\"};
    const std::array<std::string_view, 2> paths = {given[0], given[1]};
    {
        Vector<Row> done{Allocator<Row>(arena)};
        Vector<Row> got{Allocator<Row>(arena)};
        const std::span<const std::string_view> one(paths.data(), 1);
        platform.residual(one, done);
        // A row's read failed: the path's volume, System Protection's two
        // reads (unknown here either way), BitLocker's six, File History's
        // three, then pagefile's, hibernation's and crash_dumps' one each.
        const std::vector<std::string> seen = sweep(platform, [&] { platform.residual(one, got); }, [&] {
            return unknown_row(got, done);
        });
        std::cout << "win_facts_machine.residual_faults: " << joined(seen) << "\n";
        CHECK_EQ(joined(seen), std::string("path GetVolumePathNameW; -; -; encryption; encryption; encryption; "
                                           "encryption; encryption; encryption; file_history; file_history; "
                                           "file_history; pagefile; hibernation; crash_dumps; completed"));
        // Two paths: the volume's and its rows' nine calls each, then the six.
        platform.residual(paths, done);
        const std::vector<std::string> both = sweep(platform, [&] { platform.residual(paths, got); }, [&] {
            return unknown_row(got, done);
        });
        std::cout << "win_facts_machine.residual_faults: two paths: " << joined(both) << "\n";
        CHECK_EQ(both.size(), 25U);
        CHECK_EQ(both.back(), std::string("completed"));
        for (const std::string& s : both) {
            CHECK(s != "wrong" && !s.starts_with("not path"));
        }
    }
    CHECK_EQ(arena.used(), 0U);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}

QIVEN3_CASE(win_facts_machine, synced_faults) {
#if defined(QIVEN_FAULTS)
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        String out{Allocator<char>(arena)};
        const std::string path = text(scratch.root);
        const std::vector<std::string> seen =
            sweep(platform, [&] { (void)platform.synced(path, out); }, [] { return std::string("answered"); });
        std::cout << "win_facts_machine.synced_faults: " << joined(seen) << "\n";
        // The OneDrive variable's folder, which the path is not under; the
        // other two unset; HKCU's Accounts with no account and no sync root.
        CHECK_EQ(joined(seen), std::string("path GetEnvironmentVariableW; path CreateFileW; path "
                                           "GetFinalPathNameByHandleW; path GetEnvironmentVariableW; path "
                                           "GetEnvironmentVariableW; path RegOpenKeyExW; path RegEnumKeyExW; path "
                                           "RegOpenKeyExW; path RegEnumKeyExW; completed"));
        // The folder itself: found by the variable, its root resolved.
        wchar_t* folder = nullptr;
        std::size_t n = 0;
        if (_wdupenv_s(&folder, &n, L"OneDrive") == 0 && folder != nullptr) {
            const std::unique_ptr<wchar_t, decltype(&std::free)> owned(folder, &std::free);
            const std::string cloud = text(fs::path(owned.get()));
            const std::vector<std::string> found =
                sweep(platform, [&] { (void)platform.synced(cloud, out); }, [] { return std::string("answered"); });
            std::cout << "win_facts_machine.synced_faults: the folder: " << joined(found) << "\n";
            CHECK_EQ(joined(found), std::string("path GetEnvironmentVariableW; path CreateFileW; path "
                                                "GetFinalPathNameByHandleW; completed"));
        }
    }
    CHECK_EQ(arena.used(), 0U);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}

