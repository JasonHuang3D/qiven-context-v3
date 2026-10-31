#include "write/build.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <iterator>
#include <memory>
#include <span>
#include <string_view>

#include "entry/kinds.hpp"
#include "memory/fail.hpp"

namespace qiven3::write::build::detail {

namespace {

using entries::Code;
using entries::Why;

entries::EntriesError refusal(const File& file, std::size_t e, Code code, Why why) {
    entries::EntriesError r{.code = code, .why = why};
    r.entry = static_cast<std::uint32_t>(e);
    r.name = memory::Inline<64>(file.text(file.entries()[e].name));
    return r;
}

// The path from entries[e] to node, a member or an index at each step.
void walk(const File& file, std::size_t e, json::Index node, entries::detail::Path& path) {
    const json::Value& v = file.value();
    path.member("entries");
    path.index(e);
    path.member("content");
    json::Index at = file.entries()[e].content;
    while (at != node) {
        const json::Index end = v.next(at);
        json::Index into = at;
        if (v.kind(at) == json::Kind::object) {
            for (json::Index k = at + 1; k < end; k = v.next(k + 1)) {
                if (node >= k + 1 && node < v.next(k + 1)) {
                    path.member(v.text(k));
                    into = k + 1;
                    break;
                }
            }
        } else if (v.kind(at) == json::Kind::array) {
            std::size_t i = 0;
            for (json::Index k = at + 1; k < end; k = v.next(k), ++i) {
                if (node >= k && node < v.next(k)) {
                    path.index(i);
                    into = k;
                    break;
                }
            }
        }
        if (into == at) {
            memory::fail_fast("write/build: a site outside its entry's content");
        }
        at = into;
    }
}

void put_size(std::size_t n, memory::String& out) {
    std::array<char, 24> digits{};
    const std::to_chars_result r = std::to_chars(digits.data(), std::to_address(digits.end()), n);
    out.append(digits.data(), static_cast<std::size_t>(std::distance(digits.data(), r.ptr)));
    out.push_back(':');
}

}  // namespace

void identity_key(const entry::kinds::IdentityView& identity, memory::String& out) {
    out.clear();
    put_size(identity.kind, out);
    for (const std::string_view part : identity.parts) {
        put_size(part.size(), out);
        out.append(part);
    }
}

entry::kinds::IdentityView placeholder_identity(entries::Hole hole, std::string_view key) {
    std::string_view kind = "question";
    if (hole == entries::Hole::item) {
        kind = "item";
    } else if (hole == entries::Hole::lane) {
        kind = "checkpoint";
    }
    entry::kinds::IdentityView id;
    id.kind = static_cast<std::uint8_t>(entry::kinds::index_of(kind).value_or(0));
    id.parts[0] = key;
    return id;
}

void refuse_target(const File& file, std::size_t e) {
    entries::EntriesError r = refusal(file, e, Code::form, Why::placeholder);
    entries::detail::Path path;
    path.member("entries");
    path.index(e);
    path.member("target");
    r.path = memory::Inline<128>(path.view());
    throw r;
}

void refuse_site(const File& file, std::size_t e, const entries::Site& site) {
    const bool piece = site.hole == entries::Hole::piece;
    entries::EntriesError r = refusal(file, e, Code::form, piece ? Why::piece : Why::placeholder);
    entries::detail::Path path;
    if (site.node == file.entries()[e].target) {
        path.member("entries");
        path.index(e);
        path.member("target");
    } else {
        walk(file, e, site.node, path);
        if (piece) {
            path.member("piece");
        }
    }
    r.path = memory::Inline<128>(path.view());
    throw r;
}

void refuse_content(const File& file, std::size_t e, const entry::content::ContentError& fault) {
    entries::EntriesError r = refusal(file, e, Code::invalid, Why::content);
    r.fault = fault.code;
    r.limit = fault.limit;
    r.value = fault.value;
    entries::detail::Path path;
    path.member("entries");
    path.index(e);
    path.member(fault.code == entry::content::Code::basis ? "basis" : "content");  // basis: the entry's member
    path.member(fault.path.view());
    r.path = memory::Inline<128>(path.view());
    throw r;
}

const Built* latest(std::span<const Built> built, std::string_view scope, std::string_view identity) {
    for (std::size_t i = built.size(); i > 0; --i) {
        const Built& b = built[i - 1];
        if (b.scope.view() == scope && std::string_view(b.identity) == identity) {
            return &b;
        }
    }
    return nullptr;
}

const Built* head(std::span<const Built> built, std::string_view scope, std::string_view lane) {
    for (std::size_t i = built.size(); i > 0; --i) {
        const Built& b = built[i - 1];
        if (b.scope.view() == scope && b.lane.view() == lane) {
            return &b;
        }
    }
    return nullptr;
}

const Built* by_id(std::span<const Built> built, std::string_view scope, std::string_view id) {
    for (const Built& b : built) {
        if (b.scope.view() == scope && b.id.view() == id) {
            return &b;
        }
    }
    return nullptr;
}

}  // namespace qiven3::write::build::detail
