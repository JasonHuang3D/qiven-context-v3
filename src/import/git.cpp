#include "import/git.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::import::git {

namespace {

constexpr std::size_t npos = std::string_view::npos;
constexpr std::size_t id_size = 40;
// A quote's text up to its line break is searched by when it holds at least
// this many code points [proposal].
constexpr std::size_t least_points = 12;

constexpr bool is_hex(char c) noexcept { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

constexpr bool all_hex(std::string_view text) noexcept {
    return std::all_of(text.begin(), text.end(), is_hex);
}

[[noreturn]] void malformed(std::string_view call, std::string_view reason, std::size_t record) {
    GitError error{Code::output, memory::Inline<16>(call), {}, {}, memory::Inline<128>(reason), record};
    throw error;
}

Revision revision_of(std::string_view hex) noexcept {
    Revision revision;
    std::copy(hex.begin(), hex.end(), revision.hex.begin());
    return revision;
}

// An id, 40 lowercase hex, or output.
Revision id_of(std::string_view text, std::string_view call, std::size_t record) {
    if (text.size() != id_size || !all_hex(text)) {
        malformed(call, "an id not 40 lowercase hex", record);
    }
    return revision_of(text);
}

// Decimal seconds as git writes them: 1 to 19 digits, no leading zero but
// 0's own, at most the largest std::int64_t.
std::optional<std::int64_t> time_of(std::string_view digits) noexcept {
    if (digits.empty() || digits.size() > 19 || (digits.size() > 1 && digits.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (static_cast<std::uint64_t>(c) - std::uint64_t{'0'});
    }
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value);
}

// "<id> <author> <committer>", or output.
Commit commit_of(std::string_view text, std::string_view call, std::size_t record) {
    if (text.size() <= id_size || text[id_size] != ' ') {
        malformed(call, "a commit line not an id and a space", record);
    }
    Commit commit;
    commit.id = id_of(text.substr(0, id_size), call, record);
    const std::string_view times = text.substr(id_size + 1);
    const std::size_t space = times.find(' ');
    const std::optional<std::int64_t> author = time_of(times.substr(0, space));
    const std::optional<std::int64_t> committer =
        space == npos ? std::nullopt : time_of(times.substr(space + 1));
    if (!author || !committer) {
        malformed(call, "a commit's times not two decimal seconds", record);
    }
    commit.author = *author;
    commit.committer = *committer;
    return commit;
}

}  // namespace

std::string_view name(Code code) noexcept {
    switch (code) {
    case Code::argument:
        return "argument";
    case Code::failed:
        return "failed";
    case Code::not_clean:
        return "not_clean";
    case Code::not_held:
        return "not_held";
    case Code::output:
        return "output";
    case Code::not_found:
        return "not_found";
    case Code::unchecked:
        return "unchecked";
    }
    return "unknown";
}

Revision parse_head(std::string_view bytes) {
    if (bytes.size() != id_size + 1 || bytes.back() != '\n') {
        malformed("rev-parse", "not one line of an id", 1);
    }
    return id_of(bytes.substr(0, id_size), "rev-parse", 1);
}

std::optional<Commit> parse_commit(std::string_view bytes) {
    if (bytes.empty()) {
        return std::nullopt;
    }
    if (bytes.back() != '\n' || bytes.find('\n') != bytes.size() - 1) {
        malformed("log", "not one line", 1);
    }
    return commit_of(bytes.substr(0, bytes.size() - 1), "log", 1);
}

std::optional<Revision> parse_first(std::string_view bytes) {
    std::optional<Revision> first;
    std::size_t record = 0;
    for (std::size_t at = 0; at < bytes.size();) {
        ++record;
        const std::size_t end = bytes.find('\n', at);
        if (end == npos) {
            malformed("log", "a line not ended", record);
        }
        const Revision id = id_of(bytes.substr(at, end - at), "log", record);
        if (!first) {
            first = id;
        }
        at = end + 1;
    }
    return first;
}

// Each commit "\0<id> <author> <committer>\0", then, when it names files,
// "\n" and each name ended by "\0"; a name is never empty, so the next
// commit's "\0" closes the names.
void parse_added(std::string_view bytes, memory::Vector<Added>& out) {
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        malformed("log", "a log past 4 GiB", 0);
    }
    out.reserve(out.size() + static_cast<std::size_t>(std::count(bytes.begin(), bytes.end(), '\0')));
    std::size_t record = 0;
    for (std::size_t at = 0; at < bytes.size();) {
        ++record;
        if (bytes[at] != '\0') {
            malformed("log", "a commit not opened by a NUL", record);
        }
        const std::size_t end = bytes.find('\0', at + 1);
        if (end == npos) {
            malformed("log", "a commit line not closed by a NUL", record);
        }
        const Commit commit = commit_of(bytes.substr(at + 1, end - at - 1), "log", record);
        at = end + 1;
        if (at == bytes.size() || bytes[at] == '\0') {
            continue;  // a commit naming no file
        }
        if (bytes[at] != '\n') {
            malformed("log", "a commit line not followed by a line break", record);
        }
        ++at;
        do {
            const std::size_t stop = bytes.find('\0', at);
            if (stop == npos) {
                malformed("log", "a name not closed by a NUL", record);
            }
            const std::string_view name = bytes.substr(at, stop - at);
            if (name.empty()) {
                malformed("log", "an empty name", record);
            }
            if (utf::check(name)) {
                malformed("log", "a name not UTF-8", record);
            }
            out.push_back(Added{Place{static_cast<std::uint32_t>(at), static_cast<std::uint32_t>(name.size())},
                                commit});
            at = stop + 1;
        } while (at < bytes.size() && bytes[at] != '\0');
    }
}

