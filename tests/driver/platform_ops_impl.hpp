#pragma once

// The drive's platform half's definitions (driver/platform_ops.hpp), which
// the instantiation files alone include.

#include <algorithm>
#include <chrono>
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

inline std::vector<std::string> strings(const Value& request, std::string_view key) {
    std::vector<std::string> out;
    for (const Value& item : argument(request, key).as_array(key)) {
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

inline std::size_t count(const Value& request, std::string_view key) {
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
Result name(Drive<P>& d, memory::Arena&, const Value&) {
    return Value(std::string(d.platform().name()));
}

template <class P>
Result config_dir(Drive<P>& d, memory::Arena& arena, const Value& request) {
    memory::String out = string(arena);
    d.platform().config_dir(flag(request, "create", false), out);
    return Value(copy(out));
}

template <class P>
Result resolve(Drive<P>& d, memory::Arena& arena, const Value& request) {
    memory::String out = string(arena);
    d.platform().resolve(text(request, "path"), out);
    return Value(copy(out));
}

template <class P>
Result normcase(Drive<P>& d, memory::Arena& arena, const Value& request) {
    memory::String out = string(arena);
    d.platform().normcase(text(request, "path"), out);
    return Value(copy(out));
}

template <class P>
Result facts(Drive<P>& d, memory::Arena& arena, const Value& request) {
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
Result synced(Drive<P>& d, memory::Arena& arena, const Value& request) {
    memory::String out = string(arena);
    return d.platform().synced(text(request, "path"), out) ? Result(Value(copy(out))) : std::nullopt;
}

template <class P>
Result replace(Drive<P>& d, memory::Arena&, const Value& request) {
    d.platform().replace(text(request, "path"), from_hex(text(request, "data")), deadline(request, "deadline"));
    return std::nullopt;
}

template <class P>
Result create(Drive<P>& d, memory::Arena&, const Value& request) {
    d.platform().create(text(request, "path"), flag(request, "directory", false));
    return std::nullopt;
}

template <class P>
Result read(Drive<P>& d, memory::Arena& arena, const Value& request) {
    memory::String out = string(arena);
    d.platform().read(text(request, "path"), out);
    return Value(to_hex(out));
}

template <class P>
Result program(Drive<P>& d, memory::Arena& arena, const Value& request) {
    memory::String out = string(arena);
    d.platform().program(text(request, "name"), out);
    return Value(copy(out));
}

// run's command: argv, cwd, env as [name, value] pairs, the deadline and
// the caps ({"out": n, "err": n}, 1 MiB each when absent).
template <class P>
Result run(Drive<P>& d, memory::Arena& arena, const Value& request) {
    const std::vector<std::string> argv = strings(request, "argv");
    const std::vector<std::string_view> argv_views = views(argv);
    const std::string cwd = request.find("cwd") ? text(request, "cwd") : std::string();
    std::vector<std::pair<std::string, std::string>> env;
    if (const Value* given = request.find("env")) {
        for (const Value& pair : given->as_array("env")) {
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
    const Value* caps = request.find("caps");
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
Result code_page(Drive<P>& d, memory::Arena& arena, const Value&) {
    memory::String out = string(arena);
    d.platform().code_page(out);
    return Value(copy(out));
}

// The text through stdout(): on the Windows adapter, the drive's own
// standard output, before this request's reply.
template <class P>
Result standard_output(Drive<P>& d, memory::Arena&, const Value& request) {
    const std::string utf8 = text(request, "text");
    std::cout << std::flush;
    d.platform().standard_output().write(utf8);
    return std::nullopt;
}

template <class P>
Result residual(Drive<P>& d, memory::Arena& arena, const Value& request) {
    const std::vector<std::string> paths = strings(request, "paths");
    const std::vector<std::string_view> path_views = views(paths);
    memory::Vector<platform::Row> rows{memory::Allocator<platform::Row>(arena)};
    d.platform().residual(path_views, rows);
    Value out = Value::array();
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
Result random_bytes(Drive<P>& d, memory::Arena& arena, const Value& request) {
    memory::String bytes(count(request, "n"), '\0', memory::Allocator<char>(arena));
    d.platform().random_bytes(std::as_writable_bytes(std::span<char>(bytes)));
    return Value(to_hex(bytes));
}

template <class P>
Result now(Drive<P>& d, memory::Arena&, const Value&) {
    const auto t = d.platform().now();
    Value value;
    value.set("ms", static_cast<std::int64_t>(t.time_since_epoch().count()));
    value.set("at", printed(t));
    return value;
}

// pages(n), held by its id: its size and whether each byte is zero.
template <class P>
Result pages(Drive<P>& d, memory::Arena&, const Value& request) {
    auto taken = d.platform().pages(count(request, "n"));
    Value value = pages_value(taken);
    const std::int64_t id = d.next_id();
    d.held().emplace(id, std::move(taken));
    value.set("id", id);
    return value;
}

template <class P>
auto& held(Drive<P>& d, const Value& request) {
    const auto found = d.held().find(integer(request, "id"));
    if (found == d.held().end()) {
        throw RequestError("no pages held by that id");
    }
    return *found;
}

template <class P>
Result pages_check(Drive<P>& d, memory::Arena&, const Value& request) {
    return pages_value(held(d, request).second);
}

template <class P>
Result pages_free(Drive<P>& d, memory::Arena&, const Value& request) {
    d.held().erase(held(d, request).first);
    return std::nullopt;
}

}  // namespace ops

template <platform::Platform P>
PlatformDrive<P>::PlatformDrive(Session& session, Make make) : session_(session), make_(make) {
    const std::pair<const char*, Operation> table[] = {
        {"name", ops::name<P>},
        {"config_dir", ops::config_dir<P>},
        {"resolve", ops::resolve<P>},
        {"normcase", ops::normcase<P>},
        {"facts", ops::facts<P>},
        {"synced", ops::synced<P>},
        {"replace", ops::replace<P>},
        {"create", ops::create<P>},
        {"read", ops::read<P>},
        {"program", ops::program<P>},
        {"run", ops::run<P>},
        {"code_page", ops::code_page<P>},
        {"stdout", ops::standard_output<P>},
        {"residual", ops::residual<P>},
        {"random_bytes", ops::random_bytes<P>},
        {"now", ops::now<P>},
        {"pages", ops::pages<P>},
        {"pages_check", ops::pages_check<P>},
        {"pages_free", ops::pages_free<P>},
    };
    for (const auto& [key, operation] : table) {
        table_.emplace(key, operation);
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
Value PlatformDrive<P>::reply(const Value& request) {
    std::string op;
    try {
        op = text(request, "op");
        const auto found = table_.find(op);
        if (found == table_.end()) {
            return ok_reply(common(session_, op, request));
        }
        P& p = platform();
        if (!pages_) {
            pages_.emplace(p.pages(result_bytes));
        }
        memory::Arena arena(pages_->bytes());
        return ok_reply(found->second(*this, arena, request));
    } catch (...) {
        return caught(op);
    }
}

}  // namespace qiven3::drive
