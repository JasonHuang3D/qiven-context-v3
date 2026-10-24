#include "log/ring.hpp"

#include <algorithm>

#include "memory/fail.hpp"

namespace qiven3::log {

Ring::Ring(std::span<char> storage) noexcept : storage_(storage) {
    if (storage.size() < 2 || storage.size() > max_bytes) {
        memory::fail_fast("log::Ring: a span under 2 bytes or past 1 MiB");
    }
}

void Ring::evict(State& s) const noexcept {
    const std::size_t end = s.wrap == 0 ? s.tail : s.wrap;
    const std::string_view held = piece(s.head, end);
    const std::size_t newline = held.find('\n');
    if (newline == std::string_view::npos) {
        memory::fail_fast("log::Ring: a held line without its end");
    }
    s.head += newline + 1;
    if (s.wrap != 0 && s.head == s.wrap) {
        // The older piece is gone: the newer is the one piece.
        s = State{0, s.tail, 0};
    }
}

void Ring::write_at(std::size_t at, std::string_view line) noexcept {
    const auto to = std::next(storage_.begin(), static_cast<std::ptrdiff_t>(at));
    std::copy(line.begin(), line.end(), to);
    *std::next(to, static_cast<std::ptrdiff_t>(line.size())) = '\n';
}

bool Ring::push(std::string_view line) noexcept {
    const std::size_t n = line.size() + 1;
    if (line.find('\n') != std::string_view::npos || line.size() >= storage_.size() - 1) {
        return false;
    }
    State s = unpack(state_.load(std::memory_order_relaxed));
    const State before = s;
    std::uint64_t evicted = 0;
    std::size_t at = 0;
    bool wraps = false;
    for (;;) {
        if (s.wrap == 0) {
            if (s.head == s.tail) {
                // Empty: from the front.
                s = State{};
            }
            if (s.tail + n <= storage_.size()) {
                at = s.tail;
                break;
            }
            if (n <= s.head) {
                wraps = true;
                break;
            }
        } else if (s.tail + n <= s.head) {
            at = s.tail;
            break;
        }
        evict(s);
        ++evicted;
    }
    // The evictions published before a byte they held is written over.
    if (evicted != 0 || s.head != before.head || s.tail != before.tail) {
        state_.store(pack(s), std::memory_order_release);
    }
    first_ += evicted;
    write_at(at, line);
    if (wraps) {
        s = State{s.head, n, s.tail};
    } else {
        s.tail = at + n;
    }
    ++next_;
    state_.store(pack(s), std::memory_order_release);
    return true;
}

RingView Ring::since(std::uint64_t seq) const noexcept {
    RingView v = view(state_.load(std::memory_order_relaxed));
    std::uint64_t skip = seq > first_ ? seq - first_ : 0;
    for (std::string_view* part : {&v.older, &v.newer}) {
        while (skip > 0 && !part->empty()) {
            part->remove_prefix(part->find('\n') + 1);
            --skip;
        }
    }
    return v;
}

}  // namespace qiven3::log
