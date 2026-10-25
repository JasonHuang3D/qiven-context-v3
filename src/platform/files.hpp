#pragma once

// files: durable writes and the lock, for the Windows adapter and the fake.
// replace writes a file whole beside its target and renames it over it;
// stage makes a staging directory whose files are written as streams and
// published under a name by one rename; lock holds a file's first byte.
//
// The retries are here: a rename refused with error 5 or 32 (replace's and
// publish's) and a lock refused with 33 are tried again every 50 ms until
// the deadline, then the operation's own file is deleted and PlatformError
// busy thrown, its detail naming what could not be deleted. The system's
// calls are a System's (below): the Windows adapter's or the fake's, each
// giving the system's error by its Win32 number, 0 for none. They are the
// hooks through which the fake fails a rename or holds a lock.
//
// files takes no memory: its names and details are composed in place, and
// its handles hold the system's handles, never a heap object. A handle
// refers to its System, which outlives it.
//
// thread: one (a System serves one thread at a time)

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "platform/platform.hpp"

namespace qiven3::platform::files {

// The system's error, by its Win32 number; 0 none.
using Error = std::uint32_t;
inline constexpr Error none = 0;
inline constexpr Error file_not_found = 2;
inline constexpr Error path_not_found = 3;
inline constexpr Error access_denied = 5;
inline constexpr Error general = 31;
inline constexpr Error sharing_violation = 32;
inline constexpr Error lock_violation = 33;
inline constexpr Error file_exists = 80;
inline constexpr Error already_exists = 183;

// The retries' interval, and the random bytes of a temporary name.
inline constexpr std::chrono::milliseconds interval{50};
inline constexpr std::size_t random_count = 8;

// A temporary name's 16 lowercase hexadecimal digits: replace's file is
// ".<name>.<digits>.tmp" beside its target, stage's directory
// ".<digits>.tmp" in its directory. The System composes each from them.
using Digits = std::array<char, 2 * random_count>;

constexpr Digits digits(std::span<const std::byte, random_count> bytes) noexcept {
    constexpr std::string_view hex = "0123456789abcdef";
    Digits out{};
    for (std::size_t i = 0; i < random_count; ++i) {
        const auto b = static_cast<unsigned>(bytes[i]);
        out[2 * i] = hex[b >> 4U];
        out[(2 * i) + 1] = hex[b & 0xFU];
    }
    return out;
}

// The Win32 name of an error this module meets, or "" for another.
constexpr std::string_view error_name(Error error) noexcept {
    switch (error) {
    case file_not_found:
        return "ERROR_FILE_NOT_FOUND";
    case path_not_found:
        return "ERROR_PATH_NOT_FOUND";
    case access_denied:
        return "ERROR_ACCESS_DENIED";
    case general:
        return "ERROR_GEN_FAILURE";
    case sharing_violation:
        return "ERROR_SHARING_VIOLATION";
    case lock_violation:
        return "ERROR_LOCK_VIOLATION";
    case file_exists:
        return "ERROR_FILE_EXISTS";
    case already_exists:
        return "ERROR_ALREADY_EXISTS";
    default:
        return "";
    }
}

constexpr bool retried(Error error) noexcept { return error == access_denied || error == sharing_violation; }
constexpr bool present(Error error) noexcept { return error == file_exists || error == already_exists; }
constexpr bool absent(Error error) noexcept { return error == file_not_found || error == path_not_found; }

// Why a path's text is refused, or "" when it passes: UTF-8, strictly, and
// no control character (U+0000 to U+001F, U+007F to U+009F).
constexpr std::string_view text_fault(std::string_view text) noexcept {
    constexpr std::array<char32_t, 5> least = {0, 0, 0x80, 0x800, 0x10000};
    for (std::size_t at = 0; at < text.size();) {
        const auto b = static_cast<unsigned char>(text[at]);
        const std::size_t n = b < 0x80U ? 1 : (b >> 5U) == 0x6U ? 2 : (b >> 4U) == 0xEU ? 3 : (b >> 3U) == 0x1EU ? 4 : 0;
        if (n == 0 || n > text.size() - at) {
            return "not UTF-8";
        }
        char32_t cp = n == 1 ? b : n == 2 ? (b & 0x1FU) : n == 3 ? (b & 0x0FU) : (b & 0x07U);
        for (std::size_t i = 1; i < n; ++i) {
            const auto c = static_cast<unsigned char>(text[at + i]);
            if ((c & 0xC0U) != 0x80U) {
                return "not UTF-8";
            }
            cp = (cp << 6U) | (c & 0x3FU);
        }
        if (cp < least[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return "not UTF-8";
        }
        if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) {
            return "holds a control character";
        }
        at += n;
    }
    return "";
}

// Why a Staged's relative path is refused, or "": text_fault's, then
// '/'-separated components, none empty, "." or "..", none holding '\' or
// ':' (each a separator or a drive's mark on Windows).
constexpr std::string_view relative_fault(std::string_view path) noexcept {
    const std::string_view text = text_fault(path);
    if (!text.empty()) {
        return text;
    }
    for (std::size_t at = 0;;) {
        const std::size_t end = path.find('/', at);
        const std::string_view part = path.substr(at, end == std::string_view::npos ? path.size() - at : end - at);
        if (part.empty() || part == "." || part == "..") {
            return "an empty, . or .. component";
        }
        if (part.find_first_of("\\:") != std::string_view::npos) {
            return "a component holding \\ or :";
        }
        if (end == std::string_view::npos) {
            return "";
        }
        at = end + 1;
    }
}

// Why a name is refused, or "": relative_fault's, and one component.
constexpr std::string_view name_fault(std::string_view name) noexcept {
    const std::string_view fault = relative_fault(name);
    return !fault.empty() ? fault : name.find('/') != std::string_view::npos ? "more than one component" : "";
}

// A path's last component, after its last '/' or '\': replace's and
// lock's target's name, empty when the path ends in a separator.
constexpr std::string_view last_component(std::string_view path) noexcept {
    const std::size_t at = path.find_last_of("/\\");
    return at == std::string_view::npos ? path : path.substr(at + 1);
}

// The system's calls under the retries. Each giving an Error leaves nothing
// open behind it; those a destructor calls are noexcept.
// - random_bytes(into); elapsed(), a monotonic clock; pause(w), a wait of w;
//   absolute(path), the system's absolute rule.
// - replace's: write_temporary(path, digits, data) makes its file
//   exclusively, writes, flushes and closes it; rename_temporary renames it
//   over path, replacing it; delete_temporary deletes it.
// - stage's: make_staged(directory, digits, dir) makes the directory;
//   open_staged(dir, path, file) a file in it, its parents too,
//   exclusively; write(file, bytes); close(file) flushes and closes it, and
//   is none for a closed one; close_all(dir) closes each still open;
//   publish(dir, name) renames the directory to name beside it, with no
//   replacing; discard(dir, left) closes and deletes its files and it,
//   naming in left the file it could not delete by its path inside it,
//   nothing for the directory itself; forget(dir) ends the system's record
//   of a published or discarded one.
// - lock's: open_lock(path, held) opens path, made if missing; try_lock
//   locks its first byte, or gives 33; unlock unlocks if locked and closes.
template <class S>
concept System =
    std::movable<typename S::File> && std::movable<typename S::Dir> && std::movable<typename S::Held> &&
    std::default_initializable<typename S::File> && std::default_initializable<typename S::Dir> &&
    std::default_initializable<typename S::Held> &&
    requires(S& s, std::string_view p, std::string_view b, const Digits& h, std::span<std::byte> into,
             typename S::File& f, typename S::Dir& d, typename S::Held& l, Detail& left) {
        s.random_bytes(into);
        { s.elapsed() } -> std::same_as<std::chrono::milliseconds>;
        s.pause(interval);
        { s.absolute(p) } -> std::same_as<bool>;
        { s.write_temporary(p, h, b) } -> std::same_as<Error>;
        { s.rename_temporary(p, h) } -> std::same_as<Error>;
        { s.delete_temporary(p, h) } -> std::same_as<Error>;
        { s.make_staged(p, h, d) } -> std::same_as<Error>;
        { s.open_staged(d, p, f) } -> std::same_as<Error>;
        { s.write(f, b) } -> std::same_as<Error>;
        { s.close(f) } noexcept -> std::same_as<Error>;
        { s.close_all(d) } -> std::same_as<Error>;
        { s.publish(d, p) } -> std::same_as<Error>;
        { s.discard(d, left) } noexcept -> std::same_as<Error>;
        { s.forget(d) } noexcept;
        { s.open_lock(p, l) } -> std::same_as<Error>;
        { s.try_lock(l) } -> std::same_as<Error>;
        { s.unlock(l) } noexcept;
    };

// "<step>: error <n> (<name>)" appended to detail.
inline void add_error(Detail& detail, std::string_view step, Error error) noexcept {
    detail.add(step).add(": error ").number(error);
    const std::string_view named = error_name(error);
    if (!named.empty()) {
        detail.add(" (").add(named).add(")");
    }
}

// The error's PlatformError: code, operation, "<step>: error <n> (<name>)".
[[noreturn]] inline void raise(Code code, std::string_view operation, std::string_view step, Error error) {
    Detail detail;
    add_error(detail, step, error);
    throw PlatformError(code, operation, detail);
}

// path when fault is not empty.
inline void refuse(std::string_view operation, std::string_view fault) {
    if (!fault.empty()) {
        throw PlatformError(Code::path, operation, fault);
    }
}

// Whether the deadline is spent `taken` after its start; a deadline that
// is not a number is spent at once.
inline bool spent(std::chrono::milliseconds taken, Seconds deadline) noexcept {
    return !(std::chrono::duration_cast<Seconds>(taken).count() < deadline.count());
}

// A file written as a stream; the destructor closes as close() does, its
// error dropped.
template <System S>
class Sink {
public:
    Sink() noexcept = default;
    Sink(S& system, typename S::File file) noexcept : system_(&system), file_(std::move(file)) {}
    Sink(Sink&& other) noexcept : system_(std::exchange(other.system_, nullptr)), file_(std::move(other.file_)) {}
    Sink& operator=(Sink&& other) noexcept {
        if (this != &other) {
            end();
            system_ = std::exchange(other.system_, nullptr);
            file_ = std::move(other.file_);
        }
        return *this;
    }
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;
    ~Sink() { end(); }

