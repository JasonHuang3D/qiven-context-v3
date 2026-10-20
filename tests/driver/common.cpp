// The drives' common half (driver/drive.hpp): options, request helpers,
// adapter_for and current, the HAL's table, the replies, the record of each
// request's arena and fault, and the loop's streams.
//
// The streams stay in the C runtime's text mode: a request is canonical
// JSON, whose strings escape every control character, so no CR and no
// 0x1A comes raw, and a reply's line ending CRLF on Windows is JSON's
// whitespace to its reader.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "driver/drive.hpp"
#include "memory/memory.hpp"
#include "platform/current.hpp"

#if defined(QIVEN3_DRIVE_HAL)
#include "hal/hal.hpp"
#endif


namespace qiven3::drive {
namespace {

std::optional<std::string> config_of(const Value& request) {
    return request.find("config") ? std::optional<std::string>(text(request, "config")) : std::nullopt;
}

std::optional<std::string_view> view(const std::optional<std::string>& text) {
    return text ? std::optional<std::string_view>(*text) : std::nullopt;
}

// adapter_for and current as a command's main calls them: the made
// adapter's name.
std::optional<Value> adapter_for(Session&, const Value& request) {
    const std::optional<std::string> config = config_of(request);
#if defined(_WIN32)
    return Value(std::string(platform::adapter_for(text(request, "system"), view(config)).name()));
#else
    platform::adapter_for(text(request, "system"), view(config));
#endif
}

std::optional<Value> current(Session&, const Value& request) {
    const std::optional<std::string> config = config_of(request);
#if defined(_WIN32)
    return Value(std::string(platform::current(view(config)).name()));
#else
    platform::current(view(config));
#endif
}

}  // namespace

int refuse(std::string_view program, const std::string& why) {
    std::cerr << program << ": " << why << "\n";
    return 2;
}

std::optional<Options> read_options(int argc, char** argv, bool fake, std::string& why) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool valued = arg == "--config" || arg == "--hal" || (fake && (arg == "--root" || arg == "--seed"));
        if (!valued) {
            why = "an unknown argument: " + arg;
            return std::nullopt;
        }
        if (i + 1 >= argc) {
            why = arg + " takes a value";
            return std::nullopt;
        }
        const std::string value = argv[++i];
        if (arg == "--root") {
            options.root = value;
        } else if (arg == "--config") {
            options.config = value;
        } else if (arg == "--seed") {
            try {
                if (value.empty() || value[0] < '0' || value[0] > '9') {
                    throw std::invalid_argument(value);
                }
                std::size_t used = 0;
                options.seed = std::stoull(value, &used);
                if (used != value.size()) {
                    throw std::invalid_argument(value);
                }
            } catch (const std::exception&) {
                why = "--seed takes a decimal integer: " + value;
                return std::nullopt;
            }
        } else if (value == "fake") {
            options.hal_fake = true;
        } else {
            why = "--hal takes fake: " + value;
            return std::nullopt;
        }
    }
    if (fake && !options.root) {
        why = "the fake's drive takes --root DIR";
        return std::nullopt;
    }
#if !defined(QIVEN3_DRIVE_HAL)
    if (options.hal_fake) {
        why = "--hal fake: no HAL in this build (QIVEN3_HAL off)";
        return std::nullopt;
    }
#endif
    return options;
}

const Value& argument(const Value& request, std::string_view key) {
    const Value* found = request.find(key);
    if (found == nullptr) {
        throw RequestError("the argument '" + std::string(key) + "' is missing");
    }
    return *found;
}

std::string text(const Value& request, std::string_view key) { return argument(request, key).as_string(key); }

std::int64_t integer(const Value& request, std::string_view key, std::optional<std::int64_t> fallback) {
    const Value* found = request.find(key);
    if (found == nullptr && fallback) {
        return *fallback;
    }
    return argument(request, key).as_integer(key);
}

bool flag(const Value& request, std::string_view key, bool fallback) {
    const Value* found = request.find(key);
    return found == nullptr ? fallback : found->as_boolean(key);
}

platform::Seconds deadline(const Value& request, std::string_view key) {
    return platform::Seconds(static_cast<double>(integer(request, key, 5000)) / 1000.0);
}

const Operations& operations() {
    static const Operations table = [] {
        Operations all;
        all.emplace("adapter_for", adapter_for);
        all.emplace("current", current);
#if defined(QIVEN3_DRIVE_HAL)
        add_hal_operations(all);
#endif
        return all;
    }();
    return table;
}

