// windows/files' held cases on the Windows adapter, on the real file system
// in a scratch directory of each case's own under the job's temporary
// directory: two adapters' creates racing; replace and lock over files'
// retries, a reader holding the target or the temporary file and a second
// lock driving them, released before the deadline or held past it.

#include <array>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "qiven/memory/memory.hpp"
#include "qiven/platform/windows/windows.hpp"
#include "qiven/runner/runner.hpp"

namespace {

namespace fs = std::filesystem;
using qiven::platform::Code;
using qiven::platform::PlatformError;
using qiven::platform::Seconds;
using qiven::platform::win::WindowsPlatform;
using namespace std::chrono_literals;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_bytes(const fs::path& path, std::string_view bytes) {
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Whether no handle holds path without sharing delete: it renames away and
// back. A handle the adapter leaked would fail the first rename.
bool unheld(const fs::path& path) {
    fs::path moved = path;
    moved += ".moved";
    std::error_code error;
    fs::rename(path, moved, error);
    if (error) {
        return false;
    }
    fs::rename(moved, path, error);
    return !error;
}

// Whether a directory holds a replace's temporary file, ".<name>.<16>.tmp".
bool temporary_left(const fs::path& directory) {
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        const std::string name = text(entry.path().filename());
        if (name.starts_with(".") && name.ends_with(".tmp")) {
            return true;
        }
    }
    return false;
}

// A scratch directory of the case's own, removed at its end.
struct Scratch {
    Scratch() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        root = fs::temp_directory_path() / ("qiven-win-files-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(root);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    [[nodiscard]] std::string at(std::string_view name) const { return text(root / name); }
    fs::path root;
};

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

constexpr Seconds five{5.0};
constexpr Seconds short_wait{0.2};

// The steady clock in whole milliseconds, as the adapter's elapsed() reads
// it: two readings hold between them every span it reads in their while.
std::chrono::milliseconds steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

using Clock = std::chrono::steady_clock;

// A holder's time: three of the adapter's 50 ms retry intervals.
constexpr std::chrono::milliseconds held_for{150};
// The deadline a holder of replace's temporary file is held past: its
// watcher's time to take the file, on a loaded machine, within it.
constexpr Seconds held_past{1.0};

// The whole milliseconds of a span, for a case's output.
long long ms_of(Clock::duration span) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(span).count();
}

// replace's temporary files in a directory, ".<name>.<16 digits>.tmp" for
// the target's name.
std::vector<std::string> temporaries(const fs::path& directory, std::string_view name) {
    const std::string prefix = "." + std::string(name) + ".";
    std::vector<std::string> out;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        const std::string found = text(entry.path().filename());
        if (found.size() == prefix.size() + 20 && found.starts_with(prefix) && found.ends_with(".tmp")) {
            out.push_back(found);
        }
    }
    return out;
}

// P9's holder, by the standard library alone, a reader as std::ifstream
// opens one: sharing read and write, never delete. On the target, taken at
// once; or on replace's temporary file, which a watcher thread finds beside
// the target and opens once the adapter has closed it, a reader on the
// target holding the rename back until then. Released held_for after it is
// taken, or else at release(). Its times and the file's name are read
// after release(); the release's time before its readers close.
class Holder {
public:
    Holder(const fs::path& target, bool temporary, std::optional<std::chrono::milliseconds> hold)
        : target_(target), temporary_(temporary), hold_(hold), gate_(target, std::ios::binary) {
        ready_ = gate_.is_open();
        if (!temporary_) {
            taken_ = Clock::now();
        }
        watcher_ = std::thread([this] { run(); });
    }
    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;
    Holder(Holder&&) = delete;
    Holder& operator=(Holder&&) = delete;
    ~Holder() { release(); }

    void release() {
        {
            const std::scoped_lock lock(mutex_);
            stop_ = true;
        }
        woken_.notify_all();
        if (watcher_.joinable()) {
            watcher_.join();
        }
    }
    [[nodiscard]] bool ready() const { return ready_; }
    [[nodiscard]] const std::optional<Clock::time_point>& taken() const { return taken_; }
    [[nodiscard]] const std::optional<Clock::time_point>& released() const { return released_; }
    [[nodiscard]] const std::string& name() const { return name_; }

private:
    // The temporary file opened, if one is there and the adapter's handle
    // on it is closed.
    bool take_temporary() {
        for (const std::string& found : temporaries(target_.parent_path(), text(target_.filename()))) {
            reader_.open(target_.parent_path() / found, std::ios::binary);
            if (reader_.is_open()) {
                name_ = found;
                return true;
            }
            reader_.clear();
        }
        return false;
    }

