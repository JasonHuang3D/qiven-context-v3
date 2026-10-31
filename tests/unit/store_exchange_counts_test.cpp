// store/exchange's counts, on the bed and steps of exchange_bed.hpp, which describes them.

#include <iostream>

#include "qiven/runner/runner.hpp"
#include "support/stores/exchange_bed.hpp"

using namespace qiven3::test::exchange_bed;  // NOLINT(google-build-using-namespace)

#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
namespace {

// A step's allocations by allocator, its engine calls and platform calls.
struct Count {
    std::size_t arenas = 0;
    std::size_t heap = 0;
    std::size_t calls = 0;
    std::size_t caller = 0;
    std::size_t platform = 0;
    friend bool operator==(const Count&, const Count&) = default;
};

// The counts each step makes, pinned as measured on Windows; Linux's as
// Windows' change, not measured, where plain records changed them. The
// engine heap's count is the engine's own, by its system's file layer;
// elsewhere not compared.
struct Pinned {
    std::string_view name;
    Count windows;
    Count on_linux;
};
#if defined(_WIN32) || defined(__linux__)
constexpr bool heap_pinned = true;
#else
constexpr bool heap_pinned = false;
#endif
constexpr std::array<Pinned, 5> pinned = {{
    {"export", {1, 209, 50, 0, 1}, {1, 202, 50, 0, 1}},
    {"heads", {0, 0, 4, 2, 0}, {0, 0, 4, 2, 0}},
    {"next", {0, 0, 16, 9, 0}, {0, 0, 16, 9, 0}},
    {"import", {13, 137, 203, 2, 0}, {13, 137, 203, 2, 0}},
    {"verify", {0, 17, 28, 0, 0}, {0, 17, 28, 0, 0}},
}};

Count pinned_of(std::string_view name) {
    for (const Pinned& p : pinned) {
        if (p.name == name) {
#if defined(_WIN32)
            return p.windows;
#else
            return p.on_linux;
#endif
        }
    }
    return {};
}

}  // namespace

QIVEN_CASE(store_exchange, counts) {
    const auto b = std::make_unique<Bed>();
    qiven3::memory::Countdown arenas;
    qiven3::memory::Countdown heap;
    qiven3::memory::Countdown calls;
    qiven3::memory::Countdown caller;
    qiven3::memory::Countdown platform;
    b->attached = &arenas;
    b->source->attach(&arenas);
    b->target->attach(&arenas);
    b->engine->heap().attach(&heap);
    sqlite::attach_calls(&calls, &arenas);
    b->call_countdown = &caller;
    b->fake.attach(&platform);
    for (const Step& s : steps()) {
        if (s.side == Side::importing) {
            b->empty_target();
        }
        if (s.made) {
            b->make_export();
        }
        const Chars c = b->caller();
        arenas.disarm();
        heap.disarm();
        calls.disarm();
        caller.disarm();
        platform.disarm();
        const Ran r = s.run(*b, c);
        const Count got{arenas.calls(), heap.calls(), calls.calls(), caller.calls(), platform.calls()};
        std::cout << "store_exchange.counts: " << s.name << ": arenas " << got.arenas << ", engine heap " << got.heap
                  << ", engine calls " << got.calls << ", caller " << got.caller << ", platform " << got.platform
                  << "; " << r.result << "\n";
        Count want = pinned_of(s.name);
        want.heap = heap_pinned ? want.heap : got.heap;
        CHECK(got == want);
        b->x.reset();
    }
    b->fake.attach(nullptr);
    sqlite::attach_calls(nullptr, nullptr);
    b->engine->heap().attach(nullptr);
    b->call_countdown = nullptr;
    b->attached = nullptr;
    b->source->attach(nullptr);
    b->target->attach(nullptr);
    CHECK(b->reopened_target() == source_counts);
}

#else
QIVEN_CASE(store_exchange, counts) { QIVEN_SKIP(unfaulted); }
#endif
