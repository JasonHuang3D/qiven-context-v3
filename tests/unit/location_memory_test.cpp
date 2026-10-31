// location's budgets and counts, on the fake over a scratch directory of the case's own (location_bed.hpp's
// steps, support/fixtures). budget: each step's command arena peak within its 1 MiB, every page given back; store_dir's peak on
// the caller's arena. counts: each step's command arena allocations, the store's arenas, the engine calls the
// wrapper maps and the platform calls, exactly; the engine heap's allocations with an engine installed; the
// platform's operations in order. The fault runs are location_faults_test.cpp's.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/location.hpp"
#include "runner/runner.hpp"
#include "store/engine/sqlite.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/location_bed.hpp"

namespace {
using namespace qiven3::test::location_bed;
}  // namespace

QIVEN3_CASE(location, budget) {
    const auto b = std::make_unique<Bed>(Scratch{});
    for (const Step s : steps) {
        b->prepare(s);
        std::size_t peak = 0;
        const int code = b->run(s, nullptr, &peak);
        std::cout << "location.budget: " << name_of(s) << ": exit " << code << ", peak " << peak << " of "
                  << location::budget << " bytes; pages lent after " << b->fake->lent() << "\n";
        CHECK_EQ(code, Bed::expected(s));
        CHECK(peak > 0 && peak <= location::budget);
        CHECK_EQ(b->fake->lent(), std::size_t{0});
        CHECK(b->record_held(s, code));
    }
    // store_dir on the caller's arena: its strings, the record's bytes and
    // the dir, each at most twice its length with its terminator.
    b->prepare(Step::show);
    std::vector<std::byte> pages(std::size_t{1} << 16U);
    qiven3::memory::Arena arena(pages);
    {
        qiven3::memory::String got{qiven3::memory::Allocator<char>(arena)};
        location::store_dir(*b->fake, got);
        CHECK_EQ(std::string(got), text(b->held));
    }
    const std::size_t lengths = text(b->config_dir).size() + text(b->record).size() + b->before.size() +
                                text(b->held).size() + 4;
    std::cout << "location.budget: store_dir: peak " << arena.peak() << " on the caller's arena, its strings "
              << lengths << " bytes\n";
    CHECK(arena.peak() >= lengths && arena.peak() <= 2 * lengths + 64);
}

#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
namespace {

struct Count {
    std::size_t arenas = 0;
    std::size_t store = 0;
    std::size_t calls = 0;
    std::size_t platform = 0;
};

// The counts each step makes, pinned as measured, and the platform's
// operations in order. The command arena's count is its standard
// library's at the root's length (a string's growth): MSVC's on Windows,
// libstdc++'s and libc++'s on Linux; the engine heap's its file layer's,
// on Windows and Linux; elsewhere not compared.
struct Pinned {
    Step step;
    Count windows;
    Count libstdcxx;
    Count libcxx;
    std::string_view operations;
};
#if defined(_WIN32) || (defined(__linux__) && (defined(__GLIBCXX__) || defined(_LIBCPP_VERSION)))
constexpr bool counts_pinned = true;
#else
constexpr bool counts_pinned = false;
#endif
constexpr std::string_view check_ops = "pages config_dir lock resolve pages resolve synced facts standard_output "
                                       "standard_output standard_output";
constexpr std::array<Pinned, 9> pinned = {{
    {Step::init_new, {25, 3, 102, 29}, {27, 3, 102, 29}, {24, 3, 102, 29},
     "now pages pages resolve synced pages resolve synced create create replace random_bytes standard_output "
     "standard_output"},
    {Step::init_held, {25, 2, 81, 24}, {27, 2, 81, 24}, {25, 2, 81, 24},
     "now pages pages resolve synced replace random_bytes standard_output standard_output"},
    {Step::init_chosen, {5, 0, 0, 6}, {5, 0, 0, 6}, {5, 0, 0, 6}, "pages config_dir lock standard_output"},
    {Step::init_warned, {11, 1, 0, 14}, {10, 1, 0, 14}, {10, 1, 0, 14}, "standard_output"},
    {Step::show, {16, 0, 0, 12}, {17, 0, 0, 12}, {15, 0, 0, 12},
     "pages config_dir lock read facts standard_output standard_output standard_output standard_output "
     "standard_output"},
    {Step::change_same, {25, 1, 0, 18}, {24, 1, 0, 18}, {22, 1, 0, 18}, "normcase normcase standard_output standard_output"},
    {Step::change_new, {39, 3, 102, 33}, {39, 3, 102, 33}, {36, 3, 102, 33},
     "normcase normcase now pages pages resolve synced pages resolve synced create create standard_output replace "
     "random_bytes standard_output standard_output"},
    {Step::change_held, {39, 2, 81, 28}, {39, 2, 81, 28}, {37, 2, 81, 28},
     "normcase normcase now pages pages resolve synced standard_output replace random_bytes standard_output "
     "standard_output"},
    {Step::change_left, {24, 1, 0, 17}, {22, 1, 0, 17}, {21, 1, 0, 17}, "normcase normcase standard_output"},
}};

// The step's operations as pinned: init's and change's after the check's,
// change's with read after lock.
std::string operations_of(const Pinned& p) {
    const std::string_view op = p.step <= Step::init_warned ? "init" : (p.step == Step::show ? "show" : "change");
    if (op == "show" || p.step == Step::init_chosen) {
        return std::string(p.operations);
    }
    std::string check(check_ops);
    if (op == "change") {
        check.replace(check.find("lock") + 4, 0, " read");
    }
    return check + " " + std::string(p.operations);
}

// The engine heap's allocations, an engine installed, on Windows and Linux.
struct HeapPinned {
    Step step;
    std::size_t windows;
    std::size_t on_linux;
};
constexpr std::array<HeapPinned, 2> heap_pinned = {{{Step::init_new, 654, 642}, {Step::change_held, 214, 208}}};

}  // namespace

