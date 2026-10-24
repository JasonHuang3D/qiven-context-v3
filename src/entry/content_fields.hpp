#pragma once

// content_fields: the content check's codes, its error and its walk over
// one field's value, by kinds' tables. A field's value is checked by its
// row: a list's JSON type, then empty, too_many and each item; a shape's
// exact keys (unknown_field, then each member, then a missing member at
// the object's end, in the shape's order, and trigger's "at least one");
// a reference by refs' check_reference, its code taken as content's; a
// map's object as a repo or entry reference with a digest, another type
// syntax; a scalar's JSON type, empty, too_long in code points, then its
// syntax. Of every fault the walk meets, the first code in the table's
// order wins, and within a code the first in document order, a missing key
// placed at its object's end. A syntax is not read once a fault of an
// earlier code is held, nor a reference once one of the earliest is.
//
// Readings: an admit statement that is all whitespace (refs' WHITESPACE)
// is empty, and the two equal repeated, at admit; a test id past 200
// characters is syntax, its limit being its syntax's; the lists of ids
// that repeat none are those of entry ids (usage's served and cited, a
// checkpoint's refs).
//
// Allocations: two for each syntax the walk compiles, once in a check and
// held until it ends (slug, id, digest, revision, clause id, test id,
// client, session); a repo subject's two and each reference's (refs'
// counts), each freed before the next. A compile's MemoryError leaves
// nothing held once the walk is dropped. Each function is constexpr and a
// template on the allocator, so its vectors run in constant evaluation
// too; it reads by index, with no pointer arithmetic, and never recurses.
// content logs nothing.
//
// thread: none

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/matcher.hpp"
#include "entry/refs.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utc.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::entry::content {

// The codes, in the table's order: the first decides.
enum class Code : std::uint8_t {
    basis,
    scope,
    unknown_field,
    not_allowed,
    missing,
    type,
    empty,
    too_many,
    too_long,
    repeated,
    syntax,
    too_large,
};
inline constexpr std::array<std::string_view, 12> code_names = {
    "basis", "scope",    "unknown_field", "not_allowed", "missing", "type",
    "empty", "too_many", "too_long",      "repeated",    "syntax",  "too_large",
};
constexpr std::string_view name(Code code) noexcept { return code_names[static_cast<std::size_t>(code)]; }

// A fault: its code, the limit and the measured value (items, characters or
// bytes; 0 where the code has none), and the field's path, such as
// admit.mistake or evidence[2].digest, held by value: empty for basis,
// scope, a content that is no object and too_large; cut at path_size.
inline constexpr std::size_t path_size = 96;
struct ContentError {
    Code code = Code::syntax;
    std::uint32_t limit = 0;
    std::uint32_t value = 0;
    memory::Inline<path_size> path;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(ContentError) == 120, "ContentError's size is pinned: its code, two counts and its path");
#endif

// The syntaxes content reads beyond refs' and header's, each the whole
// value.
inline constexpr std::string_view slug_syntax = "^[a-z0-9][a-z0-9-]*$";
inline constexpr std::string_view clause_id_syntax = "^[A-Z][A-Z0-9]*(-[A-Z0-9]+)*$";
inline constexpr std::string_view test_id_syntax = "^[A-Za-z_][A-Za-z0-9_]*((\\.|::|/)[A-Za-z_][A-Za-z0-9_]*)*$";
inline constexpr std::size_t test_id_limit = 200;

// The secret check's twelve patterns' names, which a direction's clears
// names; the secret module's table holds the same, in its order.
inline constexpr std::array<std::string_view, 12> pattern_names = {
    "private-key",  "github-ghp",    "github-pat",  "sk-key",     "aws-akia",   "slack-xox",
    "slack-xoxd",   "url-password",  "pair-password", "auth-header", "cli-secret", "tencent-akid",
};

