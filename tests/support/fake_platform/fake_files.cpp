#include "support/fake_platform/fake_files.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <map>
#include <new>
#include <string>
#include <system_error>
#include <utility>

#include "support/fake_platform/fake_platform.hpp"

// The Guidelines' mark of an owning pointer, as fake_platform.cpp's.
namespace gsl { template <class T> using owner = T; }  // namespace gsl

namespace qiven3::platform::fake {
namespace fs = std::filesystem;
using files::Error;

namespace {

fs::path to_path(std::string_view text) {
    std::u8string units;
    for (const char c : text) {
        units.push_back(static_cast<char8_t>(c));
    }
    return fs::path(units);
}

std::string to_text(const std::u8string& units) {
    std::string text;
    for (const char8_t c : units) {
        text.push_back(static_cast<char>(c));
    }
    return text;
}

fs::path to_path(const files::Digits& digits) { return to_path(std::string_view(digits.data(), digits.size())); }

// Whether path a comes before b, component by component in UTF-8's byte
// order: as bytes, '/' below every other, the order of the Windows
// adapter's discard (platform/win/stage.cpp).
bool before(const std::u8string& a, const std::u8string& b) noexcept {
    const auto rank = [](char8_t c) { return c == u8'/' ? 0U : static_cast<unsigned>(c) + 1U; };
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [&](char8_t x, char8_t y) { return rank(x) < rank(y); });
}

// replace's file beside path: ".<name>.<digits>.tmp".
fs::path temporary(std::string_view path, const files::Digits& digits) {
    const fs::path target = to_path(path);
    return target.parent_path() / (u8"." + target.filename().u8string() + u8"." + to_path(digits).u8string() + u8".tmp");
}

// A filesystem error by its Win32 number.
Error win32(const std::error_code& error, Error exists) {
    if (!error) {
        return files::none;
    }
    if (error == std::errc::no_such_file_or_directory) {
        return files::path_not_found;
    }
    if (error == std::errc::file_exists) {
        return exists;
    }
    return error == std::errc::permission_denied ? files::access_denied : files::general;
}

struct Closer {
    void operator()(gsl::owner<std::FILE*> file) const noexcept { static_cast<void>(std::fclose(file)); }
};
using Stream = std::unique_ptr<std::FILE, Closer>;

// A new file by the C library's exclusive "wbx", as create's.
Error open_new(const fs::path& path, Stream& out) {
    gsl::owner<std::FILE*> file = nullptr;
#if defined(_WIN32)
    const int code = _wfopen_s(&file, path.c_str(), L"wbx");
#else
    file = std::fopen(path.c_str(), "wbx");
    const int code = file != nullptr ? 0 : errno;
#endif
    out.reset(file);
    if (file != nullptr) {
        return files::none;
    }
    return code == EEXIST ? files::file_exists : code == ENOENT ? files::path_not_found
           : code == EACCES ? files::access_denied : files::general;
}

Error put(std::FILE* file, std::string_view bytes) {
    return std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size() ? files::none : files::general;
}

// Flushed and closed: the flush's error; the close's is the Closer's.
Error shut(Stream& stream) noexcept {
    const bool flushed = std::fflush(stream.get()) == 0;
    stream.reset();
    return flushed ? files::none : files::general;
}

// One refusal of a busy count, if any is left.
bool refused(std::uint64_t& count) noexcept {
    if (count == 0) {
        return false;
    }
    if (count != never) {
        --count;
    }
    return true;
}

}  // namespace

struct FakeFiles::State {
    struct Open {
        Stream stream;
        std::size_t dir = 0;
    };
    struct Staging {
        fs::path parent, temporary;
    };
    struct Locked {
        std::string path;
        bool locked = false;
    };
    std::size_t next = 0;
    std::map<std::size_t, Open> open;
    std::map<std::size_t, Staging> dirs;
    std::map<std::size_t, Locked> locks;
};

FakeFiles::FakeFiles(FakePlatform& platform, const Busy& busy)
    : platform_(&platform), busy_(busy), state_(std::make_unique<State>()) {}

FakeFiles::~FakeFiles() = default;

bool FakeFiles::faulted() noexcept {
#if defined(QIVEN3_FAULTS)
    return countdown_ != nullptr && countdown_->fail();
#else
    return false;
#endif
}

void FakeFiles::random_bytes(std::span<std::byte> into) { platform_->random_bytes(into); }

bool FakeFiles::absolute(std::string_view path) const { return to_path(path).is_absolute(); }

