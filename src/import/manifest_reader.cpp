// import/manifest: Reader's walk over each part of the manifest, in the
// form's order (manifest_reader.hpp), and the checks across parts.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "import/manifest.hpp"
#include "import/manifest_reader.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::import::manifest::detail {

void Reader::documents(const Field& f) {
    const json::Index n = node(f, JKind::object, true);
    if (v->count(n) < 1 || v->count(n) > max_documents) {
        path.member(f.key);
        refuse(Code::bound);
    }
    m->documents_.reserve(v->count(n));
    const Step s(path, path.member(f.key));
    for (json::Index k = n + 1; k < v->next(n); k = v->next(k + 1)) {
        const Step one(path, path.member(v->text(k)));
        if (!is_name(v->text(k))) {
            refuse(Code::form);
        }
        if (v->kind(k + 1) != JKind::object) {
            refuse(Code::type);
        }
        const auto d = members(k + 1, Table<3>{"repo", "path", "revision"});
        const Document doc{k, text(d[0], 100, true, is_repo), text(d[1], 240, true, is_path),
                           text(d[2], 40, true, is_revision)};
        if (!m->documents_.empty() && v->text(doc.repo) != m->repository()) {
            path.member("repo");
            refuse(Code::two_repositories);
        }
        m->documents_.push_back(doc);
    }
}

void Reader::rulings(const Field& f) {
    const json::Index n = list(f, 0, max_documents, true);
    m->rulings_.reserve(v->count(n));
    const Step s(path, path.member(f.key));
    std::size_t i = 0;
    for (json::Index k = n + 1; k < v->next(n); k = v->next(k), ++i) {
        const Step item(path, path.index(i));
        const std::optional<std::size_t> d = m->document(v->text(checked(k, 64, is_name)));
        if (!d) {
            refuse(Code::unknown_document);
        }
        if (std::find(m->rulings_.begin(), m->rulings_.end(), *d) != m->rulings_.end()) {
            refuse(Code::duplicate);
        }
        used.at(*d) = true;
        m->rulings_.push_back(size32(*d));
    }
}

void Reader::read_entry(json::Index n) {
    if (v->kind(n) != JKind::object) {
        refuse(Code::type);
    }
    const auto f = members(n, Table<16>{"id", "kind", "basis", "scope", "band", "band_basis", "doc", "section",
                                         "ordinal", "source", "note", "also_in", "words", "claim", "decides",
                                         "reasons"});
    Entry e;
    e.id = text(f[0], 64, true, is_id);
    entry = memory::Inline<64>(v->text(e.id));
    const Text kind = text(f[1], 64, true);
    static constexpr std::array<std::string_view, 3> kinds = {"direction", "decision", "claim"};
    const auto it = std::find(kinds.begin(), kinds.end(), v->text(kind));
    if (it == kinds.end()) {
        path.member(f[1].key);
        refuse(Code::value);
    }
    e.kind = static_cast<Kind>(it - kinds.begin());
    // Each kind's own fields: words 12, decides 14 and reasons 15, claim 13.
    const std::array<bool, 4> own = {e.kind == Kind::direction, e.kind == Kind::claim, e.kind == Kind::decision,
                                     e.kind == Kind::decision};
    for (std::size_t j = 0; j < own.size(); ++j) {
        if (!own.at(j) && f.at(12 + j).at != 0) {
            path.member(f.at(12 + j).key);
            refuse(Code::unknown_field);
        }
    }
    e.basis = fixed(f[2], e.kind == Kind::direction ? "owner-words" : "claimed");
    e.scope = text(f[3], 72, true, is_scope);
    e.band = text(f[4], 64, true);
    static constexpr std::array<std::string_view, 4> bands = {"core", "high", "normal", "low"};
    if (e.kind == Kind::direction ? v->text(e.band) != "high"
                                  : std::find(bands.begin(), bands.end(), v->text(e.band)) == bands.end()) {
        path.member(f[4].key);
        refuse(Code::value);
    }
    e.band_basis = text(f[5], text_chars, false);
    e.doc = doc(f[6]);
    e.section = text(f[7], 32, true, is_section);
    e.ordinal = integer(f[8], max_ordinal);
    for (const Entry& before : m->entries_) {
        if (before.ordinal == e.ordinal && v->text(before.doc) == v->text(e.doc) &&
            v->text(before.section) == v->text(e.section)) {
            path.member(f[8].key);
            refuse(Code::duplicate);
        }
    }
    switch (e.kind) {
    case Kind::direction: e.text = text(f[12], max_words, true); break;
    case Kind::claim: e.text = text(f[13], 1000, true); break;
    case Kind::decision:
        e.text = text(f[14], 500, true);
        e.reasons = texts(f[15], max_items, 300);
        break;
    }
    e.sources = sources(f[9], 1, true, true);
    e.note = text(f[10], text_chars, false);
    const json::Index also = list(f[11], 1, max_items, false);
    e.also = Span{size32(m->alsos_.size()), also == 0 ? 0 : size32(v->count(also))};
    const Step s(path, path.member(f[11].key));
    std::size_t k = 0;
    for (json::Index a = also + 1; also != 0 && a < v->next(also); a = v->next(a), ++k) {
        const Step item(path, path.index(k));
        if (v->kind(a) != JKind::object) {
            refuse(Code::type);
        }
        const auto g = members(a, Table<4>{"doc", "section", "words", "part"});
        Also one{doc(g[0]), text(g[1], 32, true, is_section), text(g[2], max_words, false), false};
        if (node(g[3], JKind::boolean, false) != 0) {
            one.part = v->boolean(g[3].at);
        }
        m->alsos_.push_back(one);
    }
    m->entries_.push_back(e);
}

