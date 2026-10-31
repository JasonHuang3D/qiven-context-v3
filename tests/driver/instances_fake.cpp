// qiven3-drive-fake: the drive for the fake platform, its instantiation
// file. It instantiates the drive's platform half for the fake, adds the
// fake's own operations and runs the request loop (driver/drive.hpp), with
// the store's requests where --store selects one (driver/store_ops.hpp).
//
// The fake's own: set_clock, its clock in microseconds; set_read_error,
// read's io for a path; refuse_pages, the k-th call of pages from now
// refused; calls, its record; written, its stream's bytes; lent, the bytes
// of the pages it has lent and not had back, which each reply's "held"
// gives too. Under QIVEN_FAULTS each of the fake's calls is a fault point
// of a request's "fault" (driver/drive.hpp).
//
// For the files' cases: set_busy, how often replace's rename ("target",
// "temporary"), publish's ("publish") and lock ("lock") refuse before
// they succeed, -1 for ever; kept, what the fake's files hold open (files,
// staging directories, locks). For location's: set_facts, the facts of
// every path ("volume" and each condition's state, "fake" its reason);
// set_synced, the synced folders, [folder, client] each; set_links, the
// links resolve follows, [from, to] each; set_encoding, the stream's,
// "utf8" or "ascii"; location, location's command (qiven3/location.hpp)
// run by its main on the fake as the binary runs it, "argv" from the
// subcommand on, giving its exit "code", the bytes it wrote ("out", in
// hexadecimal), the fake's "calls" it made and its arena's "peak"; and
// store_dir, the recorded directory as "dir", or the StoreError refusing
// it as "refused" (driver/store_wire.hpp's form). location takes its own
// pages, its budget's, and no arena of the drive's; store_dir's is the
// member's. For the processes' cases: set_programs, the programs start
// and run find by their whole argv (the fake's Program); set_found, the
// paths program(name) gives.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "driver/platform_ops_impl.hpp"
#include "driver/store_ops.hpp"
#include "driver/store_wire.hpp"
#include "qiven3/location.hpp"
#include "qiven3/location_impl.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::drive {

template class PlatformDrive<platform::fake::FakePlatform>;