std::size_t FakeFiles::open_files() const noexcept { return state_->open.size(); }

std::size_t FakeFiles::kept() const noexcept { return state_->open.size() + state_->dirs.size() + state_->locks.size(); }

Error FakeFiles::write_temporary(std::string_view path, const files::Digits& digits, std::string_view data) {
    if (faulted()) {
        return files::general;
    }
    Stream stream;
    const Error opened = open_new(temporary(path, digits), stream);
    if (opened != files::none) {
        return opened;
    }
    const Error written = put(stream.get(), data);
    const Error closed = shut(stream);
    return written != files::none ? written : closed;
}

Error FakeFiles::rename_temporary(std::string_view path, const files::Digits& digits) {
    if (faulted()) {
        return files::general;
    }
    if (refused(busy_.temporary)) {
        return files::sharing_violation;
    }
    if (refused(busy_.target)) {
        return files::access_denied;
    }
    std::error_code error;
    fs::rename(temporary(path, digits), to_path(path), error);
    return win32(error, files::already_exists);
}

Error FakeFiles::delete_temporary(std::string_view path, const files::Digits& digits) {
    if (faulted()) {
        return files::general;
    }
    if (refused(busy_.temporary)) {
        return files::sharing_violation;
    }
    std::error_code error;
    const bool removed = fs::remove(temporary(path, digits), error);
    return error ? win32(error, files::file_exists) : removed ? files::none : files::file_not_found;
}

Error FakeFiles::make_staged(std::string_view directory, const files::Digits& digits, Dir& out) {
    if (faulted()) {
        return files::general;
    }
    const fs::path parent = to_path(directory);
    const fs::path made = parent / (u8"." + to_path(digits).u8string() + u8".tmp");
    std::error_code error;
    if (!fs::create_directory(made, error)) {
        return error ? win32(error, files::already_exists) : files::already_exists;
    }
    out.id = ++state_->next;
    state_->dirs.emplace(out.id, State::Staging{parent, made});
    return files::none;
}

Error FakeFiles::open_staged(const Dir& dir, std::string_view path, File& out) {
    if (faulted()) {
        return files::general;
    }
    const auto staging = state_->dirs.find(dir.id);
    if (staging == state_->dirs.end()) {
        return files::path_not_found;
    }
    const fs::path made = staging->second.temporary / to_path(path);
    // The record's node first, so that its insert failing makes nothing:
    // taken out, then put back, with no allocation, once the file is made.
    // The insert is a fault point, its allocation's throw; that is general.
    const std::size_t id = state_->next + 1;
    decltype(state_->open)::node_type record;
    try {
        if (faulted()) {
            throw std::bad_alloc();
        }
        record = state_->open.extract(state_->open.emplace(id, State::Open{Stream(), dir.id}).first);
    } catch (const std::bad_alloc&) {
        return files::general;
    }
    std::error_code error;
    fs::create_directories(made.parent_path(), error);
    if (error) {
        return win32(error, files::already_exists);
    }
    const Error opened = open_new(made, record.mapped().stream);
    if (opened != files::none) {
        return opened;
    }
    out.id = state_->next = id;
    state_->open.insert(std::move(record));
    return files::none;
}

Error FakeFiles::write(const File& file, std::string_view bytes) {
    if (faulted()) {
        return files::general;
    }
    const auto open = state_->open.find(file.id);
    return open == state_->open.end() ? files::general : put(open->second.stream.get(), bytes);
}

Error FakeFiles::close(const File& file) noexcept {
    // By a walk, as forget's: the map's find may throw, by its declaration.
    Error error = files::none;
    for (auto open = state_->open.begin(); open != state_->open.end(); ++open) {
        if (open->first == file.id) {
            error = shut(open->second.stream);
            state_->open.erase(open);
            break;
        }
    }
    return faulted() ? files::general : error;
}

Error FakeFiles::close_all(const Dir& dir) {
    if (faulted()) {
        return files::general;
    }
    Error error = files::none;
    for (auto at = state_->open.begin(); at != state_->open.end();) {
        if (at->second.dir == dir.id) {
            const Error shut_one = shut(at->second.stream);
            error = error != files::none ? error : shut_one;
            at = state_->open.erase(at);
        } else {
            ++at;
        }
    }
    return error;
}

