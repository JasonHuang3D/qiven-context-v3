#include "write/entries_reader.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "entry/content.hpp"
#include "entry/content_fields.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/matcher.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "write/entries.hpp"

namespace qiven3::write::entries::detail {

namespace {

using JKind = json::Kind;
namespace kinds = entry::kinds;
namespace header = entry::header;

entry::matcher::Automaton compiled(std::string_view syntax, memory::Arena& arena) {
    return entry::matcher::compile(syntax, memory::Allocator<char>(arena));
}

// An absolute path: from the root, a drive's or a share's, with no
// control byte.
bool absolute(std::string_view p) noexcept {
    if (std::any_of(p.begin(), p.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20U || c == '\x7f'; })) {
        return false;
    }
    const bool drive = p.size() >= 3 && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
                       p[1] == ':' && (p[2] == '/' || p[2] == '\\');
    return p.starts_with("/") || drive || p.starts_with("\\\\");
}

// A node's bytes in its value's canonical form, as json's encode counts
// them: an object's or array's own, its members' or items' counted apart.
std::size_t node_size(const json::Value& v, json::Index k) {
    switch (v.kind(k)) {
        case JKind::null: return 4;
        case JKind::boolean: return v.boolean(k) ? 4 : 5;
        case JKind::integer: return json::detail::width(v.integer(k));
        case JKind::string: return json::detail::quoted(v.text(k));
        case JKind::array: return v.count(k) == 0 ? 2 : v.count(k) + 1;
        case JKind::object: return v.count(k) == 0 ? 2 : 2 * v.count(k) + 1;
    }
    return 0;
}

// The placeholders' prefixes, by their Hole.
constexpr std::array<std::string_view, 4> prefixes = {"@name:", "@item:", "@lane:", "@question:"};

}  // namespace

std::size_t Path::member(std::string_view key) noexcept {
    const std::size_t mark = size_;
    if (key.empty()) {
        return mark;
    }
    if (size_ > 0) {
        put('.');
    }
    for (const char c : key) {
        put(c);
    }
    return mark;
}

std::size_t Path::index(std::size_t i) noexcept {
    const std::size_t mark = size_;
    std::array<char, 24> digits{};
    const std::to_chars_result r = std::to_chars(digits.data(), std::to_address(digits.end()), i);
    put('[');
    const auto n = static_cast<std::size_t>(std::distance(digits.data(), r.ptr));
    for (const char c : std::string_view(digits.data(), n)) {
        put(c);
    }
    put(']');
    return mark;
}

Reader::Reader(File& file, memory::Arena& arena)
    : m(&file), v(&file.value_), slug(compiled(entry::content::slug_syntax, arena)),
      scope(compiled(header::scope_syntax, arena)), writer(compiled(header::writer_syntax, arena)),
      id(compiled(header::id_syntax, arena)), lane(compiled(header::lane_syntax, arena)) {}

void Reader::refuse(Why why) const {
    throw EntriesError{.why = why, .path = memory::Inline<128>(path.view()), .entry = entry, .name = entry_name};
}

void Reader::refuse_at(std::string_view key, Why why) {
    path.member(key);
    refuse(why);
}

void Reader::fault(entry::content::Code code, std::size_t limit, std::size_t value, std::string_view at) {
    path.member(at);
    const auto clip = [](std::size_t n) { return static_cast<std::uint32_t>(std::min<std::size_t>(n, UINT32_MAX)); };
    throw EntriesError{Code::invalid, Why::content, ParseCode::syntax, code, clip(limit), clip(value),
                       memory::Inline<128>(path.view()), entry, entry_name};
}

memory::Vector<std::uint32_t>::iterator Reader::named(std::string_view name) const {
    return std::lower_bound(m->by_name_.begin(), m->by_name_.end(), name,
                            [this](std::uint32_t a, std::string_view b) { return m->text(m->entries_[a].name) < b; });
}

json::Index Reader::node(const Field& f, JKind kind, bool required) {
    if (f.at == 0) {
        if (required) {
            refuse_at(f.key.view(), Why::missing);
        }
        return 0;
    }
    if (v->kind(f.at) != kind) {
        refuse_at(f.key.view(), Why::type);
    }
    return f.at;
}

Text Reader::text(const Field& f, bool required, std::size_t bytes, const entry::matcher::Automaton* syntax) {
    const json::Index n = node(f, JKind::string, required);
    if (n == 0) {
        return 0;
    }
    const std::string_view t = v->text(n);
    if (t.empty() || t.size() > bytes || (syntax != nullptr && !entry::matcher::hit(*syntax, t))) {
        refuse_at(f.key.view(), Why::syntax);
    }
    return n;
}