namespace detail {

// The syntaxes a walk compiles at most once each.
enum class Syntax : std::uint8_t { slug, id, digest, revision, clause_id, test_id, client, session };
inline constexpr std::array<std::string_view, 8> SYNTAXES = {
    slug_syntax,      header::id_syntax, header::content_syntax, refs::revision_syntax,
    clause_id_syntax, test_id_syntax,    refs::client_syntax,    refs::session_syntax,
};

// Whether text is all whitespace: at least one code point, each of the 29.
constexpr bool blank(std::string_view text) noexcept {
    std::size_t at = 0;
    while (at < text.size()) {
        const utf::Read r = utf::next(text, at);
        if (r.size == 0 || !refs::is_space(r.point)) {
            return false;
        }
        at += r.size;
    }
    return !text.empty();
}

// A path's text, built and cut as the walk goes, held in place.
class PathText {
public:
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view(bytes_.data(), bytes_.size()).substr(0, size_);
    }
    constexpr void cut(std::size_t n) noexcept { size_ = std::min(n, size_); }
    constexpr void key(std::string_view k) noexcept {
        if (size_ != 0) {
            put('.');
        }
        for (const char c : k) {
            put(c);
        }
    }
    constexpr void item(std::size_t i) noexcept {
        std::array<char, 20> digits{};
        put('[');
        for (const char c : header::detail::decimal(static_cast<std::int64_t>(i), digits)) {
            put(c);
        }
        put(']');
    }

private:
    constexpr void put(char c) noexcept {
        if (size_ < bytes_.size()) {
            bytes_.at(size_) = c;
            ++size_;
        }
    }
    std::array<char, path_size> bytes_{};
    std::size_t size_ = 0;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(PathText) == 104, "PathText's size is pinned: its bytes and their count");
#endif

// One check's walk over a content value: the best fault so far, the path
// at hand and the syntaxes compiled. It views the content and the header's
// writer and at within the check's call.
template <typename B>
class WalkView {
public:
    constexpr WalkView(const canon::Basic<B>& content, std::string_view writer, kinds::Basis basis,
                       std::string_view at) noexcept
        : v_(&content), writer_(writer), at_(at), basis_(basis),
          automata_(fresh(content.allocator(), std::make_index_sequence<SYNTAXES.size()>{})) {}
    WalkView(const WalkView&) = delete;
    WalkView& operator=(const WalkView&) = delete;
    WalkView(WalkView&&) = delete;
    WalkView& operator=(WalkView&&) = delete;
    constexpr ~WalkView() = default;

    [[nodiscard]] constexpr const std::optional<ContentError>& best() const noexcept { return best_; }
    [[nodiscard]] constexpr const canon::Basic<B>& value() const noexcept { return *v_; }
    [[nodiscard]] constexpr PathText& path() noexcept { return path_; }
    [[nodiscard]] constexpr std::string_view writer() const noexcept { return writer_; }
    [[nodiscard]] constexpr kinds::Basis basis() const noexcept { return basis_; }
    [[nodiscard]] constexpr bool can_improve(Code c) const noexcept { return !best_ || c < best_->code; }

    // A fault at the path at hand, kept if its code is earlier.
    constexpr void fault(Code c, std::size_t limit = 0, std::size_t measured = 0) {
        if (can_improve(c)) {
            best_ = ContentError{c, narrow(limit), narrow(measured), memory::Inline<path_size>(path_.view())};
        }
    }
    // A fault at a key under the path at hand.
    constexpr void fault_at(std::string_view key, Code c) {
        const std::size_t mark = path_.size();
        path_.key(key);
        fault(c);
        path_.cut(mark);
    }

    // A field's value at node k, by its row: a list, a shape or one value.
    constexpr void field(const kinds::Field& f, json::Index k) {
        if (f.items == 0) {
            one(f, k);
            return;
        }
        if (f.value == kinds::Value::shape) {
            list(f, k, [this](const kinds::Field& g, json::Index i) { shape(g.shape, i); });
        } else {
            list(f, k, [this](const kinds::Field& g, json::Index i) { scalar(g, i); });
        }
    }