    void run() {
        std::unique_lock lock(mutex_);
        while (temporary_ && !stop_ && !take_temporary()) {
            woken_.wait_for(lock, std::chrono::milliseconds(1), [this] { return stop_; });
        }
        if (temporary_ && reader_.is_open()) {
            taken_ = Clock::now();
            gate_.close();
        }
        if (taken_) {
            if (hold_) {
                woken_.wait_until(lock, *taken_ + *hold_, [this] { return stop_; });
            } else {
                woken_.wait(lock, [this] { return stop_; });
            }
            released_ = Clock::now();
        }
        reader_.close();
        gate_.close();
    }

    fs::path target_;
    bool temporary_;
    std::optional<std::chrono::milliseconds> hold_;
    std::ifstream gate_;    // on the target
    std::ifstream reader_;  // on the temporary file
    bool ready_ = false;
    std::optional<Clock::time_point> taken_;
    std::optional<Clock::time_point> released_;
    std::string name_;
    std::mutex mutex_;
    std::condition_variable woken_;
    bool stop_ = false;
    std::thread watcher_;
};

#if defined(QIVEN_FAULTS)
using qiven::platform::win::ClockTrace;

// A retry loop's clock by its trace, read between before and after: its
// reads in order within them, the first at its start, then one after each
// refused try; a pause of the text's 50 ms after each such read but a
// spent one. Spent: it gave up at its first read past the deadline, never
// before; else every read short of it.
void check_clock(qiven::runner::Context& qiven_ctx, const ClockTrace& t, std::chrono::milliseconds before,
                 std::chrono::milliseconds after, Seconds deadline, bool spent) {
    CHECK(t.reads >= (spent ? 2U : 1U));
    CHECK(before <= t.first && t.first <= t.previous && t.previous <= t.last && t.last <= after);
    CHECK_EQ(t.pauses, spent ? t.reads - 2 : t.reads - 1);
    CHECK(t.asked == 50ms * static_cast<std::chrono::milliseconds::rep>(t.pauses));
    CHECK(t.previous - t.first < deadline);
    CHECK(spent == (t.last - t.first >= deadline));
}
#endif

}  // namespace

// P27's race: two adapters, each on a thread of its own, create the same
// absent files, each pair of creates released together by a barrier: of
// each pair one makes the empty file and the other finds it present,
// never both.
QIVEN_CASE(win_files, create_race) {
    const Scratch scratch;
    constexpr std::size_t races = 64;
    WindowsPlatform one("qiven-platform-test", 16, std::nullopt);
    WindowsPlatform two("qiven-platform-test", 16, std::nullopt);
    const std::array<WindowsPlatform*, 2> creators{&one, &two};
    std::vector<std::vector<Thrown>> outcomes(2, std::vector<Thrown>(races));
    std::barrier gate(2);
    const auto creates = [&](std::size_t creator) {
        for (std::size_t k = 0; k < races; ++k) {
            gate.arrive_and_wait();
            outcomes[creator][k] =
                thrown([&] { creators[creator]->create(scratch.at("raced-" + std::to_string(k)), false); });
        }
    };
    std::thread second(creates, std::size_t{1});
    creates(0);
    second.join();
    std::array<std::size_t, 2> made{};
    for (std::size_t k = 0; k < races; ++k) {
        const std::string name = "raced-" + std::to_string(k);
        const Thrown& first = outcomes[0][k];
        const Thrown& other = outcomes[1][k];
        made[0] += first.detail == "none thrown" ? 1U : 0U;
        made[1] += other.detail == "none thrown" ? 1U : 0U;
        const Thrown& lost = first.detail == "none thrown" ? other : first;
        const Thrown& won = first.detail == "none thrown" ? first : other;
        CHECK_EQ(name + ": " + won.detail, name + ": none thrown");
        CHECK_EQ(name + ": " + lost.detail, name + ": CreateFileW: ERROR_FILE_EXISTS (80)");
        CHECK(lost.code == Code::exists);
        CHECK_EQ(fs::file_size(scratch.root / name), 0U);
    }
    std::cout << "win_files.create_race: of " << races << " pairs the first made " << made[0] << ", the second "
              << made[1] << "\n";
}

