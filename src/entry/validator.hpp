#pragma once

// validator: the shared validator's part 2, against a view. validate
// checks each entry of one call, one scope and lane, against the store's
// view and the call's earlier entries, both counting as held, entry by
// entry, each in this order, the first fault deciding:
// - parent: an entry of another scope or lane than the call's; the view's
//   head not the expected one; the first entry's parent not the expected
//   head (none for an empty lane), a later one's not the previous entry;
// - seq: not the parent's seq plus 1 (1 with no parent);
// - target: not held, or of another scope; a revision (an entry of a kind
//   but the change kinds, with a target) whose target has another kind; a
//   check or accept of a change kind; a retire of a retire or a forget; a
//   forget of a forget, or a second forget of one entry (the view's
//   forgotten set, or an earlier forget of the call);
// - at_order: an at earlier than the parent's, or not later than the
//   target's, each compared as text;
// - reference: an entry reference in the content (an evidence or source
//   item, or a map's object, of type entry) to an id not held, or of
//   another scope;
// - forgotten_target: a revision whose target's body is forgotten: the
//   view's forgotten set, or, for a target of the call, no body or an
//   earlier forget of it; a target whose body the view does not give;
// - identity: a revision of a kind with a record identity whose identity
//   is not its target's (kinds' identity, over each one's content);
// - map_evidence: a decision or check whose evidence names a map entry.
// The first fault is RuleError(code, index, id): the entry's index in the
// call and its id. Each entry has passed part 1 (check), the header's
// codes included. try_validate gives the same first fault as a value, so
// its vectors run in constant evaluation too.
//
// At import the rules split in two. try_chain reads one line as it
// arrives, before its insert: the chain rules alone, parent (not the
// view's head of its lane, none for an empty lane) and seq. try_final
// reads one entry once every header is in, against the final view, each
// held there, with its content or none: target, at_order, reference,
// forgotten_target, identity and map_evidence in that order, the first
// deciding; a parent the view does not hold is parent. In the final view a
// second forget is a forget that is not the first of its target, which
// the final view's forget_of names (the first forget held, in the order
// inserted); forgotten_target takes a revision whose target is forgotten
// when itself forgotten (a whole record), and refuses it otherwise; a
// revision whose content the view does not give (forgotten, or missing)
// is not read for identity. Its allocations are validate's for one entry,
// with no index.
//
// View is the store's view, which it gives inside its transaction or
// lock: the head of a lane; a header and a body's content by id, each
// into the caller's buffer, false when not held; whether an id is
// forgotten (a forget targets it); and, for a content hit's settling
// (stage's, not validate's), whether a scope's live clearings at a time
// clear a content digest for a pattern, and whether a header with a given
// content is held. An entry of the call is Appended: its header, whether
// it has a body, and the body's content bytes, as check's Entry gives.
//
// Allocations: validate's own: the call's index, one, 4 bytes an entry,
// for a call with entries; each entry with a body, its content's value,
// canon's four; a revision of a kind with a record identity, its target's
// content's value, canon's four. The view's reads fill validate's
// buffers, each as the view states (a copied header one, a text one where
// its capacity is short): the first entry's parent's header, each
// target's and each referenced entry's held in the view, and a revision's
// target's text there. Each is freed before validate returns; the peak
// stays within 256 KiB plus 8 times the call's contents and its
// revisions' targets' contents. A call past 2^32 - 2 entries is
// MemoryError budget before any. Platform calls: the view's alone. Each function is
// constexpr and a template on the allocator, so its vectors run in
// constant evaluation too; it reads by index, with no pointer arithmetic.
// validator logs nothing.
//
// thread: none; the view's.

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::entry::validator {

// The codes, in the order validate reads them.
enum class Code : std::uint8_t { parent, seq, target, at_order, reference, forgotten_target, identity, map_evidence };
inline constexpr std::array<std::string_view, 8> code_names = {
    "parent", "seq", "target", "at_order", "reference", "forgotten_target", "identity", "map_evidence"};
constexpr std::string_view name(Code code) noexcept { return code_names[static_cast<std::size_t>(code)]; }

// An id by value: sha256: and 64 hex, 71 characters; a longer text is
// kept to its first 71.
struct Id {
    static constexpr std::size_t capacity = 71;
    std::array<char, capacity> bytes{};
    std::uint8_t size = 0;

    static constexpr Id of(std::string_view text) noexcept {
        Id id;
        const std::size_t n = std::min(text.size(), capacity);
        for (std::size_t i = 0; i < n; ++i) {
            id.bytes.at(i) = text[i];
        }
        id.size = static_cast<std::uint8_t>(n);
        return id;
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view(bytes.data(), size);
    }
    constexpr bool operator==(const Id&) const = default;
};
static_assert(sizeof(Id) == 72, "Id's size is pinned: 71 characters and their count");

// A lane's head: its id and seq.
struct Head {
    Id id;
    std::int64_t seq = 0;
};
static_assert(sizeof(Head) == 80, "Head's size is pinned: an Id and seq");

// The first fault: its code, the entry's index in the call and its id.
struct RuleError {
    Code code = Code::parent;
    std::size_t index = 0;
    Id id;
    constexpr bool operator==(const RuleError&) const = default;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(RuleError) == 88, "RuleError's size is pinned: its code, the index and an Id");
#endif

template <typename A>
using Text = std::basic_string<char, std::char_traits<char>, A>;

// The store's view, over allocator A's buffers.
template <typename V, typename A>
concept View = requires(V& v, std::string_view s, header::Header<A>& h, Text<A>& out) {
    { v.head(s, s) } -> std::same_as<std::optional<Head>>;
    { v.header(s, h) } -> std::same_as<bool>;
    { v.body(s, out) } -> std::same_as<bool>;
    { v.forgotten(s) } -> std::same_as<bool>;
    { v.cleared(s, s, s, s) } -> std::same_as<bool>;
    { v.holds_content(s) } -> std::same_as<bool>;
};

// The final view of an import: the view, and the first forget held of an
// id, in the order inserted, or none.
template <typename V, typename A>
concept FinalView = View<V, A> && requires(V& v, std::string_view s) {
    { v.forget_of(s) } -> std::same_as<std::optional<Id>>;
};

// An entry of the call: its header, whether it has a body, and the body's
// content bytes.
template <typename E, typename A>
concept Appended = requires(const E& e) {
    { e.header() } -> std::same_as<const header::Header<A>&>;
    { e.has_body() } -> std::same_as<bool>;
    { e.body().content() } -> std::convertible_to<std::string_view>;
};

// The most entries one call takes.
inline constexpr std::size_t max_entries = UINT32_MAX - 1U;

namespace detail {

using header::Slot;

constexpr bool change(std::string_view kind) noexcept {
    return std::find(kinds::CHANGE_KINDS.begin(), kinds::CHANGE_KINDS.end(), kind) != kinds::CHANGE_KINDS.end();
}

// target's kind rules: whether an entry of kind may target one of
// target_kind (held, of its scope; a second forget is the caller's).
constexpr bool may_target(std::string_view kind, std::string_view target_kind) noexcept {
    if (!change(kind)) {
        return target_kind == kind;
    }
    if (kind == "check" || kind == "accept") {
        return !change(target_kind);
    }
    if (kind == "retire") {
        return target_kind != "retire" && target_kind != "forget";
    }
    return target_kind != "forget";
}

// Whether a kind has a record identity.
constexpr bool has_identity(std::string_view kind) noexcept {
    const std::optional<std::size_t> k = kinds::index_of(kind);
    return k.has_value() && kinds::KINDS[*k].identity != kinds::Holds::none;
}

// Whether a revision keeps its target's record identity.
template <typename A, typename B>
constexpr bool same_identity(const header::Header<A>& h, const canon::Basic<B>& content,
                             const header::Header<A>& target, const canon::Basic<B>& target_content) {
    return kinds::identity(h, content) == kinds::identity(target, target_content);
}

// The id of an object of type entry at node i, or none.
template <typename B>
constexpr std::optional<std::string_view> entry_reference(const canon::Basic<B>& v, json::Index i) {
    if (v.kind(i) != json::Kind::object || kinds::detail::member(v, i, "type") != "entry") {
        return std::nullopt;
    }
    return kinds::detail::member(v, i, "id");
}

// The content's entry references in its keys' order (evidence, a map's
// object, source), each given to f(id, in_evidence) until f returns a
// code.
template <typename B, typename F>
constexpr std::optional<Code> each_reference(const canon::Basic<B>& v, std::string_view kind, const F& f) {
    if (v.empty() || v.kind(0) != json::Kind::object) {
        return std::nullopt;
    }
    const auto list = [&](std::string_view key, bool evidence) -> std::optional<Code> {
        const std::optional<json::Index> a = v.find(0, key);
        if (!a || v.kind(*a) != json::Kind::array) {
            return std::nullopt;
        }
        for (json::Index k = *a + 1; k < v.next(*a); k = v.next(k)) {
            if (const std::optional<std::string_view> id = entry_reference(v, k)) {
                if (const std::optional<Code> c = f(*id, evidence)) {
                    return c;
                }
            }
        }
        return std::nullopt;
    };
    if (const std::optional<Code> c = list("evidence", true)) {
        return c;
    }
    if (kind == "map") {
        if (const std::optional<json::Index> o = v.find(0, "object")) {
            if (const std::optional<std::string_view> id = entry_reference(v, *o)) {
                if (const std::optional<Code> c = f(*id, false)) {
                    return c;
                }
            }
        }
    }
    return list("source", false);
}

[[noreturn]] inline void too_large(std::size_t entries) {
    throw memory::MemoryError::budget("validator", max_entries * sizeof(std::uint32_t),
                                      memory::mul(entries, sizeof(std::uint32_t)).value_or(SIZE_MAX), 0);
}

// One entry of the final view as Appended reads it: its header, and its
// content or none.
template <typename A>
struct FinalEntryView {
    const header::Header<A>* head = nullptr;
    std::optional<std::string_view> text;

    [[nodiscard]] constexpr const header::Header<A>& header() const noexcept { return *head; }
    [[nodiscard]] constexpr bool has_body() const noexcept { return text.has_value(); }
    [[nodiscard]] constexpr const FinalEntryView& body() const noexcept { return *this; }
    [[nodiscard]] constexpr std::string_view content() const noexcept { return text.value_or(std::string_view()); }
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(FinalEntryView<std::allocator<char>>) == 32,
              "FinalEntryView's size is pinned: a header's address and a content or none");
#endif

// One call's work: the view, the entries, the index of their ids, and the
// buffers the view fills. It borrows the view and the entries, made and
// dropped within validate's call. Final: one entry of an import's final
// view, the chain rules not read, and no index.
template <typename V, typename E, typename A, bool Final = false>
class CallView {
public:
    using H = header::Header<A>;
    using Rebound = typename std::allocator_traits<A>::template rebind_alloc<std::uint32_t>;

    constexpr CallView(V& view, std::span<const E> entries, const A& allocator)
        : view_(&view), entries_(entries), allocator_(allocator), index_(Rebound(allocator)), parent_(allocator),
          target_(allocator), reference_(allocator), text_(allocator) {
        if constexpr (!Final) {
            const std::size_t n = entries.size();
            if (n > max_entries || !memory::mul(n, sizeof(std::uint32_t))) {
                too_large(n);
            }
            index_.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                index_[i] = static_cast<std::uint32_t>(i);
                if (kind(i) == "forget") {
                    any_forget_ = true;
                }
            }
            std::sort(index_.begin(), index_.end(),
                      [this](std::uint32_t a, std::uint32_t b) { return id(a) < id(b); });
        }
    }

    [[nodiscard]] constexpr std::string_view id(std::size_t i) const {
        return entries_[i].header().value(Slot::id);
    }
    [[nodiscard]] constexpr std::string_view kind(std::size_t i) const {
        return entries_[i].header().value(Slot::kind);
    }

    // The call's entry of an id before entry i, or none.
    [[nodiscard]] constexpr std::optional<std::size_t> earlier(std::string_view of, std::size_t i) const {
        const auto at = std::lower_bound(index_.begin(), index_.end(), of,
                                         [this](std::uint32_t a, std::string_view b) { return id(a) < b; });
        if (at == index_.end() || id(*at) != of || *at >= i) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(*at);
    }
    // A held header of an id: the call's before entry i, else the view's
    // into buffer; none when not held.
    constexpr const H* held(std::string_view of, std::size_t i, H& buffer) {
        if (const std::optional<std::size_t> j = earlier(of, i)) {
            return &entries_[*j].header();
        }
        return view_->header(of, buffer) ? &buffer : nullptr;
    }
    // Whether a forget of the call before entry i targets an id.
    [[nodiscard]] constexpr bool forgot_earlier(std::string_view of, std::size_t i) const {
        if (!any_forget_) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (kind(j) == "forget" && entries_[j].header().value(Slot::target) == of) {
                return true;
            }
        }
        return false;
    }

    constexpr std::optional<Code> check(std::size_t i, std::string_view scope, std::string_view lane,
                                        const std::optional<Head>& head, std::optional<std::string_view> expected);

private:
    V* view_;
    std::span<const E> entries_;
    A allocator_;
    std::vector<std::uint32_t, Rebound> index_;
    H parent_;
    H target_;
    H reference_;
    Text<A> text_;
    bool any_forget_ = false;
};

