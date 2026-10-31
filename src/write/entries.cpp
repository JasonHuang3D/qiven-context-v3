#include "write/entries.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "write/entries_reader.hpp"

namespace qiven3::write::entries {

std::string_view name(Code code) noexcept { return code == Code::form ? "form" : "invalid"; }

std::string_view name(Why why) noexcept {
    static constexpr std::array<std::string_view, 14> names = {
        "too_large", "json",   "type",     "unknown_member", "not_allowed", "missing",      "value",
        "count",     "syntax", "repeated", "placeholder",    "piece",       "unused_piece", "content",
    };
    return names.at(static_cast<std::size_t>(why));
}

File::File(memory::Arena& arena) noexcept
    : value_(memory::Allocator<char>(arena)), entries_(memory::Allocator<Entry>(arena)),
      pieces_(memory::Allocator<Piece>(arena)), sites_(memory::Allocator<Site>(arena)),
      by_name_(memory::Allocator<std::uint32_t>(arena)) {}

std::string_view File::text(Text t) const noexcept { return t == 0 ? std::string_view{} : value_.text(t); }

std::span<const Site> File::sites(const Entry& e) const {
    if (e.sites.first > sites_.size() || e.sites.count > sites_.size() - e.sites.first) {
        memory::fail_fast("write/entries: a span past its sites");
    }
    return std::span<const Site>(sites_).subspan(e.sites.first, e.sites.count);
}

void build_content(const File& file, std::size_t e, std::span<const std::string_view> fills, json::Value& out) {
    const Entry& entry = file.entries()[e];
    const std::span<const Site> sites = file.sites(entry);
    if (fills.size() != sites.size()) {
        memory::fail_fast("write/entries: a fill count other than the sites'");
    }
    const json::Value& v = file.value();
    json::Writer<memory::Allocator<char>> w(out);
    struct Frame {
        json::Index end = 0;
        bool object = false;
        std::uint32_t piece = none;  // the site of a piece reference
    };
    std::array<Frame, json::max_depth> frames{};
    std::size_t open = 0;
    std::size_t next = 0;  // the next site
    const auto close = [&](json::Index at) {
        while (open > 0 && frames.at(open - 1).end <= at) {
            const Frame& f = frames.at(open - 1);
            if (f.piece != none) {
                w.string("digest");
                w.string(fills[f.piece]);
            }
            if (open == 1 && !file.import()) {
                const json::Index ep = file.episode();
                w.string("episode");
                w.open(json::Kind::object);
                for (json::Index k = ep + 1; k < v.next(ep); k = v.next(k + 1)) {
                    w.string(v.text(k));
                    w.string(v.text(k + 1));
                }
                w.close();
            }
            w.close();
            --open;
        }
    };
    const json::Index end = v.next(entry.content);
    json::Index k = entry.content;
    while (k < end) {
        close(k);
        const bool key = open > 0 && frames.at(open - 1).object;
        if (key) {
            if (frames.at(open - 1).piece != none && v.text(k) == "piece") {
                k = v.next(k + 1);
                continue;
            }
            w.string(v.text(k));
            ++k;
        }
        const bool site = next < sites.size() && sites[next].node == k;
        switch (v.kind(k)) {
            case json::Kind::null: w.null(); break;
            case json::Kind::boolean: w.boolean(v.boolean(k)); break;
            case json::Kind::integer: w.integer(v.integer(k)); break;
            case json::Kind::string: w.string(site ? fills[next] : v.text(k)); break;
            case json::Kind::array:
            case json::Kind::object:
                w.open(v.kind(k));
                frames.at(open++) = Frame{v.next(k), v.kind(k) == json::Kind::object,
                                          site ? static_cast<std::uint32_t>(next) : none};
                break;
        }
        next += site ? 1U : 0U;
        ++k;
    }
    close(end);
}

File read(std::string_view bytes, memory::Arena& arena, Hooks hooks) {
    if (bytes.size() > max_bytes) {
        throw EntriesError{.why = Why::too_large};
    }
    File f(arena);
    if (const std::optional<ParseCode> fault = canon::try_parse(bytes, f.value_, detail::Keys{}, canon::Layout::any)) {
        throw EntriesError{.why = Why::json, .parse = *fault};
    }
    detail::Reader r(f, arena);
    r.file();
    r.check_contents(arena, hooks);
    return f;
}

}  // namespace qiven3::write::entries
