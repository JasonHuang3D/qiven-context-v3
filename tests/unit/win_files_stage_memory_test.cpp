// win/files' stage on the Windows adapter, its heap counted: this binary's
// own operator new counts each call, and no member of stage, open, write,
// close, publish or discard calls it, nor the Staged's or Sink's end, nor
// a refusal; their memory is a staging directory's pages, one VirtualAlloc
// a stage, whose fault win_files_stage.faults counts.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "platform/win/windows.hpp"
#include "runner/runner.hpp"

namespace {

// Each call of operator new in this binary, all threads.
std::atomic<std::size_t> news{0};

// An aligned block: the system's pointer kept just below the one given.
void* aligned_block(std::size_t size, std::size_t align) noexcept {
    const std::size_t room = size + align + sizeof(void*);
    void* raw = room < size ? nullptr : std::malloc(room);
    if (raw == nullptr) {
        return nullptr;
    }
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
    const std::uintptr_t at = (start + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1);
    reinterpret_cast<void**>(at)[-1] = raw;
    return reinterpret_cast<void*>(at);
}

}  // namespace

// The heap's four replaceable forms; the library's others call these.
void* operator new(std::size_t size) {
    news.fetch_add(1, std::memory_order_relaxed);
    void* const p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new(std::size_t size, std::align_val_t align) {
    news.fetch_add(1, std::memory_order_relaxed);
    void* const p = aligned_block(size == 0 ? 1 : size, static_cast<std::size_t>(align));
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void operator delete(void* p, std::align_val_t) noexcept {
    if (p != nullptr) {
        std::free(static_cast<void**>(p)[-1]);
    }
}

namespace {

namespace fs = std::filesystem;
using qiven3::platform::PlatformError;
using qiven3::platform::Seconds;
using qiven3::platform::win::WindowsPlatform;
using qiven3::platform::win::WindowsSink;
using qiven3::platform::win::WindowsStaged;

// Whether op throws PlatformError, with no string made.
template <class Op>
bool refused(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const PlatformError&) {
        return true;
    }
    return false;
}

// operator new's calls since the last asking.
struct Count {
    std::size_t at = news.load();
    std::size_t operator()() {
        const std::size_t now = news.load();
        return now - std::exchange(at, now);
    }
};

}  // namespace

QIVEN3_CASE(win_files_stage, memory) {
    const auto device = std::make_unique<std::random_device>();
    const fs::path root =
        fs::temp_directory_path() / ("qiven3-win-stage-" + std::to_string((*device)()) + std::to_string((*device)()));
    fs::create_directories(root);
    const fs::path bags = root / "bags";
    fs::create_directory(bags);
    std::string dir;
    for (const char8_t c : bags.u8string()) {
        dir.push_back(static_cast<char>(c));
    }
    const std::string payload(std::size_t{1} << 20U, 'x');
    const Seconds five{5.0};
    {
        WindowsPlatform platform(std::nullopt);
        Count taken;
        // The count's control: a string past its own buffer calls it once.
        const std::string control(std::string_view(payload).substr(0, 64));
        CHECK_EQ(taken(), 1U);
        CHECK_EQ(control.size(), 64U);
        std::optional<WindowsStaged> staged(platform.stage(dir));
        CHECK_EQ(taken(), 0U);
        WindowsSink bodies = staged->open("data/bodies.jsonl");
        WindowsSink headers = staged->open("data/headers.jsonl");
        CHECK_EQ(taken(), 0U);
        bodies.write(payload);
        headers.write("{}\n");
        CHECK_EQ(taken(), 0U);
        bodies.close();
        CHECK_EQ(taken(), 0U);
        staged->publish("bag", five);
        CHECK_EQ(taken(), 0U);
        CHECK(refused([&] { headers.write("late"); }));
        CHECK(refused([&] { staged->publish("bag", five); }));
        CHECK(refused([&] { static_cast<void>(staged->open("../x")); }));
        CHECK(refused([&] { static_cast<void>(platform.stage("relative")); }));
        staged.emplace(platform.stage(dir));
        CHECK(refused([&] { staged->publish("bag", five); }));  // exists
        CHECK_EQ(taken(), 0U);
        staged.emplace(platform.stage(dir));
        WindowsSink sink = staged->open("a/b/c.txt");
        sink.write("c");
        staged->discard();
        sink = WindowsSink();
        staged.reset();
        CHECK_EQ(taken(), 0U);
        {
            WindowsStaged ending = platform.stage(dir);
            const WindowsSink open = ending.open("x/y.txt");
        }
        CHECK_EQ(taken(), 0U);
        std::cout << "win_files_stage.memory: operator new calls by the members " << taken() << "\n";
    }
    std::error_code error;
    fs::remove_all(root, error);
}