// P9's reader on the adapter: a reader holds the target past the deadline,
// busy with the old bytes and no temporary file, never before the deadline
// by the clock the adapter reads; a deadline of 0 one try. Under
// QIVEN_FAULTS the trace shows the retries as check_clock states them,
// and an unarmed countdown, which fails none, counts the adapter's Win32
// calls: the held replace of deadline 0 makes those of a replace that
// renames at its first try, and one more, its temporary file's deletion;
// the deadline's replace those and one rename more for each pause.
// Released, the new bytes. Then P9's holders (Holder): a reader on the
// target, then one on the temporary file, each released before the
// deadline; one on the temporary file held past it, the file left and
// named.
QIVEN_CASE(win_files, replace_held) {
    const Scratch scratch;
    WindowsPlatform platform("qiven-platform-test", 16, std::nullopt);
    const fs::path target = scratch.root / "map-marks.json";
    write_bytes(target, "old");
#if defined(QIVEN_FAULTS)
    qiven::memory::Countdown countdown;
    platform.attach(&countdown);
    ClockTrace held_trace;
    ClockTrace zero_trace;
#endif
    // The Win32 calls op makes; 0 without QIVEN_FAULTS.
    const auto calls = [&](auto op) {
        std::size_t made = 0;
#if defined(QIVEN_FAULTS)
        countdown.disarm();
        op();
        made = countdown.calls();
#else
        op();
#endif
        return made;
    };
    std::size_t retried = 0;
    std::size_t once = 0;
    {
        const std::ifstream reader(target, std::ios::binary);  // shares read and write, not delete
        CHECK(reader.is_open());
#if defined(QIVEN_FAULTS)
        platform.trace(&held_trace);
#endif
        const auto before = steady_ms();
        Thrown held;
        retried = calls([&] { held = thrown([&] { platform.replace(text(target), "new", short_wait); }); });
        const auto after = steady_ms();
        CHECK(held.code == Code::busy);
        CHECK_EQ(held.detail, std::string("rename: error 5 (ERROR_ACCESS_DENIED)"));
        CHECK(after - before >= short_wait);
#if defined(QIVEN_FAULTS)
        check_clock(qiven_ctx, held_trace, before, after, short_wait, true);
        platform.trace(&zero_trace);
#endif
        Thrown zero;
        once = calls([&] { zero = thrown([&] { platform.replace(text(target), "new", Seconds{0.0}); }); });
#if defined(QIVEN_FAULTS)
        platform.trace(nullptr);
        CHECK_EQ(zero_trace.reads, 2U);  // its start, and its one try's
        CHECK_EQ(zero_trace.pauses, 0U);
#endif
        CHECK(zero.code == Code::busy);
        CHECK_EQ(zero.detail, std::string("rename: error 5 (ERROR_ACCESS_DENIED)"));
        CHECK(!temporary_left(scratch.root));
    }
    CHECK_EQ(bytes_of(target), std::string("old"));
    const std::size_t first_try = calls([&] { platform.replace(text(target), "new", five); });
    CHECK_EQ(bytes_of(target), std::string("new"));
    CHECK(!temporary_left(scratch.root));
#if defined(QIVEN_FAULTS)
    CHECK(first_try > 0U);
    CHECK_EQ(once, first_try + 1);
    CHECK_EQ(retried, once + held_trace.pauses);
    CHECK(!countdown.fired());
    platform.attach(nullptr);
#else
    static_cast<void>(retried + once + first_try);
#endif

    // A reader on the target, then one on the temporary file, each released
    // held_for after it is taken: replaced no sooner than it lets go, within
    // the deadline, no temporary file left.
    for (const bool temporary : {false, true}) {
        const char* const kind = temporary ? "the temporary file" : "the target";
        write_bytes(target, "old");
        Holder holder(target, temporary, held_for);
        CHECK(holder.ready());
        const auto before = Clock::now();
        const Thrown released = thrown([&] { platform.replace(text(target), "new", five); });
        const auto after = Clock::now();
        holder.release();
        CHECK_EQ(released.detail, std::string("none thrown"));
        CHECK_EQ(bytes_of(target), std::string("new"));
        CHECK(!temporary_left(scratch.root));
        CHECK(holder.taken().has_value() && holder.released().has_value());
        if (holder.taken() && holder.released()) {
            const auto taken = *holder.taken();
            const auto let_go = *holder.released();
            std::cout << "win_files.replace_held: " << kind << " held " << ms_of(let_go - taken) << " ms, replaced "
                      << ms_of(after - let_go) << " ms after its release, " << ms_of(after - before)
                      << " ms after asking\n";
            CHECK(let_go - taken >= held_for);
            CHECK(let_go <= after);
            CHECK(after - before < five);
        }
        CHECK(!temporary || !holder.name().empty());
    }

    // A reader on the temporary file held past the deadline: busy, the old
    // bytes, the temporary file left, and the detail names it, its deletion
    // refused too.
    write_bytes(target, "old");
    {
        Holder holder(target, true, std::nullopt);
        CHECK(holder.ready());
        const auto before = Clock::now();
        const Thrown past = thrown([&] { platform.replace(text(target), "new", held_past); });
        const auto after = Clock::now();
        holder.release();
        std::cout << "win_files.replace_held: the temporary file held past a deadline of "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(held_past).count() << " ms, busy "
                  << ms_of(after - before) << " ms after asking\n";
        CHECK(past.code == Code::busy);
        CHECK_EQ(past.operation, std::string("replace"));
        CHECK_EQ(bytes_of(target), std::string("old"));
        const std::vector<std::string> left = temporaries(scratch.root, text(target.filename()));
        CHECK_EQ(left.size(), 1U);
        CHECK(!holder.name().empty());
        if (left.size() == 1) {
            CHECK_EQ(left.front(), holder.name());
            CHECK_EQ(past.detail, "rename: error 32 (ERROR_SHARING_VIOLATION); left " + holder.name() +
                                      ", delete: error 32 (ERROR_SHARING_VIOLATION)");
            fs::remove(scratch.root / left.front());
        }
    }
    CHECK(!temporary_left(scratch.root));
    CHECK(unheld(target));
}