void Reader::refused(const Field& f) {
    if (f.at != 0) {
        refuse_at(f.key.view(), Why::not_allowed);
    }
}

void Reader::file() {
    if (v->kind(0) != JKind::object) {
        refuse(Why::type);
    }
    const auto f = members(0, Table<6>{"form", "writer", "scope", "episode", "pieces", "entries"});
    if (v->text(text(f[0], true, 64, nullptr)) != form_value) {
        refuse_at("form", Why::value);
    }
    m->writer_ = text(f[1], true, 64, &writer);
    const bool import = m->import();
    m->scope_ = text(f[2], !import, 64, &scope);
    if (import) {
        refused(f[3]);
    } else {
        episode(f[3]);
    }
    pieces(f[4]);
    const json::Index list = node(f[5], JKind::array, true);
    const std::size_t most = import ? max_import_entries : max_slot_entries;
    if (v->count(list) < 1 || v->count(list) > most) {
        refuse_at("entries", Why::count);
    }
    // A site is a string or an object of the value, so its count bounds
    // theirs.
    std::size_t places = 0;
    for (json::Index k = 0; k < v->size(); ++k) {
        places += v->kind(k) == JKind::string || v->kind(k) == JKind::object ? 1U : 0U;
    }
    m->sites_.reserve(places);
    m->entries_.reserve(v->count(list));
    m->by_name_.reserve(v->count(list));
    {
        const Step s(path, path.member("entries"));
        std::size_t i = 0;
        for (json::Index k = list + 1; k < v->next(list); k = v->next(k), ++i) {
            const Step item(path, path.index(i));
            entry = static_cast<std::uint32_t>(i);
            read_entry(k);
            entry = none;
            entry_name = memory::Inline<64>();
        }
    }
    pieces_used();
}

void Reader::episode(const Field& f) {
    const json::Index n = node(f, JKind::object, true);
    const Step s(path, path.member(f.key.view()));
    const auto g = members(n, Table<2>{"client", "session"});
    node(g[0], JKind::string, true);
    node(g[1], JKind::string, true);
    m->episode_ = n;
}

void Reader::pieces(const Field& f) {
    const json::Index n = node(f, JKind::object, false);
    if (n == 0) {
        return;
    }
    const Step s(path, path.member(f.key.view()));
    if (v->count(n) < 1 || v->count(n) > max_pieces) {
        refuse(Why::count);
    }
    m->pieces_.reserve(v->count(n));
    for (json::Index k = n + 1; k < v->next(n); k = v->next(k + 1)) {
        const std::string_view key = v->text(k);
        const Step one(path, path.member(key));
        if (key.size() > name_chars || !entry::matcher::hit(slug, key)) {
            refuse(Why::syntax);
        }
        if (v->kind(k + 1) != JKind::object) {
            refuse(Why::type);
        }
        const auto g = members(k + 1, Table<2>{"text", "path"});
        if (g[0].at != 0 && g[1].at != 0) {
            refuse_at("path", Why::not_allowed);
        }
        Piece p{k, 0, g[1].at != 0};
        if (p.path) {
            p.value = text(g[1], true, max_path_bytes, nullptr);
            if (!absolute(v->text(p.value))) {
                refuse_at("path", Why::syntax);
            }
        } else {
            p.value = node(g[0], JKind::string, true);
            if (v->text(p.value).size() > max_piece_bytes) {
                refuse_at("text", Why::too_large);
            }
        }
        m->pieces_.push_back(p);
    }
}