std::optional<Value> common(Session& session, const std::string& name, const Value& request) {
    const auto found = operations().find(name);
    if (found == operations().end()) {
        throw RequestError(name.starts_with("hal_") && !session.options().hal_fake
                               ? "no HAL here: " + name + " needs --hal fake in a build with it"
                               : "no operation '" + name + "'");
    }
    return found->second(session, request);
}

Value ok_reply(std::optional<Value> result) {
    Value out;
    out.set("ok", true);
    if (result) {
        out.set("result", std::move(*result));
    }
    return out;
}

Value error_reply(Value error) {
    Value reply;
    reply.set("ok", false);
    reply.set("error", std::move(error));
    return reply;
}

Value typed(std::string_view kind, std::string_view code, std::string_view operation, std::string_view detail) {
    Value error;
    error.set("kind", std::string(kind));
    error.set("code", std::string(code));
    error.set("operation", std::string(operation));
    error.set("detail", std::string(detail));
    return error;
}

Value plain(std::string_view kind, std::string_view detail) {
    Value error;
    error.set("kind", std::string(kind));
    error.set("detail", std::string(detail));
    return error;
}

Value caught(std::string_view name) {
    try {
        throw;
    } catch (const platform::PlatformError& error) {
        return error_reply(
            typed("platform", platform::name(error.code), error.operation.view(), error.detail.view()));
    } catch (const memory::MemoryError& error) {
        return error_reply(typed("memory", memory::name(error.code), error.operation.view(), error.detail.view()));
#if defined(QIVEN3_DRIVE_HAL)
    } catch (const hal::HalError& error) {
        return error_reply(typed("hal", hal::name(error.code), error.operation.view(), error.detail.view()));
#endif
    } catch (const RequestError& error) {
        return error_reply(plain("request", error.what()));
    } catch (const WireError& error) {
        return error_reply(plain("request", error.what()));
    } catch (const std::exception& error) {
        return error_reply(plain("exception", std::string(name) + ": " + error.what()));
    }
}

std::size_t budget(const Value& request, std::size_t stated) {
    const Value* given = request.find("budget");
    if (given == nullptr) {
        return stated;
    }
    const std::int64_t bytes = given->as_integer("budget");
    if (bytes < 0) {
        throw RequestError("budget: a count of bytes, not negative");
    }
    return static_cast<std::size_t>(bytes);
}

void begin(Session& session, const Value& request) {
    session.metered() = Metered{};
    const Value* fault = request.find("fault");
    if (fault == nullptr) {
        return;
    }
#if defined(QIVEN3_FAULTS)
    const std::int64_t k = integer(*fault, "k");
    const std::string of = text(*fault, "of");
    Faults& faults = session.faults();
    if (k < 0 || (of != "allocation" && of != "platform")) {
        throw RequestError("fault: {\"k\": K, not negative, \"of\": \"allocation\" or \"platform\"}");
    }
    faults.armed = of == "allocation" ? &faults.allocation : &faults.platform;
    faults.k = static_cast<std::size_t>(k);
    faults.armed->arm(faults.k);
#else
    (void)session;
    throw RequestError("not applicable: built without QIVEN3_FAULTS, so no fault");
#endif
}

Value finish(Session& session, Value reply, std::size_t held) {
    const Metered& metered = session.metered();
    if (metered.counted) {
        Value memory;
        memory.set("budget", static_cast<std::int64_t>(metered.budget));
        memory.set("peak", static_cast<std::int64_t>(metered.peak));
        memory.set("left", static_cast<std::int64_t>(metered.left));
        reply.set("memory", std::move(memory));
    }
    reply.set("held", static_cast<std::int64_t>(held));
#if defined(QIVEN3_FAULTS)
    Faults& faults = session.faults();
    if (faults.armed != nullptr) {
        Value fault;
        fault.set("k", static_cast<std::int64_t>(faults.k));
        fault.set("of", faults.armed == &faults.allocation ? "allocation" : "platform");
        fault.set("calls", static_cast<std::int64_t>(faults.armed->calls()));
        fault.set("fired", faults.armed->fired());
        reply.set("fault", std::move(fault));
        faults.armed = nullptr;
    }
    faults.allocation.disarm();
    faults.platform.disarm();
#endif
    return reply;
}

void ready_streams() {
#if defined(_WIN32)
    // A caller's mistake ends the drive through fail_fast: abort's status,
    // with no message and no fault report.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

bool next_line(std::string& line) {
    if (!std::getline(std::cin, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return true;
}

void send(const Value& reply) { std::cout << encode(reply) << '\n' << std::flush; }

}  // namespace qiven3::drive
