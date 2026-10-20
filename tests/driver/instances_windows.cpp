// qiven3-drive-windows: the drive for the Windows adapter, platform::Current,
// its instantiation file, built on Windows alone. It instantiates the
// drive's platform half for the adapter and runs the request loop
// (driver/drive.hpp).

#include <iostream>
#include <string>

#include "driver/platform_ops_impl.hpp"
#include "platform/current.hpp"

namespace qiven3::drive {

template class PlatformDrive<platform::Current>;

namespace {

void make(std::optional<platform::Current>& into, const Options& options) {
    into.emplace(options.config ? std::optional<std::string_view>(*options.config) : std::nullopt);
}

}  // namespace
}  // namespace qiven3::drive

int main(int argc, char** argv) {
    using namespace qiven3::drive;
    std::string why;
    const std::optional<Options> options = read_options(argc, argv, false, why);
    if (!options) {
        std::cerr << "qiven3-drive-windows: " << why << "\n";
        return 2;
    }
    Session session(*options);
    PlatformDrive<qiven3::platform::Current> drive(session, make);
    return loop([&drive](const Value& request) { return drive.reply(request); });
}
