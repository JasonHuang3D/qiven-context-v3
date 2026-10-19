#pragma once

// The Windows adapter: the one product target that includes a Windows
// header or links a Windows library, and it includes them in its sources
// alone, never here. Its first operations are the configuration location,
// resolve, the console, random bytes and the clock; the files' operations
// (replace, stage, create, read, lock), the facts (facts, synced, residual)
// and the processes (program, start, run) are PlatformError unsupported
// until their modules are built.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platform/platform.hpp"

namespace qiven3::platform::win {

// A Win32 error as a PlatformError's detail names it: its name and number,
// "ERROR_PATH_NOT_FOUND (3)", or "Win32 error <n>" for one off the table.
std::string error_name(unsigned long error);

class WindowsPlatform final : public Platform {
public:
    // config_dir, when given, overrides the known folder's qiven3.
    explicit WindowsPlatform(std::optional<std::string> config_dir);
    ~WindowsPlatform() override;
    WindowsPlatform(const WindowsPlatform&) = delete;
    WindowsPlatform& operator=(const WindowsPlatform&) = delete;

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

private:
    std::optional<std::string> config_dir_;
    std::unique_ptr<Output> output_;
};

}  // namespace qiven3::platform::win
