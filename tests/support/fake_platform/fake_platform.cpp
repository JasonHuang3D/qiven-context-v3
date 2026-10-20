#include "support/fake_platform/fake_platform.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <optional>
#include <system_error>

#include "memory/fail.hpp"

// The Guidelines' mark of an owning pointer, an alias of the type: the
// stream create opens is held by it, as clang-tidy's owning-memory reads.
namespace gsl { template <class T> using owner = T; }  // namespace gsl

namespace qiven3::platform::fake {
namespace fs = std::filesystem;
namespace {

// SHA-256 (FIPS 180-4), for the seed blocks alone.
std::string sha256(const std::string& message) {
    static constexpr std::array<std::uint32_t, 64> k = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string data = message;
    const std::uint64_t bits = static_cast<std::uint64_t>(message.size()) * 8;
    data += static_cast<char>(0x80);
    while (data.size() % 64 != 56) {
        data += '\0';
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        data += static_cast<char>((bits >> shift) & 0xFF);
    }
    const auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t at = 0; at < data.size(); at += 64) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            for (std::size_t b = 0; b < 4; ++b) {
                w[i] = (w[i] << 8) | static_cast<unsigned char>(data[at + i * 4 + b]);
            }
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::array<std::uint32_t, 8> v = h;
        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const std::uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const std::uint32_t t2 = s0 + maj;
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (std::size_t i = 0; i < 8; ++i) {
            h[i] += v[i];
        }
    }
    std::string digest;
    for (std::uint32_t word : h) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            digest += static_cast<char>((word >> shift) & 0xFF);
        }
    }
    return digest;
}

[[noreturn]] void fail(Code code, std::string_view operation, std::string_view detail) {
    throw PlatformError(code, operation, detail);
}

[[noreturn]] void unsupported(std::string_view operation) {
    fail(Code::unsupported, operation, "not implemented by the fake yet");
}

// The next code point of UTF-8 text from at, strictly; none for an invalid
// sequence, whose first byte at then passes.
std::optional<char32_t> code_point(std::string_view text, std::size_t& at) {
    const auto b = static_cast<unsigned char>(text[at]);
    const std::size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
    char32_t cp = n == 1 ? b : n == 2 ? (b & 0x1Fu) : n == 3 ? (b & 0x0Fu) : (b & 0x07u);
    bool ok = n != 0 && at + n <= text.size();
    for (std::size_t i = 1; ok && i < n; ++i) {
        const auto c = static_cast<unsigned char>(text[at + i]);
        ok = (c & 0xC0) == 0x80;
        cp = (cp << 6) | (c & 0x3Fu);
    }
    static constexpr std::array<char32_t, 5> least = {0, 0, 0x80, 0x800, 0x10000};
    ok = ok && cp >= least[n] && cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF);
    at += ok ? n : 1;
    return ok ? std::optional<char32_t>(cp) : std::nullopt;
}

fs::path to_path(std::string_view text) {
    std::u8string units;
    units.reserve(text.size());
    for (const char c : text) {
        units.push_back(static_cast<char8_t>(c));
    }
    return fs::path(units);
}

std::string to_text(const fs::path& path) {
    std::string text;
    for (const char8_t c : path.u8string()) {
        text.push_back(static_cast<char>(c));
    }
    return text;
}

// A path's text as UTF-8, strictly: path.
void check_utf8(std::string_view operation, std::string_view text) {
    for (std::size_t at = 0; at < text.size();) {
        if (!code_point(text, at)) {
            fail(Code::path, operation, "not UTF-8");
        }
    }
}

// A path the contract takes: UTF-8, no control character, absolute.
fs::path checked(std::string_view operation, std::string_view text) {
    for (std::size_t at = 0; at < text.size();) {
        const auto cp = code_point(text, at);
        if (!cp) {
            fail(Code::path, operation, "not UTF-8");
        }
        if (*cp < 0x20 || (*cp >= 0x7F && *cp <= 0x9F)) {
            fail(Code::path, operation, "holds a control character");
        }
    }
    fs::path path = to_path(text);
    if (!path.is_absolute()) {
        fail(Code::path, operation, "not absolute");
    }
    return path.lexically_normal();
}

bool is_sep(char c) { return c == '/' || c == '\\'; }

// Whether path is prefix or lies under it, by whole component.
bool under(std::string_view path, std::string_view prefix) {
    if (prefix.empty() || !path.starts_with(prefix)) {
        return false;
    }
    return path.size() == prefix.size() || is_sep(prefix.back()) || is_sep(path[prefix.size()]);
}

std::string normal(std::string_view text) { return to_text(to_path(text).lexically_normal()); }

