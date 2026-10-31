// qiven3-drive-windows: the drive for the Windows adapter, platform::Current,
// its instantiation file, built on Windows alone. It instantiates the
// drive's platform half for the adapter, adds the adapter's own operation
// and runs the request loop (driver/drive.hpp).
//
// The adapter's own: arguments, the binary's command line converted
// (WindowsArguments) on the request's arena, whose budget is the
// conversion's measure; its "units" each argument's UTF-16 units as
// integers, since a request's text holds no lone surrogate. It answers
// "argv", each argument's UTF-8 as hexadecimal, "argc", and "ended",
// whether argv[argc] is null.
//
// For location's: store_dir, the recorded directory as "dir", or the
// StoreError refusing it as "refused" (driver/store_wire.hpp's form), on
// the member's arena, as the fake's drive answers it. Location's commands
// themselves run here as the binary, qiven3 location ..., whose standard
// output the case reads (contract/platform_location.py): the drive's own
// stream stays its replies'.

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "driver/platform_ops_impl.hpp"
#include "driver/store_wire.hpp"
#include "platform/current.hpp"
#include "qiven3/location.hpp"
#include "qiven3/location_impl.hpp"

namespace qiven3::drive {

template class PlatformDrive<platform::Current>;

namespace {

using Drive = PlatformDrive<platform::Current>;
using Result = std::optional<Value>;

void make(std::optional<platform::Current>& into, const Options& options) {
    into.emplace(options.config ? std::optional<std::string_view>(*options.config) : std::nullopt);
}

// The request's arguments as UTF-16: RequestError for a unit past 16 bits.
std::vector<std::wstring> wide_arguments(ArgView request) {
    std::vector<std::wstring> out;
    for (const ArgView one : argument(request, "units").as_array("units")) {
        std::wstring units;
        for (const ArgView unit : one.as_array("units")) {
            const std::int64_t value = unit.as_integer("units");
            if (value < 0 || value > 0xFFFF) {
                throw RequestError("units: a unit past 16 bits");
            }
            units.push_back(static_cast<wchar_t>(value));
        }
        out.push_back(std::move(units));
    }
    return out;
}

std::vector<const wchar_t*> pointers(const std::vector<std::wstring>& wide) {
    std::vector<const wchar_t*> out;
    out.reserve(wide.size());
    for (const std::wstring& one : wide) {
        out.push_back(one.c_str());
    }
    return out;
}

std::size_t arguments_budget(ArgView request) {
    return platform::win::WindowsArguments::bytes(pointers(wide_arguments(request)));
}

Result arguments(Drive&, memory::Arena& arena, ArgView request) {
    const std::vector<std::wstring> wide = wide_arguments(request);
    const std::vector<const wchar_t*> given = pointers(wide);
    const platform::win::WindowsArguments converted(given, arena);
    const std::span<const char* const> argv(converted.argv(), given.size() + 1);
    Value list = array();
    for (std::size_t i = 0; i < given.size(); ++i) {
        list.push(scalar(to_hex(argv[i])));
    }
    Value value = object();
    value.set("argv", list);
    value.set("argc", static_cast<std::int64_t>(converted.argc()));
    value.set("ended", argv[given.size()] == nullptr);
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

}  // namespace
}  // namespace qiven3::drive

int main(int argc, char** argv) {
    using namespace qiven3::drive;
    std::string why;
    const std::optional<Options> options = read_options(argc, argv, false, why);
    if (!options) {
        return refuse("qiven3-drive-windows", why);
    }
    Session session(*options);
    Drive drive(session, make);
    drive.add("arguments", arguments, arguments_budget);
    drive.add("store_dir", store_dir, ops::member);
    return loop(drive);
}
