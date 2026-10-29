// fuzz/entries, write/entries' fuzz target: read over the input, twice,
// each in an arena of its own. A finding is any exception but the reader's
// own refusal, a crash or a hang; thrown here, a refusal of a why its code
// does not give (invalid's alone is content), a json refusal with a path
// or an entry, an entry's index past the entries' bound, the arena's budget run
// out in room for 256 bytes an input byte and 1 MiB past the work span (a
// content built in 1 MiB of its own), a second read
// another outcome, and of a file read: entries or pieces past their
// bounds, a site past its list or naming no earlier entry or no piece, a
// piece no site names, a content whose stand-ins build past 65,536 bytes
// canonical, or the arena holding the work span once read.
// entries_input is the regression case's entry; LLVMFuzzerTestOneInput
// libFuzzer's, in the fuzz binary alone.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "entry/kinds.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "write/entries.hpp"
#include "write/entries_reader.hpp"

namespace {

namespace we = qiven3::write::entries;

void finding(std::string_view what) { throw std::logic_error("fuzz/entries: " + std::string(what)); }

// The sites of a file read, each in its list and naming what it may.
void sites(const we::File& f) {
    const std::size_t most = f.import() ? we::max_import_entries : we::max_slot_entries;
    if (f.entries().empty() || f.entries().size() > most || f.pieces().size() > we::max_pieces) {
        finding("entries or pieces past their bounds");
    }
    std::vector<bool> used(f.pieces().size(), false);
    for (std::size_t i = 0; i < f.entries().size(); ++i) {
        for (const we::Site& s : f.sites(f.entries()[i])) {
            if (s.hole == we::Hole::piece) {
                if (s.named >= used.size()) {
                    finding("a piece reference naming no piece");
                }
                used[s.named] = true;
            } else if (s.hole == we::Hole::name && s.named >= i) {
                finding("@name: naming no earlier entry");
            }
        }
    }
    if (std::find(used.begin(), used.end(), false) != used.end()) {
        finding("a piece no reference names");
    }
}

// Each content with stand-ins in its holes, within the content's bound,
// each built in an arena of its own.
void contents(const we::File& f) {
    std::vector<std::byte> pages(std::size_t{1} << 20U);
    for (std::size_t i = 0; i < f.entries().size(); ++i) {
        qiven3::memory::Arena arena(pages);
        std::vector<std::string_view> fills;
        for (const we::Site& s : f.sites(f.entries()[i])) {
            fills.push_back(s.hole == we::Hole::piece ? we::detail::stand_digest : we::detail::stand_id);
        }
        qiven3::json::Value out{qiven3::memory::Allocator<char>(arena)};
        we::build_content(f, i, fills, out);
        std::string bytes;
        qiven3::json::encode(out, bytes);
        if (bytes.size() > qiven3::entry::kinds::content_limit) {
            finding("a content read past its bound");
        }
    }
}

// The outcome of one read: the refusal's parts, or the entries' kinds.
std::string read(std::string_view bytes, std::vector<std::byte>& pages) {
    qiven3::memory::Arena arena(pages);
    try {
        const we::File f = we::read(bytes, arena);
        if (arena.used() >= we::detail::work_bytes) {
            finding("the work span held once read");
        }
        sites(f);
        contents(f);
        std::string out = "ok";
        for (const we::Entry& e : f.entries()) {
            out += " " + std::string(f.text(e.kind)) + ":" + std::to_string(e.sites.count);
        }
        return out;
    } catch (const we::EntriesError& e) {
        if ((e.code == we::Code::invalid) != (e.why == we::Why::content) || we::name(e.why).empty() ||
            (e.why == we::Why::json && (e.path.size() != 0 || e.entry != we::none)) ||
            (e.entry != we::none && e.entry >= we::max_import_entries)) {
            finding("a refusal the reader does not give");
        }
        return std::string(we::name(e.code)) + "/" + std::string(we::name(e.why)) + "@" + std::string(e.path.view()) +
               "#" + std::to_string(e.entry);
    } catch (const qiven3::memory::MemoryError&) {
        finding("the arena's budget run out");
    }
    return {};
}

}  // namespace

namespace qiven3::fuzz {

int entries_input(const std::uint8_t* data, std::size_t size) {
    const std::string text(data, data + size);
    std::vector<std::byte> pages(we::detail::work_bytes + (256 * size) + (std::size_t{1} << 20U));
    const std::string first = read(text, pages);
    if (read(text, pages) != first) {
        finding("a second read another outcome");
    }
    return 0;
}

}  // namespace qiven3::fuzz

#if defined(QIVEN3_FUZZ_BINARY)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    return qiven3::fuzz::entries_input(data, size);
}
#endif
