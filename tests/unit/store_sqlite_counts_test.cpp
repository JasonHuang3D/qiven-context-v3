// store/sqlite's counts, on the bed and steps of sqlite_bed.hpp, which describes them.

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "support/stores/sqlite_bed.hpp"

using namespace qiven3::test::sqlite_bed;  // NOLINT(google-build-using-namespace)

#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
namespace {

// The counts each step makes, pinned as measured on Windows; Linux's as Windows' change, not measured, where plain
// records changed them. The engine heap's, by its system's file layer, differs by system; elsewhere not compared.
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
constexpr std::array<Pinned, 15> pinned = {{
    {"create", {1, 652, 98, 0, 5}, {1, 642, 98, 0, 5}},
    {"append", {2, 14, 74, 0, 1}, {2, 14, 74, 0, 1}},
    {"append_acc", {2, 21, 43, 0, 1}, {2, 21, 43, 0, 1}},
    {"head", {0, 0, 4, 0, 0}, {0, 0, 4, 0, 0}},
    {"heads", {0, 0, 11, 1, 0}, {0, 0, 11, 1, 0}},
    {"header", {0, 0, 3, 1, 0}, {0, 0, 3, 1, 0}},
    {"body", {0, 0, 3, 1, 0}, {0, 0, 3, 1, 0}},
    {"forgotten", {0, 0, 3, 0, 0}, {0, 0, 3, 0, 0}},
    {"scan", {0, 10, 47, 2, 0}, {0, 10, 47, 2, 0}},
    {"size", {0, 0, 8, 0, 0}, {0, 0, 8, 0, 0}},
    {"forget", {3, 719, 64, 1, 1}, {3, 722, 64, 1, 1}},
    {"purge", {0, 0, 3, 0, 0}, {0, 0, 3, 0, 0}},
    {"open", {1, 209, 80, 0, 3}, {1, 203, 80, 0, 3}},
    {"read_only", {1, 208, 68, 0, 3}, {1, 202, 68, 0, 3}},
    {"guard", {1, 0, 0, 0, 3}, {1, 0, 0, 0, 3}},
}};

void pin(std::vector<Step>& all) {
    for (Step& s : all) {
        for (const Pinned& p : pinned) {
            if (p.name == s.name) {
#if defined(_WIN32)
                s.count = p.windows;
#else
                s.count = p.on_linux;
#endif
            }
        }
    }
}

}  // namespace

QIVEN3_CASE(store_sqlite, counts) {
    const auto b = std::make_unique<Bed>();
    std::vector<Step> all = steps();
    pin(all);
    qiven3::memory::Countdown arenas;
    qiven3::memory::Countdown heap;
    qiven3::memory::Countdown calls;
    qiven3::memory::Countdown caller;
    qiven3::memory::Countdown platform;
    b->engine->heap().attach(&heap);
    sqlite::attach_calls(&calls, &arenas);
    b->call_countdown = &caller;
    b->fake.attach(&platform);
    for (const Step& s : all) {
        const Chars c = b->caller();
        arenas.disarm();
        heap.disarm();
        calls.disarm();
        caller.disarm();
        platform.disarm();
        (void)s.run(*b, c);
        const Count got{arenas.calls(), heap.calls(), calls.calls(), caller.calls(), platform.calls()};
        std::cout << "store_sqlite.counts: " << s.name << ": arenas " << got.arenas << ", engine heap " << got.heap
                  << ", engine calls " << got.calls << ", caller " << got.caller << ", platform " << got.platform
                  << "\n";
        Count want = s.count;
        want.heap = heap_pinned ? want.heap : got.heap;
        CHECK(got == want);
    }
    b->fake.attach(nullptr);
    sqlite::attach_calls(nullptr, nullptr);
    b->engine->heap().attach(nullptr);
    b->store->attach(nullptr);
    CHECK(b->store->verify() == (qiven3::store::Counts{5, 3, 1, 1}));  // the course's
    CHECK(Store::open(b->path, false, true, b->fake).verify() == b->store->verify());
}
#else
QIVEN3_CASE(store_sqlite, counts) { QIVEN3_SKIP(unfaulted); }
#endif
