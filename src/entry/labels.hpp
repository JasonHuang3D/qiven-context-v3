#pragma once

// labels: the label of a version, computed when read and never stored.
// label takes the version's writer and basis and the live checks and
// accepts that target it, in the order; the caller tells for each whether
// it is live (its body held, no retire of it) and, for an accept or a
// transcript check, what the reader's re-verification gave: no bytes
// (unverified, v3's case), equal (re-verified), or unequal, when the entry
// counts for nothing. A check or an accept by the version's own writer (one
// writer, as fold reads it) is kept and changes no label.
//
// The base by basis: owner-words reads its bindings, a live accept, or a
// live transcript check of result verified, by another writer; the earliest
// re-verified one makes the owner's word, else the earliest unverified one
// a record bound (unverified), else the version's document of record, the
// first repo reference with a revision in its source, the owner's words as
// quoted in its path at its revision, recorded by the writer, else
// unbound. The document of record is no binding and lifts nothing; its
// digest is checked off the request path, never here. observed, claimed
// and derived name the writer. Then the strongest live check by another writer adds
// its suffix, the earliest among equals named, by this precedence: refuted
// (test or rerun, or a transcript re-verified), accepted (an accept
// re-verified), verified (test or rerun, or a transcript re-verified),
// accepted (unverified), disputed (a review, or a transcript unverified),
// checked (a review, or a transcript unverified). On owner-words the
// binding rows replace both accepted rows and the verified or checked a
// binding would add; a refutation, a test's or a rerun's verification and
// a review still apply. A refuted version is not served; its label is the
// one served on request. Strength verified or accepted lifts a reflection
// writer's lesson (lifted).
//
// version_of and check_of read a header and its content: the basis and
// the document of record; a check's method and result; the transcript
// reference's message and digest, an accept's message, or a transcript
// check's first transcript reference in its evidence.
//
// Allocations: none. A label is a value of a fixed capacity, the longest
// label a valid entry gives: the owner's words as quoted in a path of
// refs' limit, each character four UTF-8 bytes, with a refutation by
// transcript, each writer at its syntax's longest; a longer one is
// MemoryError budget, never cut. Each is constexpr, so its vectors run in constant
// evaluation too; it reads by index, with no pointer arithmetic. labels
// logs nothing.
//
// thread: none

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "entry/fold.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/refs.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::entry::labels {

using fold::Method;
using fold::Result;
using fold::Transcript;

enum class Basis : std::uint8_t { owner_words, observed, claimed, derived };
enum class Act : std::uint8_t { check, accept };

// The strength a check adds, weakest first: the strongest decides.
enum class Strength : std::uint8_t { none, checked, disputed, accepted_unverified, verified, accepted, refuted };
inline constexpr std::array<std::string_view, 7> strength_names = {
    "none", "checked", "disputed", "accepted, unverified", "verified", "accepted", "refuted"};
constexpr std::string_view name(Strength s) noexcept { return strength_names[static_cast<std::size_t>(s)]; }

// An owner-words version's binding: none, unverified or re-verified.
enum class Binding : std::uint8_t { none, unverified, reverified };

// The version as the label reads it: views made and dropped in the call;
// its document of record's path and revision, both empty with none.
struct VersionView {
    std::string_view writer;
    Basis basis = Basis::claimed;
    std::string_view path;
    std::string_view revision;
};

// A check or an accept of the version, in the order: its writer, and for
// an accept or a transcript check its transcript reference's message and
// digest (empty elsewhere).
struct CheckView {
    std::string_view writer;
    std::string_view message;
    std::string_view digest;
    Act act = Act::check;
    Method method = Method::none;
    Result result = Result::none;
    Transcript transcript = Transcript::unverified;
    bool live = true;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(CheckView) == 56, "CheckView's size is pinned: three views and five flags, padded");
#endif

// The longest label: owner-words as quoted in a document of record, its
// path at refs' limit in characters of four UTF-8 bytes each and its
// revision of 40 hex digits, with a refutation by transcript, each writer
// at its syntax's longest. An accepted suffix, the longest, never follows
// an owner-words base: an accept by another writer is its binding. The
// longest bound base, 296 bytes, and the owner's word's, 212, are shorter.
inline constexpr std::size_t writer_max = 32 + 2 + 9;
inline constexpr std::size_t message_max = 128;
inline constexpr std::size_t path_max = refs::path_limit * 4;
inline constexpr std::size_t revision_size = 40;
// "the owner's words as quoted in <path> (<revision>), recorded by <writer>"
inline constexpr std::size_t quoted_max = 31 + path_max + 2 + revision_size + 15 + writer_max;
// "; refuted by <writer> (transcript)"
inline constexpr std::size_t suffix_max = 13 + writer_max + 2 + 10 + 1;
inline constexpr std::size_t capacity = quoted_max + suffix_max;
static_assert(quoted_max == 931 && suffix_max == 69 && capacity == 1000, "the longest label, by its parts");