namespace {

using Fake = platform::fake::FakePlatform;
using Drive = PlatformDrive<Fake>;
using Result = std::optional<Value>;
using Pairs = std::vector<std::pair<std::string, std::string>>;

void make(std::optional<Fake>& into, const Options& options) {
    platform::fake::Config config;
    config.root = *options.root;
    config.config_dir = options.config ? *options.config : *options.root + "/qiven3";
    config.seed = options.seed;
    into.emplace(std::move(config));
}

Result set_clock(Drive& d, memory::Arena&, ArgView request) {
    d.platform().set_clock(
        std::chrono::sys_time<std::chrono::microseconds>(std::chrono::microseconds(integer(request, "us"))));
    return std::nullopt;
}

Result set_read_error(Drive& d, memory::Arena&, ArgView request) {
    d.platform().set_read_error(text(request, "path"), text(request, "detail"));
    return std::nullopt;
}

Result refuse_pages(Drive& d, memory::Arena&, ArgView request) {
    d.platform().refuse_pages(ops::count(request, "at"));
    return std::nullopt;
}

Result calls(Drive& d, memory::Arena&, ArgView) {
    Value names = array();
    for (const std::string& call : d.platform().calls()) {
        names.push(scalar(call));
    }
    return names;
}

Result written(Drive& d, memory::Arena&, ArgView) { return scalar(to_hex(d.platform().written())); }

std::size_t lent_of(const Fake& fake) { return fake.lent(); }

Result lent(Drive& d, memory::Arena&, ArgView) {
    return scalar(static_cast<std::int64_t>(d.platform().lent()));
}

// A busy count: absent 0, -1 for ever.
std::uint64_t times(ArgView request, std::string_view key) {
    const std::int64_t n = integer(request, key, 0);
    if (n < -1) {
        throw RequestError(std::string(key) + ": a count, or -1 for ever");
    }
    return n == -1 ? platform::fake::never : static_cast<std::uint64_t>(n);
}

Result set_busy(Drive& d, memory::Arena&, ArgView request) {
    d.platform().set_busy(platform::fake::Busy{times(request, "target"), times(request, "temporary"),
                                               times(request, "publish"), times(request, "lock")});
    return std::nullopt;
}

Result kept(Drive& d, memory::Arena&, ArgView) {
    return scalar(static_cast<std::int64_t>(d.platform().files().kept()));
}

platform::fake::FakeReading reading(ArgView request, std::string_view key) {
    const std::string name = text(request, key);
    constexpr std::array<std::pair<std::string_view, platform::State>, 4> states = {{
        {"on", platform::State::on},
        {"off", platform::State::off},
        {"locked", platform::State::locked},
        {"unknown", platform::State::unknown},
    }};
    for (const auto& [spelled, state] : states) {
        if (name == spelled) {
            return platform::fake::FakeReading{state, "fake"};
        }
    }
    throw RequestError(std::string(key) + ": on, off, locked or unknown");
}

Result set_facts(Drive& d, memory::Arena&, ArgView request) {
    d.platform().set_default_facts(platform::fake::FakeFacts{text(request, "volume"), reading(request, "protection"),
                                                             reading(request, "file_history"),
                                                             reading(request, "bitlocker")});
    return std::nullopt;
}

// [[a, b], ...] as pairs.
Pairs pairs(ArgView request, std::string_view key) {
    Pairs out;
    for (const ArgView item : argument(request, key).as_array(key)) {
        const std::vector<ArgView> two = item.as_array(key);
        if (two.size() != 2) {
            throw RequestError(std::string(key) + ": pairs, two texts each");
        }
        out.emplace_back(two[0].as_string(key), two[1].as_string(key));
    }
    return out;
}

Result set_synced(Drive& d, memory::Arena&, ArgView request) {
    d.platform().set_synced(pairs(request, "folders"));
    return std::nullopt;
}

Result set_links(Drive& d, memory::Arena&, ArgView request) {
    d.platform().set_links(pairs(request, "links"));
    return std::nullopt;
}

Result set_encoding(Drive& d, memory::Arena&, ArgView request) {
    const std::string name = text(request, "encoding");
    if (name != "utf8" && name != "ascii") {
        throw RequestError("encoding: utf8 or ascii");
    }
    d.platform().set_encoding(name == "ascii" ? platform::fake::Encoding::ascii : platform::fake::Encoding::utf8);
    return std::nullopt;
}

Result location_command(Drive& d, memory::Arena&, ArgView request) {
    const std::vector<std::string> texts = ops::strings(request, "argv");
    const std::vector<std::string_view> argv = ops::views(texts);
    Fake& fake = d.platform();
    const std::size_t mark = fake.written().size();
    const std::size_t first = fake.calls().size();
    std::size_t peak = 0;
    location::Hooks hooks;
    hooks.peak = &peak;
    const int code = location::main(std::span<const std::string_view>(argv), fake, hooks);
    Value made = array();
    for (std::size_t i = first; i < fake.calls().size(); ++i) {
        made.push(scalar(fake.calls()[i]));
    }
    Value value;
    value.set("code", code);
    value.set("out", to_hex(std::string_view(fake.written()).substr(mark)));
    value.set("calls", std::move(made));
    value.set("peak", static_cast<std::int64_t>(peak));
    return value;
}

Result store_dir(Drive& d, memory::Arena& arena, ArgView) {
    memory::String dir = ops::string(arena);
    Value value;
    try {
        location::store_dir(d.platform(), dir);
    } catch (const store::StoreError& error) {
        value.set("refused", wire::error(error));
        return value;
    }
    value.set("dir", ops::copy(dir));
    return value;
}

// Each program by its whole argv: its "lines" (each given LF), "err",
// exit "code", whether it "hang"s, the stream it "flood"s, "out" or "err",
// and what it "echo"es, "cwd" or "env"; the earlier ones dropped.
Result set_programs(Drive& d, memory::Arena&, ArgView request) {
    std::map<std::vector<std::string>, platform::fake::Program> programs;
    for (const ArgView item : argument(request, "programs").as_array("programs")) {
        platform::fake::Program program;
        program.lines = item.find("lines") ? ops::strings(item, "lines") : std::vector<std::string>();
        program.err = item.find("err") ? text(item, "err") : std::string();
        const std::int64_t code = integer(item, "code", 0);
        if (code < std::numeric_limits<int>::min() || code > std::numeric_limits<int>::max()) {
            throw RequestError("code: an int");
        }
        program.code = static_cast<int>(code);
        program.hang = flag(item, "hang", false);
        program.flood = item.find("flood") ? text(item, "flood") : std::string();
        if (!program.flood.empty() && program.flood != "out" && program.flood != "err") {
            throw RequestError("flood: out or err");
        }
        program.echo = item.find("echo") ? text(item, "echo") : std::string();
        if (!program.echo.empty() && program.echo != "cwd" && program.echo != "env") {
            throw RequestError("echo: cwd or env");
        }
        programs.insert_or_assign(ops::strings(item, "argv"), std::move(program));
    }
    d.platform().set_programs(std::move(programs));
    return std::nullopt;
}

// program(name)'s paths, [name, path] each; the earlier ones dropped.
Result set_found(Drive& d, memory::Arena&, ArgView request) {
    std::map<std::string, std::string, std::less<>> found;
    for (auto& [name, path] : pairs(request, "found")) {
        found.insert_or_assign(std::move(name), std::move(path));
    }
    d.platform().set_found(std::move(found));
    return std::nullopt;
}

struct Own {
    const char* name;
    Drive::Operation operation;
    Drive::Budget budget;
};

// The fake's own operations; store_dir's arena the member's.
constexpr std::array<Own, 16> own = {{
    {"set_clock", set_clock, nullptr},
    {"set_read_error", set_read_error, nullptr},
    {"refuse_pages", refuse_pages, nullptr},
    {"calls", calls, nullptr},
    {"written", written, nullptr},
    {"lent", lent, nullptr},
    {"set_busy", set_busy, nullptr},
    {"kept", kept, nullptr},
    {"set_facts", set_facts, nullptr},
    {"set_synced", set_synced, nullptr},
    {"set_links", set_links, nullptr},
    {"set_encoding", set_encoding, nullptr},
    {"location", location_command, nullptr},
    {"store_dir", store_dir, ops::member},
    {"set_programs", set_programs, nullptr},
    {"set_found", set_found, nullptr},
}};

}  // namespace
}  // namespace qiven3::drive

int main(int argc, char** argv) {
    using namespace qiven3::drive;
    std::string why;
    const std::optional<Options> options = read_options(argc, argv, true, why);
    if (!options) {
        return refuse("qiven3-drive-fake", why);
    }
    Session session(*options);
    Drive drive(session, make, lent_of);
    for (const Own& each : own) {
        drive.add(each.name, each.operation, each.budget);
    }
    return serve(session, drive);
}
