// fuzz/gitoutput, import/git's output parsers' fuzz target (named so: the
// bare name is a string B2 allows only in import/git, and a target's name
// is lowercase letters alone). The input's first byte chooses the parser,
// modulo 5, and the rest is the bytes git wrote: parse_head, parse_commit,
// parse_first, parse_added, or, for search_text, a document with a quote's
// place read from its first four bytes. A finding is any exception but
// the parser's own refusal by output, a crash or a hang; thrown here,
// bytes read where the oracle refuses them or refused where it reads them,
// a refusal with another record than the oracle's or no reason, a value
// other than the oracle's, or a place outside the bytes. The oracle is
// this file's own, written apart from the parsers: an id 40 lowercase hex,
// a time 1 to 19 digits written as std::to_string writes it and not past
// the largest std::int64_t, the log split at each NUL and its fields read
// in turn, UTF-8 checked by RFC 3629's table. gitoutput_input is the
// regression case's entry;
// LLVMFuzzerTestOneInput libFuzzer's, in the fuzz binary alone.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "import/git.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"

namespace {

namespace git = qiven3::import::git;

void finding(std::string_view what) { throw std::logic_error("fuzz/gitoutput: " + std::string(what)); }

bool id(std::string_view text) {
    if (text.size() != 40) {
        return false;
    }
    for (const char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

std::optional<std::int64_t> seconds(std::string_view text) {
    if (text.empty() || text.size() > 19) {
        return std::nullopt;
    }
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
    }
    const std::string max = std::to_string(std::numeric_limits<std::int64_t>::max());
    if (text.size() == max.size() && std::string(text) > max) {
        return std::nullopt;
    }
    const std::int64_t value = std::stoll(std::string(text));
    if (std::to_string(value) != text) {
        return std::nullopt;  // a leading zero
    }
    return value;
}

// "<id> <author> <committer>": exactly two spaces, three fields.
std::optional<git::Commit> commit_line(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t from = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ' ') {
            fields.push_back(line.substr(from, i - from));
            from = i + 1;
        }
    }
    if (fields.size() != 3 || !id(fields[0])) {
        return std::nullopt;
    }
    const std::optional<std::int64_t> author = seconds(fields[1]);
    const std::optional<std::int64_t> committer = seconds(fields[2]);
    if (!author || !committer) {
        return std::nullopt;
    }
    git::Commit c;
    for (std::size_t i = 0; i < 40; ++i) {
        c.id.hex[i] = fields[0][i];
    }
    c.author = *author;
    c.committer = *committer;
    return c;
}

bool well_formed(std::string_view text) {
    std::size_t at = 0;
    while (at < text.size()) {
        const unsigned b = static_cast<unsigned char>(text[at]);
        std::size_t need = 0;
        unsigned lo = 0x80U;
        unsigned hi = 0xBFU;
        if (b <= 0x7FU) {
            need = 0;
        } else if (b >= 0xC2U && b <= 0xDFU) {
            need = 1;
        } else if (b == 0xE0U) {
            need = 2;
            lo = 0xA0U;
        } else if (b == 0xEDU) {
            need = 2;
            hi = 0x9FU;
        } else if (b >= 0xE1U && b <= 0xEFU) {
            need = 2;
        } else if (b == 0xF0U) {
            need = 3;
            lo = 0x90U;
        } else if (b >= 0xF1U && b <= 0xF3U) {
            need = 3;
        } else if (b == 0xF4U) {
            need = 3;
            hi = 0x8FU;
        } else {
            return false;
        }
        if (text.size() - at - 1 < need) {
            return false;
        }
        for (std::size_t k = 1; k <= need; ++k) {
            const unsigned c = static_cast<unsigned char>(text[at + k]);
            if (c < lo || c > hi) {
                return false;
            }
            lo = 0x80U;
            hi = 0xBFU;
        }
        at += need + 1;
    }
    return true;
}

// An oracle's reading: refused at a record, or read.
struct Expected {
    bool read = true;
    std::size_t record = 0;
    std::vector<std::pair<std::string, git::Commit>> added;
};

// The log split at each NUL: an empty field first; then each commit's
// line; after it an empty field (the next commit's NUL), the last field
// (empty: the log's end), or LF and its first name, then names until an
// empty field. A name nonempty and UTF-8.
Expected added_oracle(std::string_view log) {
    Expected want;
    std::vector<std::string_view> fields;
    std::size_t from = 0;
    for (std::size_t i = 0; i <= log.size(); ++i) {
        if (i == log.size() || log[i] == '\0') {
            fields.push_back(log.substr(from, i - from));
            from = i + 1;
        }
    }
    if (log.empty()) {
        return want;
    }
    const auto refuse = [&](std::size_t record) {
        want.read = false;
        want.record = record;
        want.added.clear();
        return want;
    };
    if (!fields[0].empty()) {
        return refuse(1);
    }
    std::size_t record = 0;
    std::size_t f = 1;
    while (f < fields.size()) {
        ++record;
        if (f + 1 == fields.size()) {
            return refuse(record);  // a commit line not closed by a NUL
        }
        const std::optional<git::Commit> c = commit_line(fields[f]);
        if (!c) {
            return refuse(record);
        }
        ++f;
        if (f + 1 == fields.size() && fields[f].empty()) {
            break;  // the log's end
        }
        if (fields[f].empty()) {
            ++f;  // the next commit's NUL
            continue;
        }
        if (fields[f].front() != '\n') {
            return refuse(record);
        }
        std::string_view name = fields[f].substr(1);
        while (true) {
            if (f + 1 == fields.size() || name.empty() || !well_formed(name)) {
                return refuse(record);  // a name not closed, empty or not UTF-8
            }
            want.added.emplace_back(std::string(name), *c);
            ++f;
            if (fields[f].empty()) {
                ++f;  // the log's end or the next commit's NUL
                break;
            }
            name = fields[f];
        }
    }
    return want;
}

