// qiven3-drive's main and request loop (driver/drive.hpp).

#include <cstdio>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "driver/drive.hpp"
#include "platform/current.hpp"
#include "support/fake_platform/fake_platform.hpp"

#if defined(QIVEN3_DRIVE_HAL)
#include "hal/hal.hpp"
#endif

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace qiven3::drive {
namespace {

constexpr int exit_usage = 2;

Value error_reply(Value error) {
    Value reply;
    reply.set("ok", false);
    reply.set("error", std::move(error));
    return reply;
}

Value typed(const char* kind, std::string_view code, const std::string& operation,
            const std::string& detail) {
    Value error;
    error.set("kind", kind);
    error.set("code", std::string(code));
    error.set("operation", operation);
    error.set("detail", detail);
    return error;
}

Value plain(const char* kind, const std::string& detail) {
    Value error;
    error.set("kind", kind);
    error.set("detail", detail);
    return error;
}

std::optional<Options> read_options(int argc, char** argv, std::string& why) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool valued = arg == "--platform" || arg == "--root" || arg == "--seed" ||
                            arg == "--config" || arg == "--hal";
        if (!valued) {
            why = "an unknown argument: " + arg;
            return std::nullopt;
        }
        if (i + 1 >= argc) {
            why = arg + " takes a value";
            return std::nullopt;
        }
        const std::string value = argv[++i];
        if (arg == "--platform") {
            options.platform = value;
        } else if (arg == "--root") {
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
    if (options.platform != "fake" && options.platform != "windows") {
        why = "--platform takes fake or windows";
        return std::nullopt;
    }
    if (options.platform == "fake" && !options.root) {
        why = "--platform fake takes --root DIR";
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

}  // namespace

const Value& argument(const Value& request, std::string_view key) {
    const Value* found = request.find(key);
    if (found == nullptr) {
        throw RequestError("the argument '" + std::string(key) + "' is missing");
    }
    return *found;
}

std::string text(const Value& request, std::string_view key) {
    return argument(request, key).as_string(key);
}

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
        add_platform_operations(all);
#if defined(QIVEN3_DRIVE_HAL)
        add_hal_operations(all);
#endif
        return all;
    }();
    return table;
}

Session::Session(Options options) : options_(std::move(options)) {}

Session::~Session() = default;

platform::Platform& Session::platform() {
    if (!platform_) {
        if (options_.platform == "windows") {
            platform_ = platform::adapter_for("windows", options_.config);
        } else {
            platform::fake::Config config;
            config.root = *options_.root;
            config.config_dir = options_.config ? *options_.config : *options_.root + "/qiven3";
            config.seed = options_.seed;
            auto made = std::make_unique<platform::fake::FakePlatform>(std::move(config));
            fake_ = made.get();
            platform_ = std::move(made);
        }
    }
    return *platform_;
}

platform::fake::FakePlatform& Session::fake(std::string_view operation) {
    platform();
    if (fake_ == nullptr) {
        throw RequestError(std::string(operation) + " is the fake's alone");
    }
    return *fake_;
}

Value Session::reply(const Value& request) {
    std::string name;
    try {
        name = text(request, "op");
        const auto found = operations().find(name);
        if (found == operations().end()) {
            throw RequestError(name.starts_with("hal_") && !options_.hal_fake
                                   ? "no HAL here: " + name + " needs --hal fake in a build with it"
                                   : "no operation '" + name + "'");
        }
        std::optional<Value> result = found->second(*this, request);
        Value out;
        out.set("ok", true);
        if (result) {
            out.set("result", std::move(*result));
        }
        return out;
    } catch (const platform::PlatformError& error) {
        return error_reply(typed("platform", platform::code_name(error.code()), error.operation(),
                                 error.detail()));
#if defined(QIVEN3_DRIVE_HAL)
    } catch (const hal::HalError& error) {
        return error_reply(typed("hal", hal::code_name(error.code()), error.operation(), error.detail()));
#endif
    } catch (const RequestError& error) {
        return error_reply(plain("request", error.what()));
    } catch (const WireError& error) {
        return error_reply(plain("request", error.what()));
    } catch (const std::exception& error) {
        return error_reply(plain("exception", name + ": " + error.what()));
    }
}

}  // namespace qiven3::drive

int main(int argc, char** argv) {
#if defined(_WIN32)
    // Bytes as they are: no CRLF for LF, no end of input at 0x1A.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    using namespace qiven3::drive;
    std::string why;
    const std::optional<Options> options = read_options(argc, argv, why);
    if (!options) {
        std::cerr << "qiven3-drive: " << why << "\n";
        return exit_usage;
    }
    Session session(*options);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        Value reply;
        try {
            reply = session.reply(parse(line));
        } catch (const WireError& error) {
            reply = error_reply(plain("request", error.what()));
        }
        std::cout << encode(reply) << '\n' << std::flush;
    }
    return 0;
}
