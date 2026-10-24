#include "app/main.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "qiven3/location.hpp"

namespace qiven3 {
namespace app {

std::optional<CommandView> command(int argc, const char* const* argv, Views& views) noexcept {
    if (argc < 2 || argv == nullptr) {
        return std::nullopt;
    }
    const std::size_t given = static_cast<std::size_t>(argc) - 1;
    if (given > views.size()) {
        return std::nullopt;
    }
    const std::span<const char* const> all(argv, static_cast<std::size_t>(argc));
    for (std::size_t i = 0; i < given; ++i) {
        const char* text = all[i + 1];
        views.at(i) = text == nullptr ? std::string_view{} : std::string_view(text);
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