    void write(std::string_view bytes) {
        if (system_ == nullptr) {
            throw PlatformError(Code::io, "write", "the file is closed");
        }
        const Error error = system_->write(file_, bytes);
        if (error != none) {
            raise(Code::io, "write", "write", error);
        }
    }
    // A second close is none.
    void close() {
        if (system_ == nullptr) {
            return;
        }
        const Error error = std::exchange(system_, nullptr)->close(file_);
        if (error != none) {
            raise(Code::io, "close", "close", error);
        }
    }

private:
    void end() noexcept {
        if (system_ != nullptr) {
            static_cast<void>(std::exchange(system_, nullptr)->close(file_));
        }
    }
    S* system_ = nullptr;
    typename S::File file_{};
};

// A staging directory, held with its digits; the destructor discards it if
// it is neither published nor discarded, its error dropped. After publish,
// or a discard that removed it, it is ended: open is io, publish io,
// discard none.
template <System S>
class Staged {
public:
    Staged() noexcept = default;
    Staged(S& system, typename S::Dir dir, const Digits& name) noexcept
        : system_(&system), dir_(std::move(dir)), name_(name) {}
    Staged(Staged&& other) noexcept
        : system_(std::exchange(other.system_, nullptr)), dir_(std::move(other.dir_)), name_(other.name_) {}
    Staged& operator=(Staged&& other) noexcept {
        if (this != &other) {
            end();
            system_ = std::exchange(other.system_, nullptr);
            dir_ = std::move(other.dir_);
            name_ = other.name_;
        }
        return *this;
    }
    Staged(const Staged&) = delete;
    Staged& operator=(const Staged&) = delete;
    ~Staged() { end(); }

