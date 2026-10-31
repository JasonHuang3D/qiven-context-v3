#pragma once

// The log cases' own support (tests/unit/log_*.cpp): a ring's or a drain's
// lines as strings, a drain's output, the widest event (eight 64-byte
// names), and the static half's report (unit/log_static.cpp).

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "log/log.hpp"
#include "memory/arena.hpp"

namespace qiven3::test {

// Lines, each without its '\n'.
class Lines {
public:
    explicit Lines(std::string_view bytes) { add(bytes); }
    explicit Lines(const log::RingView& view) {
        add(view.older);
        add(view.newer);
    }
    [[nodiscard]] std::size_t size() const noexcept { return lines_.size(); }
    [[nodiscard]] const std::string& at(std::size_t i) const { return lines_.at(i); }

private:
    void add(std::string_view bytes) {
        while (!bytes.empty()) {
            const std::size_t end = bytes.find('\n');
            lines_.emplace_back(bytes.substr(0, end));
            bytes.remove_prefix(end == std::string_view::npos ? bytes.size() : end + 1);
        }
    }
    std::vector<std::string> lines_;
};

// A drain's output: its bytes and its writes; fail makes each write fail,
// as does an append refused.
struct Sink {
    std::string bytes;
    int writes = 0;
    bool fail = false;
    bool write(std::string_view b) noexcept {
        ++writes;
        if (fail) {
            return false;
        }
        try {
            bytes.append(b);
        } catch (...) {
            return false;
        }
        return true;
    }
};

// The widest event, written to lg.
void widest(log::Log& lg) noexcept;
// Its descriptor's bound on its line.
std::size_t widest_max() noexcept;
// Its line encoded over arena, as a Log with the largest number writes it:
// the arena's peak. Past arena's budget, MemoryError budget.
std::size_t widest_peak(memory::Arena& arena);
// The static half held: reached only in a binary whose build held it.
bool constant_held() noexcept;

}  // namespace qiven3::test