bool same(const git::Commit& a, const git::Commit& b) {
    return a.id == b.id && a.author == b.author && a.committer == b.committer;
}

template <class Op>
std::optional<std::size_t> refused(Op&& op) {
    try {
        op();
    } catch (const git::GitError& e) {
        if (e.code != git::Code::output || e.detail.view().empty()) {
            finding("a refusal not output, or naming no reason");
        }
        return e.record;
    }
    return std::nullopt;
}

void head(std::string_view bytes) {
    const bool want = bytes.size() == 41 && bytes.back() == '\n' && id(bytes.substr(0, 40));
    git::Revision got;
    const std::optional<std::size_t> record = refused([&] { got = git::parse_head(bytes); });
    if (want != !record || (record && *record != 1) || (want && got.view() != bytes.substr(0, 40))) {
        finding("parse_head not the oracle's");
    }
}

void commit(std::string_view bytes) {
    const bool lone = !bytes.empty() && bytes.back() == '\n' && bytes.find('\n') == bytes.size() - 1;
    const std::optional<git::Commit> want = lone ? commit_line(bytes.substr(0, bytes.size() - 1)) : std::nullopt;
    std::optional<git::Commit> got;
    const std::optional<std::size_t> record = refused([&] { got = git::parse_commit(bytes); });
    if (bytes.empty()) {
        if (record || got) {
            finding("parse_commit of no bytes not none");
        }
        return;
    }
    if (want.has_value() != !record || (record && *record != 1) || (want && (!got || !same(*got, *want)))) {
        finding("parse_commit not the oracle's");
    }
}

void first(std::string_view bytes) {
    std::size_t bad = 0;
    for (std::size_t i = 0; bad == 0 && i * 41 < bytes.size(); ++i) {
        const std::string_view block = bytes.substr(i * 41, 41);
        if (block.size() != 41 || block.back() != '\n' || !id(block.substr(0, 40))) {
            bad = i + 1;
        }
    }
    std::optional<git::Revision> got;
    const std::optional<std::size_t> record = refused([&] { got = git::parse_first(bytes); });
    if ((bad != 0) != record.has_value() || (record && *record != bad) ||
        (!record && got.has_value() == bytes.empty()) || (got && got->view() != bytes.substr(0, 40))) {
        finding("parse_first not the oracle's");
    }
}

void added(std::string_view bytes) {
    const Expected want = added_oracle(bytes);
    // Room for one Added a NUL and the vector's alignment.
    std::vector<std::byte> pages((sizeof(git::Added) * (bytes.size() + 1)) + 4096);
    qiven3::memory::Arena arena(pages);
    qiven3::memory::Vector<git::Added> out{qiven3::memory::Allocator<git::Added>(arena)};
    const std::optional<std::size_t> record = refused([&] { git::parse_added(bytes, out); });
    if (want.read == record.has_value() || (record && *record != want.record)) {
        finding("parse_added's refusal not the oracle's");
    }
    if (record) {
        return;
    }
    if (out.size() != want.added.size()) {
        finding("parse_added's count not the oracle's");
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        const git::Place p = out[i].path;
        if (p.at > bytes.size() || p.size > bytes.size() - p.at) {
            finding("parse_added's place past the bytes");
        }
        if (bytes.substr(p.at, p.size) != want.added[i].first || !same(out[i].commit, want.added[i].second)) {
            finding("parse_added's entry not the oracle's");
        }
    }
}

// The quote's place from four bytes, each half modulo the document's size
// and one; then search_text's view: the quote whole, or the quote up to its
// line break, or from its line's start to that break.
void search(std::string_view bytes) {
    if (bytes.size() < 4) {
        return;
    }
    const std::string_view document = bytes.substr(4);
    const auto half = [&](std::size_t k) {
        return (static_cast<std::size_t>(static_cast<unsigned char>(bytes[k])) << 8U) |
               static_cast<unsigned char>(bytes[k + 1]);
    };
    const std::size_t at = half(0) % (document.size() + 1);
    const std::size_t size = half(2) % (document.size() - at + 1);
    const std::string_view got = git::search_text(document, at, size);
    const std::string_view quote = document.substr(at, size);
    const std::size_t lf = quote.find('\n');
    if (got.data() < document.data() || got.data() + got.size() > document.data() + document.size()) {
        finding("search_text's view outside the document");
    }
    if (lf == std::string_view::npos) {
        if (got.data() != quote.data() || got.size() != size) {
            finding("search_text's view not the quote whole");
        }
        return;
    }
    const std::size_t start = static_cast<std::size_t>(got.data() - document.data());
    const bool line_start = start == 0 || document[start - 1] == '\n';
    if (start + got.size() != at + lf || (start != at && !line_start) || start > at ||
        got.find('\n') != std::string_view::npos) {
        finding("search_text's view not up to the quote's line break");
    }
}

}  // namespace

namespace qiven3::fuzz {

int gitoutput_input(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    const std::string text(data + 1, data + size);
    switch (data[0] % 5U) {
    case 0:
        head(text);
        break;
    case 1:
        commit(text);
        break;
    case 2:
        first(text);
        break;
    case 3:
        added(text);
        break;
    default:
        search(text);
        break;
    }
    return 0;
}

}  // namespace qiven3::fuzz

#if defined(QIVEN3_FUZZ_BINARY)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    return qiven3::fuzz::gitoutput_input(data, size);
}
#endif
