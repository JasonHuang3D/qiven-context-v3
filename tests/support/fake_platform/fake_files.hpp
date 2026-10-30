#pragma once

// The fake's system calls under files' retries (platform/files.hpp): real
// files under the fake's scratch root, through std::filesystem and the C
// library's exclusive "wbx", each error given by its Win32 number; locks
// per path in memory, no file made; a monotonic clock that pause()
// advances, so a retry until the deadline takes no time; and the busy
// counts, how often a call refuses as a holder would before it succeeds,
// each refusal counted down. Under QIVEN3_FAULTS each call giving an error
// asks the countdown first and, when it fires, gives 31 (a close, a
// discard's close and an unlock done first, so nothing stays open);
// open_staged asks it again at its record's insert, before it makes
// anything, as that allocation's throw, also 31.
//
// thread: one

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>

#include "platform/files.hpp"

namespace qiven3::platform::fake {

class FakePlatform;

// Held for good: a count that never runs out.
inline constexpr std::uint64_t never = std::numeric_limits<std::uint64_t>::max();

// How often each call refuses before it succeeds.
struct Busy {
    std::uint64_t target = 0;     // replace's rename, 5: a reader holds the target
    std::uint64_t temporary = 0;  // replace's rename and deletion, 32: a handle on its file
    std::uint64_t publish = 0;    // publish's rename, 5, and discard's deletion, 32: a file inside held
    std::uint64_t lock = 0;       // lock, 33: another holder
    std::uint64_t remove = 0;     // remove, 32: a handle without sharing delete
    std::uint64_t rename = 0;     // rename, 5 for an odd count, 32 an even: a handle on either file
};

class FakeFiles {
public:
    // The system's handles: ids of what the fake keeps; 0 none.
    struct File {
        std::size_t id = 0;
    };
    struct Dir {
        std::size_t id = 0;
    };
    struct Held {
        std::size_t id = 0;
    };

    FakeFiles(FakePlatform& platform, const Busy& busy);
    FakeFiles(const FakeFiles&) = delete;
    FakeFiles& operator=(const FakeFiles&) = delete;
    FakeFiles(FakeFiles&&) = delete;
    FakeFiles& operator=(FakeFiles&&) = delete;
    ~FakeFiles();

    void random_bytes(std::span<std::byte> into);
    [[nodiscard]] std::chrono::milliseconds elapsed() const noexcept { return clock_; }
    void pause(std::chrono::milliseconds wait) noexcept { clock_ += wait; }
    bool absolute(std::string_view path) const;

    files::Error write_temporary(std::string_view path, const files::Digits& digits, std::string_view data);
    files::Error rename_temporary(std::string_view path, const files::Digits& digits);
    files::Error delete_temporary(std::string_view path, const files::Digits& digits);

    files::Error make_staged(std::string_view directory, const files::Digits& digits, Dir& out);
    files::Error open_staged(const Dir& dir, std::string_view path, File& out);
    files::Error write(const File& file, std::string_view bytes);
    files::Error close(const File& file) noexcept;
    files::Error close_all(const Dir& dir);
    // The files open, across the staging directories: files' bound.
    [[nodiscard]] std::size_t open_files() const noexcept;
    files::Error publish(const Dir& dir, std::string_view name);
    files::Error discard(const Dir& dir, Detail& left) noexcept;
    void forget(const Dir& dir) noexcept;

    files::Error open_lock(std::string_view path, Held& out);
    files::Error try_lock(const Held& held);
    void unlock(const Held& held) noexcept;

    // list's: . and .. first, then the names by std::filesystem in the
    // reverse of their UTF-8 bytes' order, as no system lists, so files'
    // sort is proved; 1113 for a name not UTF-8. remove's: 5 for a
    // directory, 2 for none. rename's: 183 for a file or directory under
    // name but the file's own, checked first, then std::filesystem's
    // rename (to its own name, the file kept, as on Windows), which holds
    // within one process, the only user the fake's cases have.
    files::Error list_names(std::string_view directory, Vector<String>& names);
    files::Error remove_file(std::string_view path);
    files::Error rename_file(std::string_view path, std::string_view name);

    void set_busy(const Busy& busy) noexcept { busy_ = busy; }
    [[nodiscard]] const Busy& busy() const noexcept { return busy_; }
    // What the fake keeps: files open, staging directories, locks opened.
    [[nodiscard]] std::size_t kept() const noexcept;
#if defined(QIVEN3_FAULTS)
    void attach(memory::Countdown* countdown) noexcept { countdown_ = countdown; }
#endif

private:
    struct State;
    // Whether the countdown fires: the call then gives 31.
    bool faulted() noexcept;

    FakePlatform* platform_;
    Busy busy_;
    std::chrono::milliseconds clock_{0};
    std::unique_ptr<State> state_;
#if defined(QIVEN3_FAULTS)
    memory::Countdown* countdown_ = nullptr;
#endif
};

static_assert(files::System<FakeFiles>);

}  // namespace qiven3::platform::fake