void Reader::read_entry(json::Index object) {
    if (v->kind(object) != JKind::object) {
        refuse(Why::type);
    }
    const auto g = members(object, Table<6>{"name", "scope", "kind", "basis", "target", "content"});
    Entry e;
    e.name = text(g[0], false, name_chars, &slug);
    entry_name = memory::Inline<64>(m->text(e.name));
    const auto at = named(m->text(e.name));
    if (e.name != 0 && at != m->by_name_.end() && m->text(m->entries_[*at].name) == m->text(e.name)) {
        refuse_at("name", Why::repeated);
    }
    if (m->import()) {
        e.scope = text(g[1], false, 64, &scope);
    } else {
        refused(g[1]);
    }
    e.scope = e.scope != 0 ? e.scope : m->scope_;
    if (e.scope == 0) {
        refuse_at("scope", Why::missing);
    }
    e.kind = text(g[2], true, 32, nullptr);
    const std::optional<std::size_t> k = kinds::index_of(m->text(e.kind));
    if (!k) {
        refuse_at("kind", Why::value);
    }
    e.basis = text(g[3], true, 32, nullptr);
    if (!header::among(header::basis_names, m->text(e.basis))) {
        refuse_at("basis", Why::value);
    }
    if (kinds::KINDS.at(*k).identity != kinds::Holds::none) {
        refused(g[4]);
    }
    const bool change = kinds::KINDS.at(*k).group == kinds::Group::change;
    e.target = node(g[4], JKind::string, change);
    if (e.target != 0) {
        const Step s(path, path.member("target"));
        if (v->text(e.target).starts_with("@")) {
            e.target_site = static_cast<std::uint32_t>(m->sites_.size());
            hole(e.target);
        } else if (!entry::matcher::hit(id, v->text(e.target))) {
            refuse(Why::syntax);
        }
    }
    const json::Index c = node(g[5], JKind::object, true);
    {
        const Step s(path, path.member("content"));
        content(e, c);
    }
    m->entries_.push_back(e);
    if (e.name != 0) {
        m->by_name_.insert(at, static_cast<std::uint32_t>(m->entries_.size() - 1));
    }
}

void Reader::content(Entry& e, json::Index c) {
    e.content = c;
    e.sites.first = static_cast<std::uint32_t>(m->sites_.size());
    for (json::Index k = c + 1; k < v->next(c); k = v->next(k + 1)) {
        const std::string_view key = v->text(k);
        const json::Index value = k + 1;
        if (key == "episode" || key == "clears") {
            refuse_at(key, Why::not_allowed);
        }
        const std::string_view kind = m->text(e.kind);
        if (((key == "object" && kind == "map") || (key == "message" && kind == "accept")) &&
            v->kind(value) == JKind::object) {
            const Step s(path, path.member(key));
            reference(value, false);
            continue;
        }
        if (v->kind(value) != JKind::array) {
            continue;
        }
        const Step s(path, path.member(key));
        if (key == "evidence" || key == "source") {
            references(value);
        } else if (key == "refs" || key == "closes") {
            std::size_t i = 0;
            for (json::Index n = value + 1; n < v->next(value); n = v->next(n), ++i) {
                if (v->kind(n) == JKind::string && v->text(n).starts_with("@")) {
                    const Step item(path, path.index(i));
                    hole(n);
                }
            }
        }
    }
    e.sites.count = static_cast<std::uint32_t>(m->sites_.size() - e.sites.first);
}

void Reader::references(json::Index list) {
    std::size_t i = 0;
    for (json::Index r = list + 1; r < v->next(list); r = v->next(r), ++i) {
        if (v->kind(r) != JKind::object) {
            continue;
        }
        const Step item(path, path.index(i));
        reference(r, true);
    }
}

void Reader::reference(json::Index r, bool holes) {
    const std::optional<json::Index> t = v->find(r, "type");
    const std::string_view type = t && v->kind(*t) == JKind::string ? v->text(*t) : std::string_view{};
    if (type == "entry" && holes) {
        const std::optional<json::Index> n = v->find(r, "id");
        if (n && v->kind(*n) == JKind::string && v->text(*n).starts_with("@")) {
            const Step s(path, path.member("id"));
            hole(*n);
        }
    } else if (type == "repo" || type == "transcript" || type == "output") {
        refused(Field{v->find(r, "digest").value_or(0), memory::Inline<16>("digest")});
        const json::Index p =
            node(Field{v->find(r, "piece").value_or(0), memory::Inline<16>("piece")}, JKind::string, true);
        const std::uint32_t named = piece(v->text(p));
        if (named == none) {
            refuse_at("piece", Why::piece);
        }
        m->sites_.push_back(Site{r, Hole::piece, named, 0});
    }
}

void Reader::hole(json::Index node) {
    const std::string_view t = v->text(node);
    for (std::size_t h = 0; h < prefixes.size(); ++h) {
        if (!t.starts_with(prefixes.at(h))) {
            continue;
        }
        const Hole hole = static_cast<Hole>(h);
        const std::string_view rest = t.substr(prefixes.at(h).size());
        const bool fits = hole == Hole::lane ? entry::matcher::hit(lane, rest)
                                             : rest.size() <= name_chars && entry::matcher::hit(slug, rest);
        if (!fits) {
            refuse(Why::placeholder);
        }
        std::uint32_t earlier = none;
        if (hole == Hole::name) {
            const auto at = named(rest);
            if (at == m->by_name_.end() || m->text(m->entries_[*at].name) != rest) {
                refuse(Why::placeholder);
            }
            earlier = *at;
        }
        m->sites_.push_back(Site{node, hole, earlier, static_cast<std::uint8_t>(prefixes.at(h).size())});
        return;
    }
    refuse(Why::placeholder);
}

