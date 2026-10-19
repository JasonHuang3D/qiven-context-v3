#include "platform/platform.hpp"

namespace qiven3::platform {
namespace {

std::string message(Code code, const std::string& operation, const std::string& detail) {
    std::string text = operation + ": " + std::string(code_name(code));
    if (!detail.empty()) {
        text += ": " + detail;
    }
    return text;
}

}  // namespace

std::string_view code_name(Code code) noexcept {
    switch (code) {
    case Code::unsupported:
        return "unsupported";
    case Code::config:
        return "config";
    case Code::path:
        return "path";
    case Code::exists:
        return "exists";
    case Code::busy:
        return "busy";
    case Code::io:
        return "io";
    case Code::start:
        return "start";
    case Code::deadline:
        return "deadline";
    case Code::output:
        return "output";
    }
    return "unknown";
}

PlatformError::PlatformError(Code code, std::string operation, std::string detail)
    : std::runtime_error(message(code, operation, detail)),
      code_(code),
      operation_(std::move(operation)),
      detail_(std::move(detail)) {}

}  // namespace qiven3::platform
