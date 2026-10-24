#pragma once

// The drive's platform half's definitions (driver/platform_ops.hpp), which
// the instantiation files alone include.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "driver/platform_ops.hpp"

namespace qiven3::drive {
namespace ops {

using Result = std::optional<Value>;

inline memory::String string(memory::Arena& arena) { return memory::String(memory::Allocator<char>(arena)); }

inline std::string copy(const memory::String& text) { return std::string(text.data(), text.size()); }

inline std::string state_name(platform::State state) {
    switch (state) {
    case platform::State::on: return "on";
    case platform::State::off: return "off";
    case platform::State::locked: return "locked";
    case platform::State::unknown: return "unknown";
    }
    return "unknown";
}

inline Value reading(const platform::Reading& r) {
    Value value;
    value.set("state", state_name(r.state));
    value.set("reason", copy(r.reason));
    return value;
}

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

inline std::string two(unsigned value) {
    return std::string(1, static_cast<char>('0' + value / 10 % 10)) + static_cast<char>('0' + value % 10);
}

// The text's printed form, YYYY-MM-DDTHH:MM:SS.mmmZ.
inline std::string printed(std::chrono::sys_time<std::chrono::milliseconds> t) {
    using namespace std::chrono;
    const auto day = floor<days>(t);
    const year_month_day date{day};
    const hh_mm_ss<milliseconds> time{t - day};
    const int y = static_cast<int>(date.year());
    const auto ms = static_cast<unsigned>(time.subseconds().count());
    return two(static_cast<unsigned>(y / 100)) + two(static_cast<unsigned>(y % 100)) + "-" +
           two(static_cast<unsigned>(date.month())) + "-" + two(static_cast<unsigned>(date.day())) + "T" +
           two(static_cast<unsigned>(time.hours().count())) + ":" +
           two(static_cast<unsigned>(time.minutes().count())) + ":" +
           two(static_cast<unsigned>(time.seconds().count())) + "." +
           std::string(1, static_cast<char>('0' + ms / 100)) + two(ms % 100) + "Z";
}

inline std::size_t count(ArgView request, std::string_view key) {
    const std::int64_t n = integer(request, key);
    if (n < 0) {
        throw RequestError(std::string(key) + ": a count, not negative");
    }
    return static_cast<std::size_t>(n);
}

template <class Pages>
Value pages_value(const Pages& pages) {
    const std::span<std::byte> bytes = pages.bytes();
    Value value;
    value.set("size", static_cast<std::int64_t>(bytes.size()));
    value.set("zero", std::ranges::all_of(bytes, [](std::byte b) { return b == std::byte{0}; }));
    return value;
}

template <class P>
using Drive = PlatformDrive<P>;

template <class P>
Result name(Drive<P>& d, memory::Arena&, ArgView) {
    return scalar(d.platform().name());
}

template <class P>
Result config_dir(Drive<P>& d, memory::Arena& arena, ArgView request) {
    memory::String out = string(arena);
    d.platform().config_dir(flag(request, "create", false), out);
    return scalar(copy(out));
}

template <class P>
Result resolve(Drive<P>& d, memory::Arena& arena, ArgView request) {
    memory::String out = string(arena);
    d.platform().resolve(text(request, "path"), out);
    return scalar(copy(out));
}

template <class P>
Result normcase(Drive<P>& d, memory::Arena& arena, ArgView request) {
    memory::String out = string(arena);
    d.platform().normcase(text(request, "path"), out);
    return scalar(copy(out));
}

template <class P>
Result facts(Drive<P>& d, memory::Arena& arena, ArgView request) {
    platform::Facts f(arena);
    d.platform().facts(text(request, "path"), f);
    Value value;
    value.set("volume", copy(f.volume));
    value.set("protection", reading(f.protection));
    value.set("file_history", reading(f.file_history));
    value.set("bitlocker", reading(f.bitlocker));
    return value;
}

template <class P>
Result synced(Drive<P>& d, memory::Arena& arena, ArgView request) {
    memory::String out = string(arena);
    return d.platform().synced(text(request, "path"), out) ? Result(scalar(copy(out))) : std::nullopt;
}

template <class P>
Result replace(Drive<P>& d, memory::Arena&, ArgView request) {
    d.platform().replace(text(request, "path"), from_hex(text(request, "data")), deadline(request, "deadline"));
    return std::nullopt;
}

template <class P>
Result create(Drive<P>& d, memory::Arena&, ArgView request) {
    d.platform().create(text(request, "path"), flag(request, "directory", false));
    return std::nullopt;
}

template <class P>
Result remove_directory(Drive<P>& d, memory::Arena&, ArgView request) {
    return scalar(d.platform().remove_directory(text(request, "path")));
}

template <class P>
Result read(Drive<P>& d, memory::Arena& arena, ArgView request) {
    memory::String out = string(arena);
    d.platform().read(text(request, "path"), out);
    return scalar(to_hex(out));
}

template <class P>
Result program(Drive<P>& d, memory::Arena& arena, ArgView request) {
    memory::String out = string(arena);
    d.platform().program(text(request, "name"), out);
    return scalar(copy(out));
}

// run's command: argv, cwd, env as [name, value] pairs, the deadline and
// the caps ({"out": n, "err": n}, 1 MiB each when absent).
template <class P>
Result run(Drive<P>& d, memory::Arena& arena, ArgView request) {
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
    const std::optional<ArgView> caps = request.find("caps");
    const std::int64_t mib = 1 << 20;
    command.caps.out = static_cast<std::uint64_t>(caps ? integer(*caps, "out", mib) : mib);
    command.caps.err = static_cast<std::uint64_t>(caps ? integer(*caps, "err", mib) : mib);
    const std::string data = request.find("data") ? from_hex(text(request, "data")) : std::string();
    platform::Ran ran(arena);
    d.platform().run(command, data, ran);
    Value value;
    value.set("code", ran.code);
    value.set("out", to_hex(ran.out));
    value.set("err", to_hex(ran.err));
    return value;
}

template <class P>
Result code_page(Drive<P>& d, memory::Arena& arena, ArgView) {
    memory::String out = string(arena);
    d.platform().code_page(out);
    return scalar(copy(out));
}

// The text through stdout(): on the Windows adapter, the drive's own
// standard output, before this request's reply.
template <class P>
Result standard_output(Drive<P>& d, memory::Arena&, ArgView request) {
    const std::string utf8 = text(request, "text");
    std::cout << std::flush;
    d.platform().standard_output().write(utf8);
    return std::nullopt;
}

template <class P>
Result residual(Drive<P>& d, memory::Arena& arena, ArgView request) {
    const std::vector<std::string> paths = strings(request, "paths");
    const std::vector<std::string_view> path_views = views(paths);
    memory::Vector<platform::Row> rows{memory::Allocator<platform::Row>(arena)};
    d.platform().residual(path_views, rows);
    Value out = array();
    for (const platform::Row& row : rows) {
        Value value;
        value.set("name", copy(row.name));
        value.set("applies", copy(row.applies));
        value.set("kept", copy(row.kept));
        value.set("how_long", copy(row.how_long));
        value.set("owner_can", copy(row.owner_can));
        out.push(std::move(value));
    }
    return out;
}

template <class P>
Result random_bytes(Drive<P>& d, memory::Arena& arena, ArgView request) {
    memory::String bytes(count(request, "n"), '\0', memory::Allocator<char>(arena));
    d.platform().random_bytes(std::as_writable_bytes(std::span<char>(bytes)));
    return scalar(to_hex(bytes));
}

// wipe of n bytes the drive filled with 0xa5: the bytes read back after.
template <class P>
Result wipe(Drive<P>& d, memory::Arena& arena, ArgView request) {
    memory::String bytes(count(request, "n"), static_cast<char>(0xa5), memory::Allocator<char>(arena));
    d.platform().wipe(std::as_writable_bytes(std::span<char>(bytes)));
    return scalar(to_hex(bytes));
}

template <class P>
Result now(Drive<P>& d, memory::Arena&, ArgView) {
    const auto t = d.platform().now();
    Value value;
    value.set("ms", static_cast<std::int64_t>(t.time_since_epoch().count()));
    value.set("at", printed(t));
    return value;
}

// pages(n), held by its id: its size and whether each byte is zero.
template <class P>
Result pages(Drive<P>& d, memory::Arena&, ArgView request) {
    auto taken = d.platform().pages(count(request, "n"));
    Value value = pages_value(taken);
    const std::int64_t id = d.next_id();
    d.held().emplace(id, std::move(taken));
    value.set("id", id);
    return value;
}

template <class P>
auto& held(Drive<P>& d, ArgView request) {
    const auto found = d.held().find(integer(request, "id"));
    if (found == d.held().end()) {
        throw RequestError("no pages held by that id");
    }
    return *found;
}

template <class P>
Result pages_check(Drive<P>& d, memory::Arena&, ArgView request) {
    return pages_value(held(d, request).second);
}

template <class P>
Result pages_free(Drive<P>& d, memory::Arena&, ArgView request) {
    d.held().erase(held(d, request).first);
    return std::nullopt;
}

// The stated budgets (driver/platform_ops.hpp): a sum past the size's
// range asks every byte, which no system lends.
inline std::size_t member(ArgView) { return member_budget; }

inline std::size_t none(ArgView) { return 0; }

inline std::size_t sum(std::size_t a, std::size_t b) { return memory::add(a, b).value_or(SIZE_MAX); }

inline std::size_t read_budget(ArgView request) {
    return request.find("size") ? sum(member_budget, count(request, "size")) : member_budget;
}

inline std::size_t run_budget(ArgView request) {
    const std::optional<ArgView> caps = request.find("caps");
    const std::int64_t mib = 1 << 20;
    const auto cap = [&](std::string_view key) {
        const std::int64_t n = caps ? integer(*caps, key, mib) : mib;
        if (n < 0) {
            throw RequestError(std::string(key) + ": a cap, not negative");
        }
        return static_cast<std::size_t>(n);
    };
    return sum(member_budget, sum(cap("out"), cap("err")));
}

#if defined(QIVEN3_FAULTS)
// The adapter's fault points, which the fake alone has.
template <class P>
concept Faulted = requires(P& p, memory::Countdown* countdown) { p.attach(countdown); };
#endif

}  // namespace ops

template <platform::Platform P>
PlatformDrive<P>::PlatformDrive(Session& session, Make make, Lent lent)
    : session_(session), make_(make), lent_(lent) {
    const std::pair<const char*, Entry> table[] = {
        {"name", {ops::name<P>, ops::member}},
        {"config_dir", {ops::config_dir<P>, ops::member}},
        {"resolve", {ops::resolve<P>, ops::member}},
        {"normcase", {ops::normcase<P>, ops::member}},
        {"facts", {ops::facts<P>, ops::member}},
        {"synced", {ops::synced<P>, ops::member}},
        {"replace", {ops::replace<P>, ops::member}},
        {"create", {ops::create<P>, ops::member}},
        {"remove_directory", {ops::remove_directory<P>, ops::member}},
        {"read", {ops::read<P>, ops::read_budget}},
        {"program", {ops::program<P>, ops::member}},
        {"run", {ops::run<P>, ops::run_budget}},
        {"code_page", {ops::code_page<P>, ops::member}},
        {"stdout", {ops::standard_output<P>, ops::member}},
        {"residual", {ops::residual<P>, ops::member}},
        {"random_bytes", {ops::random_bytes<P>, ops::member}},
        {"wipe", {ops::wipe<P>, ops::member}},
        {"now", {ops::now<P>, ops::member}},
        {"pages", {ops::pages<P>, ops::none}},
        {"pages_check", {ops::pages_check<P>, ops::none}},
        {"pages_free", {ops::pages_free<P>, ops::none}},
    };
    for (const auto& [key, entry] : table) {
        table_.emplace(key, entry);
    }
}

template <platform::Platform P>
P& PlatformDrive<P>::platform() {
    if (!platform_) {
        make_(platform_, session_.options());
    }
    return *platform_;
}

template <platform::Platform P>
std::size_t PlatformDrive<P>::holding() const {
    if (!platform_) {
        return 0;
    }
    if (lent_ != nullptr) {
        return lent_(*platform_);
    }
    std::size_t bytes = 0;
    for (const auto& [id, pages] : held_) {
        bytes += pages.bytes().size();
    }
    return bytes;
}

// The operation as its caller runs it: pages(budget), then the operation
// on an arena over those bytes alone; the pages go back at the end.
template <platform::Platform P>
Value PlatformDrive<P>::run(const Entry& entry, ArgView request) {
    P& p = platform();
#if defined(QIVEN3_FAULTS)
    Faults& faults = session_.faults();
    if constexpr (ops::Faulted<P>) {
        p.attach(&faults.platform);
    } else if (faults.armed == &faults.platform) {
        throw RequestError("not applicable: this adapter has no fault points; the fake's calls fail in turn");
    }
#endif
    if (entry.budget == nullptr) {
        memory::Arena none{std::span<std::byte>{}};
        return ok_reply(entry.operation(*this, none, request));
    }
    const std::size_t bytes = budget(request, entry.budget(request));
    std::optional<Pages> pages;
    if (bytes > 0) {
        pages.emplace(p.pages(bytes));
    }
    const std::span<std::byte> span = pages ? pages->bytes().first(bytes) : std::span<std::byte>{};
    return ok_reply(on_arena(session_, span,
                             [&](memory::Arena& arena) { return entry.operation(*this, arena, request); }));
}

template <platform::Platform P>
Value PlatformDrive<P>::reply(ArgView request) {
    std::string op;
    Value answer;
    try {
        op = text(request, "op");
        begin(session_, request);
        const auto found = table_.find(op);
        answer = found == table_.end() ? ok_reply(common(session_, op, request)) : run(found->second, request);
    } catch (...) {
        answer = caught(op);
    }
#if defined(QIVEN3_FAULTS)
    if constexpr (ops::Faulted<P>) {
        if (platform_) {
            platform_->attach(nullptr);
        }
    }
#endif
    return finish(session_, std::move(answer), holding());
}

}  // namespace qiven3::drive