template <typename V, typename E, typename A, bool Final>
constexpr std::optional<Code> CallView<V, E, A, Final>::check(std::size_t i, std::string_view scope, std::string_view lane,
                                                   const std::optional<Head>& head,
                                                   std::optional<std::string_view> expected) {
    const H& h = entries_[i].header();
    const std::string_view k = h.value(Slot::kind);
    const std::string_view at = h.value(Slot::at);
    // parent and seq, and the parent's at.
    std::string_view parent_at;
    if (h.value(Slot::scope) != scope || h.value(Slot::lane) != lane) {
        return Code::parent;
    }
    if constexpr (Final) {
        // The chain rules passed at the line's arrival; its parent's at.
        if (const std::optional<std::string_view> named = h.text(Slot::parent)) {
            const H* p = held(*named, i, parent_);
            if (p == nullptr) {
                return Code::parent;
            }
            parent_at = p->value(Slot::at);
        }
    } else if (i == 0) {
        const std::optional<std::string_view> now =
            head ? std::optional<std::string_view>(head->id.view()) : std::nullopt;
        const std::optional<std::string_view> named = h.text(Slot::parent);
        if (now != expected || named != expected) {
            return Code::parent;
        }
        if (expected) {
            const H* p = held(*expected, 0, parent_);
            if (p == nullptr) {
                return Code::parent;
            }
            parent_at = p->value(Slot::at);
        }
        if (h.seq() < 1 || h.seq() - 1 != (head ? head->seq : 0)) {
            return Code::seq;
        }
    } else {
        const H& p = entries_[i - 1].header();
        if (h.text(Slot::parent) != std::optional<std::string_view>(p.value(Slot::id))) {
            return Code::parent;
        }
        if (h.seq() < 1 || h.seq() - 1 != p.seq()) {
            return Code::seq;
        }
        parent_at = p.value(Slot::at);
    }
    // target.
    const std::optional<std::string_view> t = h.text(Slot::target);
    const H* target = nullptr;
    std::optional<std::size_t> in_call;
    if (t) {
        in_call = earlier(*t, i);
        target = held(*t, i, target_);
        if (target == nullptr || target->value(Slot::scope) != scope ||
            !may_target(k, target->value(Slot::kind))) {
            return Code::target;
        }
        if constexpr (Final) {
            if (k == "forget") {
                const std::optional<Id> first = view_->forget_of(*t);
                if (!first || first->view() != h.value(Slot::id)) {
                    return Code::target;
                }
            }
        } else if (k == "forget" && ((!in_call && view_->forgotten(*t)) || forgot_earlier(*t, i))) {
            return Code::target;
        }
    }
    // at_order.
    if ((!parent_at.empty() && at < parent_at) || (target != nullptr && at <= target->value(Slot::at))) {
        return Code::at_order;
    }
    // reference, noting whether evidence names a map.
    canon::Basic<A> value(allocator_);
    bool cites_map = false;
    if (entries_[i].has_body()) {
        canon::parse(std::string_view(entries_[i].body().content()), value);
        const auto reference = [&](std::string_view of, bool evidence) -> std::optional<Code> {
            const H* r = held(of, i, reference_);
            if (r == nullptr || r->value(Slot::scope) != scope) {
                return Code::reference;
            }
            cites_map = cites_map || (evidence && r->value(Slot::kind) == "map");
            return std::nullopt;
        };
        if (const std::optional<Code> c = each_reference(value, k, reference)) {
            return c;
        }
    }
    // forgotten_target and identity, on a revision.
    if (t && target != nullptr && !change(k)) {
        const bool forgotten = in_call ? !entries_[*in_call].has_body() || forgot_earlier(*t, i)
                                       : view_->forgotten(*t);
        bool read = true;
        if constexpr (Final) {
            // A whole record stands; a content not given is not read.
            if (forgotten && !view_->forgotten(h.value(Slot::id))) {
                return Code::forgotten_target;
            }
            read = !forgotten && !(h.text(Slot::content) && !entries_[i].has_body());
        } else if (forgotten) {
            return Code::forgotten_target;
        }
        if (read && has_identity(k)) {
            canon::Basic<A> target_value(allocator_);
            if (in_call) {
                canon::parse(std::string_view(entries_[*in_call].body().content()), target_value);
            } else {
                if (!view_->body(*t, text_)) {
                    return Code::forgotten_target;
                }
                canon::parse(std::string_view(text_), target_value);
            }
            if (!same_identity(h, value, *target, target_value)) {
                return Code::identity;
            }
        }
    }
    // map_evidence.
    if ((k == "decision" || k == "check") && cites_map) {
        return Code::map_evidence;
    }
    return std::nullopt;
}

}  // namespace detail

