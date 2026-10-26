#pragma once

// The Windows adapter: WindowsPlatform, a concrete type satisfying the
// concept Platform with no base class, and its handles, each a move-only
// value holding its HANDLEs or its address. It is the one product target
// that includes a Windows header or links a Windows library, and it
// includes them in its sources alone, never here: a HANDLE is held as the
// void* it is.
//
// Beside it, the binary's command line, its UTF-16 arguments as UTF-8
// (WindowsArguments, run_utf8).
//
// Its first operations are the configuration location, resolve, normcase,
// the console, random bytes, the clock, pages and wipe; win/files' are
// replace, stage, create, remove_directory, read and lock
// (platform/win/files.hpp); win/facts' are facts, synced and residual
// (platform/win/facts.hpp); win/process' are program, start and the
// Child's tree (process.cpp, child.cpp), run and read_line PlatformError
// unsupported until its second part. A Lock, Staged and Sink
// are files' over the adapter's WindowsFiles, which lives in the adapter:
// so the adapter does not move, as a handle holds its System's address.
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

#include "platform/files.hpp"
#include "platform/platform.hpp"
#include "platform/win/files.hpp"

namespace qiven3::platform::win {

// A Win32 error as a detail names it: its name and number,
// "ERROR_PATH_NOT_FOUND (3)", or "Win32 error <n>" for one off the table.
void add_error(Detail& detail, unsigned long error) noexcept;

// Pages: WindowsPages, in platform/win/files.hpp, as a staging directory
// holds its path on them.

// A file written as a stream and a staging directory (win/files): files'
// over the adapter's WindowsFiles, as the Lock is. The Sink holds its
// System's address and its file's serial; the Staged its System's
// address, its Dir (id, pages, units) and its 16 digits.
using WindowsSink = files::Sink<WindowsFiles>;
using WindowsStaged = files::Staged<WindowsFiles>;

// A held lock (win/files): its System's address and one handle; the
// destructor unlocks and closes it.
using WindowsLock = files::Lock<WindowsFiles>;

// A started process tree in a Job Object (win/process): the process, the
// Job Object and three pipes, under QIVEN3_FAULTS the countdown its calls
// ask first; the destructor kills the tree and closes each. write, wait
// and kill fail io; wait past its timeout deadline, the tree running;
// kill ends the tree, its exit code 1, and again nothing more.
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
    friend class WindowsPlatform;
    WindowsChild(void* process, void* job, void* input, void* output, void* error) noexcept;
    void close_all() noexcept;
    bool faulted() noexcept;
    void* process_ = nullptr;
    void* job_ = nullptr;
    void* input_ = nullptr;
    void* output_ = nullptr;
    void* error_ = nullptr;
#if defined(QIVEN3_FAULTS)
    memory::Countdown* countdown_ = nullptr;
#endif
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
    // Made in place, never moved: its handles hold its WindowsFiles' address.
    WindowsPlatform(WindowsPlatform&&) = delete;
    WindowsPlatform& operator=(WindowsPlatform&&) = delete;
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
    // A resolved path's volume, System Protection, File History and
    // BitLocker (win/facts): path; a read refused or failing is unknown.
    void facts(std::string_view path, Facts& out);
    // The sync client whose folder holds a resolved path, false for none
    // (win/facts): path.
    bool synced(std::string_view path, String& out);
    void replace(std::string_view path, std::string_view data, Seconds deadline);
    WindowsStaged stage(std::string_view directory);
    void create(std::string_view path, bool directory);
    // RemoveDirectoryW, win/files': error 145 false, 2 and 3 true.
    bool remove_directory(std::string_view path);
    void read(std::string_view path, String& out);
    WindowsLock lock(std::string_view path, Seconds deadline);
    // <name>.exe in the first absolute entry of v3's own PATH that holds it
    // as a file, resolved (win/process): start.
    void program(std::string_view name, String& out);
    // A process tree started suspended in a new Job Object, its three pipe
    // ends alone inherited, then resumed (win/process): start.
    WindowsChild start(const Command& command);
    void run(const Command& command, std::string_view data, Ran& out);
    // The console's code page, cp<n>.
    void code_page(String& out);
    [[nodiscard]] StandardOutput standard_output() noexcept { return StandardOutput(*this); }
    // The platform's six residual rows for the caller's absolute paths
    // (win/facts): path.
    void residual(std::span<const std::string_view> paths, Vector<Row>& out);
    // into.size() bytes from the system's generator: io.
    void random_bytes(std::span<std::byte> into);
    // Each byte zeroed by SecureZeroMemory, a store the compiler keeps.
    void wipe(std::span<std::byte> bytes) noexcept;
    // UTC, milliseconds since 1970, truncated.
    [[nodiscard]] std::chrono::sys_time<std::chrono::milliseconds> now() const;
    // n bytes as whole pages, zeroed: memory.
    [[nodiscard]] WindowsPages pages(std::size_t n);
#if defined(QIVEN3_FAULTS)
    // The countdown win/files' calls ask first; null for none.
    void attach(memory::Countdown* countdown) noexcept { files_.attach(countdown); }
#endif

private:
    friend class StandardOutput;
    friend class WindowsFiles;
    void write_output(std::string_view utf8);
    // resolve's body on the first path buffer's first `given` units: path.
    void resolve_units(std::size_t given, String& out);

