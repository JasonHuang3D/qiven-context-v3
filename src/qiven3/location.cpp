// location's parts that take no platform: the command line, the record's
// bytes and its reading, the old directory's look, the drive check's lines
// and the error lines. The standard library's file system reads its paths
// as UTF-8, never by the system's code page.

#include "qiven3/location.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "entry/projectmap_files.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utc.hpp"
#include "store/errors.hpp"

namespace qiven3::location {

namespace {

namespace fs = std::filesystem;
namespace projectmap = entry::projectmap;
using Json = canon::Basic<memory::Allocator<char>>;

constexpr char separator = projectmap::NATIVE == projectmap::System::windows ? '\\' : '/';

// v3's files in a store's directory, change's second rule's.
constexpr std::array<std::string_view, 5> v3_files = {"store.db", "store.db-wal", "store.db-shm", "checkouts.json",
                                                      "map-marks.json"};

// A path from its UTF-8 bytes, and a last component joined after them by
// the system's separator: the standard library's path decomposition and
// its directory listing reach names core does not call.
fs::path path_of(std::string_view utf8, std::string_view last = {}) {
    std::u8string text;
    for (const char c : utf8) {
        text.push_back(static_cast<char8_t>(c));
    }
    if (!last.empty()) {
        if (!utf8.empty() && !utf8.ends_with('/') && !utf8.ends_with('\\')) {
            text.push_back(static_cast<char8_t>(separator));
        }
        for (const char c : last) {
            text.push_back(static_cast<char8_t>(c));
        }
    }
    return fs::path(text);
}

// A line composed in the caller's buffer, cut at a code point where it
// does not fit; room is kept for the line end.
class LineView {
public:
    explicit LineView(std::span<char> out) noexcept : out_(out) {}
    LineView& add(std::string_view text) noexcept {
        const std::size_t room = out_.size() > size_ + 1 ? out_.size() - size_ - 1 : 0;
        std::size_t n = std::min(room, text.size());
        if (n < text.size()) {
            while (n > 0 && (static_cast<unsigned char>(text.at(n)) & 0xC0U) == 0x80U) {
                --n;
            }
        }
        std::ranges::copy(text.substr(0, n), out_.subspan(size_).begin());
        size_ += n;
        return *this;
    }
    std::string_view line() noexcept {
        if (size_ < out_.size()) {
            out_.subspan(size_, 1).front() = '\n';
            ++size_;
        }
        return {out_.data(), size_};
    }

private:
    std::span<char> out_;
    std::size_t size_ = 0;
};

constexpr std::string_view state_name(platform::State state) noexcept {
    constexpr std::array<std::string_view, 4> names = {"on", "off", "locked", "unknown"};
    return names.at(static_cast<std::size_t>(state));
}

// Each condition's name, what it means where it warns, and how to look.
struct TextView {
    std::string_view label, warns, how;
};
constexpr std::array<TextView, 3> texts = {
    TextView{"System Protection",
         "Restore points keep earlier blocks of the drive's files, which v3 cannot forget.",
         "System Properties, its System Protection tab (SystemPropertiesProtection, which asks for an "
         "administrator)"},
    TextView{"File History", "Its versions keep copies of the folders it includes, which v3 cannot forget.",
         "the Control Panel's File History"},
    TextView{"BitLocker",
         "It keeps no copy and encrypts at rest, and the store cannot be opened while the drive is locked.",
         "the Control Panel's BitLocker Drive Encryption"},
};

[[noreturn]] void unreadable(std::string_view old, std::string_view op, const std::error_code& error) {
    platform::Detail detail("old_unreadable: error ");
    detail.number(static_cast<std::uint64_t>(error.value() < 0 ? 0 : error.value()));
    throw store::StoreError(store::Code::location, op, old, detail.view());
}

}  // namespace

std::optional<ArgumentsView> arguments(std::span<const std::string_view> argv) noexcept {
    if (argv.empty()) {
        return std::nullopt;
    }
    ArgumentsView a;
    const std::string_view sub = argv.front();
    if (sub == "init") {
        a.command = Command::init;
    } else if (sub == "change") {
        a.command = Command::change;
    } else if (sub == "show") {
        a.command = Command::show;
    } else {
        return std::nullopt;
    }
    bool directory = false;
    bool config = false;
    bool want_config = false;
    for (const std::string_view arg : argv.subspan(1)) {
        if (want_config) {
            if (arg.empty()) {
                return std::nullopt;
            }
            a.config = arg;
            want_config = false;
        } else if (arg == "--accept") {
            if (a.command == Command::show || a.accept) {
                return std::nullopt;
            }
            a.accept = true;
        } else if (arg == "--config") {
            if (config) {
                return std::nullopt;
            }
            config = true;
            want_config = true;
        } else if (arg.starts_with("--") || a.command == Command::show || directory) {
            return std::nullopt;
        } else {
            a.directory = arg;
            directory = true;
        }
    }
    if (want_config || (a.command != Command::show && !directory)) {
        return std::nullopt;
    }
    return a;
}

std::optional<std::string_view> config_option(std::span<const std::string_view> argv) noexcept {
    const std::optional<ArgumentsView> a = arguments(argv);
    if (!a || a->config.empty()) {
        return std::nullopt;
    }
    return a->config;
}

namespace detail {

void join(std::string_view dir, std::string_view name, String& out) {
    out.clear();
    out.append(dir);
    if (!dir.empty() && !dir.ends_with('/') && !dir.ends_with('\\')) {
        out.push_back(separator);
    }
    out.append(name);
}

bool absent(std::string_view path) {
    std::error_code error;
    return fs::status(path_of(path), error).type() == fs::file_type::not_found;
}

void record_bytes(utc::Millis at, std::string_view dir, std::string_view op, String& out) {
    if (!projectmap::absolute(dir, projectmap::NATIVE) || projectmap::control(dir)) {
        throw platform::PlatformError(platform::Code::path, op, "not absolute, or holding a control character");
    }
    const std::optional<std::array<char, utc::at_size>> text = utc::format_at(at);
    if (!text) {
        throw store::StoreError(store::Code::internal, op, dir, "the clock is outside the years 1 to 9999");
    }
    Json value(out.get_allocator());
    value.set("at", std::string_view(text->data(), text->size()));
    value.set("dir", dir);
    String made{out.get_allocator()};
    if (canon::try_canonical(value, made)) {
        throw platform::PlatformError(platform::Code::path, op, "a path the record cannot hold");
    }
    made.push_back('\n');
    out = std::move(made);
}

void parse_record(std::string_view bytes, std::string_view op, std::string_view path, String& dir) {
    const store::StoreError refusal(store::Code::location, op, path, "config");
    if (!bytes.ends_with('\n')) {
        throw refusal;
    }
    Json value(dir.get_allocator());
    if (canon::try_parse(bytes.substr(0, bytes.size() - 1), value)) {
        throw refusal;
    }
    if (value.kind() != json::Kind::object || value.count() != 2) {
        throw refusal;
    }
    const std::optional<json::Index> at = value.find(0, "at");
    const std::optional<json::Index> d = value.find(0, "dir");
    if (!at || !d || value.kind(*at) != json::Kind::string || value.kind(*d) != json::Kind::string) {
        throw refusal;
    }
    const std::string_view text = value.text(*d);
    if (!utc::parse_at(value.text(*at)) || !projectmap::absolute(text, projectmap::NATIVE) ||
        projectmap::control(text)) {
        throw refusal;
    }
    dir.assign(text);
}

void left_behind(std::string_view old, std::string_view op) {
    std::error_code error;
    const fs::path directory = path_of(old);
    const fs::file_status status = fs::status(directory, error);
    if (status.type() == fs::file_type::not_found) {
        return;
    }
    if (error) {
        unreadable(old, op, error);
    }
    if (!fs::is_directory(status)) {
        return;
    }
    std::array<bool, v3_files.size()> held{};
    for (std::size_t i = 0; i < v3_files.size(); ++i) {
        const fs::file_status file = fs::status(path_of(old, v3_files.at(i)), error);
        if (file.type() != fs::file_type::not_found && error) {
            unreadable(old, op, error);
        }
        held.at(i) = fs::exists(file);
    }
    if (std::ranges::none_of(held, [](bool b) { return b; })) {
        return;
    }
    platform::Detail detail("not_moved:");
    std::string_view between = " ";
    for (std::size_t i = 0; i < v3_files.size(); ++i) {
        if (held.at(i)) {
            detail.add(between).add(v3_files.at(i));
            between = ", ";
        }
    }
    throw store::StoreError(store::Code::location, op, old, detail.view());
}

bool fact_line(Condition condition, const platform::Reading& reading, std::string_view volume, String& out) {
    const TextView& text = texts.at(static_cast<std::size_t>(condition));
    const bool warns = reading.state != platform::State::off;
    out.clear();
    if (warns) {
        out.append("warning: ");
    }
    out.append(text.label).append(": ").append(state_name(reading.state)).append(" (").append(reading.reason);
    out.append("), volume ").append(volume).append(".");
    if (warns) {
        out.append(" ").append(text.warns);
    } else if (condition == Condition::bitlocker) {
        out.append(" Nothing encrypts the store at rest.");
    }
    out.append(" To look: ").append(text.how).append(".\n");
    return warns;
}

std::string_view error_line(const store::StoreError& error, std::span<char> out) noexcept {
    if (out.empty()) {
        return {};
    }
    const std::string_view text = store::message(error, out.first(out.size() - 1));
    out.subspan(text.size(), 1).front() = '\n';
    return {out.data(), text.size() + 1};
}

std::string_view error_line(const platform::PlatformError& error, std::span<char> out) noexcept {
    return LineView(out)
        .add("PlatformError ")
        .add(platform::name(error.code))
        .add(": ")
        .add(error.operation.view())
        .add("; ")
        .add(error.detail.view())
        .line();
}

std::string_view error_line(const memory::MemoryError& error, std::span<char> out) noexcept {
    return LineView(out)
        .add("MemoryError ")
        .add(memory::name(error.code))
        .add(": ")
        .add(error.operation.view())
        .add("; ")
        .add(error.detail.view())
        .line();
}

std::string_view error_line(const std::exception& /*error*/, std::span<char> out) noexcept {
    return LineView(out).add("the standard library raised an exception of its own").line();
}

std::string_view usage_line() noexcept {
    return "usage: qiven3 location init DIRECTORY [--accept] [--config DIR] | change DIRECTORY [--accept] "
           "[--config DIR] | show [--config DIR]\n";
}

std::string_view not_accepted_line() noexcept {
    return "not accepted: nothing recorded; run again with --accept to go on\n";
}

std::string_view moved_line() noexcept {
    return "the old drive keeps what a move left: a move to another volume leaves the deleted files' clusters, and "
           "its restore points and File History versions keep their copies; v3 reaches none of them\n";
}

std::string_view recorded_prefix() noexcept { return "recorded "; }

}  // namespace detail

}  // namespace qiven3::location
