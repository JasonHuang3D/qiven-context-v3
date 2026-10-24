#pragma once

// The Windows adapter: WindowsPlatform, a concrete type satisfying the
// concept Platform with no base class, and its handles, each a move-only
// value holding its HANDLEs or its address. It is the one product target
// that includes a Windows header or links a Windows library, and it
// includes them in its sources alone, never here: a HANDLE is held as the
// void* it is.
//
// Its first operations are the configuration location, resolve, normcase,
// the console, random bytes, the clock, pages and wipe. The files'
// operations (replace, stage, create, remove_directory, read, lock), the
// facts (facts, synced, residual) and the processes (program, start, run)
// are PlatformError unsupported until their modules (win/files, win/facts,
// win/process) are built; their handles' types are here so that the
// adapter satisfies the concept now.
//
// Its scratch, a path's UTF-16 forms, lives in pages it takes once
// through pages(): 128 KiB for two paths of 32,767 UTF-16 units, and the
// configuration override's UTF-16 units after them.
//
// thread: one (an adapter object serves one thread at a time: its scratch)

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "platform/platform.hpp"

namespace qiven3::platform::win {

// A Win32 error as a detail names it: its name and number,
// "ERROR_PATH_NOT_FOUND (3)", or "Win32 error <n>" for one off the table.
void add_error(Detail& detail, unsigned long error) noexcept;

// Pages from VirtualAlloc, reserved and committed at once; the destructor
// gives them back by VirtualFree. Empty for a count of 0.
class WindowsPages {
public:
    WindowsPages() noexcept = default;
    WindowsPages(void* address, std::size_t size) noexcept : address_(address), size_(size) {}
    WindowsPages(WindowsPages&& other) noexcept;
    WindowsPages& operator=(WindowsPages&& other) noexcept;
    WindowsPages(const WindowsPages&) = delete;
    WindowsPages& operator=(const WindowsPages&) = delete;
    ~WindowsPages();

    [[nodiscard]] std::span<std::byte> bytes() const noexcept;
    // The pages' first byte, as VirtualAlloc gave it.
    [[nodiscard]] void* address() const noexcept { return address_; }

private:
    void* address_ = nullptr;
    std::size_t size_ = 0;
};

// A file written as a stream (win/files, package 5): one handle; the
// destructor closes it.
class WindowsSink {
public:
    WindowsSink() noexcept = default;
    WindowsSink(WindowsSink&& other) noexcept;
    WindowsSink& operator=(WindowsSink&& other) noexcept;
    WindowsSink(const WindowsSink&) = delete;
    WindowsSink& operator=(const WindowsSink&) = delete;
    ~WindowsSink();

    void write(std::string_view bytes);
    void close();

private:
    void* handle_ = nullptr;
};

// A staging directory (win/files, package 5); the destructor discards it
// if it is unpublished. It holds nothing until then.
class WindowsStaged {
public:
    WindowsStaged() noexcept = default;
    WindowsStaged(WindowsStaged&&) noexcept = default;
    WindowsStaged& operator=(WindowsStaged&&) noexcept = default;
    WindowsStaged(const WindowsStaged&) = delete;
    WindowsStaged& operator=(const WindowsStaged&) = delete;
    ~WindowsStaged() = default;

    WindowsSink open(std::string_view path);
    void publish(std::string_view name, Seconds deadline);
    void discard();
};

// A held lock (win/files, package 5): one handle; the destructor unlocks
// and closes it.
class WindowsLock {
public:
    WindowsLock() noexcept = default;
    WindowsLock(WindowsLock&& other) noexcept;
    WindowsLock& operator=(WindowsLock&& other) noexcept;
    WindowsLock(const WindowsLock&) = delete;
    WindowsLock& operator=(const WindowsLock&) = delete;
    ~WindowsLock();

    void release();

private:
    void* handle_ = nullptr;
};

// A started process tree in a Job Object (win/process): the process, the
// Job Object and three pipes; the destructor kills the tree and closes
// each.
class WindowsChild {
public:
    WindowsChild() noexcept = default;
    WindowsChild(WindowsChild&& other) noexcept;
    WindowsChild& operator=(WindowsChild&& other) noexcept;
    WindowsChild(const WindowsChild&) = delete;
    WindowsChild& operator=(const WindowsChild&) = delete;
    ~WindowsChild();

