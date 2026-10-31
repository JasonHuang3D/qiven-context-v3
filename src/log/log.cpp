#include "log/log.hpp"

#include <algorithm>

#include "log/catalogue.hpp"
#include "log/line.hpp"

namespace qiven3::log {

namespace {

std::span<char> take_ring(memory::Arena& arena) {
    return {static_cast<char*>(arena.allocate(Log::ring_bytes, 1)), Log::ring_bytes};
}

// The events' pages; a failure gives the ring's span back first.
std::span<std::byte> take_pages(memory::Arena& arena, std::span<char> ring) {
    try {
        return {static_cast<std::byte*>(arena.allocate(Log::event_bytes, alignof(std::max_align_t))),
                Log::event_bytes};
    } catch (...) {
        arena.deallocate(ring.data(), ring.size());
        throw;
    }
}

constexpr Name min_name = level_name(minimum);

}  // namespace

Log::Log(memory::Arena& arena, Count number, Name binary)
    : parent_(&arena),
      ring_storage_(take_ring(arena)),
      pages_(take_pages(arena, ring_storage_)),
      ring_(ring_storage_),
      number_(number.value()),
      start_(std::chrono::steady_clock::now()) {
    event(*this, events::log_start, binary, min_name, Count(ring_bytes));
}

Log::~Log() {
    parent_->deallocate(pages_.data(), pages_.size());
    parent_->deallocate(ring_storage_.data(), ring_storage_.size());
}

void Log::write(Level level, std::string_view event, std::uint64_t op, std::span<const FieldView> fields) noexcept {
    const HeadView head{level_name(level).text(), event, number_, op, Millis::since(start_).value()};
    try {
        memory::Arena arena(pages_);
#if defined(QIVEN_FAULTS)
        arena.attach(countdown_);
#endif
        bool pushed = false;
        {
            memory::String line{memory::Allocator<char>(arena)};
            encode_line(head, fields, line);
            pushed = line.size() <= max_line_bytes && ring_.push(line);
        }
        peak_ = std::max(peak_, arena.peak());
        if (!pushed) {
            ++failed_;
        }
    } catch (const memory::MemoryError&) {
        ++failed_;
    }
}

void Log::dropped(std::uint64_t lost) noexcept { event(*this, events::log_dropped, Count(lost)); }

Op& Op::operator=(Op&& other) noexcept {
    if (this != &other) {
        if (log_ != nullptr) {
            end("dropped", Count(0U));
        }
        log_ = std::exchange(other.log_, nullptr);
        id_ = other.id_;
        name_ = other.name_;
        start_ = other.start_;
    }
    return *this;
}

Op::~Op() {
    if (log_ != nullptr) {
        end("dropped", Count(0U));
    }
}

void Op::end(Name outcome, Count peak) noexcept {
    event(*this, events::op_end, name_, outcome, Millis::since(start_), peak);
    log_ = nullptr;
}

Op begin(Log& log, Name name) noexcept {
    Op op(log, name);
    event(op, events::op_begin, name);
    return op;
}

}  // namespace qiven3::log
