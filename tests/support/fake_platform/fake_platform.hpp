#pragma once

// The fake platform, qiven3_platform_fake: FakePlatform, a concrete type
// satisfying the concept Platform on the C++ standard library alone, so the
// seam is proved on every system. It is in memory but for create,
// remove_directory and read, which work on real files (the first two
// under its scratch root alone), and
// config_dir(true), which makes its configuration directory. Its pages are
// the standard library's, aligned to 4,096 bytes and zeroed; its normcase
// folds ASCII letters alone. It reads no registry and starts no process.
// Each operation called is kept in its record, in order; under
// QIVEN3_FAULTS each is a fault point (attach).
//
// replace, stage and lock are files' (platform/files.hpp) over the fake's
// system calls (fake_files.hpp), on real files under its scratch root but
// lock, held per path in the fake; the busy counts drive their refusals.
//
// thread: one

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/platform.hpp"
#include "support/fake_platform/fake_files.hpp"

namespace qiven3::platform::fake {

// A program the fake runs, found by its whole argv.
struct Program {
    std::vector<std::string> lines;  // its standard output, each line ending LF
    std::string err;                 // its standard error
    int code = 0;                    // its exit code
    bool hang = false;               // never ends: the call is deadline
    std::string flood;               // "out" or "err": past any cap, output naming it
};

// The fake's stream: UTF-8 bytes as given, or ASCII, each code point above
// U+007F one '?'. An invalid UTF-8 byte is one '?' in either.
enum class Encoding : std::uint8_t { utf8, ascii };

struct FakeReading {
    State state = State::unknown;
    std::string reason = "fake";
};

struct FakeFacts {
    std::string volume;
    FakeReading protection, file_history, bitlocker;
};

struct FakeRow {
    std::string name, applies, kept, how_long, owner_can;
};

struct Config {
    std::string root;        // the scratch root: create makes nothing outside it
    std::string config_dir;  // the configuration location
    std::map<std::string, std::string, std::less<>> read_errors;  // path: read's io detail
    std::vector<std::pair<std::string, FakeFacts>> facts;          // by path prefix, the longest
    FakeFacts default_facts;
    std::vector<std::pair<std::string, std::string>> synced;  // folder, client's name
    std::vector<std::pair<std::string, std::string>> links;   // from, to: resolve follows
    std::string code_page = "cp65001";
    Encoding encoding = Encoding::utf8;
    std::map<std::vector<std::string>, Program> programs;  // by argv
    std::map<std::string, std::string, std::less<>> found;  // program(name): its path
    std::vector<FakeRow> residual;
    std::uint64_t seed = 0;  // random_bytes' seed
    std::chrono::sys_time<std::chrono::microseconds> clock{};
    Busy busy;  // how often replace, publish or lock is busy before it succeeds

    // A move may throw, stated: a map's move allocates its head node in
    // one standard library.
    Config() = default;
    Config(const Config&) = default;
    Config& operator=(const Config&) = default;
    Config(Config&&) noexcept(false) = default;
    Config& operator=(Config&&) noexcept(false) = default;
    ~Config() = default;
};

// The k-th 32-byte block of seed s: SHA-256 over s and k, each an 8-byte
// big-endian integer.
std::array<std::uint8_t, 32> seed_block(std::uint64_t seed, std::uint64_t k);

class FakePlatform;

// A page as the fake lends it: 4,096 bytes at a multiple of 4,096.
struct alignas(4096) FakePage {
    std::array<std::byte, 4096> bytes;
};

// The fake's pages, from std::allocator, value-initialized; the destructor
// gives them back and the fake counts them back.
class FakePages {
public:
    FakePages() noexcept = default;
    FakePages(FakePlatform& owner, FakePage* first, std::size_t count) noexcept
        : owner_(&owner), first_(first), count_(count) {}
    FakePages(FakePages&& other) noexcept;
    FakePages& operator=(FakePages&& other) noexcept;
    FakePages(const FakePages&) = delete;
    FakePages& operator=(const FakePages&) = delete;
    ~FakePages();

    [[nodiscard]] std::span<std::byte> bytes() const noexcept;

private:
    void give_back() noexcept;
    FakePlatform* owner_ = nullptr;
    FakePage* first_ = nullptr;
    std::size_t count_ = 0;
};

// The files' handles, files' own over the fake's system calls.
using FakeSink = files::Sink<FakeFiles>;
using FakeStaged = files::Staged<FakeFiles>;
using FakeLock = files::Lock<FakeFiles>;

// A program's run in memory: its lines, then its end; a hang or a flood
// reaches the caller as deadline or output.
class FakeChild {
public:
    explicit FakeChild(Program program) : program_(std::move(program)) {}
    FakeChild(FakeChild&&) noexcept = default;
    FakeChild& operator=(FakeChild&&) noexcept = default;
    FakeChild(const FakeChild&) = delete;
    FakeChild& operator=(const FakeChild&) = delete;
    ~FakeChild() = default;

