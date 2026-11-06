#pragma once

// A main's log in the test tree: a Log over pages its main gives once, its
// ring kept for the crash report while it lives, and drained into the
// output QIVEN_LOG names (qiven::diag::Out). The runner's main and the drives'
// request loop each hold one, the process's one Log.
//
// thread: one (the Log's)

#include <array>
#include <cstddef>
#include <span>

#include "qiven/log/log.hpp"
#include "qiven/diag/diag.hpp"
#include "qiven/diag/out.hpp"
#include "qiven/memory/arena.hpp"

namespace qiven::test {

class Journal {
public:
    // The bytes a Journal's pages hold: its Log's ring and events, and the
    // arena's padding for their alignment.
    static constexpr std::size_t bytes = qiven::log::Log::ring_bytes + qiven::log::Log::event_bytes + 256;
    // Pages of static storage or on the heap, so that no frame holds them.
    struct alignas(std::max_align_t) Pages {
        std::array<std::byte, bytes> data{};
    };

    // The Log, numbered 1, over pages, which outlive the Journal; it writes
    // log.start. Past the pages, MemoryError budget, nothing kept.
    Journal(Pages& pages, qiven::log::Name binary)
        : arena_(std::span<std::byte>(pages.data)),
          log_(arena_, qiven::log::Count(1U), binary),
          out_(qiven::diag::Out::from_environment()) {
        qiven::diag::keep(log_.ring());
    }
    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;
    Journal(Journal&&) = delete;
    Journal& operator=(Journal&&) = delete;
    // The last lines drained, and the ring no longer kept.
    ~Journal() {
        drain();
        qiven::diag::drop(log_.ring());
    }

    [[nodiscard]] qiven::log::Log& log() noexcept { return log_; }
    // The lines since the last drain, to the output; false when a write
    // failed.
    bool drain() noexcept { return log_.drain(out_); }

private:
    qiven::memory::Arena arena_;
    qiven::log::Log log_;
    qiven::diag::Out out_;
};

}  // namespace qiven::test