    bool read_line(Seconds timeout, String& out);
    void write(std::string_view bytes);
    void close_input();
    int wait(Seconds timeout);
    void kill();

private:
    void close_all() noexcept;
    void* process_ = nullptr;
    void* job_ = nullptr;
    void* input_ = nullptr;
    void* output_ = nullptr;
    void* error_ = nullptr;
};

class WindowsPlatform;

// v3's standard output: UTF-16 through WriteConsoleW on a console; on a
// pipe or a file the ANSI code page, a character it lacks one '?', never a
// look-alike, each LF as CRLF. It borrows the adapter's scratch.
class StandardOutput {
public:
    explicit StandardOutput(WindowsPlatform& platform) noexcept : platform_(&platform) {}
    void write(std::string_view utf8);

private:
    WindowsPlatform* platform_;
};

class WindowsPlatform {
public:
    // config_dir, when given, overrides the known folder's qiven3. The
    // scratch is taken here: PlatformError memory if the system refuses it.
    explicit WindowsPlatform(std::optional<std::string_view> config_dir);
    WindowsPlatform(WindowsPlatform&&) noexcept = default;
    WindowsPlatform& operator=(WindowsPlatform&&) noexcept = default;
    WindowsPlatform(const WindowsPlatform&) = delete;
    WindowsPlatform& operator=(const WindowsPlatform&) = delete;
    ~WindowsPlatform() = default;

    [[nodiscard]] std::string_view name() const noexcept { return "windows"; }
    // The configuration location, made first if create: path, config,
    // exists, io.
    void config_dir(bool create, String& out);
    // An absolute path's one form: path.
    void resolve(std::string_view path, String& out);
    // A path's form for comparison, '/' as '\' and case folded by
    // LCMapStringEx: path.
    void normcase(std::string_view path, String& out);
    void facts(std::string_view path, Facts& out);
    bool synced(std::string_view path, String& out);
    void replace(std::string_view path, std::string_view data, Seconds deadline);
    WindowsStaged stage(std::string_view directory);
    void create(std::string_view path, bool directory);
    // RemoveDirectoryW, win/files': error 145 false, 2 and 3 true.
    bool remove_directory(std::string_view path);
    void read(std::string_view path, String& out);
    WindowsLock lock(std::string_view path, Seconds deadline);
    void program(std::string_view name, String& out);
    WindowsChild start(const Command& command);
    void run(const Command& command, std::string_view data, Ran& out);
    // The console's code page, cp<n>.
    void code_page(String& out);
    [[nodiscard]] StandardOutput standard_output() noexcept { return StandardOutput(*this); }
    void residual(std::span<const std::string_view> paths, Vector<Row>& out);
    // into.size() bytes from the system's generator: io.
    void random_bytes(std::span<std::byte> into);
    // Each byte zeroed by SecureZeroMemory, a store the compiler keeps.
    void wipe(std::span<std::byte> bytes) noexcept;
    // UTC, milliseconds since 1970, truncated.
    [[nodiscard]] std::chrono::sys_time<std::chrono::milliseconds> now() const;
    // n bytes as whole pages, zeroed: memory.
    [[nodiscard]] WindowsPages pages(std::size_t n);

private:
    friend class StandardOutput;
    void write_output(std::string_view utf8);

    WindowsPages scratch_;
    std::size_t override_ = 0;  // the override's UTF-16 units after the scratch
    bool overridden_ = false;
    bool bad_override_ = false;  // not UTF-8, or past 32,767 units: path at config_dir
};

static_assert(Pages<WindowsPages> && Sink<WindowsSink> && Staged<WindowsStaged> && Lock<WindowsLock> &&
              Child<WindowsChild>);
static_assert(Platform<WindowsPlatform>);

// The pinned sizes, on x64: Pages its address and size; Sink and Lock one
// handle each; Child the process, the Job Object and three pipes.
#if defined(_WIN64)
static_assert(sizeof(WindowsPages) == 16, "the Windows adapter's Pages: address and size");
static_assert(sizeof(WindowsSink) == 8 && sizeof(WindowsLock) == 8, "one handle each");
static_assert(sizeof(WindowsChild) == 40, "process, Job Object, three pipes");
static_assert(sizeof(WindowsPlatform) == 32, "the scratch's Pages, the override's size and two flags");
#endif

}  // namespace qiven3::platform::win
