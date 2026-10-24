#pragma once

// events: the kept crash buffers and the report's events, both systems'.
// diag::keep puts a ring in a slot of the process's table, at most 8, and
// diag::drop takes it out; the report reads each slot by an acquire load
// and each ring by its own (log/ring.hpp), with no lock. The table lives in
// the file of the system's report, signal.cpp on Linux and
// diag_windows.cpp on Windows, so that a handler names no other object's
// symbol. Inline, with no checked member of the standard library and no
// call that can fail: Linux's handlers call it, and K18 reads their
// object's undefined symbols. Each compare-exchange names its failure
// order: the hardened library checks the order the one-order form derives,
// and the compiler may keep that check out of line, a call off the list.
//
// The events are the rings' lines in the order of their slots, each ring's
// oldest first, the last 16 of them in all; each is cut at 300 code points.
//
// thread: hold and release from any thread; events from any thread or a
// signal handler, reading what the rings' last release stores committed. A
// ring dropped and ended while a report reads it is a residual: a Log drops
// its ring before its end, on its own thread.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <string_view>

#include "log/ring.hpp"

namespace qiven3::diag::detail {

inline constexpr std::size_t rings_max = 8;
inline constexpr std::size_t events_max = 16;
inline constexpr std::size_t event_points = 300;

using Rings = std::array<std::atomic<const log::Ring*>, rings_max>;
static_assert(std::atomic<const log::Ring*>::is_always_lock_free, "the kept rings are read from a signal handler");

// Puts ring in a free slot: false when it is kept already or no slot is free.
inline bool hold(Rings& rings, const log::Ring& ring) noexcept {
    for (const std::atomic<const log::Ring*>& slot : rings) {
        if (slot.load(std::memory_order_acquire) == &ring) {
            return false;
        }
    }
    for (std::atomic<const log::Ring*>& slot : rings) {
        const log::Ring* none = nullptr;
        if (slot.compare_exchange_strong(none, &ring, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

// Takes ring out of its slot: false when it is not kept.
inline bool release(Rings& rings, const log::Ring& ring) noexcept {
    for (std::atomic<const log::Ring*>& slot : rings) {
        const log::Ring* kept = &ring;
        if (slot.compare_exchange_strong(kept, nullptr, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

// The front of line holding at most `points` code points, cut at a code
// point's start.
constexpr std::string_view cut(std::string_view line, std::size_t points) noexcept {
    std::size_t seen = 0;
    for (auto at = line.begin(); at != line.end(); ++at) {
        if ((static_cast<unsigned char>(*at) & 0xC0U) != 0x80U) {
            if (seen == points) {
                return {line.begin(), at};
            }
            ++seen;
        }
    }
    return line;
}

// The report's events: head(n), then each(line) for each of the n lines,
// without its '\n', cut at event_points. One view of each ring, taken once,
// serves both.
template <class Head, class Each>
void events(const Rings& rings, const Head& head, const Each& each) noexcept {
    // Each set below, so that no code clears it.
    std::array<log::RingView, rings_max> views;
    std::uint64_t total = 0;
    auto view = views.begin();
    for (const std::atomic<const log::Ring*>& slot : rings) {
        const log::Ring* ring = slot.load(std::memory_order_acquire);
        *view = ring != nullptr ? ring->lines() : log::RingView{};
        if (ring != nullptr) {
            for (const std::string_view piece : {view->older, view->newer}) {
                for (const char c : piece) {
                    total += c == '\n' ? 1U : 0U;
                }
            }
        }
        ++view;
    }
    std::uint64_t skip = total > events_max ? total - events_max : 0U;
    head(total - skip);
    for (const log::RingView& held : views) {
        for (const std::string_view piece : {held.older, held.newer}) {
            auto from = piece.begin();
            for (auto at = piece.begin(); at != piece.end(); ++at) {
                if (*at != '\n') {
                    continue;
                }
                if (skip > 0) {
                    --skip;
                } else {
                    each(cut(std::string_view(from, at), event_points));
                }
                from = std::next(at);
            }
        }
    }
}

}  // namespace qiven3::diag::detail
