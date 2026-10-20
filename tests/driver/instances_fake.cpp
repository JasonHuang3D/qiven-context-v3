// qiven3-drive-fake: the drive for the fake platform, its instantiation
// file. It instantiates the drive's platform half for the fake, adds the
// fake's own operations and runs the request loop (driver/drive.hpp).
//
// The fake's own: set_clock, its clock in microseconds; set_read_error,
// read's io for a path; refuse_pages, the k-th call of pages from now
// refused; calls, its record; written, its stream's bytes; lent, the bytes
// of the pages it has lent and not had back, which each reply's "held"
// gives too. Under QIVEN3_FAULTS each of the fake's calls is a fault point
// of a request's "fault" (driver/drive.hpp).

#include <iostream>
#include <string>
#include <utility>

#include "driver/platform_ops_impl.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::drive {

template class PlatformDrive<platform::fake::FakePlatform>;

namespace {

using Fake = platform::fake::FakePlatform;
using Drive = PlatformDrive<Fake>;
using Result = std::optional<Value>;

void make(std::optional<Fake>& into, const Options& options) {
    platform::fake::Config config;
    config.root = *options.root;
    config.config_dir = options.config ? *options.config : *options.root + "/qiven3";
    config.seed = options.seed;
    into.emplace(std::move(config));
}

Result set_clock(Drive& d, memory::Arena&, const Value& request) {
    d.platform().set_clock(
        std::chrono::sys_time<std::chrono::microseconds>(std::chrono::microseconds(integer(request, "us"))));
    return std::nullopt;
}

Result set_read_error(Drive& d, memory::Arena&, const Value& request) {
    d.platform().set_read_error(text(request, "path"), text(request, "detail"));
    return std::nullopt;
}

Result refuse_pages(Drive& d, memory::Arena&, const Value& request) {
    d.platform().refuse_pages(ops::count(request, "at"));
    return std::nullopt;
}

Result calls(Drive& d, memory::Arena&, const Value&) {
    Value names = Value::array();
    for (const std::string& call : d.platform().calls()) {
        names.push(Value(call));
    }
    return names;
}

Result written(Drive& d, memory::Arena&, const Value&) { return Value(to_hex(d.platform().written())); }

std::size_t lent_of(const Fake& fake) { return fake.lent(); }

Result lent(Drive& d, memory::Arena&, const Value&) {
    return Value(static_cast<std::int64_t>(d.platform().lent()));
}

}  // namespace
}  // namespace qiven3::drive

int main(int argc, char** argv) {
    using namespace qiven3::drive;
    std::string why;
    const std::optional<Options> options = read_options(argc, argv, true, why);
    if (!options) {
        std::cerr << "qiven3-drive-fake: " << why << "\n";
        return 2;
    }
    Session session(*options);
    Drive drive(session, make, lent_of);
    drive.add("set_clock", set_clock);
    drive.add("set_read_error", set_read_error);
    drive.add("refuse_pages", refuse_pages);
    drive.add("calls", calls);
    drive.add("written", written);
    drive.add("lent", lent);
    return loop(drive);
}
