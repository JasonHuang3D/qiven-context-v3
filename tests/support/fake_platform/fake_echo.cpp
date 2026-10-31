// The fake's programs that echo their command (Program::echo), as the
// forms of qiven3-child platform of the same names print it on the Windows
// adapter: "cwd", the directory the program runs in, the command's cwd or,
// empty, v3's own working directory; "env", each name of its environment
// a line, in its order, then each argument after argv[0] as
// "NAME=<value>", or "NAME unset". Its environment is the contract's: each
// allowlist name v3's own environment holds and the caller does not give,
// then the caller's pairs, names compared with ASCII letters folded, as
// the Windows adapter compares them.
//
// thread: one

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::platform::fake {
namespace {

namespace fs = std::filesystem;

// The names v3's own environment gives every child, as the Windows
// adapter's allowlist.
constexpr std::array<std::string_view, 4> allowlist{"SystemRoot", "SystemDrive", "TEMP", "TMP"};

char fold(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool same_name(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) { return fold(x) == fold(y); });
}

std::string to_text(const fs::path& path) {
    std::string text;
    for (const char8_t c : path.u8string()) {
        text.push_back(static_cast<char>(c));
    }
    return text;
}

// v3's own value of name, as UTF-8; none where its environment lacks it.
std::optional<std::string> own(std::string_view name) {
#if defined(_WIN32)
    // Its size first, its terminating null counted; none when 0.
    const std::wstring wide = fs::path(std::string(name)).wstring();
    std::size_t n = 0;
    if (_wgetenv_s(&n, nullptr, 0, wide.c_str()) != 0 || n == 0) {
        return std::nullopt;
    }
    std::wstring value(n, L'\0');
    if (_wgetenv_s(&n, value.data(), value.size(), wide.c_str()) != 0 || n == 0) {
        return std::nullopt;
    }
    value.resize(n - 1);
    return to_text(fs::path(value));
#else
    const char* value = std::getenv(std::string(name).c_str());
    return value == nullptr ? std::nullopt : std::optional<std::string>(value);
#endif
}

// The child's environment, by the contract, in its order.
std::vector<std::pair<std::string, std::string>> environment(const Command& command) {
    std::vector<std::pair<std::string, std::string>> block;
    for (const std::string_view name : allowlist) {
        if (std::ranges::any_of(command.env, [&](const auto& pair) { return same_name(pair.first, name); })) {
            continue;
        }
        if (std::optional<std::string> value = own(name)) {
            block.emplace_back(name, std::move(*value));
        }
    }
    for (const auto& pair : command.env) {
        block.emplace_back(pair.first, pair.second);
    }
    return block;
}

}  // namespace

std::vector<std::string> echoed(const Program& program, const Command& command) {
    if (program.echo == "cwd") {
        if (!command.cwd.empty()) {
            return {std::string(command.cwd)};
        }
        std::error_code error;
        const fs::path here = fs::current_path(error);
        if (error) {
            throw PlatformError(Code::start, "start", "the working directory unread: " + error.message());
        }
        return {to_text(here)};
    }
    if (program.echo != "env") {
        return program.lines;
    }
    const std::vector<std::pair<std::string, std::string>> block = environment(command);
    std::vector<std::string> lines;
    for (const auto& pair : block) {
        lines.push_back(pair.first);
    }
    for (std::size_t k = 1; k < command.argv.size(); ++k) {
        const std::string_view name = command.argv[k];
        const auto at = std::ranges::find_if(block, [&](const auto& pair) { return same_name(pair.first, name); });
        lines.push_back(std::string(name) + (at == block.end() ? std::string(" unset") : "=" + at->second));
    }
    return lines;
}

}  // namespace qiven3::platform::fake