void set(String& out, std::string_view text) { out.assign(text.data(), text.size()); }

void set(Reading& out, const FakeReading& reading) {
    out.state = reading.state;
    set(out.reason, reading.reason);
}

std::string joined(const Program& program) {
    std::string out;
    for (const std::string& line : program.lines) {
        out += line;
        out += '\n';
    }
    return out;
}

// The system's error, by name and number, as a detail names it.
std::string named(std::errc error) {
    const std::error_code code = std::make_error_code(error);
    return code.message() + " (errno " + std::to_string(code.value()) + ")";
}

}  // namespace

std::array<std::uint8_t, 32> seed_block(std::uint64_t seed, std::uint64_t k) {
    std::string message;
    for (std::uint64_t value : {seed, k}) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            message += static_cast<char>((value >> shift) & 0xFF);
        }
    }
    const std::string digest = sha256(message);
    std::array<std::uint8_t, 32> block{};
    for (std::size_t i = 0; i < block.size(); ++i) {
        block[i] = static_cast<std::uint8_t>(digest[i]);
    }
    return block;
}

// --- the handles ---

FakePages::FakePages(FakePages&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), first_(std::exchange(other.first_, nullptr)),
      count_(std::exchange(other.count_, 0)) {}

FakePages& FakePages::operator=(FakePages&& other) noexcept {
    if (this != &other) {
        give_back();
        owner_ = std::exchange(other.owner_, nullptr);
        first_ = std::exchange(other.first_, nullptr);
        count_ = std::exchange(other.count_, 0);
    }
    return *this;
}

FakePages::~FakePages() { give_back(); }

void FakePages::give_back() noexcept {
    if (first_ != nullptr) {
        std::allocator<FakePage>().deallocate(first_, count_);
        owner_->lent_ -= count_ * sizeof(FakePage);
        first_ = nullptr;
        count_ = 0;
    }
}

std::span<std::byte> FakePages::bytes() const noexcept {
    return std::as_writable_bytes(std::span<FakePage>(first_, count_));
}

void FakeSink::write(std::string_view) { unsupported("write"); }
void FakeSink::close() { unsupported("close"); }
FakeSink FakeStaged::open(std::string_view) { unsupported("open"); }
void FakeStaged::publish(std::string_view, Seconds) { unsupported("publish"); }
void FakeStaged::discard() { unsupported("discard"); }
void FakeLock::release() { unsupported("release"); }

void FakeChild::check(std::string_view operation) const {
    if (!program_.flood.empty() && !killed_) {
        fail(Code::output, operation, program_.flood);
    }
}

bool FakeChild::read_line(Seconds, String& out) {
    out.clear();
    check("read_line");
    if (killed_ || line_ >= program_.lines.size()) {
        if (program_.hang && !killed_) {
            fail(Code::deadline, "read_line", "the fake's program hangs");
        }
        return false;
    }
    set(out, program_.lines[line_++]);
    out.push_back('\n');
    return true;
}

void FakeChild::write(std::string_view) { check("write"); }

int FakeChild::wait(Seconds) {
    check("wait");
    if (program_.hang && !killed_) {
        fail(Code::deadline, "wait", "the fake's program hangs");
    }
    return killed_ ? 1 : program_.code;
}

void FakeOutput::write(std::string_view utf8) {
    for (std::size_t at = 0; at < utf8.size();) {
        const std::size_t from = at;
        const auto cp = code_point(utf8, at);
        if (!cp || (encoding_ == Encoding::ascii && *cp > 0x7F)) {
            bytes_ += '?';
        } else {
            bytes_.append(utf8.substr(from, at - from));
        }
    }
}

// --- the fake ---

FakePlatform::FakePlatform(const Config& config) : config_(config), output_(config_.encoding) {}

void FakePlatform::record(std::string_view operation) const {
    calls_.emplace_back(operation);
#if defined(QIVEN3_FAULTS)
    if (countdown_ != nullptr && operation != "wipe" && countdown_->fail()) {
        fail(operation == "pages" ? Code::memory : Code::io, operation, "fault injected");
    }
#endif
}

void FakePlatform::set_read_error(std::string_view path, std::string_view detail) {
    config_.read_errors.insert_or_assign(std::string(path), std::string(detail));
}

void FakePlatform::refuse_pages(std::size_t k) noexcept {
    refuse_at_ = k == 0 ? 0 : pages_calls_ + k;
}

std::string_view FakePlatform::name() const {
    record("name");
    return "fake";
}