    WindowsPages scratch_;
    std::size_t override_ = 0;  // the override's UTF-16 units after the scratch
    bool overridden_ = false;
    bool bad_override_ = false;  // not UTF-8, or past 32,767 units: path at config_dir
    bool error_mode_ = false;    // set by the first start
    WindowsFiles files_{*this};
};

static_assert(Pages<WindowsPages> && Sink<WindowsSink> && Staged<WindowsStaged> && Lock<WindowsLock> &&
              Child<WindowsChild>);
static_assert(Platform<WindowsPlatform>);

// The pinned sizes, on x64: Pages its address and size; Sink and Lock
// their System's address and one handle (the Sink's its file's serial);
// Staged its System's address, its Dir and its digits; Child the process,
// the Job Object and three pipes, under QIVEN3_FAULTS the countdown's
// address; the adapter its scratch, the override's size, three flags and
// its WindowsFiles: the adapter's address, under
// QIVEN3_FAULTS the countdown's, stage's 16 slots of 24 bytes and a serial.
#if defined(_WIN64)
static_assert(sizeof(WindowsPages) == 16, "the Windows adapter's Pages: address and size");
static_assert(sizeof(WindowsSink) == 16, "the System's address and one handle");
static_assert(sizeof(WindowsFiles::Dir) == 32 && sizeof(WindowsStaged) == 56, "the System's address, Dir, digits");
static_assert(sizeof(WindowsLock) == 16, "the System's address and one handle");
#if defined(QIVEN3_FAULTS)
static_assert(sizeof(WindowsChild) == 48, "process, Job Object, three pipes, the countdown");
static_assert(sizeof(WindowsFiles) == 408 && sizeof(WindowsPlatform) == 440, "scratch, size, flags, files");
#else
static_assert(sizeof(WindowsChild) == 40, "process, Job Object, three pipes");
static_assert(sizeof(WindowsFiles) == 400 && sizeof(WindowsPlatform) == 432, "scratch, size, flags, files");
#endif
#endif

// n bytes as whole pages, zeroed: memory. WindowsPlatform::pages's body,
// which the binary's arguments take before any adapter is made.
[[nodiscard]] WindowsPages take_pages(std::size_t n);

// The binary's command line (win/arguments.cpp): its arguments as the
// system gives them to wmain, UTF-16, each converted strictly to UTF-8 and
// ended by a null, argv[argc] null, on the caller's arena. A command line
// is at most command_line_units units, so at most most_arguments
// arguments, and a unit at most 3 bytes of UTF-8 (a surrogate pair's two,
// 4): the conversion takes at most arguments_bound bytes.
inline constexpr std::size_t command_line_units = 32767;
inline constexpr std::size_t most_arguments = 16384;
inline constexpr std::size_t arguments_bound =
    3 * command_line_units + most_arguments + (most_arguments + 1) * sizeof(char*);

class WindowsArguments {
public:
    // The bytes the conversion of `wide` takes on its arena: argc + 1
    // pointers, then each argument's UTF-8 and its null. PlatformError
    // path for an argument that is not valid UTF-16 (an unpaired
    // surrogate), naming its index; memory past the bound's counts.
    [[nodiscard]] static std::size_t bytes(std::span<const wchar_t* const> wide);
    // Converted on the arena: two allocations, the pointers' then the
    // text's, bytes(wide) in all, each a fault point of the arena's; the
    // errors of bytes(), and MemoryError budget past the arena.
    WindowsArguments(std::span<const wchar_t* const> wide, memory::Arena& arena);
    WindowsArguments(const WindowsArguments&) = delete;
    WindowsArguments& operator=(const WindowsArguments&) = delete;
    WindowsArguments(WindowsArguments&&) = delete;
    WindowsArguments& operator=(WindowsArguments&&) = delete;
    ~WindowsArguments();

    [[nodiscard]] int argc() const noexcept { return static_cast<int>(count_); }
    [[nodiscard]] const char* const* argv() const noexcept { return static_cast<const char* const*>(pointers_); }

private:
    memory::Arena* arena_;
    void* pointers_ = nullptr;  // count_ + 1 pointers
    char* text_ = nullptr;
    std::size_t count_ = 0;
    std::size_t size_ = 0;  // the text's bytes
};

// A main that reads its arguments as UTF-8.
using Utf8Main = int (*)(int argc, const char* const* argv) noexcept;

// wmain's body: wide converted on pages taken for exactly its bytes, then
// entry on them, entry's status; `refused` where the conversion or the
// pages fail, with no line (no adapter is made to print one).
int run_utf8(int argc, const wchar_t* const* wide, Utf8Main entry, int refused) noexcept;

}  // namespace qiven3::platform::win