    Sink<S> open(std::string_view path) {
        live("open");
        refuse("open", relative_fault(path));
        typename S::File file{};
        const Error error = system_->open_staged(dir_, path, file);
        if (error != none) {
            raise(present(error) ? Code::exists : Code::io, "open", "open", error);
        }
        return Sink<S>(*system_, std::move(file));
    }

    // Closes each file still open, then renames the directory to name:
    // refused with 5 or 32, retried until the deadline, then busy; name
    // present, exists; another error io. Each discards first, its detail
    // naming what is left: the staging directory, ".<digits>.tmp", and the
    // file inside it that stopped the deletion.
    void publish(std::string_view name, Seconds deadline) {
        live("publish");
        refuse("publish", name_fault(name));
        const std::chrono::milliseconds start = system_->elapsed();
        Error error = system_->close_all(dir_);
        const std::string_view step = error != none ? "close" : "rename";
        while (error == none) {
            error = system_->publish(dir_, name);
            if (error == none) {
                finish();
                return;
            }
            if (!retried(error) || spent(system_->elapsed() - start, deadline)) {
                break;
            }
            system_->pause(interval);
            error = none;
        }
        Detail detail;
        add_error(detail, step, error);
        discard_into(detail);
        const Code code = step == "close" ? Code::io : retried(error) ? Code::busy : present(error) ? Code::exists : Code::io;
        throw PlatformError(code, "publish", detail);
    }