void Reader::goal(const Field& f) {
    const json::Index n = node(f, JKind::object, true);
    const Step s(path, path.member(f.key));
    const auto g = members(n, Table<15>{"kind", "basis", "scope", "band", "band_basis", "doc", "section",
                                         "objective", "boundary", "test_questions", "source", "source_reasons",
                                         "seed_retire_reason", "seed_retire_reason_source", "when"});
    Goal& goal = m->goal_;
    static_cast<void>(fixed(g[0], "goal"));
    static_cast<void>(fixed(g[1], "claimed"));
    goal.scope = text(g[2], 72, true, is_scope);
    static_cast<void>(fixed(g[3], "core"));
    goal.band_basis = text(g[4], text_chars, false);
    goal.doc = doc(g[5]);
    goal.section = text(g[6], 32, true, is_section);
    goal.objective = text(g[7], 500, true);
    goal.boundary = text(g[8], 500, false);
    goal.test_questions = texts(g[9], max_items, 300);
    const json::Index list_at = list(g[10], 1, max_sources, true);
    goal.sources = Span{size32(m->sources_.size()), size32(v->count(list_at))};
    {
        const Step src(path, path.member(g[10].key));
        std::size_t i = 0;
        for (json::Index k = list_at + 1; k < v->next(list_at); k = v->next(k), ++i) {
            const Step item(path, path.index(i));
            if (v->kind(k) != JKind::object) {
                refuse(Code::type);
            }
            const auto e = members(k, Table<1>{"entry"});
            m->sources_.push_back(Source{0, 0, text(e[0], 64, true, is_id)});
        }
    }
    const json::Index reasons = node(g[11], JKind::object, false);
    if (reasons != 0) {
        const Step src(path, path.member(g[11].key));
        m->pairs_.reserve(v->count(reasons));
        goal.source_reasons = Span{0, size32(v->count(reasons))};
        for (json::Index k = reasons + 1; k < v->next(reasons); k = v->next(k + 1)) {
            const Step one(path, path.member(v->text(k)));
            const std::span<const Source> named = m->sources(goal.sources);
            if (std::none_of(named.begin(), named.end(),
                             [&](const Source& r) { return v->text(r.entry) == v->text(k); })) {
                refuse(Code::unknown_entry);
            }
            m->pairs_.push_back(Pair{k, checked(k + 1, text_chars, nullptr)});
        }
    }
    goal.seed_retire_reason = text(g[12], text_chars, false);
    goal.seed_retire_reason_source = one_source(g[13], false);
    goal.when = text(g[14], text_chars, false);
}

void Reader::person(const Field& f) {
    const json::Index n = node(f, JKind::object, true);
    const Step s(path, path.member(f.key));
    const auto p = members(n, Table<13>{"kind", "basis", "scope", "band", "band_basis", "scope_basis", "status",
                                         "decides", "reasons", "decides_source", "source", "reasons_source",
                                         "note"});
    Person& person = m->person_;
    static_cast<void>(fixed(p[0], "decision"));
    static_cast<void>(fixed(p[1], "claimed"));
    static_cast<void>(fixed(p[2], "person"));
    static_cast<void>(fixed(p[3], "core"));
    person.band_basis = text(p[4], text_chars, false);
    person.scope_basis = text(p[5], text_chars, false);
    person.status = text(p[6], text_chars, false);
    person.decides = text(p[7], 500, true);
    person.reasons = texts(p[8], max_items, 300);
    person.decides_source = sources(p[9], 1, false, false);
    person.sources = sources(p[10], 1, false, false);
    const json::Index groups = list(p[11], 1, max_items, false);
    if (groups != 0) {
        if (v->count(groups) != person.reasons.count) {
            path.member(p[11].key);
            refuse(Code::value);
        }
        m->groups_.reserve(v->count(groups));
        person.reasons_source = Span{0, size32(v->count(groups))};
        const Step src(path, path.member(p[11].key));
        std::size_t i = 0;
        for (json::Index k = groups + 1; k < v->next(groups); k = v->next(k), ++i) {
            const Step item(path, path.index(i));
            if (v->kind(k) != JKind::array) {
                refuse(Code::type);
            }
            m->groups_.push_back(sources(Field{k, {}}, 1, false, true));
        }
    }
    person.note = text(p[12], text_chars, false);
}

