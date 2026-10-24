#pragma once

// content: the content check, the admission test and the episode, by
// kinds' tables. check(header, content) reads an entry's content against
// its header's kind, writer and basis, the header having passed header's
// check, and throws the first fault as ContentError(code, path, limit,
// value), the first code in the table's order deciding: basis, scope,
// unknown_field, not_allowed, missing, type, empty, too_many, too_long,
// repeated, syntax, too_large. basis and scope lead, read from the header
// (a check's method choosing its basis, observed or claimed off the four),
// so that a header with no body meets them, an empty content value, and no
// newer minor's field hides them: the bundle reader's opacity reads
// unknown_field and not_allowed after them.
//
// The content is one object. A key is a common field, carried by its row
// for the kind, the writer's role and the basis, or one of the kind's own,
// by its row: one no kind defines is unknown_field, one this kind or
// writer may not carry not_allowed. Members are read in the value's order,
// each by content_fields' walk; then each required field absent, the
// common ones in their table's order, then the kind's own; then the kind's
// rules: a map carries contract or constraints (else missing, contract); a
// reflection writer's claim or lesson, and a check of method transcript,
// hold a transcript reference in evidence (else missing, evidence); a
// clause's source holds a repo reference, the law it is compiled from
// (else missing, source). expires is later than the header's at (else
// syntax). Last, a content past 65,536 bytes canonical is too_large.
//
// Readings: basis and scope are the header's, their path empty; a kind
// outside this minor's 22 is the caller's to keep opaque before the check
// (fail_fast); a check of method transcript and a clause's source take the
// reflection writer's code, missing; an applies_to of no key passes, each
// of its keys being optional.
//
// Allocations: the walk's (content_fields), none for a content of no
// syntax; the size is counted in place. A size's sum is checked.
//
// thread: none

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "entry/content_fields.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::entry::content {

namespace detail {

// Whether some kind, or the common table, defines a key.
constexpr bool defined(std::string_view key) noexcept {
    for (const kinds::Field& f : kinds::COMMON) {
        if (f.name == key) {
            return true;
        }
    }
    for (const kinds::Field& f : kinds::detail::OWN) {
        if (f.name == key) {
            return true;
        }
    }
    return false;
}

// A kind's field by its key, common or its own, with how it is carried
// for this entry; none for a key the kind does not define.
struct Row {
    kinds::Field field;
    kinds::Carried carried;
    bool common;
    kinds::Common which;
};

template <typename B>
constexpr std::optional<Row> row(const kinds::Kind& k, std::string_view key, const WalkView<B>& walk) noexcept {
    for (std::size_t c = 0; c < kinds::COMMON.size(); ++c) {
        if (kinds::COMMON.at(c).name == key) {
            const auto which = static_cast<kinds::Common>(c);
            const kinds::Carried carried =
                kinds::resolve(kinds::common_carried(k, which), walk.writer(), walk.basis());
            return Row{kinds::COMMON.at(c), carried, true, which};
        }
    }
    for (const kinds::Field& f : kinds::fields(k)) {
        if (f.name == key) {
            return Row{f, kinds::resolve(f.carried, walk.writer(), walk.basis()), false, kinds::Common::subject};
        }
    }
    return std::nullopt;
}

// Whether a kind allows a basis; a check's by its method, observed or
// claimed off the four.
template <typename B>
constexpr bool allowed(const kinds::Kind& k, kinds::Basis basis, const canon::Basic<B>& content) {
    if (k.name == "check" && !content.empty() && content.kind(0) == json::Kind::object) {
        const std::optional<json::Index> m = content.find(0, "method");
        if (m && content.kind(*m) == json::Kind::string) {
            if (const std::optional<kinds::Basis> b = kinds::check_basis(content.text(*m))) {
                return *b == basis;
            }
        }
    }
    return (k.bases & kinds::bit(basis)) != 0;
}

// Whether a list field holds a reference of a type.
template <typename B>
constexpr bool holds(const canon::Basic<B>& v, std::string_view field, std::string_view type) {
    const std::optional<json::Index> l = v.find(0, field);
    if (!l || v.kind(*l) != json::Kind::array) {
        return false;
    }
    for (json::Index e = *l + 1; e < v.next(*l); e = v.next(e)) {
        if (v.kind(e) == json::Kind::object) {
            const std::optional<json::Index> t = v.find(e, "type");
            if (t && v.kind(*t) == json::Kind::string && v.text(*t) == type) {
                return true;
            }
        }
    }
    return false;
}

// The kind's rules past its fields, at the root's end.
template <typename B>
constexpr void rules(const kinds::Kind& k, WalkView<B>& walk) {
    const canon::Basic<B>& v = walk.value();
    if (k.name == "map" && !v.find(0, "contract") && !v.find(0, "constraints")) {
        walk.fault_at("contract", Code::missing);
    }
    const bool reflect = header::role(walk.writer()) == header::reflect_role;
    const std::optional<json::Index> m = v.find(0, "method");
    const bool transcript_check =
        k.name == "check" && m && v.kind(*m) == json::Kind::string && v.text(*m) == "transcript";
    if ((reflect || transcript_check) && v.find(0, "evidence") && !holds(v, "evidence", "transcript")) {
        walk.fault_at("evidence", Code::missing);
    }
    if (k.name == "clause" && !holds(v, "source", "repo")) {
        walk.fault_at("source", Code::missing);
    }
}

// The content's canonical size, counted as json's encode counts it, or
// none past a size_t.
template <typename B>
constexpr std::optional<std::size_t> canonical_size(const canon::Basic<B>& v) {
    std::optional<std::size_t> bytes = 0;
    for (json::Index k = 0; k < v.size() && bytes; ++k) {
        std::size_t n = 0;
        switch (v.kind(k)) {
            case json::Kind::null: n = 4; break;
            case json::Kind::boolean: n = v.boolean(k) ? 4 : 5; break;
            case json::Kind::integer: n = json::detail::width(v.integer(k)); break;
            case json::Kind::string: n = json::detail::quoted(v.text(k)); break;
            case json::Kind::array: n = v.count(k) == 0 ? 2 : v.count(k) + 1; break;
            case json::Kind::object: n = v.count(k) == 0 ? 2 : 2 * v.count(k) + 1; break;
        }
        bytes = memory::add(*bytes, n);
    }
    return bytes;
}

}  // namespace detail