    // The expires of a content, later than the header's at.
    constexpr void later(json::Index k) {
        const std::optional<utc::Millis> at = utc::parse_at(at_);
        const std::optional<utc::Millis> expires =
            v_->kind(k) == json::Kind::string ? utc::parse_at(v_->text(k)) : std::nullopt;
        if (at && expires && *expires <= *at) {
            fault(Code::syntax);
        }
    }

private:
    // The automata, none compiled: each holds the allocator alone.
    template <std::size_t... I>
    static constexpr std::array<matcher::Basic<B>, sizeof...(I)> fresh(const B& allocator,
                                                                      std::index_sequence<I...> /*each*/) noexcept {
        return {((void)I, matcher::Basic<B>(allocator))...};
    }

    static constexpr std::uint32_t narrow(std::size_t n) noexcept {
        return static_cast<std::uint32_t>(std::min<std::size_t>(n, UINT32_MAX));
    }

    // One value, not a list.
    constexpr void one(const kinds::Field& f, json::Index k) {
        if (f.value == kinds::Value::shape) {
            shape(f.shape, k);
        } else {
            scalar(f, k);
        }
    }

    // A list: its type, empty, too_many, each item, then an id repeated.
    template <typename Each>
    constexpr void list(const kinds::Field& f, json::Index k, const Each& each) {
        if (v_->kind(k) != json::Kind::array) {
            fault(Code::type);
            return;
        }
        const std::size_t n = v_->count(k);
        if (n == 0) {
            fault(Code::empty);
            return;
        }
        if (n > f.items) {
            fault(Code::too_many, f.items, n);
        }
        const std::size_t mark = path_.size();
        std::size_t i = 0;
        for (json::Index e = k + 1; e < v_->next(k); e = v_->next(e)) {
            path_.item(i);
            each(f, e);
            path_.cut(mark);
            ++i;
        }
        if (f.value == kinds::Value::id && can_improve(Code::repeated)) {
            repeated(k);
        }
    }

    // The first item of an id list equal to an earlier one.
    constexpr void repeated(json::Index k) {
        const std::size_t mark = path_.size();
        std::size_t i = 0;
        for (json::Index e = k + 1; e < v_->next(k); e = v_->next(e)) {
            for (json::Index d = k + 1; d < e && v_->kind(e) == json::Kind::string; d = v_->next(d)) {
                if (v_->kind(d) == json::Kind::string && v_->text(d) == v_->text(e)) {
                    path_.item(i);
                    fault(Code::repeated);
                    path_.cut(mark);
                    return;
                }
            }
            ++i;
        }
    }

    // An object of a shape's exact keys.
    constexpr void shape(kinds::Shape s, json::Index k) {
        if (v_->kind(k) != json::Kind::object) {
            fault(Code::type);
            return;
        }
        const auto keys = kinds::members(s);
        const std::optional<json::Index> t = v_->find(k, "type");
        const std::string_view pin_type =
            s == kinds::Shape::pin && t && v_->kind(*t) == json::Kind::string ? v_->text(*t) : std::string_view{};
        const std::size_t mark = path_.size();
        bool any = false;
        for (json::Index m = k + 1; m < v_->next(k); m = v_->next(m + 1)) {
            path_.key(v_->text(m));
            const std::optional<std::size_t> f = member(keys, v_->text(m));
            if (!f) {
                fault(Code::unknown_field);
            } else if (keys[*f].items != 0) {
                any = true;
                list(keys[*f], m + 1, [this](const kinds::Field& g, json::Index i) { scalar(g, i); });
            } else {
                any = true;
                scalar(keys[*f], m + 1);
                if (s == kinds::Shape::admit && v_->kind(m + 1) == json::Kind::string && blank(v_->text(m + 1))) {
                    fault(Code::empty);
                }
            }
            path_.cut(mark);
        }
        for (const kinds::Field& f : keys) {
            if (kinds::resolve(f.carried, writer_, basis_, pin_type) == kinds::Carried::required &&
                !v_->find(k, f.name.view())) {
                fault_at(f.name.view(), Code::missing);
            }
        }
        if (kinds::at_least_one(s) && !any) {
            fault(Code::missing);
        }
        if (s == kinds::Shape::admit) {
            const std::optional<json::Index> a = v_->find(k, "underivable");
            const std::optional<json::Index> b = v_->find(k, "mistake");
            if (a && b && v_->kind(*a) == json::Kind::string && v_->kind(*b) == json::Kind::string &&
                v_->text(*a) == v_->text(*b)) {
                fault(Code::repeated);
            }
        }
    }

