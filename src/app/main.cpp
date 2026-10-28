#include "app/main.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "qiven3/location.hpp"
#include "wire/wire.hpp"

namespace qiven3 {
namespace app {

std::optional<CommandView> command(const wire::RequestView& request, Views& views) noexcept {
    const std::size_t given = request.size();
    if (given == 0 || given > views.size()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < given; ++i) {
        views.at(i) = request[i];
    }
    const std::span<const std::string_view> read(views.data(), given);
    if (read.front() == "location") {
        return CommandView{read.subspan(1), Subcommand::location};
    }
    return std::nullopt;
}

bool well_formed(const CommandView& command) noexcept {
    switch (command.subcommand) {
    case Subcommand::location:
        return location::arguments(command.arguments).has_value();
    }
    return false;
}

std::optional<std::string_view> config_option(const CommandView& command) noexcept {
    switch (command.subcommand) {
    case Subcommand::location:
        return location::config_option(command.arguments);
    }
    return std::nullopt;
}

std::string_view usage_line() noexcept { return "usage: qiven3 location init|change|show [ARGUMENTS]\n"; }

}  // namespace app
}  // namespace qiven3