Error FakeFiles::publish(const Dir& dir, std::string_view name) {
    if (faulted()) {
        return files::general;
    }
    const auto staging = state_->dirs.find(dir.id);
    if (staging == state_->dirs.end()) {
        return files::path_not_found;
    }
    if (refused(busy_.publish)) {
        return files::access_denied;
    }
    // A rename onto a present directory with no replacing: 183, as
    // MoveFileExW gives, where POSIX's rename would replace an empty one.
    const fs::path target = staging->second.parent / to_path(name);
    std::error_code error;
    if (fs::exists(fs::symlink_status(target, error))) {
        return files::already_exists;
    }
    fs::rename(staging->second.temporary, target, error);
    return win32(error, files::already_exists);
}

Error FakeFiles::discard(const Dir& dir, Detail& left) noexcept {
    // The standard library's allocations under its calls: general.
    try {
        const auto staging = state_->dirs.find(dir.id);
        if (staging == state_->dirs.end()) {
            return files::none;
        }
        for (auto at = state_->open.begin(); at != state_->open.end();) {
            if (at->second.dir == dir.id) {
                static_cast<void>(shut(at->second.stream));
                at = state_->open.erase(at);
            } else {
                ++at;
            }
        }
        // What is left is named by its path inside the directory, nothing
        // for the directory itself; files names the directory.
        const fs::path& made = staging->second.temporary;
        if (faulted()) {
            return files::general;
        }
        std::error_code error;
        // A file held inside: what is left is its first file by before(),
        // whatever order the listing gives.
        if (busy_.publish != 0) {
            std::u8string first;
            for (const fs::directory_entry& entry : fs::recursive_directory_iterator(made, error)) {
                if (entry.is_regular_file(error)) {
                    std::u8string inside = fs::relative(entry.path(), made, error).generic_u8string();
                    if (first.empty() || before(inside, first)) {
                        first = std::move(inside);
                    }
                }
            }
            if (!first.empty()) {
                refused(busy_.publish);
                left.add(to_text(first));
                return files::sharing_violation;
            }
        }
        fs::remove_all(made, error);
        if (error) {
            return win32(error, files::already_exists);
        }
        return files::none;
    } catch (...) {
        return files::general;
    }
}

void FakeFiles::forget(const Dir& dir) noexcept {
    for (auto at = state_->open.begin(); at != state_->open.end();) {
        at = at->second.dir == dir.id ? state_->open.erase(at) : std::next(at);
    }
    state_->dirs.erase(dir.id);
}

Error FakeFiles::open_lock(std::string_view path, Held& out) {
    if (faulted()) {
        return files::general;
    }
    out.id = ++state_->next;
    state_->locks.emplace(out.id, State::Locked{std::string(path), false});
    return files::none;
}

Error FakeFiles::try_lock(const Held& held) {
    if (faulted()) {
        return files::general;
    }
    const auto mine = state_->locks.find(held.id);
    if (mine == state_->locks.end()) {
        return files::general;
    }
    if (refused(busy_.lock)) {
        return files::lock_violation;
    }
    for (const auto& [id, other] : state_->locks) {
        if (id != held.id && other.locked && other.path == mine->second.path) {
            return files::lock_violation;
        }
    }
    mine->second.locked = true;
    return files::none;
}

void FakeFiles::unlock(const Held& held) noexcept { state_->locks.erase(held.id); }

// The contract's operations: recorded, then files' over these calls, the
// paths of replace and stage under the fake's root.
namespace {

void inside(std::string_view operation, std::string_view path, const std::string& root) {
    const std::u8string at = to_path(path).lexically_normal().u8string();
    const std::u8string top = to_path(root).lexically_normal().u8string();
    const bool sep = at.size() > top.size() && (at[top.size()] == u8'/' || at[top.size()] == u8'\\');
    const bool ends = !top.empty() && (top.back() == u8'/' || top.back() == u8'\\');
    if (top.empty() || !at.starts_with(top) || (at.size() != top.size() && !sep && !ends)) {
        throw PlatformError(Code::path, operation, "outside the fake's root");
    }
}

}  // namespace

void FakePlatform::replace(std::string_view path, std::string_view data, Seconds deadline) {
    record("replace");
    files::refuse("replace", files::text_fault(path));
    inside("replace", path, config_.root);
    files::replace(files_, path, data, deadline);
}

FakeStaged FakePlatform::stage(std::string_view directory) {
    record("stage");
    files::refuse("stage", files::text_fault(directory));
    inside("stage", directory, config_.root);
    return files::stage(files_, directory);
}

FakeLock FakePlatform::lock(std::string_view path, Seconds deadline) {
    record("lock");
    return files::lock(files_, path, deadline);
}

}  // namespace qiven3::platform::fake
