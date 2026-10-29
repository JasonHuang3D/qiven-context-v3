#include "import/manifest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "import/manifest_reader.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::import::manifest {

std::string_view name(Code code) noexcept {
    static constexpr std::array<std::string_view, 14> names = {
        "too_large",     "json",          "missing",          "type",      "bound",
        "form",          "value",         "unknown_field",    "unknown_document",
        "unknown_entry", "duplicate",     "unused_document",  "two_repositories",
        "not_verbatim",
    };
    return names.at(static_cast<std::size_t>(code));
}

Manifest::Manifest(memory::Arena& arena) noexcept
    : value_(memory::Allocator<char>(arena)), documents_(memory::Allocator<Document>(arena)),
      rulings_(memory::Allocator<std::uint32_t>(arena)), entries_(memory::Allocator<Entry>(arena)),
      by_id_(memory::Allocator<std::uint32_t>(arena)), texts_(memory::Allocator<Text>(arena)),
      sources_(memory::Allocator<Source>(arena)), alsos_(memory::Allocator<Also>(arena)),
      pairs_(memory::Allocator<Pair>(arena)), groups_(memory::Allocator<Span>(arena)),
      replacements_(memory::Allocator<Replacement>(arena)), amendments_(memory::Allocator<Amendment>(arena)) {}

std::string_view Manifest::text(Text t) const noexcept { return t == 0 ? std::string_view{} : value_.text(t); }

std::optional<std::size_t> Manifest::document(std::string_view name) const noexcept {
    for (std::size_t d = 0; d < documents_.size(); ++d) {
        if (text(documents_[d].name) == name) {
            return d;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> Manifest::entry(std::string_view id) const noexcept {
    const auto it = std::lower_bound(by_id_.begin(), by_id_.end(), id,
                                     [&](std::uint32_t a, std::string_view b) { return text(entries_[a].id) < b; });
    if (it == by_id_.end() || text(entries_[*it].id) != id) {
        return std::nullopt;
    }
    return *it;
}

Manifest read_manifest(std::string_view bytes, memory::Arena& arena) {
    if (bytes.size() > max_bytes) {
        throw ManifestError{Code::too_large, ParseCode::syntax, {}, {}};
    }
    using detail::is_repo;
    using detail::JKind;
    using detail::Reader;
    using detail::Step;
    using detail::Table;
    Manifest m(arena);
    if (const std::optional<ParseCode> fault =
            canon::try_parse(bytes, m.value_, detail::Keys{}, canon::Layout::any)) {
        throw ManifestError{Code::json, *fault, {}, {}};
    }
    const json::Value& v = m.value_;
    Reader r{&m, &v, {}, {}, {}};
    if (v.kind(0) != JKind::object) {
        r.refuse(Code::type);
    }
    // The flattened lists' bounds, from the value's nodes: a text is a string,
    // a source or an also_in item an object, a person's reasons_source group
    // an array.
    std::size_t strings = 0;
    std::size_t objects = 0;
    for (json::Index k = 0; k < v.size(); ++k) {
        strings += v.kind(k) == JKind::string ? 1U : 0U;
        objects += v.kind(k) == JKind::object ? 1U : 0U;
    }
    m.texts_.reserve(strings);
    m.sources_.reserve(objects);
    m.alsos_.reserve(objects);
    const auto f = r.members(0, Table<10>{"manifest", "written_by", "records", "documents", "rulings", "entries",
                                          "goal", "person", "replacements", "amendments"});
    m.form_ = r.text(f[0], 64, true);
    m.written_by_ = r.text(f[1], 64, false);
    m.records_ = r.text(f[2], 100, true, is_repo);
    r.documents(f[3]);
    r.rulings(f[4]);
    const json::Index entries = r.list(f[5], 1, max_entries, true);
    m.entries_.reserve(v.count(entries));
    {
        const Step s(r.path, r.path.member(f[5].key));
        std::size_t i = 0;
        for (json::Index k = entries + 1; k < v.next(entries); k = v.next(k), ++i) {
            const Step item(r.path, r.path.index(i));
            r.read_entry(k);
            r.entry = memory::Inline<64>();
        }
    }
    r.goal(f[6]);
    r.person(f[7]);
    r.replacements(f[8]);
    r.amendments(f[9]);
    r.references();
    return m;
}

namespace {

// The place past one byte of the document as read: an LF and the spaces
// and tabs after it are one byte, a space.
std::size_t step(std::string_view d, std::size_t at) noexcept {
    if (d[at] != '\n') {
        return at + 1;
    }
    ++at;
    while (at < d.size() && (d[at] == ' ' || d[at] == '\t')) {
        ++at;
    }
    return at;
}

}  // namespace

bool verbatim(std::string_view words, std::string_view document) noexcept {
    if (words.empty()) {
        return false;
    }
    for (std::size_t start = 0; start < document.size(); start = step(document, start)) {
        std::size_t at = start;
        std::size_t k = 0;
        while (k < words.size() && at < document.size() &&
               (document[at] == '\n' ? ' ' : document[at]) == words[k]) {
            at = step(document, at);
            ++k;
        }
        if (k == words.size()) {
            return true;
        }
    }
    return false;
}

void check_verbatim(const Manifest& manifest, std::size_t entry, std::string_view document) {
    const std::span<const Entry> entries = manifest.entries();
    if (entry >= entries.size() || entries[entry].kind != Kind::direction) {
        memory::fail_fast("manifest: a verbatim check of no direction");
    }
    if (!verbatim(manifest.text(entries[entry].text), document)) {
        detail::Path path;
        path.member("entries");
        path.index(entry);
        path.member("words");
        throw ManifestError{Code::not_verbatim, ParseCode::syntax, memory::Inline<128>(path.view()),
                            memory::Inline<64>(manifest.text(entries[entry].id))};
    }
}

}  // namespace qiven3::import::manifest