// P20's lock half: held past the deadline, busy, never before it by the
// clock the adapter reads, and under QIVEN_FAULTS the retries as
// check_clock states them; at 0 busy, one try; released, acquired. Held by
// another adapter's lock released held_for after it took it, on a thread
// of its own: acquired no sooner than that release, within the deadline.
QIVEN_CASE(win_files, lock) {
    const Scratch scratch;
    WindowsPlatform platform("qiven-platform-test", 16, std::nullopt);
    const fs::path file = scratch.root / "location.lock";
    auto held = platform.lock(text(file), five);
    CHECK(fs::is_regular_file(file));
#if defined(QIVEN_FAULTS)
    ClockTrace trace;
    platform.trace(&trace);
#endif
    const auto before = steady_ms();
    const Thrown second = thrown([&] { platform.lock(text(file), short_wait); });
    const auto after = steady_ms();
    CHECK(second.code == Code::busy);
    CHECK_EQ(second.operation, std::string("lock"));
    CHECK_EQ(second.detail, std::string("lock: error 33 (ERROR_LOCK_VIOLATION)"));
    CHECK(after - before >= short_wait);
#if defined(QIVEN_FAULTS)
    platform.trace(nullptr);
    check_clock(qiven_ctx, trace, before, after, short_wait, true);
#endif
#if defined(QIVEN_FAULTS)
    ClockTrace zero_trace;
    platform.trace(&zero_trace);
#endif
    CHECK(thrown([&] { platform.lock(text(file), Seconds{0.0}); }).code == Code::busy);
#if defined(QIVEN_FAULTS)
    platform.trace(nullptr);
    CHECK_EQ(zero_trace.reads, 2U);  // its start, and its one try's
    CHECK_EQ(zero_trace.pauses, 0U);
#endif
    auto moved = std::move(held);
    moved.release();
    moved.release();
    held.release();
    { const auto again = platform.lock(text(file), Seconds{0.0}); }
    {
        const auto last = platform.lock(text(file), five);
        CHECK(thrown([&] { platform.lock(text(file), Seconds{0.0}); }).code == Code::busy);
    }

    // Another holder, released before the deadline: then acquired.
    {
        WindowsPlatform other("qiven-platform-test", 16, std::nullopt);
        auto theirs = other.lock(text(file), five);
        const auto taken = Clock::now();
        std::optional<Clock::time_point> let_go;
        std::thread holder([&] {
            std::this_thread::sleep_until(taken + held_for);
            let_go = Clock::now();
            theirs.release();
        });
#if defined(QIVEN_FAULTS)
        ClockTrace waited;
        platform.trace(&waited);
#endif
        const auto asked_ms = steady_ms();
        const auto asked = Clock::now();
        std::optional<Clock::time_point> acquired;
        const Thrown ours = thrown([&] {
            const auto mine = platform.lock(text(file), five);
            acquired = Clock::now();
        });
        const auto returned_ms = steady_ms();
        holder.join();
#if defined(QIVEN_FAULTS)
        platform.trace(nullptr);
        check_clock(qiven_ctx, waited, asked_ms, returned_ms, five, false);
#endif
        CHECK_EQ(ours.detail, std::string("none thrown"));
        CHECK(let_go.has_value() && acquired.has_value());
        if (let_go && acquired) {
            std::cout << "win_files.lock: another holder held " << ms_of(*let_go - taken) << " ms, acquired "
                      << ms_of(*acquired - *let_go) << " ms after its release, " << ms_of(*acquired - asked)
                      << " ms after asking\n";
            CHECK(*let_go - taken >= held_for);
            CHECK(taken <= asked && *let_go <= *acquired);
            CHECK(*acquired - asked < five);
        }
    }
    CHECK(fs::is_regular_file(file));  // kept, never written
    CHECK_EQ(fs::file_size(file), 0U);
    CHECK(unheld(file));
    const Thrown missing = thrown([&] { platform.lock(text(scratch.root / "none" / "x.lock"), five); });
    CHECK(missing.code == Code::io);
    CHECK_EQ(missing.detail, std::string("open: error 3 (ERROR_PATH_NOT_FOUND)"));
    CHECK(thrown([&] { platform.lock("relative.lock", five); }).code == Code::path);
}
