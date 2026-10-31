// The per-call wire's budget, nothing allocated, counted: this binary's own
// operator new counts each call, and read_request calls it for no request,
// read or refused, at its bounds or past them, nor a request's copy and
// views; answer and write call it for none of their own, on an output
// that allocates nothing, written or failing. Its one platform call is
// failed in turn by wire.answer_faults; there is no arena to run a byte
// short, as nothing is allocated. Under MemorySanitizer and
// ThreadSanitizer, whose runtimes define operator new, the cases run
// without the count.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <string_view>
#include <utility>

#include "platform/platform.hpp"
#include "qiven/runner/runner.hpp"
#include "wire/wire.hpp"

// MemorySanitizer's and ThreadSanitizer's runtimes define operator new in
// the binary themselves, so that no other may: there the count is not
// applicable, and the cases say so.
#if defined(__has_feature)
#if __has_feature(memory_sanitizer) || __has_feature(thread_sanitizer)
#define QIVEN3_RUNTIME_NEW 1
#endif
#endif

#if !defined(QIVEN3_RUNTIME_NEW)
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

// The heap's four replaceable forms; the library's others call these but
// the sized deletes, below.
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
// The sized forms, which GCC asks for beside the unsized ones.
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t, std::align_val_t align) noexcept { ::operator delete(p, align); }
#endif

namespace {

namespace wire = qiven3::wire;

constexpr std::size_t most = std::numeric_limits<std::size_t>::max();

#if !defined(QIVEN3_RUNTIME_NEW)
constexpr bool counted = true;

// operator new's calls since the last asking.
struct Count {
    std::size_t at = news.load();
    std::size_t operator()() {
        const std::size_t now = news.load();
        return now - std::exchange(at, now);
    }
};
#else
constexpr bool counted = false;

// No count where the runtime holds operator new.
struct Count {
    std::size_t operator()() { return 0; }
};
#endif

// That taken, operator new's calls, is none, where they are counted.
void none_counted(qiven::runner::Context& qiven_ctx, std::string_view name, std::size_t taken) {
    if constexpr (counted) {
        std::cout << name << ": operator new calls " << taken << "\n";
        CHECK_EQ(taken, std::size_t{0});
    } else {
        std::cout << name << ": operator new not counted: the sanitizer's runtime defines it\n";
    }
}

// An output keeping how many bytes it took, allocating nothing; its write
// throws where told to.
struct Counting {
    struct Out {
        std::size_t taken = 0;
        bool refuse = false;
        void write(std::string_view bytes) {
            if (refuse) {
                throw qiven3::platform::PlatformError(qiven3::platform::Code::io, "stdout", "refused");
            }
            taken += bytes.size();
        }
    };
    Out out;
    Out& standard_output() { return out; }
};

}  // namespace

QIVEN_CASE(wire_memory, read) {
    // Arguments of each shape, the argument vector built before counting.
    const std::array<const char*, 9> argv = {"qiven3", "location", "init", "", "caf\xc3\xab",
                                             "\xf0\x9f\x98\x80", "--config", "x", nullptr};
    const std::array<const char*, 4> bad = {"qiven3", "ok", "\xed\xa0\x80", nullptr};
    const std::array<const char*, 4> holed = {"qiven3", "ok", nullptr, nullptr};
    // The widest; the vector's own sizes, 7 arguments, 8 bytes the most in
    // one and 30 in all; then each a step below them.
    const std::array<wire::Bounds, 5> bounds = {wire::Bounds{most, most, most}, wire::Bounds{7, 8, 30},
                                                wire::Bounds{6, 8, 30}, wire::Bounds{7, 7, 30},
                                                wire::Bounds{7, 8, 29}};
    std::size_t read = 0;
    std::size_t refused = 0;
    Count count;
    for (const wire::Bounds& b : bounds) {
        for (int argc = 0; argc <= 8; ++argc) {
            const std::optional<wire::RequestView> request = wire::read_request(argc, argv.data(), b);
            if (request) {
                const wire::RequestView copied = *request;
                std::size_t bytes = 0;
                for (std::size_t i = 0; i < copied.size(); ++i) {
                    bytes += copied[i].size();
                }
                CHECK_EQ(bytes, request->bytes());
                ++read;
            } else {
                ++refused;
            }
        }
        CHECK(!wire::read_request(3, bad.data(), b).has_value());
        CHECK(!wire::read_request(3, holed.data(), b).has_value());
        CHECK(!wire::read_request(-1, argv.data(), b).has_value());
        CHECK(!wire::read_request(2, nullptr, b).has_value());
    }
    const std::size_t taken = count();
    std::cout << "wire_memory.read: " << read << " read, " << refused << " refused\n";
    none_counted(qiven_ctx, "wire_memory.read", taken);
    // 45 vectors: the count's step refuses argc 8, the argument's argc 2
    // to 8 ("location" is 8 bytes), the total's argc 8.
    CHECK_EQ(read, std::size_t{36});
    CHECK_EQ(refused, std::size_t{9});
}

QIVEN_CASE(wire_memory, answer) {
    Counting output;
    Count count;
    const int status = wire::answer(output, "an answer of some bytes\n", 0);
    const int none = wire::answer(output, "", 4);
    wire::write(output, "a line\n");
    output.out.refuse = true;
    const int failed = wire::answer(output, "a line\n", 0);
    none_counted(qiven_ctx, "wire_memory.answer", count());
    CHECK_EQ(status, 0);
    CHECK_EQ(none, 4);
    CHECK_EQ(failed, wire::failed);
    CHECK_EQ(output.out.taken, std::size_t{31});
}