// The first fault of a call, or none.
template <typename V, typename E, typename A = std::allocator<char>>
    requires View<V, A> && Appended<E, A>
constexpr std::optional<RuleError> try_validate(V& view, std::string_view scope, std::string_view lane,
                                                std::optional<std::string_view> expected, std::span<const E> entries,
                                                const A& allocator = A()) {
    if (entries.empty()) {
        return std::nullopt;
    }
    detail::CallView<V, E, A> call(view, entries, allocator);
    const std::optional<Head> head = view.head(scope, lane);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (const std::optional<Code> c = call.check(i, scope, lane, head, expected)) {
            return RuleError{*c, i, Id::of(call.id(i))};
        }
    }
    return std::nullopt;
}

// A call's entries validated against the view; the first fault thrown as
// RuleError.
template <typename V, typename E, typename A = std::allocator<char>>
    requires View<V, A> && Appended<E, A>
constexpr void validate(V& view, std::string_view scope, std::string_view lane,
                        std::optional<std::string_view> expected, std::span<const E> entries,
                        const A& allocator = A()) {
    if (const std::optional<RuleError> e = try_validate(view, scope, lane, expected, entries, allocator)) {
        throw *e;
    }
}

// At import, a line as it arrives, before its insert: the chain rules
// alone against the view's head of its lane, the first fault or none.
template <typename V, typename A>
    requires View<V, A>