QIVEN3_CASE(location, counts) {
    const auto b = std::make_unique<Bed>(Scratch{});
    const bool at_length = text(b->root).size() == Bed::root_length;
    std::cout << "location.counts: the root " << text(b->root).size() << " bytes"
              << (at_length ? "" : ", the temporary directory too long: the command arena's count not compared") << "\n";
    Countdown arenas;
    Countdown store;
    Countdown calls;
    Countdown platform;
    for (const Pinned& p : pinned) {
        b->prepare(p.step);
        const std::size_t first = b->fake->calls().size();
        b->fake->attach(&platform);
        sqlite::attach_calls(&calls, &store);
        arenas.disarm();
        store.disarm();
        calls.disarm();
        platform.disarm();
        const int code = b->run(p.step, &arenas);
        b->fake->attach(nullptr);
        sqlite::attach_calls(nullptr, nullptr);
        const Count got{arenas.calls(), store.calls(), calls.calls(), platform.calls()};
        std::string ops;
        for (std::size_t i = first; i < b->fake->calls().size(); ++i) {
            ops += (ops.empty() ? "" : " ") + b->fake->calls().at(i);
        }
        std::cout << "location.counts: " << name_of(p.step) << ": exit " << code << "; command arena " << got.arenas
                  << ", store arenas " << got.store << ", engine calls " << got.calls << ", platform " << got.platform
                  << "\n  operations: " << ops << "\n";
        CHECK_EQ(code, Bed::expected(p.step));
        CHECK_EQ(ops, operations_of(p));
#if defined(_WIN32)
        const Count want = p.windows;
#elif defined(_LIBCPP_VERSION)
        const Count want = p.libcxx;
#else
        const Count want = p.libstdcxx;
#endif
        CHECK(!counts_pinned || (got.store == want.store && got.calls == want.calls && got.platform == want.platform));
        CHECK(!counts_pinned || !at_length || got.arenas == want.arenas);
    }
    b->engine.emplace(*b->fake, 1);
    Countdown heap;
    for (const HeapPinned& h : heap_pinned) {
        b->prepare(h.step);
        b->engine->heap().attach(&heap);
        heap.disarm();
        const int code = b->run(h.step);
        b->engine->heap().attach(nullptr);
        std::cout << "location.counts: " << name_of(h.step) << ", an engine installed: exit " << code
                  << "; engine heap " << heap.calls() << "\n";
        CHECK_EQ(code, Bed::expected(h.step));
#if defined(_WIN32)
        CHECK(!counts_pinned || heap.calls() == h.windows);
#else
        CHECK(!counts_pinned || heap.calls() == h.on_linux);
#endif
    }
    b->engine.reset();
}

#else
namespace {
constexpr std::string_view unfaulted = "not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers";
}  // namespace
QIVEN3_CASE(location, counts) { QIVEN3_SKIP(unfaulted); }
#endif