std::uint32_t Reader::piece(std::string_view name) const {
    const auto p =
        std::find_if(m->pieces_.begin(), m->pieces_.end(), [&](const Piece& q) { return m->text(q.name) == name; });
    return p == m->pieces_.end() ? none : static_cast<std::uint32_t>(p - m->pieces_.begin());
}

void Reader::pieces_used() {
    for (std::size_t p = 0; p < m->pieces_.size(); ++p) {
        const bool used = std::any_of(m->sites_.begin(), m->sites_.end(),
                                      [p](const Site& s) { return s.hole == Hole::piece && s.named == p; });
        if (!used) {
            const Step s(path, path.member("pieces"));
            refuse_at(m->text(m->pieces_[p].name), Why::unused_piece);
        }
    }
}

std::size_t Reader::standing_size(const Entry& e) const {
    std::size_t bytes = 0;
    for (json::Index k = e.content; k < v->next(e.content); ++k) {
        bytes += node_size(*v, k);
    }
    for (const Site& s : m->sites(e)) {
        if (s.hole == Hole::piece) {
            const json::Index p = v->find(s.node, "piece").value_or(0);  // held: the reader found it
            bytes = bytes - json::detail::quoted("piece") - json::detail::quoted(v->text(p)) +
                    json::detail::quoted("digest") + json::detail::quoted(stand_digest);
        } else {
            bytes = bytes - json::detail::quoted(v->text(s.node)) + json::detail::quoted(stand_id);
        }
    }
    if (!m->import()) {
        const json::Index ep = m->episode();
        bytes += json::detail::quoted("episode") + (v->count(e.content) == 0 ? 1U : 2U);
        for (json::Index k = ep; k < v->next(ep); ++k) {
            bytes += node_size(*v, k);
        }
    }
    return bytes;
}

void Reader::check_contents(memory::Arena& arena, [[maybe_unused]] Hooks hooks) {
    const std::span<std::byte> pages(static_cast<std::byte*>(arena.allocate(work_bytes, alignof(std::max_align_t))),
                                     work_bytes);
    try {
        const std::string_view by = m->writer();
        for (std::size_t i = 0; i < m->entries_.size(); ++i) {
            const Entry& e = m->entries_[i];
            path.pop(0);
            path.member("entries");
            path.index(i);
            const std::size_t in_entry = path.member("content");
            entry = static_cast<std::uint32_t>(i);
            entry_name = memory::Inline<64>(m->text(e.name));
            if (const std::size_t size = standing_size(e); size > kinds::content_limit) {
                fault(entry::content::Code::too_large, kinds::content_limit, size, {});
            }
            memory::Arena work(pages);
#if defined(QIVEN3_FAULTS)
            work.attach(hooks.work);
#endif
            memory::Vector<std::string_view> fills{memory::Allocator<std::string_view>(work)};
            fills.reserve(e.sites.count);
            for (const Site& site : m->sites(e)) {
                fills.push_back(site.hole == Hole::piece ? stand_digest : stand_id);
            }
            json::Value stand{memory::Allocator<char>(work)};
            build_content(*m, i, fills, stand);
            const header::FieldsView f{.scope = m->text(e.scope), .lane = header::role(by), .parent = {},
                                       .kind = m->text(e.kind), .writer = by, .basis = m->text(e.basis),
                                       .at = stand_at, .target = {}, .content = {}, .id = stand_id};
            const auto h = header::make(f, memory::Allocator<char>(work));
            if (const std::optional<entry::content::ContentError> c = entry::content::try_check(h, stand)) {
                if (c->code == entry::content::Code::basis) {  // the entry's basis member, not its content
                    path.pop(in_entry);
                    path.member("basis");
                }
                fault(c->code, c->limit, c->value, c->path.view());
            }
        }
    } catch (...) {
        arena.deallocate(pages.data(), pages.size());  // the span given back on a refusal or a fault too
        throw;
    }
    arena.deallocate(pages.data(), pages.size());
}

}  // namespace qiven3::write::entries::detail
