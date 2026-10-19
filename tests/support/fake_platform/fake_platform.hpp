#pragma once

// The fake platform, qiven3_platform_fake: the contract on the C++ standard
// library alone, so the seam is proved on every system. It is in memory but
// for create and read, which work on real files (create under its scratch
// root alone), and config_dir(true), which makes its configuration
// directory. It reads no registry and starts no process. Each operation
// called is kept in its record, in order.
//
// replace, stage and lock come with the platform's files module, whose
// hooks the fake's busy counts drive; until then each is PlatformError
// unsupported.

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/platform.hpp"

namespace qiven3::platform::fake {

// A program the fake runs, found by its whole argv.
struct Program {
    std::vector<std::string> lines;  // its standard output, each line ending LF
    std::string err;                 // its standard error
    int code = 0;                    // its exit code
    bool hang = false;               // never ends: the call is deadline
    std::string flood;               // "out" or "err": past any cap, output naming it
};

// The fake's stream: UTF-8 bytes as given, or ASCII, each code point above
// U+007F one '?'. An invalid UTF-8 byte is one '?' in either.
enum class Encoding { utf8, ascii };

struct Config {
    std::string root;        // the scratch root: create makes nothing outside it
    std::string config_dir;  // the configuration location
    std::map<std::string, std::string> read_errors;  // path: read's io detail
    std::vector<std::pair<std::string, Facts>> facts;  // by path prefix, the longest
    Facts default_facts{"", {State::unknown, "fake"}, {State::unknown, "fake"},
                        {State::unknown, "fake"}};
    std::vector<std::pair<std::string, std::string>> synced;  // folder, client's name
    std::vector<std::pair<std::string, std::string>> links;   // from, to: resolve follows
    std::string code_page = "cp65001";
    Encoding encoding = Encoding::utf8;
    std::map<std::vector<std::string>, Program> programs;  // by argv
    std::map<std::string, std::string> found;  // program(name): its path
    std::vector<Row> residual;
    std::uint64_t seed = 0;  // random_bytes' seed
    std::chrono::sys_time<std::chrono::microseconds> clock{};
};

// The k-th 32-byte block of seed s: SHA-256 over s and k, each an 8-byte
// big-endian integer.
std::string seed_block(std::uint64_t seed, std::uint64_t k);

class FakePlatform final : public Platform {
public:
    explicit FakePlatform(Config config);
    ~FakePlatform() override;
    FakePlatform(const FakePlatform&) = delete;
    FakePlatform& operator=(const FakePlatform&) = delete;

    std::string name() const override;
    std::string config_dir(bool create) override;
    std::string resolve(const std::string& path) override;
    Facts facts(const std::string& path) override;
    std::optional<std::string> synced(const std::string& path) override;
    void replace(const std::string& path, std::string_view data, Seconds deadline) override;
    std::unique_ptr<Staged> stage(const std::string& directory) override;
    void create(const std::string& path, bool directory) override;
    std::string read(const std::string& path) override;
    std::unique_ptr<Lock> lock(const std::string& path, Seconds deadline) override;
    std::string program(const std::string& name) override;
    std::unique_ptr<Child> start(const Command& command) override;
    Ran run(const Command& command, std::string_view data) override;
    std::string code_page() override;
    Output& standard_output() override;
    std::vector<Row> residual(const std::vector<std::string>& paths) override;
    std::string random_bytes(std::size_t n) override;
    std::chrono::sys_time<std::chrono::milliseconds> now() override;

    // The test's clock; now() gives it truncated to milliseconds.
    void set_clock(std::chrono::sys_time<std::chrono::microseconds> clock);
    // The operations called, by the contract's names, in order.
    std::vector<std::string> calls() const;
    // The bytes written to standard_output().
    std::string written() const;

private:
    void record(const char* operation) const;
    const Program& find(const Command& command, const char* operation) const;

    Config config_;
    mutable std::mutex mutex_;
    mutable std::vector<std::string> calls_;
    std::uint64_t block_ = 0;
    std::string pending_;  // the drawn block's unread bytes
    std::unique_ptr<Output> output_;
};

}  // namespace qiven3::platform::fake
