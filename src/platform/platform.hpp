#pragma once

// The platform contract: every platform fact v3 depends on sits behind it,
// and core logic depends on this header alone of src/platform/. The
// contract is a set of concepts bound at compile time: an adapter is a
// concrete type satisfying Platform for one platform, with no base class
// and no virtual function. Core code that uses the platform is a template
// on a type P satisfying Platform and takes a P& passed in, never a global;
// only a binary's main makes the adapter (platform/current.hpp).
//
// Views in, the caller's buffers out: paths, text and bytes enter as UTF-8
// std::string_view or as std::span, and a result is written into the out
// the caller gives, cleared first, a String or a value type of them on the
// caller's arena. A caller passes the defaults itself: false for
// config_dir's create and create's directory, 5 s for each deadline.
//
// Each handle (Sink, Staged, Lock, Child, Pages) is a move-only value
// holding the system's handle, never a heap object; its destructor ends
// what it holds.
//
// thread: none (the contract); each adapter states its own.

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "memory/memory.hpp"

namespace qiven3::platform {

using Seconds = std::chrono::duration<double>;
using memory::String;
using memory::Vector;

// locked is BitLocker's alone.
enum class State : std::uint8_t { on, off, locked, unknown };

// The value types own their text on the caller's arena, so each is made
// with that arena.
struct Reading {
    explicit Reading(memory::Arena& arena) : reason(memory::Allocator<char>(arena)) {}
    State state = State::unknown;
    String reason;
};

struct Facts {
    explicit Facts(memory::Arena& arena)
        : volume(memory::Allocator<char>(arena)), protection(arena), file_history(arena), bitlocker(arena) {}
    String volume;
    Reading protection, file_history, bitlocker;
};

struct Caps {
    std::uint64_t out = 0, err = 0;
};

// A process to start: views of the caller's text, used within the call.
struct Command {
    std::span<const std::string_view> argv;
    std::string_view cwd;
    std::span<const std::pair<std::string_view, std::string_view>> env;
    Seconds deadline{5.0};
    Caps caps;
};

struct Ran {
    explicit Ran(memory::Arena& arena) : out(memory::Allocator<char>(arena)), err(memory::Allocator<char>(arena)) {}
    int code = 0;
    String out, err;
};

struct Row {
    explicit Row(memory::Arena& arena)
        : name(memory::Allocator<char>(arena)), applies(memory::Allocator<char>(arena)),
          kept(memory::Allocator<char>(arena)), how_long(memory::Allocator<char>(arena)),
          owner_can(memory::Allocator<char>(arena)) {}
    String name, applies, kept, how_long, owner_can;
};

template <class H>
concept Handle = std::movable<H> && !std::copyable<H>;

// A file written as a stream; the destructor closes as close() does.
template <class H>
concept Sink = Handle<H> && requires(H& h, std::string_view b) {
    h.write(b);
    h.close();
};

// A staging directory; the destructor discards it if it is unpublished.
template <class H>
concept Staged = Handle<H> && requires(H& h, std::string_view s, Seconds d) {
    { h.open(s) } -> Sink;
    h.publish(s, d);
    h.discard();
};

// A held lock; the destructor releases it.
template <class H>
concept Lock = Handle<H> && requires(H& h) { h.release(); };

// A started process tree; the destructor kills the tree.
template <class H>
concept Child = Handle<H> && requires(H& h, Seconds t, std::string_view b, String& out) {
    { h.read_line(t, out) } -> std::same_as<bool>;  // false at the end
    h.write(b);
    h.close_input();
    { h.wait(t) } -> std::same_as<int>;
    h.kill();
};

// Whole pages of the system's memory, zeroed; the destructor gives them
// back.
template <class H>
concept Pages = Handle<H> && requires(const H& h) {
    { h.bytes() } -> std::same_as<std::span<std::byte>>;
};

// The 21 members. standard_output() is the text's stdout(), named so since
// stdout is a C macro. wipe's stores are never optimized away.
template <class P>
concept Platform = requires(P& p, const P& q, std::string_view s, bool b, Seconds d, const Command& c,
                            std::span<const std::string_view> paths, std::span<std::byte> into,
                            std::size_t n, String& out, Facts& facts, Ran& ran, Vector<Row>& rows) {
    { q.name() } -> std::same_as<std::string_view>;
    p.config_dir(b, out);
    p.resolve(s, out);
    p.normcase(s, out);
    p.facts(s, facts);
    { p.synced(s, out) } -> std::same_as<bool>;  // false: none
    p.replace(s, s, d);
    { p.stage(s) } -> Staged;
    p.create(s, b);  // if absent
    p.read(s, out);  // a file, whole
    { p.lock(s, d) } -> Lock;
    p.program(s, out);
    { p.start(c) } -> Child;
    p.run(c, s, ran);
    p.code_page(out);
    p.standard_output().write(s);
    p.residual(paths, rows);
    p.random_bytes(into);
    p.wipe(into);
    { p.now() } -> std::same_as<std::chrono::sys_time<std::chrono::milliseconds>>;
    { p.pages(n) } -> Pages;
};

enum class Code : std::uint8_t { unsupported, config, path, exists, busy, io, start, deadline, output, memory };

// The code's name, as the text spells it: "unsupported", "config", ...
std::string_view name(Code code) noexcept;

// The bytes a PlatformError's detail holds in place.
inline constexpr std::size_t detail_bytes = 256;

// A detail composed in place, with no heap: text and numbers appended, the
// excess dropped. It holds one byte past detail_bytes, so a PlatformError
// made from a full one cuts it at a code point.
class Detail {
public:
    Detail() noexcept = default;
    explicit Detail(std::string_view text) noexcept { add(text); }
    Detail& add(std::string_view text) noexcept;
    // In decimal.
    Detail& number(std::uint64_t value) noexcept;
    // As 0x and eight lowercase hexadecimal digits.
    Detail& hex(std::uint32_t value) noexcept;
    [[nodiscard]] std::string_view view() const noexcept { return std::string_view(bytes_.data(), size_); }

private:
    std::array<char, detail_bytes + 1> bytes_{};
    std::size_t size_ = 0;
};

// A platform operation's typed error: a plain value, thrown by value and
// caught by its own type, with no base class and no heap allocation.
// detail names the system's error that caused it, by name and number, where
// one did. It never crosses a process boundary: a command's main maps it to
// its exit status and output.
struct PlatformError {
    PlatformError(Code code_, std::string_view operation_, std::string_view detail_) noexcept
        : code(code_), operation(operation_), detail(detail_) {}
    PlatformError(Code code_, std::string_view operation_, const Detail& detail_) noexcept
        : PlatformError(code_, operation_, detail_.view()) {}

    Code code;
    memory::Inline<32> operation;
    memory::Inline<detail_bytes> detail;
};

}  // namespace qiven3::platform