void FakePlatform::config_dir(bool create, String& out) {
    record("config_dir");
    out.clear();
    if (config_.config_dir.empty()) {
        fail(Code::config, "config_dir", "the fake has no configuration directory");
    }
    check_utf8("config_dir", config_.config_dir);
    const fs::path path = to_path(config_.config_dir);
    if (!path.is_absolute()) {
        fail(Code::path, "config_dir", "the override is not absolute");
    }
    if (create) {
        std::error_code error;
        const fs::file_status status = fs::status(path, error);
        if (fs::exists(status) && !fs::is_directory(status)) {
            fail(Code::exists, "config_dir", "not a directory");
        }
        if (!fs::is_directory(status) && (!fs::create_directory(path, error) || error)) {
            fail(Code::io, "config_dir", "create_directory: " + error.message());
        }
    }
    set(out, config_.config_dir);
}

void FakePlatform::resolve(std::string_view path, String& out) {
    record("resolve");
    out.clear();
    std::string text = to_text(checked("resolve", path));
    for (int hops = 0;; ++hops) {
        bool followed = false;
        for (const auto& [from, to] : config_.links) {
            const std::string link = normal(from);
            if (under(text, link)) {
                text = normal(normal(to) + "/" + text.substr(link.size()));
                followed = true;
                break;
            }
        }
        if (!followed) {
            set(out, text);
            return;
        }
        if (hops == 40) {
            fail(Code::path, "resolve", "links past 40 hops");
        }
    }
}