    static constexpr std::optional<std::size_t> member(std::span<const kinds::Field> keys, std::string_view key) {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i].name == key) {
                return i;
            }
        }
        return std::nullopt;
    }

    // A value that is neither list nor shape.
    constexpr void scalar(const kinds::Field& f, json::Index k) {
        using kinds::Value;
        switch (f.value) {
            case Value::reference: reference(k, false); return;
            case Value::transcript: reference(k, true); return;
            case Value::map_object: map_object(k); return;
            case Value::only_true:
                if (v_->kind(k) != json::Kind::boolean) {
                    fault(Code::type);
                } else if (!v_->boolean(k)) {
                    fault(Code::syntax);
                }
                return;
            default: break;
        }
        if (v_->kind(k) != json::Kind::string) {
            fault(Code::type);
            return;
        }
        const std::string_view s = v_->text(k);
        if (s.empty()) {
            fault(Code::empty);
            return;
        }
        if (f.limit != 0) {
            const std::size_t n = utf::length(s);
            if (n > f.limit) {
                fault(Code::too_long, f.limit, n);
                return;
            }
        }
        if (can_improve(Code::syntax) && !syntax(f, s)) {
            fault(Code::syntax);
        }
    }

    // Whether a string of a field's value, not empty, keeps its syntax.
    constexpr bool syntax(const kinds::Field& f, std::string_view s) {
        using kinds::Value;
        switch (f.value) {
            case Value::one_of: return header::among(kinds::values(f), s);
            case Value::subject: return refs::check_subject(s, v_->allocator());
            case Value::slug: return match(Syntax::slug, s);
            case Value::time: return !utc::check_at(s);
            case Value::date: return utc::valid_date(s);
            case Value::id: return match(Syntax::id, s);
            case Value::digest: return match(Syntax::digest, s);
            case Value::revision: return match(Syntax::revision, s);
            case Value::clause_id: return match(Syntax::clause_id, s);
            case Value::test_id: return utf::length(s) <= test_id_limit && match(Syntax::test_id, s);
            case Value::path: return refs::check_path(s);
            case Value::qualified_name: return refs::check_name(s);
            case Value::client: return match(Syntax::client, s);
            case Value::session: return match(Syntax::session, s);
            case Value::pattern_name: return header::among(pattern_names, s);
            case Value::pin_name: {
                const refs::detail::Scan scan = refs::detail::scan(s);
                return !scan.fault && !scan.control && !scan.space;
            }
            default: return true;
        }
    }

    // Whether s matches a syntax, compiled at its first use in the walk.
    constexpr bool match(Syntax which, std::string_view s) {
        matcher::Basic<B>& automaton = automata_.at(static_cast<std::size_t>(which));
        if (!automaton.compiled()) {
            if (const std::optional<matcher::PatternError> fault =
                    matcher::try_compile(SYNTAXES.at(static_cast<std::size_t>(which)), automaton)) {
                throw *fault;
            }
        }
        return matcher::hit(automaton, s);
    }

    // A reference, by refs; an accept's message a transcript one.
    constexpr void reference(json::Index k, bool transcript) {
        if (!can_improve(Code::unknown_field)) {
            return;
        }
        if (const std::optional<refs::RefError> e = refs::try_check_reference(*v_, k)) {
            // The key at fault: an unknown one as written, else its field's.
            const std::string_view key = e->code == refs::Code::unknown_field ? v_->text(e->at) : refs::name(e->field);
            const bool length = e->code == refs::Code::too_long;
            const std::size_t mark = path_.size();
            if (!key.empty()) {
                path_.key(key);
            }
            fault(code_of(e->code), length ? refs::command_limit : 0, length ? utf::length(v_->text(e->at + 1)) : 0);
            path_.cut(mark);
            return;
        }
        const std::optional<json::Index> t = v_->find(k, "type");
        if (transcript && (!t || v_->text(*t) != "transcript")) {
            fault_at("type", Code::syntax);
        }
    }
    static constexpr Code code_of(refs::Code c) noexcept {
        switch (c) {
            case refs::Code::unknown_field: return Code::unknown_field;
            case refs::Code::missing: return Code::missing;
            case refs::Code::type: return Code::type;
            case refs::Code::empty: return Code::empty;
            case refs::Code::too_long: return Code::too_long;
            case refs::Code::syntax: break;
        }
        return Code::syntax;
    }

    // A map's object: a repo reference of type, repository, path, digest
    // and optionally name, or an entry reference of type, id and digest.
    constexpr void map_object(json::Index k) {
        using refs::Field;
        using refs::bit;
        if (v_->kind(k) != json::Kind::object) {
            fault(Code::type);
            return;
        }
        constexpr refs::Fields entry = bits(Field::type, Field::id, Field::digest);
        constexpr refs::Fields repo_required =
            bits(Field::type, Field::repository, Field::path, Field::digest);
        constexpr refs::Fields repo = repo_required | bit(Field::name);
        const std::optional<json::Index> t = v_->find(k, "type");
        const bool text = t && v_->kind(*t) == json::Kind::string;
        const std::string_view type = text ? v_->text(*t) : std::string_view{};
        const refs::Fields holds = type == "repo" ? repo : type == "entry" ? entry : repo | entry;
        for (json::Index m = k + 1; m < v_->next(k); m = v_->next(m + 1)) {
            const std::optional<Field> f = refs::detail::field_of(v_->text(m));
            if (!f || (holds & bit(*f)) == 0) {
                fault_at(v_->text(m), Code::unknown_field);
            }
        }
        if (type != "repo" && type != "entry") {
            fault_at("type", !t ? Code::missing : !text ? Code::type : type.empty() ? Code::empty : Code::syntax);
            return;
        }
        const refs::Fields required = type == "repo" ? repo_required : entry;
        for (std::size_t f = 0; f < refs::field_count; ++f) {
            if ((required & bit(static_cast<Field>(f))) != 0 && !v_->find(k, refs::field_names.at(f))) {
                fault_at(refs::field_names.at(f), Code::missing);
            }
        }
        for (json::Index m = k + 1; m < v_->next(k); m = v_->next(m + 1)) {
            const std::optional<Field> f = refs::detail::field_of(v_->text(m));
            if (!f || (holds & bit(*f)) == 0) {
                continue;
            }
            if (v_->kind(m + 1) != json::Kind::string) {
                fault_at(v_->text(m), Code::type);
            } else if (v_->text(m + 1).empty()) {
                fault_at(v_->text(m), Code::empty);
            } else if (can_improve(Code::syntax) &&
                       !refs::detail::value_syntax(*f, v_->text(m + 1), v_->allocator())) {
                fault_at(v_->text(m), Code::syntax);
            }
        }
    }

    const canon::Basic<B>* v_;
    std::string_view writer_;
    std::string_view at_;
    kinds::Basis basis_;
    PathText path_;
    std::optional<ContentError> best_;
    std::array<matcher::Basic<B>, SYNTAXES.size()> automata_;
};

}  // namespace detail

}  // namespace qiven3::entry::content
