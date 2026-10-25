#pragma once

// The drive's requests on a Child (driver/platform_ops.hpp), which
// driver/platform_ops_impl.hpp alone includes, and the command start and
// run both take. start, a Child held by an id; child_read_line, its next
// line in hexadecimal, or none at its stream's end; child_write, its
// bytes; child_close_input; child_wait, its exit code; child_kill, its
// tree, the Child kept; child_end, the Child ended, its destructor killing
// the tree. A timeout is an integer of milliseconds, as a deadline is.
// Each is the contract's member, called as a caller calls it, on the
// member's budget.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "driver/drive.hpp"
#include "driver/platform_files.hpp"
#include "driver/platform_ops.hpp"
#include "memory/arena.hpp"
#include "platform/platform.hpp"

namespace qiven3::drive::ops {

inline std::vector<std::string> strings(ArgView request, std::string_view key) {
    std::vector<std::string> out;
    for (const ArgView item : argument(request, key).as_array(key)) {
        out.push_back(item.as_string(key));
    }
    return out;
}

inline std::vector<std::string_view> views(const std::vector<std::string>& texts) {
    return std::vector<std::string_view>(texts.begin(), texts.end());
}

// A stream's cap from the request's "caps", 1 MiB when absent; never
// negative.
inline std::uint64_t cap(ArgView request, std::string_view key) {
    const std::optional<ArgView> caps = request.find("caps");
    const std::int64_t n = caps ? integer(*caps, key, std::int64_t{1} << 20) : std::int64_t{1} << 20;
    if (n < 0) {
        throw RequestError(std::string(key) + ": a cap, not negative");
    }
    return static_cast<std::uint64_t>(n);
}

// f(command) over the request's command: argv, cwd, env as [name, value]
// pairs, the deadline and the caps ({"out": n, "err": n}). The command
// views texts that live until f returns.
template <class F>
decltype(auto) with_command(ArgView request, F&& f) {
    const std::vector<std::string> argv = strings(request, "argv");
    const std::vector<std::string_view> argv_views = views(argv);
    const std::string cwd = request.find("cwd") ? text(request, "cwd") : std::string();
    std::vector<std::pair<std::string, std::string>> env;
    if (const std::optional<ArgView> given = request.find("env")) {
        for (const ArgView pair : given->as_array("env")) {
            const auto& two_items = pair.as_array("env");
            if (two_items.size() != 2) {
                throw RequestError("env: each item is [name, value]");
            }
            env.emplace_back(two_items[0].as_string("env"), two_items[1].as_string("env"));
        }
    }
    const std::vector<std::pair<std::string_view, std::string_view>> env_views(env.begin(), env.end());
    platform::Command command;
    command.argv = argv_views;
    command.cwd = cwd;
    command.env = env_views;
    command.deadline = deadline(request, "deadline");
    command.caps.out = cap(request, "out");
    command.caps.err = cap(request, "err");
    return std::forward<F>(f)(std::as_const(command));
}

template <class P>
std::optional<Value> start(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    return with_command(request, [&](const platform::Command& command) {
        return std::optional<Value>(keep(d, d.children(), d.platform().start(command)));
    });
}

template <class P>
std::optional<Value> child_read_line(PlatformDrive<P>& d, memory::Arena& arena, ArgView request) {
    const auto found = by_id(d.children(), request, "child");
    memory::String line{memory::Allocator<char>(arena)};
    if (!found->second.read_line(deadline(request, "timeout"), line)) {
        return std::nullopt;
    }
    return scalar(to_hex(std::string_view(line.data(), line.size())));
}

template <class P>
std::optional<Value> child_write(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    by_id(d.children(), request, "child")->second.write(from_hex(text(request, "data")));
    return std::nullopt;
}

template <class P>
std::optional<Value> child_close_input(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    by_id(d.children(), request, "child")->second.close_input();
    return std::nullopt;
}

template <class P>
std::optional<Value> child_wait(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    const int code = by_id(d.children(), request, "child")->second.wait(deadline(request, "timeout"));
    return scalar(std::int64_t{code});
}

template <class P>
std::optional<Value> child_kill(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    by_id(d.children(), request, "child")->second.kill();
    return std::nullopt;
}

template <class P>
std::optional<Value> child_end(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    d.children().erase(by_id(d.children(), request, "child"));
    return std::nullopt;
}

}  // namespace qiven3::drive::ops