void Reader::replacements(const Field& f) {
    const json::Index n = list(f, 0, max_listed, false);
    if (n == 0) {
        return;
    }
    m->replacements_.reserve(v->count(n));
    const Step s(path, path.member(f.key));
    std::size_t i = 0;
    for (json::Index k = n + 1; k < v->next(n); k = v->next(k), ++i) {
        const Step item(path, path.index(i));
        if (v->kind(k) != JKind::object) {
            refuse(Code::type);
        }
        const auto r = members(k, Table<9>{"doc", "line", "in_section", "text", "replaced", "replaced_by",
                                           "decisions", "heading_line", "judge"});
        Replacement one;
        one.doc = doc(r[0]);
        one.line = integer(r[1], max_line);
        one.in_section = text(r[2], 32, true, is_section);
        one.text = text(r[3], line_chars, true);
        one.replaced = one_source(r[4], true);
        one.replaced_by = one_source(r[5], true);
        const json::Index pair = node(r[6], JKind::object, false);
        if (pair != 0) {
            const Step d(path, path.member(r[6].key));
            const auto ft = members(pair, Table<2>{"from", "to"});
            one.from = text(ft[0], 32, true, is_section);
            one.to = text(ft[1], 32, true, is_section);
        }
        one.heading_line = text(r[7], text_chars, false);
        one.judge = text(r[8], text_chars, false);
        m->replacements_.push_back(one);
    }
}

void Reader::amendments(const Field& f) {
    const json::Index n = list(f, 0, max_listed, false);
    if (n == 0) {
        return;
    }
    m->amendments_.reserve(v->count(n));
    const Step s(path, path.member(f.key));
    std::size_t i = 0;
    for (json::Index k = n + 1; k < v->next(n); k = v->next(k), ++i) {
        const Step item(path, path.index(i));
        if (v->kind(k) != JKind::object) {
            refuse(Code::type);
        }
        const auto a = members(k, Table<5>{"doc", "line", "in_section", "text", "what"});
        Amendment one;
        one.doc = doc(a[0]);
        one.line = integer(a[1], max_line);
        one.in_section = text(a[2], 32, true, is_section);
        one.text = text(a[3], line_chars, true);
        one.what = text(a[4], text_chars, true);
        m->amendments_.push_back(one);
    }
}

// The ids, once each; each entry reference to an entry, a goal's to a
// direction; each document named.
void Reader::references() {
    const std::size_t count = m->entries_.size();
    m->by_id_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        m->by_id_.push_back(size32(i));
    }
    std::sort(m->by_id_.begin(), m->by_id_.end(), [&](std::uint32_t a, std::uint32_t b) {
        const std::string_view x = v->text(m->entries_[a].id);
        const std::string_view y = v->text(m->entries_[b].id);
        return x != y ? x < y : a < b;
    });
    std::optional<std::size_t> again;
    for (std::size_t j = 1; j < count; ++j) {
        if (v->text(m->entries_[m->by_id_[j]].id) == v->text(m->entries_[m->by_id_[j - 1]].id)) {
            again = std::min<std::size_t>(again.value_or(count), m->by_id_[j]);
        }
    }
    if (again) {
        entry = memory::Inline<64>(v->text(m->entries_[*again].id));
        path.member("entries");
        path.index(*again);
        path.member("id");
        refuse(Code::duplicate);
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Entry& e = m->entries_[i];
        const std::span<const Source> named = m->sources(e.sources);
        for (std::size_t k = 0; k < named.size(); ++k) {
            if (named[k].entry != 0 && !m->entry(v->text(named[k].entry))) {
                entry = memory::Inline<64>(v->text(e.id));
                path.member("entries");
                path.index(i);
                path.member("source");
                path.index(k);
                path.member("entry");
                refuse(Code::unknown_entry);
            }
        }
    }
    entry = memory::Inline<64>();
    const std::span<const Source> named = m->sources(m->goal_.sources);
    for (std::size_t k = 0; k < named.size(); ++k) {
        const std::optional<std::size_t> to = m->entry(v->text(named[k].entry));
        if (!to || m->entries_[*to].kind != Kind::direction) {
            path.member("goal");
            path.member("source");
            path.index(k);
            path.member("entry");
            refuse(Code::unknown_entry);
        }
    }
    for (std::size_t d = 0; d < m->documents_.size(); ++d) {
        if (!used.at(d)) {
            path.member("documents");
            path.member(v->text(m->documents_[d].name));
            refuse(Code::unused_document);
        }
    }
}

}  // namespace qiven3::import::manifest::detail