constexpr std::optional<Code> try_chain(V& view, const header::Header<A>& h) {
    using header::Slot;
    const std::optional<Head> head = view.head(h.value(Slot::scope), h.value(Slot::lane));
    const std::optional<std::string_view> now = head ? std::optional<std::string_view>(head->id.view()) : std::nullopt;
    if (h.text(Slot::parent) != now) {
        return Code::parent;
    }
    if (h.seq() < 1 || h.seq() - 1 != (head ? head->seq : 0)) {
        return Code::seq;
    }
    return std::nullopt;
}

// At import, one entry once every header is in, held in the final view
// with its content or none: part 2's other rules, the first fault or none.
template <typename V, typename A>
    requires FinalView<V, A>
constexpr std::optional<Code> try_final(V& view, const header::Header<A>& h, std::optional<std::string_view> content,
                                        const A& allocator = A()) {
    using header::Slot;
    const std::array<detail::FinalEntryView<A>, 1> one = {detail::FinalEntryView<A>{&h, content}};
    detail::CallView<V, detail::FinalEntryView<A>, A, true> call(
        view, std::span<const detail::FinalEntryView<A>>(one), allocator);
    return call.check(0, h.value(Slot::scope), h.value(Slot::lane), std::nullopt, std::nullopt);
}

}  // namespace qiven3::entry::validator
