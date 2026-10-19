#include "support/fake_platform/fake_platform.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

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

[[noreturn]] void fail(Code code, const char* operation, std::string detail) {
    throw PlatformError(code, operation, std::move(detail));
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

fs::path to_path(const std::string& text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string to_text(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// A path the contract takes: UTF-8, no control character, absolute.
fs::path checked(const char* operation, const std::string& text) {
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
bool under(const std::string& path, const std::string& prefix) {
    if (prefix.empty() || path.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    return path.size() == prefix.size() || is_sep(prefix.back()) || is_sep(path[prefix.size()]);
}

std::string normal(const std::string& text) { return to_text(to_path(text).lexically_normal()); }

class FakeOutput final : public Output {
public:
    explicit FakeOutput(Encoding encoding) : encoding_(encoding) {}
    void write(std::string_view utf8) override {
        std::lock_guard guard(mutex_);
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
    std::string bytes() const {
        std::lock_guard guard(mutex_);
        return bytes_;
    }

private:
    Encoding encoding_;
    mutable std::mutex mutex_;
    std::string bytes_;
};

std::string joined(const Program& program) {
    std::string out;
    for (const std::string& line : program.lines) {
        out += line + "\n";
    }
    return out;
}

// A program's run in memory: its lines, then its end; a hang or a flood
// reaches the caller as deadline or output.
class FakeChild final : public Child {
public:
    explicit FakeChild(Program program) : program_(std::move(program)) {}
    std::optional<std::string> read_line(Seconds) override {
        check("read_line");
        if (killed_ || line_ >= program_.lines.size()) {
            if (program_.hang && !killed_) {
                fail(Code::deadline, "read_line", "the fake's program hangs");
            }
            return std::nullopt;
        }
        return program_.lines[line_++] + "\n";
    }
    void write(std::string_view) override { check("write"); }
    void close_input() override {}
    int wait(Seconds) override {
        check("wait");
        if (program_.hang && !killed_) {
            fail(Code::deadline, "wait", "the fake's program hangs");
        }
        return killed_ ? 1 : program_.code;
    }
    void kill() override { killed_ = true; }

private:
    void check(const char* operation) const {
        if (!program_.flood.empty() && !killed_) {
            fail(Code::output, operation, program_.flood);
        }
    }
    Program program_;
    std::size_t line_ = 0;
    bool killed_ = false;
};

}  // namespace

std::string seed_block(std::uint64_t seed, std::uint64_t k) {
    std::string message;
    for (std::uint64_t value : {seed, k}) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            message += static_cast<char>((value >> shift) & 0xFF);
        }
    }
    return sha256(message);
}

FakePlatform::FakePlatform(Config config)
    : config_(std::move(config)), output_(std::make_unique<FakeOutput>(config_.encoding)) {}

FakePlatform::~FakePlatform() = default;

void FakePlatform::record(const char* operation) const {
    std::lock_guard guard(mutex_);
    calls_.emplace_back(operation);
}

std::vector<std::string> FakePlatform::calls() const {
    std::lock_guard guard(mutex_);
    return calls_;
}

std::string FakePlatform::written() const { return static_cast<FakeOutput&>(*output_).bytes(); }

std::string FakePlatform::name() const {
    record("name");
    return "fake";
}

std::string FakePlatform::config_dir(bool create) {
    record("config_dir");
    if (config_.config_dir.empty()) {
        fail(Code::config, "config_dir", "the fake has no configuration directory");
    }
    if (create) {
        const fs::path path = to_path(config_.config_dir);
        std::error_code error;
        const fs::file_status status = fs::status(path, error);
        if (fs::is_directory(status)) {
            return config_.config_dir;
        }
        if (fs::exists(status)) {
            fail(Code::exists, "config_dir", "not a directory");
        }
        if (!fs::create_directory(path, error) || error) {
            fail(Code::io, "config_dir", "create_directory: " + error.message());
        }
    }
    return config_.config_dir;
}

std::string FakePlatform::resolve(const std::string& path) {
    record("resolve");
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
            return text;
        }
        if (hops == 40) {
            fail(Code::path, "resolve", "links past 40 hops");
        }
    }
}

Facts FakePlatform::facts(const std::string& path) {
    record("facts");
    const std::string text = to_text(checked("facts", path));
    const Facts* best = &config_.default_facts;
    std::size_t length = 0;
    for (const auto& [prefix, readings] : config_.facts) {
        const std::string normed = normal(prefix);
        if (under(text, normed) && normed.size() >= length) {
            best = &readings;
            length = normed.size();
        }
    }
    return *best;
}

std::optional<std::string> FakePlatform::synced(const std::string& path) {
    record("synced");
    const std::string text = to_text(checked("synced", path));
    for (const auto& [folder, client] : config_.synced) {
        if (under(text, normal(folder))) {
            return client;
        }
    }
    return std::nullopt;
}

void FakePlatform::replace(const std::string&, std::string_view, Seconds) {
    record("replace");
    fail(Code::unsupported, "replace", "not implemented by the fake yet");
}

std::unique_ptr<Staged> FakePlatform::stage(const std::string&) {
    record("stage");
    fail(Code::unsupported, "stage", "not implemented by the fake yet");
}

void FakePlatform::create(const std::string& path, bool directory) {
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
    std::FILE* file = nullptr;
#if defined(_WIN32)
    const int failed = _wfopen_s(&file, target.c_str(), L"wbx");
#else
    file = std::fopen(target.c_str(), "wbx");
    const int failed = file == nullptr ? errno : 0;
#endif
    if (failed != 0 || file == nullptr) {
        const std::error_code why(failed, std::generic_category());
        fail(why == std::errc::file_exists ? Code::exists : Code::io, "create",
             "fopen wbx: " + why.message());
    }
    std::fclose(file);
}

std::string FakePlatform::read(const std::string& path) {
    record("read");
    const fs::path target = checked("read", path);
    const auto planted = config_.read_errors.find(path);
    if (planted != config_.read_errors.end()) {
        fail(Code::io, "read", planted->second);
    }
    std::error_code error;
    const fs::file_status status = fs::status(target, error);
    if (!fs::is_regular_file(status)) {
        fail(Code::io, "read", fs::exists(status) ? "not a file" : "not found");
    }
    std::ifstream in(target, std::ios::binary);
    std::ostringstream bytes;
    bytes << in.rdbuf();
    if (!in.good() && !in.eof()) {
        fail(Code::io, "read", "the stream failed");
    }
    return bytes.str();
}

std::unique_ptr<Lock> FakePlatform::lock(const std::string&, Seconds) {
    record("lock");
    fail(Code::unsupported, "lock", "not implemented by the fake yet");
}

std::string FakePlatform::program(const std::string& name) {
    record("program");
    const auto at = config_.found.find(name);
    if (at == config_.found.end()) {
        fail(Code::start, "program", "no " + name + " on the fake's PATH");
    }
    return at->second;
}

const Program& FakePlatform::find(const Command& command, const char* operation) const {
    if (command.argv.empty()) {
        fail(Code::start, operation, "an empty argv");
    }
    const auto at = config_.programs.find(command.argv);
    if (at == config_.programs.end()) {
        fail(Code::start, operation, "no such program: " + command.argv.front());
    }
    return at->second;
}

std::unique_ptr<Child> FakePlatform::start(const Command& command) {
    record("start");
    return std::make_unique<FakeChild>(find(command, "start"));
}

Ran FakePlatform::run(const Command& command, std::string_view) {
    record("run");
    const Program& chosen = find(command, "run");
    if (!chosen.flood.empty()) {
        fail(Code::output, "run", chosen.flood);
    }
    if (chosen.hang) {
        fail(Code::deadline, "run", "the fake's program hangs");
    }
    Ran ran{chosen.code, joined(chosen), chosen.err};
    if (ran.out.size() > command.caps.out) {
        fail(Code::output, "run", "out");
    }
    if (ran.err.size() > command.caps.err) {
        fail(Code::output, "run", "err");
    }
    return ran;
}

std::string FakePlatform::code_page() {
    record("code_page");
    return config_.code_page;
}

Output& FakePlatform::standard_output() {
    record("standard_output");
    return *output_;
}

std::vector<Row> FakePlatform::residual(const std::vector<std::string>& paths) {
    record("residual");
    for (const std::string& path : paths) {
        checked("residual", path);
    }
    return config_.residual;
}

std::string FakePlatform::random_bytes(std::size_t n) {
    record("random_bytes");
    std::lock_guard guard(mutex_);
    std::string out;
    while (out.size() < n) {
        if (pending_.empty()) {
            pending_ = seed_block(config_.seed, block_++);
        }
        const std::size_t take = std::min(pending_.size(), n - out.size());
        out += pending_.substr(0, take);
        pending_.erase(0, take);
    }
    return out;
}

std::chrono::sys_time<std::chrono::milliseconds> FakePlatform::now() {
    record("now");
    std::lock_guard guard(mutex_);
    return std::chrono::floor<std::chrono::milliseconds>(config_.clock);
}

void FakePlatform::set_clock(std::chrono::sys_time<std::chrono::microseconds> clock) {
    std::lock_guard guard(mutex_);
    config_.clock = clock;
}

}  // namespace qiven3::platform::fake
