#pragma once

// main's template, defined (app/main.hpp declares it and states its rules):
// the binary's instantiation file alone includes this header.

#include <exception>
#include <optional>
#include <string_view>
#include <type_traits>

#include "app/main.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/location_impl.hpp"
#include "wire/wire.hpp"

namespace qiven3 {
namespace app {

template <typename Make>
int main(int argc, const char* const* argv, Make make) noexcept {
    using P = std::invoke_result_t<Make&, std::optional<std::string_view>>;
    const std::optional<wire::RequestView> request = wire::read_request(argc, argv, bounds);
    if (!request) {
        return wire::failed;  // no answer: the wire's own failure
    }
    Views views{};
    const std::optional<CommandView> command = app::command(*request, views);
    const std::optional<std::string_view> config = command ? config_option(*command) : std::nullopt;
    // Where no platform is made, no line: the status alone says it.
    const int unmade = command && well_formed(*command) ? refused : usage;
    try {
        if constexpr (std::is_void_v<P>) {
            make(config);  // no adapter is built here: it throws
        } else {
            P platform = make(config);
            if (!command) {
                return wire::answer(platform, usage_line(), usage);
            }
            switch (command->subcommand) {
            case Subcommand::location:
                return location::main(command->arguments, platform);
            }
        }
    } catch (const platform::PlatformError&) {
        return unmade;
    } catch (const memory::MemoryError&) {
        return unmade;
    } catch (const std::exception&) {
        return unmade;
    }
    return unmade;
}

}  // namespace app
}  // namespace qiven3
