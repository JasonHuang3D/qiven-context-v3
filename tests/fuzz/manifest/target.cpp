// fuzz/manifest, import/manifest's fuzz target. The input's first byte
// chooses, modulo 2: read_manifest over the rest, or verbatim over the rest
// split in two at the place its second byte gives. A finding is any
// exception but the reader's own refusal, a crash or a hang; thrown here,
// a refusal of the parser's code with a path, a refusal by not_verbatim
// out of read_manifest, the arena's budget run out in room for 512 bytes
// an input byte, a manifest read whose lookups do not find each document
// and entry by its own name and id, a source naming no document or entry,
// a ruling past the documents, a second read of the same bytes another
// manifest, check_verbatim not as verbatim says, or verbatim other than
// the oracle's. The oracle is this file's own, written apart from the
// reader: the document copied with each LF and the spaces and tabs after
// it one space, then the words searched for in the copy.
// manifest_input is the regression case's entry;
// LLVMFuzzerTestOneInput libFuzzer's, in the fuzz binary alone.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "import/manifest.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"

namespace {

namespace mf = qiven3::import::manifest;

void finding(std::string_view what) { throw std::logic_error("fuzz/manifest: " + std::string(what)); }

bool oracle(std::string_view words, std::string_view document) {
    std::string read;
    for (std::size_t at = 0; at < document.size(); ++at) {
        if (document[at] != '\n') {
            read += document[at];
            continue;
        }
        read += ' ';
        while (at + 1 < document.size() && (document[at + 1] == ' ' || document[at + 1] == '\t')) {
            ++at;
        }
    }
    return !words.empty() && read.find(words) != std::string::npos;
}

void sources(const mf::Manifest& m, mf::Span span) {
    for (const mf::Source& s : m.sources(span)) {
        if ((s.entry == 0) == (s.doc == 0) || (s.entry != 0 && !m.entry(m.text(s.entry))) ||
            (s.doc != 0 && (!m.document(m.text(s.doc)) || s.section == 0))) {
            finding("a source naming no document or entry");
        }
    }
}

// The manifest's own lookups and references, as read.
void lookups(const mf::Manifest& m) {
    const std::span<const mf::Document> documents = m.documents();
    if (documents.empty() || documents.size() > mf::max_documents) {
        finding("documents past their bound");
    }
    for (std::size_t d = 0; d < documents.size(); ++d) {
        if (m.document(m.text(documents[d].name)) != d || m.text(documents[d].repo) != m.repository()) {
            finding("a document not found by its name, or of another repository");
        }
    }
    for (const std::uint32_t r : m.rulings()) {
        if (r >= documents.size()) {
            finding("a ruling past the documents");
        }
    }
    const std::span<const mf::Entry> entries = m.entries();
    if (entries.empty() || entries.size() > mf::max_entries) {
        finding("entries past their bound");
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const mf::Entry& e = entries[i];
        if (m.entry(m.text(e.id)) != i || m.text(e.text).empty() || !m.document(m.text(e.doc))) {
            finding("an entry not found by its id, without its text, or of no document");
        }
        sources(m, e.sources);
    }
}

// The first direction's words checked against the manifest's own bytes:
// check_verbatim as verbatim says, naming the entry.
void check_first(const mf::Manifest& m, std::string_view bytes) {
    const std::span<const mf::Entry> entries = m.entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].kind != mf::Kind::direction) {
            continue;
        }
        const bool want = mf::verbatim(m.text(entries[i].text), bytes);
        try {
            mf::check_verbatim(m, i, bytes);
            if (!want) {
                finding("check_verbatim passing words verbatim refuses");
            }
        } catch (const mf::ManifestError& e) {
            if (want || e.code != mf::Code::not_verbatim || e.entry.view() != m.text(entries[i].id)) {
                finding("check_verbatim's refusal not verbatim's, or naming another entry");
            }
        }
        return;
    }
}

void read(std::string_view bytes) {
    std::vector<std::byte> pages((512 * bytes.size()) + 65536);
    std::optional<std::vector<std::string>> first;
    for (int pass = 0; pass < 2; ++pass) {
        qiven3::memory::Arena arena(pages);
        try {
            const mf::Manifest m = mf::read_manifest(bytes, arena);
            lookups(m);
            std::vector<std::string> ids;
            for (const mf::Entry& e : m.entries()) {
                ids.emplace_back(m.text(e.id));
            }
            if (first && *first != ids) {
                finding("a second read another manifest");
            }
            if (pass == 0) {
                check_first(m, bytes);
            }
            first = std::move(ids);
        } catch (const mf::ManifestError& e) {
            if (first || e.code == mf::Code::not_verbatim || mf::name(e.code).empty() ||
                (e.code == mf::Code::json && e.path.size() != 0)) {
                finding("a refusal the reader does not give, or a read refused");
            }
            return;
        } catch (const qiven3::memory::MemoryError&) {
            finding("the arena's budget run out");
        }
    }
}

void words(std::string_view bytes) {
    if (bytes.empty()) {
        return;
    }
    const std::string_view rest = bytes.substr(1);
    const std::size_t split = static_cast<unsigned char>(bytes[0]) % (rest.size() + 1);
    const std::string_view w = rest.substr(0, split);
    const std::string_view document = rest.substr(split);
    if (mf::verbatim(w, document) != oracle(w, document)) {
        finding("verbatim not the oracle's");
    }
}

}  // namespace

namespace qiven3::fuzz {

int manifest_input(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    const std::string text(data + 1, data + size);
    if (data[0] % 2U == 0) {
        read(text);
    } else {
        words(text);
    }
    return 0;
}

}  // namespace qiven3::fuzz

#if defined(QIVEN3_FUZZ_BINARY)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    return qiven3::fuzz::manifest_input(data, size);
}
#endif
