// probe: one facility over each input line, for the differential tests
// (tests/drive/test_differential.py), which hold the oracle.
//
// The protocol: every line of either stream is ASCII, each input line a
// string's bytes in hex, two digits a byte in either case, an empty line
// the empty string. So no line holds a raw LF, and the C runtime's text
// mode on Windows changes nothing either side reads: a CR before an LF is
// dropped on the way in, and the test splits the output on any line end.
// - probe match: the first line, the patterns, each in hex, one space
//   between; it answers "ok", or "fault <index> <code> <at>" and ends with
//   1, the pattern's index from 0 and the matcher's fault at its byte.
//   Each line after it is a string; the answer, one digit a pattern in
//   the first line's order, 1 where the pattern hits the string: anywhere,
//   or the whole value where the pattern is printed ^...$.
// - probe parse: each line a string; the answer, the strict parser's code,
//   accepted, or memory where its arena's budget ran out.
// - probe refs: each line a string; the answer, four digits, 1 for yes:
//   whether the string is one code point of refs' whitespace table, then
//   refs' check_path, check_name and check_subject, the last over an arena
//   of the matcher's compile budget.
// A malformed line ends the child with child::usage_status and a line on
// standard error. Each facility runs over v3's own arena, as the product
// instantiates it: the matcher's at its compile budget, the parser's at a
// budget of 64 bytes an input byte and 4 KiB.

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "child/child.hpp"
#include "entry/matcher.hpp"
#include "entry/refs.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::child {

namespace {

int nibble(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// hex's bytes into out; false where hex is not whole bytes of hex digits.
bool unhex(std::string_view hex, std::string& out) {
    out.clear();
    if (hex.size() % 2 != 0) {
        return false;
    }
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int high = nibble(hex[i]);
        const int low = nibble(hex[i + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

bool next_line(std::string& line) {
    if (!std::getline(std::cin, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return true;
}

int malformed(std::string_view what) {
    std::cerr << "qiven3-child probe: " << what << "\n";
    return usage_status;
}

std::string_view fault_name(entry::matcher::Fault code) noexcept {
    switch (code) {
        case entry::matcher::Fault::syntax: return "syntax";
        case entry::matcher::Fault::unsupported: return "unsupported";
        case entry::matcher::Fault::size: return "size";
    }
    return "unknown";
}

int match() {
    std::string line;
    std::string bytes;
    if (!next_line(line)) {
        return malformed("no line of patterns");
    }
    // The patterns' count first, then one arena for them all, which keeps
    // each automaton for the process's life: compile's scratch is not given
    // back to the span, so each pattern takes at most compile_budget of it.
    const std::size_t count = static_cast<std::size_t>(std::count(line.begin(), line.end(), ' ')) + 1;
    std::vector<std::byte> span(count * entry::matcher::compile_budget);
    memory::Arena arena{std::span<std::byte>(span)};
    std::vector<entry::matcher::Automaton> automata;
    automata.reserve(count);
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t end = std::min(line.find(' ', start), line.size());
        if (!unhex(std::string_view(line).substr(start, end - start), bytes)) {
            return malformed("a pattern that is not hex");
        }
        automata.emplace_back(memory::Allocator<char>(arena));
        if (const std::optional<entry::matcher::PatternError> fault =
                entry::matcher::try_compile(bytes, automata.back())) {
            std::cout << "fault " << automata.size() - 1 << " " << fault_name(fault->code) << " " << fault->at << "\n";
            return 1;
        }
        start = end + 1;
    }
    std::cout << "ok\n";
    std::string answer;
    while (next_line(line)) {
        if (!unhex(line, bytes)) {
            return malformed("a string that is not hex");
        }
        answer.clear();
        for (const entry::matcher::Automaton& a : automata) {
            answer.push_back(entry::matcher::hit(a, bytes) ? '1' : '0');
        }
        std::cout << answer << "\n";
    }
    std::cout.flush();
    return 0;
}

int parse() {
    std::string line;
    std::string bytes;
    std::vector<std::byte> span;
    while (next_line(line)) {
        if (!unhex(line, bytes)) {
            return malformed("a string that is not hex");
        }
        const std::size_t budget = 64 * bytes.size() + 4096;
        if (span.size() < budget) {
            span.resize(budget);
        }
        memory::Arena arena(std::span<std::byte>(span).first(budget));
        json::Value value{memory::Allocator<char>(arena)};
        try {
            const std::optional<ParseCode> code = canon::try_parse(bytes, value);
            std::cout << (code ? name(*code) : std::string_view("accepted")) << "\n";
        } catch (const memory::MemoryError&) {
            std::cout << "memory\n";
        }
    }
    std::cout.flush();
    return 0;
}

// Whether bytes are one code point of the whitespace table.
bool one_space(std::string_view bytes) noexcept {
    const utf::Read r = bytes.empty() ? utf::Read{} : utf::next(bytes, 0);
    return r.size != 0 && r.size == bytes.size() && entry::refs::is_space(r.point);
}

int refs() {
    std::string line;
    std::string bytes;
    std::vector<std::byte> span(entry::matcher::compile_budget);
    while (next_line(line)) {
        if (!unhex(line, bytes)) {
            return malformed("a string that is not hex");
        }
        memory::Arena arena{std::span<std::byte>(span)};
        const bool subject = entry::refs::check_subject(bytes, memory::Allocator<char>(arena));
        std::cout << (one_space(bytes) ? '1' : '0') << (entry::refs::check_path(bytes) ? '1' : '0')
                  << (entry::refs::check_name(bytes) ? '1' : '0') << (subject ? '1' : '0') << "\n";
    }
    std::cout.flush();
    return 0;
}

}  // namespace

int probe(std::span<const std::string_view> args) {
    if (args.size() == 1 && args.front() == "match") {
        return match();
    }
    if (args.size() == 1 && args.front() == "parse") {
        return parse();
    }
    if (args.size() == 1 && args.front() == "refs") {
        return refs();
    }
    return malformed("one argument, match, parse or refs");
}

}  // namespace qiven3::child