// A label: its text in place, its strength and its binding.
struct Label {
    std::uint16_t size = 0;
    Strength strength = Strength::none;
    Binding binding = Binding::none;
    std::array<char, capacity> bytes{};

    [[nodiscard]] constexpr std::string_view text() const noexcept { return {bytes.data(), size}; }
    // A reflection writer's lesson is served as a claim until this holds.
    [[nodiscard]] constexpr bool lifted() const noexcept {
        return strength == Strength::verified || strength == Strength::accepted;
    }
};
static_assert(sizeof(Label) == 1004, "Label's size is pinned: its size, two flags and the text");

namespace detail {

[[noreturn]] inline void too_long(std::size_t request) {
    throw memory::MemoryError::budget("labels", capacity, request, 0);
}

constexpr void put(Label& l, std::string_view s) {
    const std::optional<std::size_t> end = memory::add(l.size, s.size());
    if (!end || *end > capacity) {
        too_long(end.value_or(SIZE_MAX));
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
        l.bytes[l.size + i] = s[i];
    }
    l.size = static_cast<std::uint16_t>(*end);
}

constexpr std::string_view method_name(Method m) noexcept {
    return m == Method::none ? std::string_view{} : fold::method_names[static_cast<std::size_t>(m) - 1U];
}

// A binding: a live accept, or a live transcript check of result verified,
// by another writer, its transcript not unequal.
constexpr bool binding(const CheckView& c) noexcept {
    return c.act == Act::accept || (c.method == Method::transcript && c.result == Result::verified);
}

// The strength c adds to the version, before the writer and liveness.
constexpr Strength strength(const CheckView& c, bool owner_words) noexcept {
    if (owner_words && binding(c)) {
        return Strength::none;
    }
    if (c.act == Act::accept) {
        return c.transcript == Transcript::reverified ? Strength::accepted : Strength::accepted_unverified;
    }
    const bool mechanical = c.method == Method::test || c.method == Method::rerun ||
                            (c.method == Method::transcript && c.transcript == Transcript::reverified);
    const bool review = c.method == Method::review ||
                        (c.method == Method::transcript && c.transcript == Transcript::unverified);
    if (c.result == Result::refuted) {
        return mechanical ? Strength::refuted : review ? Strength::disputed : Strength::none;
    }
    if (c.result == Result::verified) {
        return mechanical ? Strength::verified : review ? Strength::checked : Strength::none;
    }
    return Strength::none;
}

template <typename B>
constexpr std::optional<json::Index> child(const canon::Basic<B>& v, json::Index i, std::string_view key) {
    if (v.empty() || v.kind(i) != json::Kind::object) {
        return std::nullopt;
    }
    return v.find(i, key);
}

}  // namespace detail

constexpr std::optional<Basis> basis_of(std::string_view text) noexcept {
    for (std::size_t i = 0; i < header::basis_names.size(); ++i) {
        if (header::basis_names[i] == text) {
            return static_cast<Basis>(i);
        }
    }
    return std::nullopt;
}