// The first fault of a content, or none.
template <typename A, typename B>
constexpr std::optional<ContentError> try_check(const header::Header<A>& h, const canon::Basic<B>& content) {
    const std::optional<std::size_t> index = kinds::index_of(h.value(header::Slot::kind));
    if (!index) {
        memory::fail_fast("content: a kind outside this minor's 22");
    }
    const kinds::Kind& kind = kinds::KINDS.at(*index);
    const std::optional<kinds::Basis> basis = kinds::basis_of(h.value(header::Slot::basis));
    if (!basis || !detail::allowed(kind, *basis, content)) {
        return ContentError{Code::basis, 0, 0, {}};
    }
    if (kind.group == kinds::Group::work && !h.value(header::Slot::scope).starts_with("project:")) {
        return ContentError{Code::scope, 0, 0, {}};
    }
    if (content.empty()) {
        return std::nullopt;
    }
    detail::WalkView<B> walk(content, h.value(header::Slot::writer), *basis, h.value(header::Slot::at));
    if (content.kind(0) != json::Kind::object) {
        walk.fault(Code::type);
        return walk.best();
    }
    // The members in the value's order.
    for (json::Index m = 1; m < content.next(0); m = content.next(m + 1)) {
        const std::string_view key = content.text(m);
        walk.path().key(key);
        const std::optional<detail::Row> r = detail::row(kind, key, walk);
        if (!r) {
            walk.fault(detail::defined(key) ? Code::not_allowed : Code::unknown_field);
        } else if (r->carried == kinds::Carried::refused) {
            walk.fault(Code::not_allowed);
        } else {
            walk.field(r->field, m + 1);
            if (r->common && r->which == kinds::Common::expires) {
                walk.later(m + 1);
            }
        }
        walk.path().cut(0);
    }
    // The required fields absent: the common ones, then the kind's own.
    for (std::size_t c = 0; c < kinds::COMMON.size(); ++c) {
        const kinds::Field& f = kinds::COMMON.at(c);
        const kinds::Carried carried =
            kinds::resolve(kinds::common_carried(kind, static_cast<kinds::Common>(c)), walk.writer(), walk.basis());
        if (carried == kinds::Carried::required && !content.find(0, f.name.view())) {
            walk.fault_at(f.name.view(), Code::missing);
        }
    }
    for (const kinds::Field& f : kinds::fields(kind)) {
        if (kinds::resolve(f.carried, walk.writer(), walk.basis()) == kinds::Carried::required &&
            !content.find(0, f.name.view())) {
            walk.fault_at(f.name.view(), Code::missing);
        }
    }
    detail::rules(kind, walk);
    if (walk.best()) {
        return walk.best();
    }
    const std::optional<std::size_t> size = detail::canonical_size(content);
    if (!size || *size > kinds::content_limit) {
        walk.fault(Code::too_large, kinds::content_limit, size.value_or(SIZE_MAX));
    }
    return walk.best();
}

// try_check, a fault thrown as ContentError.
template <typename A, typename B>
constexpr void check(const header::Header<A>& h, const canon::Basic<B>& content) {
    if (const std::optional<ContentError> fault = try_check(h, content)) {
        throw *fault;
    }
}

}  // namespace qiven3::entry::content
