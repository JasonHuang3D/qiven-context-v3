#pragma once

// out: a main's output for its log's drain: standard error when QIVEN3_LOG
// is "-", the file it names, appended to, when it names one, and nothing
// when it is unset or empty. Each write goes to the system at once.
//
// thread: one Out a thread.

#include <string_view>

namespace qiven3::diag {

class Out {
public:
    // The output QIVEN3_LOG names; a file that cannot be opened writes
    // nothing, and failed() says so.
    static Out from_environment() noexcept;

    Out() noexcept = default;
    Out(Out&& other) noexcept;
    Out& operator=(Out&& other) noexcept;
    Out(const Out&) = delete;
    Out& operator=(const Out&) = delete;
    ~Out();

    // Writes `bytes` whole; false when a write failed.
    bool write(std::string_view bytes) noexcept;
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    void close() noexcept;
    // The system's handle: a HANDLE on Windows, a file descriptor elsewhere.
#ifdef _WIN32
    static constexpr void* none = nullptr;
    void* handle_ = nullptr;
#else
    static constexpr int none = -1;
    int handle_ = none;
#endif
    bool owned_ = false;
    bool failed_ = false;
};

}  // namespace qiven3::diag