    // Deletes the directory and its files. A file held stops it, busy
    // naming what is left, as publish does, which a later discard tries
    // again.
    void discard() {
        if (system_ == nullptr) {
            return;
        }
        Detail detail;
        const Error error = discard_into(detail);
        if (error != none) {
            throw PlatformError(retried(error) ? Code::busy : Code::io, "discard", detail);
        }
    }

private:
    void live(std::string_view operation) const {
        if (system_ == nullptr) {
            throw PlatformError(Code::io, operation, "the staging directory is published or discarded");
        }
    }
    void finish() noexcept { std::exchange(system_, nullptr)->forget(dir_); }
    // The system's discard; its error and what is left added to detail:
    // "left .<digits>.tmp", then "/" and the path inside it the system
    // names, if any.
    Error discard_into(Detail& detail) noexcept {
        Detail left;
        const Error error = system_->discard(dir_, left);
        if (error == none) {
            finish();
            return none;
        }
        detail.add(detail.view().empty() ? "left ." : "; left .").add(std::string_view(name_.data(), name_.size()));
        detail.add(".tmp");
        if (!left.view().empty()) {
            detail.add("/").add(left.view());
        }
        add_error(detail.add(", "), "delete", error);
        return error;
    }
    void end() noexcept {
        if (system_ != nullptr) {
            Detail detail;
            static_cast<void>(discard_into(detail));
            if (system_ != nullptr) {
                finish();
            }
        }
    }
    S* system_ = nullptr;
    typename S::Dir dir_{};
    Digits name_{};
};

// A held lock; the destructor releases it.
template <System S>
class Lock {
public:
    Lock() noexcept = default;
    Lock(S& system, typename S::Held held) noexcept : system_(&system), held_(std::move(held)) {}
    Lock(Lock&& other) noexcept : system_(std::exchange(other.system_, nullptr)), held_(std::move(other.held_)) {}
    Lock& operator=(Lock&& other) noexcept {
        if (this != &other) {
            release();
            system_ = std::exchange(other.system_, nullptr);
            held_ = std::move(other.held_);
        }
        return *this;
    }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    ~Lock() { release(); }

    // Unlocks and closes; a second release is none.
    void release() noexcept {
        if (system_ != nullptr) {
            std::exchange(system_, nullptr)->unlock(held_);
        }
    }
    [[nodiscard]] typename S::Held& held() noexcept { return held_; }

private:
    S* system_ = nullptr;
    typename S::Held held_{};
};

// A path replace, stage and lock take: text_fault's, then the system's
// absolute rule, and for replace and lock a last component.
template <System S>
void check(S& system, std::string_view operation, std::string_view path, bool named) {
    refuse(operation, text_fault(path));
    if (!system.absolute(path)) {
        refuse(operation, "not absolute");
    }
    if (named) {
        refuse(operation, last_component(path).empty() ? "no name" : "");
    }
}

template <System S>
Digits drawn(S& system) {
    std::array<std::byte, random_count> bytes{};
    system.random_bytes(bytes);
    return digits(bytes);
}

// data written to path whole, by a temporary file renamed over it: the
// rename refused with 5 or 32, retried every 50 ms until the deadline, then
// busy; any other error io. Each deletes the temporary file first; the
// detail names it if it is left.
template <System S>
void replace(S& system, std::string_view path, std::string_view data, Seconds deadline) {
    check(system, "replace", path, true);
    const std::chrono::milliseconds start = system.elapsed();
    const Digits name = drawn(system);
    Error error = system.write_temporary(path, name, data);
    const std::string_view step = error != none ? "write" : "rename";
    while (error == none) {
        error = system.rename_temporary(path, name);
        if (error == none) {
            return;
        }
        if (!retried(error) || spent(system.elapsed() - start, deadline)) {
            break;
        }
        system.pause(interval);
        error = none;
    }
    Detail detail;
    add_error(detail, step, error);
    // A present temporary name is another's file, kept.
    const Error deleted = step == "write" && present(error) ? none : system.delete_temporary(path, name);
    if (deleted != none && !absent(deleted)) {
        detail.add("; left .").add(last_component(path)).add(".").add(std::string_view(name.data(), name.size()));
        add_error(detail.add(".tmp, "), "delete", deleted);
    }
    throw PlatformError(step == "rename" && retried(error) ? Code::busy : Code::io, "replace", detail);
}

// A new staging directory in directory.
template <System S>
Staged<S> stage(S& system, std::string_view directory) {
    check(system, "stage", directory, false);
    const Digits name = drawn(system);
    typename S::Dir dir{};
    const Error error = system.make_staged(directory, name, dir);
    if (error != none) {
        raise(Code::io, "stage", "make", error);
    }
    return Staged<S>(system, std::move(dir), name);
}

// path's first byte locked: held by another, retried every 50 ms until the
// deadline, then busy; any other error io.
template <System S>
Lock<S> lock(S& system, std::string_view path, Seconds deadline) {
    check(system, "lock", path, true);
    const std::chrono::milliseconds start = system.elapsed();
    typename S::Held held{};
    const Error opened = system.open_lock(path, held);
    if (opened != none) {
        raise(Code::io, "lock", "open", opened);
    }
    Lock<S> taken(system, std::move(held));
    for (;;) {
        const Error error = system.try_lock(taken.held());
        if (error == none) {
            return taken;
        }
        if (error != lock_violation) {
            raise(Code::io, "lock", "lock", error);
        }
        if (spent(system.elapsed() - start, deadline)) {
            raise(Code::busy, "lock", "lock", error);
        }
        system.pause(interval);
    }
}

}  // namespace qiven3::platform::files