std::string_view search_text(std::string_view document, std::size_t at, std::size_t size) noexcept {
    if (at > document.size() || size > document.size() - at) {
        memory::fail_fast("git: a quote past its document");
    }
    const std::string_view quote = document.substr(at, size);
    const std::size_t lf = quote.find('\n');
    if (lf == npos) {
        return quote;
    }
    if (utf::length(quote.substr(0, lf)) >= least_points) {
        return quote.substr(0, lf);
    }
    std::size_t start = at;
    while (start > 0 && document[start - 1] != '\n') {
        --start;
    }
    return document.substr(start, at + lf - start);
}

const Added& Adds::at(std::size_t i) const noexcept {
    if (i >= added_.size()) {
        memory::fail_fast("git: an added file past the log's");
    }
    return added_[i];
}

namespace detail {

void refuse(Code code, std::string_view call, std::string_view subject, std::string_view revision,
            std::string_view reason) {
    GitError error{code, memory::Inline<16>(call), memory::Inline<192>(subject), memory::Inline<40>(revision),
                   memory::Inline<128>(reason), 0};
    throw error;
}

void check_revision(std::string_view call, std::string_view revision) {
    if (revision.size() < 4 || revision.size() > id_size || !all_hex(revision)) {
        refuse(Code::argument, call, "", revision, "a revision not 4 to 40 lowercase hex");
    }
}

void check_text(std::string_view call, std::string_view what, std::string_view text) {
    if (text.empty() || text.find('\0') != npos) {
        refuse(Code::argument, call, what, "", "empty or holding a NUL");
    }
}

void check_ran(const platform::Ran& ran, std::string_view call, std::string_view subject,
               std::string_view revision) {
    if (ran.code == 0) {
        return;
    }
    std::string_view err(ran.err);
    err = err.substr(0, err.find('\n'));
    if (!err.empty() && err.back() == '\r') {
        err.remove_suffix(1);
    }
    platform::Detail reason("exit ");
    if (ran.code < 0) {
        reason.add("-").number(static_cast<std::uint64_t>(-static_cast<std::int64_t>(ran.code)));
    } else {
        reason.number(static_cast<std::uint64_t>(ran.code));
    }
    reason.add(": ").add(err);
    refuse(Code::failed, call, subject, revision, reason.view());
}

bool holds(std::string_view bytes, std::string_view text) noexcept { return bytes.find(text) != npos; }

}  // namespace detail

}  // namespace qiven3::import::git