void FakePlatform::normcase(std::string_view path, String& out) {
    record("normcase");
    out.clear();
    check_utf8("normcase", path);
    for (const char c : path) {
        out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
}

void FakePlatform::facts(std::string_view path, Facts& out) {
    record("facts");
    const std::string text = to_text(checked("facts", path));
    const FakeFacts* best = &config_.default_facts;
    std::size_t length = 0;
    for (const auto& [prefix, readings] : config_.facts) {
        const std::string normed = normal(prefix);
        if (under(text, normed) && normed.size() >= length) {
            best = &readings;
            length = normed.size();
        }
    }
    set(out.volume, best->volume);
    set(out.protection, best->protection);
    set(out.file_history, best->file_history);
    set(out.bitlocker, best->bitlocker);
}

bool FakePlatform::synced(std::string_view path, String& out) {
    record("synced");
    out.clear();
    const std::string text = to_text(checked("synced", path));
    for (const auto& [folder, client] : config_.synced) {
        if (under(text, normal(folder))) {
            set(out, client);
            return true;
        }
    }
    return false;
}

void FakePlatform::replace(std::string_view, std::string_view, Seconds) {
    record("replace");
    unsupported("replace");
}

FakeStaged FakePlatform::stage(std::string_view) {
    record("stage");
    unsupported("stage");
}

void FakePlatform::create(std::string_view path, bool directory) {
    record("create");
    const fs::path target = checked("create", path);
    if (config_.root.empty() || !under(to_text(target), normal(config_.root))) {
        fail(Code::path, "create", "outside the fake's root");
    }
    std::error_code error;
    if (fs::exists(fs::symlink_status(target, error))) {
        fail(Code::exists, "create", "present");
    }
    if (directory) {
        if (!fs::create_directory(target, error) || error) {
            fail(Code::io, "create", "create_directory: " + error.message());
        }
        return;
    }
    // An empty file by the system's exclusive create, so one of two racing
    // creates wins: "wbx" by _wfopen_s on Windows, for the path's UTF-16;
    // elsewhere fopen's "wbx", which the C library opens O_CREAT | O_EXCL.
    gsl::owner<std::FILE*> file = nullptr;
#if defined(_WIN32)
    const int code = _wfopen_s(&file, target.c_str(), L"wbx");
#else
    file = std::fopen(target.c_str(), "wbx");
    const int code = file != nullptr ? 0 : errno;
#endif
    if (file == nullptr) {
        fail(code == EEXIST ? Code::exists : Code::io, "create",
             code == EEXIST ? "present" : "the file did not open: " + named(static_cast<std::errc>(code)));
    }
    static_cast<void>(std::fclose(file));
}

void FakePlatform::read(std::string_view path, String& out) {
    record("read");
    out.clear();
    const fs::path target = checked("read", path);
    const auto planted = config_.read_errors.find(path);
    if (planted != config_.read_errors.end()) {
        fail(Code::io, "read", planted->second);
    }
    std::error_code error;
    const fs::file_status status = fs::status(target, error);
    if (!fs::is_regular_file(status)) {
        fail(Code::io, "read", fs::exists(status) ? "not a file" : "not found: " + named(std::errc::no_such_file_or_directory));
    }
    // The size by the stream, which reads it from the open file.
    std::ifstream in(target, std::ios::binary | std::ios::ate);
    const std::streamoff size = in ? static_cast<std::streamoff>(in.tellg()) : -1;
    if (size < 0 || !in.seekg(0)) {
        fail(Code::io, "read", "the stream did not open");
    }
    out.resize(static_cast<std::size_t>(size));
    in.read(out.data(), static_cast<std::streamsize>(size));
    if (!in || in.gcount() != static_cast<std::streamsize>(size)) {
        out.clear();
        fail(Code::io, "read", "the stream read short");
    }
}

FakeLock FakePlatform::lock(std::string_view, Seconds) {
    record("lock");
    unsupported("lock");
}

void FakePlatform::program(std::string_view name, String& out) {
    record("program");
    out.clear();
    const auto at = config_.found.find(name);
    if (at == config_.found.end()) {
        fail(Code::start, "program", "no " + std::string(name) + " on the fake's PATH");
    }
    set(out, at->second);
}

const Program& FakePlatform::find(const Command& command, std::string_view operation) const {
    if (command.argv.empty()) {
        fail(Code::start, operation, "an empty argv");
    }
    const std::vector<std::string> argv(command.argv.begin(), command.argv.end());
    const auto at = config_.programs.find(argv);
    if (at == config_.programs.end()) {
        fail(Code::start, operation, "no such program: " + argv.front());
    }
    return at->second;
}

FakeChild FakePlatform::start(const Command& command) {
    record("start");
    return FakeChild(find(command, "start"));
}

void FakePlatform::run(const Command& command, std::string_view, Ran& out) {
    record("run");
    const Program& chosen = find(command, "run");
    if (!chosen.flood.empty()) {
        fail(Code::output, "run", chosen.flood);
    }
    if (chosen.hang) {
        fail(Code::deadline, "run", "the fake's program hangs");
    }
    const std::string lines = joined(chosen);
    if (lines.size() > command.caps.out) {
        fail(Code::output, "run", "out");
    }
    if (chosen.err.size() > command.caps.err) {
        fail(Code::output, "run", "err");
    }
    out.code = chosen.code;
    set(out.out, lines);
    set(out.err, chosen.err);
}

void FakePlatform::code_page(String& out) {
    record("code_page");
    set(out, config_.code_page);
}

FakeOutput& FakePlatform::standard_output() {
    record("standard_output");
    return output_;
}

void FakePlatform::residual(std::span<const std::string_view> paths, Vector<Row>& out) {
    record("residual");
    out.clear();
    for (const std::string_view path : paths) {
        checked("residual", path);
    }
    memory::Arena& arena = out.get_allocator().arena();
    for (const FakeRow& row : config_.residual) {
        Row& made = out.emplace_back(arena);
        set(made.name, row.name);
        set(made.applies, row.applies);
        set(made.kept, row.kept);
        set(made.how_long, row.how_long);
        set(made.owner_can, row.owner_can);
    }
}

void FakePlatform::random_bytes(std::span<std::byte> into) {
    record("random_bytes");
    for (std::byte& b : into) {
        if (pending_at_ == pending_.size()) {
            pending_ = seed_block(config_.seed, block_++);
            pending_at_ = 0;
        }
        b = static_cast<std::byte>(pending_[pending_at_++]);
    }
}

// Volatile stores, which the compiler keeps.
void FakePlatform::wipe(std::span<std::byte> bytes) noexcept {
    try {
        record("wipe");
    } catch (...) {
        memory::fail_fast("fake::FakePlatform::wipe: the record failed");
    }
    const std::span<volatile std::byte> kept(bytes.data(), bytes.size());
    for (volatile std::byte& b : kept) {
        b = std::byte{0};
    }
}

std::chrono::sys_time<std::chrono::milliseconds> FakePlatform::now() {
    record("now");
    return std::chrono::floor<std::chrono::milliseconds>(config_.clock);
}

FakePages FakePlatform::pages(std::size_t n) {
    record("pages");
    ++pages_calls_;
    if (refuse_at_ != 0 && pages_calls_ == refuse_at_) {
        fail(Code::memory, "pages", "the fake refuses this call");
    }
    if (n == 0) {
        return FakePages();
    }
    const std::optional<std::size_t> padded = memory::add(n, sizeof(FakePage) - 1);
    if (!padded) {
        fail(Code::memory, "pages", "a count that cannot be rounded to whole pages");
    }
    const std::size_t count = *padded / sizeof(FakePage);
    FakePage* first = nullptr;
    try {
        first = std::allocator<FakePage>().allocate(count);
    } catch (const std::bad_alloc&) {
        fail(Code::memory, "pages", "the standard library refused the pages");
    }
    std::uninitialized_value_construct_n(first, count);
    lent_ += count * sizeof(FakePage);
    return FakePages(*this, first, count);
}

}  // namespace qiven3::platform::fake