// The label of version, given its checks and accepts in the order.
constexpr Label label(const VersionView& version, std::span<const CheckView> checks) {
    const bool owner_words = version.basis == Basis::owner_words;
    Label l;
    std::size_t binder = checks.size();
    std::size_t named = checks.size();
    for (std::size_t i = 0; i < checks.size(); ++i) {
        const CheckView& c = checks[i];
        if (!c.live || c.transcript == Transcript::unequal || fold::one_writer(c.writer, version.writer)) {
            continue;
        }
        if (owner_words && detail::binding(c)) {
            const Binding b = c.transcript == Transcript::reverified ? Binding::reverified : Binding::unverified;
            if (b > l.binding) {
                l.binding = b;
                binder = i;
            }
        }
        const Strength s = detail::strength(c, owner_words);
        if (s > l.strength) {
            l.strength = s;
            named = i;
        }
    }
    switch (version.basis) {
        case Basis::owner_words:
            if (l.binding == Binding::reverified) {
                detail::put(l, "the owner's word (transcript ");
                detail::put(l, checks[binder].message);
                detail::put(l, ", bound by ");
                detail::put(l, checks[binder].writer);
                detail::put(l, ")");
            } else if (l.binding == Binding::unverified) {
                detail::put(l, "the model's record of the owner's words, by ");
                detail::put(l, version.writer);
                detail::put(l, ", bound (unverified) by ");
                detail::put(l, checks[binder].writer);
                detail::put(l, " (transcript ");
                detail::put(l, checks[binder].message);
                detail::put(l, ")");
            } else if (!version.revision.empty()) {
                detail::put(l, "the owner's words as quoted in ");
                detail::put(l, version.path);
                detail::put(l, " (");
                detail::put(l, version.revision);
                detail::put(l, "), recorded by ");
                detail::put(l, version.writer);
            } else {
                detail::put(l, "the model's record of the owner's words, by ");
                detail::put(l, version.writer);
                detail::put(l, ", unbound");
            }
            break;
        case Basis::observed:
            detail::put(l, "observed by ");
            detail::put(l, version.writer);
            break;
        case Basis::claimed:
            detail::put(l, "claim by ");
            detail::put(l, version.writer);
            break;
        case Basis::derived:
            detail::put(l, "derived by ");
            detail::put(l, version.writer);
            detail::put(l, ", non-authoritative");
            break;
    }
    if (l.strength == Strength::none) {
        return l;
    }
    const CheckView& c = checks[named];
    switch (l.strength) {
        case Strength::refuted:
        case Strength::verified:
            detail::put(l, l.strength == Strength::refuted ? "; refuted by " : "; verified by ");
            detail::put(l, c.writer);
            detail::put(l, " (");
            detail::put(l, detail::method_name(c.method));
            detail::put(l, ")");
            break;
        case Strength::accepted:
        case Strength::accepted_unverified:
            detail::put(l, l.strength == Strength::accepted ? "; accepted by the owner (transcript "
                                                            : "; accepted (unverified) (transcript ");
            detail::put(l, c.message);
            detail::put(l, ", recorded by ");
            detail::put(l, c.writer);
            detail::put(l, ")");
            break;
        case Strength::disputed:
        case Strength::checked:
            detail::put(l, l.strength == Strength::disputed ? "; disputed by " : "; checked by ");
            detail::put(l, header::role(c.writer));
            break;
        case Strength::none: break;
    }
    return l;
}

// A version as the label reads it, with views of its header and content:
// its writer, its basis and its document of record, the first repo
// reference with a revision in its source; none when its basis is no
// basis.
template <typename A, typename B>
constexpr std::optional<VersionView> version_of(const header::Header<A>& h, const canon::Basic<B>& content) {
    const std::optional<Basis> b = basis_of(h.value(header::Slot::basis));
    if (!b) {
        return std::nullopt;
    }
    VersionView v{h.value(header::Slot::writer), *b, {}, {}};
    const std::optional<json::Index> source = detail::child(content, 0, "source");
    if (!source || content.kind(*source) != json::Kind::array) {
        return v;
    }
    for (json::Index k = *source + 1; k < content.next(*source); k = content.next(k)) {
        const std::string_view revision = kinds::detail::member(content, k, "revision");
        if (kinds::detail::member(content, k, "type") == "repo" && !revision.empty()) {
            v.path = kinds::detail::member(content, k, "path");
            v.revision = revision;
            break;
        }
    }
    return v;
}

// A check's or an accept's entry as the label reads it, with views of its
// header and content; none for another kind. transcript is the reader's
// re-verification, unverified with no transcript reader.
template <typename A, typename B>
constexpr std::optional<CheckView> check_of(const header::Header<A>& h, const canon::Basic<B>& content, bool live,
                                            Transcript transcript = Transcript::unverified) {
    const std::string_view kind = h.value(header::Slot::kind);
    if (kind != "check" && kind != "accept") {
        return std::nullopt;
    }
    CheckView c;
    c.writer = h.value(header::Slot::writer);
    c.live = live;
    c.transcript = transcript;
    std::optional<json::Index> ref;
    if (kind == "accept") {
        c.act = Act::accept;
        ref = detail::child(content, 0, "message");
    } else {
        const std::string_view method = kinds::detail::member(content, 0, "method");
        const std::string_view result = kinds::detail::member(content, 0, "result");
        c.method = static_cast<Method>(fold::detail::position(fold::method_names, method));
        c.result = static_cast<Result>(fold::detail::position(fold::result_names, result));
        const std::optional<json::Index> evidence = detail::child(content, 0, "evidence");
        if (c.method == Method::transcript && evidence && content.kind(*evidence) == json::Kind::array) {
            for (json::Index k = *evidence + 1; k < content.next(*evidence); k = content.next(k)) {
                if (kinds::detail::member(content, k, "type") == "transcript") {
                    ref = k;
                    break;
                }
            }
        }
    }
    if (ref && content.kind(*ref) == json::Kind::object) {
        c.message = kinds::detail::member(content, *ref, "message");
        c.digest = kinds::detail::member(content, *ref, "digest");
    }
    return c;
}

}  // namespace qiven3::entry::labels