    bool read_line(Seconds timeout, String& out);
    void write(std::string_view bytes);
    void close_input() noexcept {}
    int wait(Seconds timeout);
    void kill() noexcept { killed_ = true; }

private:
    void check(std::string_view operation) const;
    Program program_;
    std::size_t line_ = 0;
    bool killed_ = false;
};

// The fake's standard output, keeping its bytes.
class FakeOutput {
public:
    explicit FakeOutput(Encoding encoding) noexcept : encoding_(encoding) {}
    void write(std::string_view utf8);
    [[nodiscard]] const std::string& bytes() const noexcept { return bytes_; }

private:
    Encoding encoding_;
    std::string bytes_;
};

class FakePlatform {
public:
    explicit FakePlatform(const Config& config);
    FakePlatform(const FakePlatform&) = delete;
    FakePlatform& operator=(const FakePlatform&) = delete;
    FakePlatform(FakePlatform&&) = delete;
    FakePlatform& operator=(FakePlatform&&) = delete;
    ~FakePlatform() = default;

    [[nodiscard]] std::string_view name() const;
    void config_dir(bool create, String& out);
    void resolve(std::string_view path, String& out);
    void normcase(std::string_view path, String& out);
    void facts(std::string_view path, Facts& out);
    bool synced(std::string_view path, String& out);
    void replace(std::string_view path, std::string_view data, Seconds deadline);
    FakeStaged stage(std::string_view directory);
    void create(std::string_view path, bool directory);
    // The empty directory at path removed (true, also when none is there);
    // one holding anything kept (false); a file there io.
    bool remove_directory(std::string_view path);
    void read(std::string_view path, String& out);
    FakeLock lock(std::string_view path, Seconds deadline);
    void program(std::string_view name, String& out);
    FakeChild start(const Command& command);
    void run(const Command& command, std::string_view data, Ran& out);
    void code_page(String& out);
    FakeOutput& standard_output();
    void residual(std::span<const std::string_view> paths, Vector<Row>& out);
    void random_bytes(std::span<std::byte> into);
    void wipe(std::span<std::byte> bytes) noexcept;
    [[nodiscard]] std::chrono::sys_time<std::chrono::milliseconds> now();
    [[nodiscard]] FakePages pages(std::size_t n);

    // The test's clock; now() gives it truncated to milliseconds.
    void set_clock(std::chrono::sys_time<std::chrono::microseconds> clock) noexcept { config_.clock = clock; }
    // read's io for path, with this detail.
    void set_read_error(std::string_view path, std::string_view detail);
    // The busy counts from now; the files' system calls.
    void set_busy(const Busy& busy) noexcept { files_.set_busy(busy); }
    [[nodiscard]] FakeFiles& files() noexcept { return files_; }
    // The k-th call of pages from now refuses, memory; 0 none.
    void refuse_pages(std::size_t k) noexcept;
    // The operations called, by the contract's names, in order.
    [[nodiscard]] const std::vector<std::string>& calls() const noexcept { return calls_; }
    // The bytes written to standard_output().
    [[nodiscard]] const std::string& written() const noexcept { return output_.bytes(); }
    // The bytes of the pages lent and not yet given back.
    [[nodiscard]] std::size_t lent() const noexcept { return lent_; }
#if defined(QIVEN3_FAULTS)
    // Each call but wipe, noexcept by the contract, asks the countdown
    // first and, when it fires, fails as io, pages as memory; null
    // detaches it.
    void attach(memory::Countdown* countdown) noexcept {
        countdown_ = countdown;
        files_.attach(countdown);
    }
#endif

private:
    friend class FakePages;
    void record(std::string_view operation) const;
    const Program& find(const Command& command, std::string_view operation) const;

    Config config_;
    mutable std::vector<std::string> calls_;
    std::uint64_t block_ = 0;
    std::array<std::uint8_t, 32> pending_{};
    std::size_t pending_at_ = 32;  // the drawn block's next byte
    FakeOutput output_;
    std::size_t lent_ = 0;
    std::size_t pages_calls_ = 0;
    std::size_t refuse_at_ = 0;
    FakeFiles files_;
#if defined(QIVEN3_FAULTS)
    memory::Countdown* countdown_ = nullptr;
#endif
};

static_assert(Pages<FakePages> && Sink<FakeSink> && Staged<FakeStaged> && Lock<FakeLock> && Child<FakeChild>);
static_assert(Platform<FakePlatform>);
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
static_assert(sizeof(FakePages) == 24, "the fake's Pages: its owner, address and count");
static_assert(sizeof(FakeSink) == 16 && sizeof(FakeStaged) == 16 && sizeof(FakeLock) == 16,
              "files' handles on the fake: the system and its id");
#endif

}  // namespace qiven3::platform::fake
