// win/process' memory on this machine: this binary's own operator new
// counts each call, all threads. start's are its three std::jthread's, two
// each (the stop state and the thread's callable, the standard library's);
// the Child's members and its end take none, nor a refusal; program's and
// read_line's out and run's two copies are the caller's arena's alone. The
// Child's state and streams are its pages (one VirtualAlloc, the fault
// sweep's), the threads' stacks and the C runtime's per-thread blocks the
// system's, which operator new does not see. Under QIVEN3_SYSTEM_HEAP the
// arena's blocks go through operator new too, so the members whose out is
// on the arena are counted on the arena alone there.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "runner/runner.hpp"
#include "support/fixtures/win_process_case.hpp"

namespace {

std::atomic<std::size_t> news{0};

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

using namespace qiven3::test::process;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::platform::Ran;

#if defined(QIVEN3_SYSTEM_HEAP)
constexpr bool arena_counted = false;
#else
constexpr bool arena_counted = true;
#endif

// operator new's calls since the last asking.
struct Count {
    std::size_t at = news.load();
    std::size_t operator()() {
        const std::size_t now = news.load();
        return now - std::exchange(at, now);
    }
};

// The code of the PlatformError op throws, with no string made; unsupported
// for none.
template <class Op>
Code code_of(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const PlatformError& error) {
        return error.code;
    }
    return Code::unsupported;
}

// Each std::jthread's two blocks, three threads a Child.
constexpr std::size_t start_news = 6;

}  // namespace

QIVEN3_CASE(win_process, memory) {
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        ChildView sleep({"sleep", "60000"});
        ChildView talk({"print", hex("short\n" + std::string(5000, 'z') + "\n"), "", "0"});
        ChildView cat({"cat", "0"});
        const std::string data(300000, 'd');
        String out{Allocator<char>(arena)};
        Count taken;
        const std::string control(std::string(64, 'c'));
        CHECK_EQ(taken(), 1U);  // the count's control: a string past its own buffer
        platform.program("cmd", out);
        const std::size_t program_peak = arena.peak();
        CHECK(!arena_counted || taken() == 0U);
        CHECK(program_peak > 0U && program_peak <= 64U);
        out = String{Allocator<char>(arena)};
        taken();
        std::optional<WindowsChild> child(platform.start(sleep.command));
        CHECK_EQ(taken(), start_news);
        CHECK(child->read_line(Seconds(30.0), out));
        CHECK_EQ(taken(), 0U);  // a short line: none on the arena either
        child->write("unread");
        child->close_input();
        CHECK_EQ(code_of([&] { static_cast<void>(child->wait(Seconds(0.0))); }), Code::deadline);
        child->kill();
        child->kill();
        CHECK_EQ(child->wait(Seconds(5.0)), 1);
        CHECK_EQ(taken(), 0U);
        child.reset();
        CHECK_EQ(taken(), 0U);

        child.emplace(platform.start(talk.command));
        CHECK_EQ(taken(), start_news);
        const std::size_t before = arena.peak();
        CHECK(child->read_line(Seconds(30.0), out));
        CHECK(child->read_line(Seconds(30.0), out));
        CHECK_EQ(out.size(), 5001U);
        const std::size_t line_peak = arena.peak() - before;
        CHECK(!arena_counted || taken() == 0U);
        CHECK(line_peak >= 5001U && line_peak <= 5001U + 64U);
        out = String{Allocator<char>(arena)};
        child.reset();
        taken();

        CHECK_EQ(code_of([&] { static_cast<void>(platform.start(Command{})); }), Code::start);
        CHECK_EQ(taken(), 0U);

        const std::size_t run_before = arena.peak();
        {
            Ran ran(arena);
            platform.run(cat.command, data, ran);
            CHECK_EQ(ran.out.size(), data.size());
        }
        const std::size_t run_peak = arena.peak() - run_before;
        const std::size_t run_news = taken();
        CHECK(arena_counted ? run_news == start_news : run_news >= start_news);
        CHECK(run_peak >= data.size() + 1 && run_peak <= data.size() + 64U);
        std::cout << "win_process.memory: operator new calls: start " << start_news << ", run " << run_news
                  << "; arena peaks: program " << program_peak << " B, a line of 5,000 bytes " << line_peak
                  << " B, run of 300,000 bytes out " << run_peak << " B\n";
    }
    CHECK_EQ(arena.used(), 0U);
}
