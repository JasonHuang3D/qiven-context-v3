// The drive's platform operations: each the contract's operation on the
// chosen adapter, its arguments and its result on the wire; adapter_for
// and current as a command's main calls them; and the fake's own.
//
// Bytes cross as hexadecimal ("data", and results of bytes); a deadline as
// an integer of milliseconds, 5000 when absent.

#include <chrono>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "driver/drive.hpp"
#include "platform/current.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::drive {
namespace {

using platform::Platform;
using Result = std::optional<Value>;

std::string state_name(platform::State state) {
    switch (state) {
    case platform::State::on: return "on";
    case platform::State::off: return "off";
    case platform::State::locked: return "locked";
    case platform::State::unknown: return "unknown";
    }
    return "unknown";
}

Value reading(const platform::Reading& r) {
    Value value;
    value.set("state", state_name(r.state));
    value.set("reason", r.reason);
    return value;
}

std::vector<std::string> strings(const Value& request, std::string_view key) {
    std::vector<std::string> out;
    for (const Value& item : argument(request, key).as_array(key)) {
        out.push_back(item.as_string(key));
    }
    return out;
}

// The command of start and run: argv, cwd, env as [name, value] pairs,
// the deadline and the caps ({"out": n, "err": n}, 1 MiB each when absent).
platform::Command command(const Value& request) {
    platform::Command c;
    c.argv = strings(request, "argv");
    c.cwd = request.find("cwd") ? text(request, "cwd") : std::string();
    if (const Value* env = request.find("env")) {
        for (const Value& pair : env->as_array("env")) {
            const auto& two = pair.as_array("env");
            if (two.size() != 2) {
                throw RequestError("env: each item is [name, value]");
            }
            c.env.emplace_back(two[0].as_string("env"), two[1].as_string("env"));
        }
    }
    c.deadline = deadline(request, "deadline");
    const Value* caps = request.find("caps");
    const std::int64_t mib = 1 << 20;
    c.caps.out = static_cast<std::uint64_t>(caps ? integer(*caps, "out", mib) : mib);
    c.caps.err = static_cast<std::uint64_t>(caps ? integer(*caps, "err", mib) : mib);
    return c;
}

std::string two(unsigned value) {
    return std::string(1, static_cast<char>('0' + value / 10 % 10)) +
           static_cast<char>('0' + value % 10);
}

// The text's printed form, YYYY-MM-DDTHH:MM:SS.mmmZ.
std::string printed(std::chrono::sys_time<std::chrono::milliseconds> t) {
    using namespace std::chrono;
    const auto day = floor<days>(t);
    const year_month_day date{day};
    const hh_mm_ss<milliseconds> time{t - day};
    const int y = static_cast<int>(date.year());
    const auto ms = static_cast<unsigned>(time.subseconds().count());
    return two(static_cast<unsigned>(y / 100)) + two(static_cast<unsigned>(y % 100)) + "-" +
           two(static_cast<unsigned>(date.month())) + "-" + two(static_cast<unsigned>(date.day())) +
           "T" + two(static_cast<unsigned>(time.hours().count())) + ":" +
           two(static_cast<unsigned>(time.minutes().count())) + ":" +
           two(static_cast<unsigned>(time.seconds().count())) + "." +
           std::string(1, static_cast<char>('0' + ms / 100)) + two(ms % 100) + "Z";
}

std::optional<std::string> config_of(const Value& request) {
    return request.find("config") ? std::optional<std::string>(text(request, "config")) : std::nullopt;
}

Result adapter_for(Session&, const Value& request) {
    return Value(platform::adapter_for(text(request, "system"), config_of(request))->name());
}

Result current(Session&, const Value& request) {
    return Value(platform::current(config_of(request))->name());
}

Result name(Session& s, const Value&) { return Value(s.platform().name()); }

Result config_dir(Session& s, const Value& request) {
    return Value(s.platform().config_dir(flag(request, "create", false)));
}

Result resolve(Session& s, const Value& request) { return Value(s.platform().resolve(text(request, "path"))); }

Result facts(Session& s, const Value& request) {
    const platform::Facts f = s.platform().facts(text(request, "path"));
    Value value;
    value.set("volume", f.volume);
    value.set("protection", reading(f.protection));
    value.set("file_history", reading(f.file_history));
    value.set("bitlocker", reading(f.bitlocker));
    return value;
}

Result synced(Session& s, const Value& request) {
    const std::optional<std::string> client = s.platform().synced(text(request, "path"));
    return client ? Result(Value(*client)) : std::nullopt;
}

Result replace(Session& s, const Value& request) {
    s.platform().replace(text(request, "path"), from_hex(text(request, "data")), deadline(request, "deadline"));
    return std::nullopt;
}

Result create(Session& s, const Value& request) {
    s.platform().create(text(request, "path"), flag(request, "directory", false));
    return std::nullopt;
}

Result read(Session& s, const Value& request) { return Value(to_hex(s.platform().read(text(request, "path")))); }

Result program(Session& s, const Value& request) { return Value(s.platform().program(text(request, "name"))); }

Result run(Session& s, const Value& request) {
    const std::string data = request.find("data") ? from_hex(text(request, "data")) : std::string();
    const platform::Ran ran = s.platform().run(command(request), data);
    Value value;
    value.set("code", ran.code);
    value.set("out", to_hex(ran.out));
    value.set("err", to_hex(ran.err));
    return value;
}

Result code_page(Session& s, const Value&) { return Value(s.platform().code_page()); }

// The text through stdout(): on the Windows adapter, the drive's own
// standard output, before this request's reply.
Result standard_output(Session& s, const Value& request) {
    const std::string utf8 = text(request, "text");
    platform::Output& output = s.platform().standard_output();
    std::cout << std::flush;
    output.write(utf8);
    return std::nullopt;
}

Result residual(Session& s, const Value& request) {
    Value rows = Value::array();
    for (const platform::Row& row : s.platform().residual(strings(request, "paths"))) {
        Value value;
        value.set("name", row.name);
        value.set("applies", row.applies);
        value.set("kept", row.kept);
        value.set("how_long", row.how_long);
        value.set("owner_can", row.owner_can);
        rows.push(std::move(value));
    }
    return rows;
}

Result random_bytes(Session& s, const Value& request) {
    const std::int64_t n = integer(request, "n");
    if (n < 0) {
        throw RequestError("n: a count, not negative");
    }
    return Value(to_hex(s.platform().random_bytes(static_cast<std::size_t>(n))));
}

Result now(Session& s, const Value&) {
    const auto t = s.platform().now();
    Value value;
    value.set("ms", static_cast<std::int64_t>(t.time_since_epoch().count()));
    value.set("at", printed(t));
    return value;
}

// The fake's own: its clock, in microseconds; its record; its stream's bytes.
Result set_clock(Session& s, const Value& request) {
    s.fake("set_clock").set_clock(
        std::chrono::sys_time<std::chrono::microseconds>(std::chrono::microseconds(integer(request, "us"))));
    return std::nullopt;
}

Result calls(Session& s, const Value&) {
    Value names = Value::array();
    for (const std::string& call : s.fake("calls").calls()) {
        names.push(Value(call));
    }
    return names;
}

Result written(Session& s, const Value&) { return Value(to_hex(s.fake("written").written())); }

}  // namespace

void add_platform_operations(Operations& into) {
    const std::pair<const char*, Result (*)(Session&, const Value&)> table[] = {
        {"adapter_for", adapter_for}, {"current", current}, {"name", name},
        {"config_dir", config_dir}, {"resolve", resolve}, {"facts", facts},
        {"synced", synced}, {"replace", replace}, {"create", create},
        {"read", read}, {"program", program}, {"run", run},
        {"code_page", code_page}, {"stdout", standard_output}, {"residual", residual},
        {"random_bytes", random_bytes}, {"now", now}, {"set_clock", set_clock},
        {"calls", calls}, {"written", written},
    };
    for (const auto& [key, operation] : table) {
        into.emplace(key, operation);
    }
}

}  // namespace qiven3::drive
