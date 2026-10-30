#pragma once

// checked/rules: cognition's rules over a store's write, as an admit hook
// (the store's contract: Admit, AdmitImport) the stores call at their
// places, and the ruled stores' default (checked/stores.hpp).
//
// The store runs its own guards first, whatever its hook (store/stage):
// forget_kind, forget_path, the chain at import, the order, the targets a
// forget erases. The hook, Rules: given, before the first line, the call's
// hits' room; read nothing. kept, once the entry is kept, at append the
// secret check: a header hit refused EntryInvalid secret, the content's hits kept with their entry's
// index. admit, inside the store's transaction or lock, against its view:
// at append the hits settled first, in order, the first not cleared
// EntryInvalid secret: a hit passes when a live clearing of the entry's
// scope clears its pattern for the entry's content at the entry's at
// (checked/clearing, over the view's generic calls), unused, no header
// held but the entry's own, the call's earlier ones
// included, having that content. Then part 2 (validator), its first fault
// EntryInvalid with its code. At forget then the whole records: a record
// whose target is a target of the call, in any scope, not forgotten, not
// itself a target, and of a kind but the change kinds (CHANGE_KINDS) is
// StoreError whole_record, "entry <j>" naming the first forget of that
// target. The store's own step follows (store/stage).
//
// At import, each line after part 1 passes the store's chain as it
// arrives (arrived: nothing of the hook's). Once every header
// is in, import_entry(view, header, content, index, at, allocator) reads
// each entry in the final view (settled): part 2's other rules, target to
// map_evidence, where a revision whose target is forgotten, itself not, is
// forgotten_target; then a body held for a forgotten id forgotten_body; a
// content with no body, not forgotten, body_missing. same_heads (heads)
// compares the source's heads with those its entries give, in order:
// head_mismatch.
//
// check(platform, lines, at, allocator, scratch) and admit(view, scope,
// lane, expected, checked, limits, at, allocator) are the same steps with
// the rules, called apart: check is the store's read with Rules, its hits
// returned; admit the hits settled, part 2, then the store's staged with
// its order, against the view's head of the call's lane.
//
// Refusals are EntryInvalid with the entry's index and its id once part 1
// has read the header through (part 2), never for secret; the field's path
// for a secret.
//
// Allocations. At append, once the entry is kept, the secret check on one of its
// own: the content's value, four with a body, the secret check's 24 and its
// hits' room for a first hit. Each is given back before the next, so the
// scratch peaks at the greater of one entry's part 1 and its secret check.
// On the call's kept allocator, the call's hits' room, one where it is
// short, given back by release. admit: part 2's (validator's) and the
// view's, on its allocator, and at forget the members' room and a held
// header, one each where short; import_entry
// validator's try_final's and the view's, on its allocator. No platform
// call. The rules log nothing.
//
// thread: none; the entries, one (their keys' platform's).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "checked/clearing.hpp"
#include "entry/check.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/secret.hpp"
#include "entry/validator.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "store/contract.hpp"
#include "store/stage.hpp"

namespace qiven3::checked {

namespace stage = store::stage;
namespace header = entry::header;
namespace secret = entry::secret;
namespace validator = entry::validator;
using Slot = header::Slot;
using Chars = memory::Allocator<char>;
using stage::AtView;
using stage::Entry;
using store::EntryView;
using store::Expected;
using store::Header;
using store::LaneHead;
using store::Limits;
using store::Operation;
using store::Vector;

// A content hit: its entry's index in the call, its pattern and path.
struct Hit {
    std::uint32_t index = 0;
    secret::Hit hit;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(Hit) == 120, "Hit's size is pinned: an index and secret's hit");
#endif

// What check admits: the store's read, and the content hits in order.
template <platform::Platform P>
struct Checked : stage::Checked<P> {
    Checked(stage::Checked<P>&& read, Vector<Hit>&& found) noexcept
        : stage::Checked<P>(std::move(read)), hits(std::move(found)) {}
    Vector<Hit> hits;
};

// whole_record's refusal, its detail naming the forget, "entry <index>".
[[noreturn]] void refuse_whole(const AtView& at, std::size_t index);

namespace detail {

// The secret check of one admitted entry: a header hit refused, the
// content's hits added to out.
template <platform::Platform P>
void secrets(const AtView& at, std::size_t index, const Entry<P>& e, Vector<Hit>& out, const Chars& scratch) {
    canon::Basic<Chars> value(scratch);
    if (e.has_body()) {
        canon::parse(std::string_view(e.body().content()), value);
    }
    secret::Hits<Chars> found(scratch);
    if (const std::optional<secret::SecretError> s = secret::try_check(e.header(), value, found)) {
        stage::refuse(at, index, {}, "secret", s->path.view(), secret::name(s->pattern));
    }
    if (found.empty()) {
        return;
    }
    const std::optional<std::size_t> room = memory::add(out.size(), found.size());
    if (!room) {
        stage::too_large(SIZE_MAX);
    }
    if (out.capacity() < *room) {
        out.reserve(*room);
    }
    for (const secret::Hit& h : found) {
        out.push_back(Hit{static_cast<std::uint32_t>(index), h});
    }
}

// Whether entry i's hit is cleared (checked/clearing), unused.
template <class V, platform::Platform P>
bool cleared(V& view, std::span<const Entry<P>> entries, std::size_t i, std::string_view pattern, const AtView& at,
             const Chars& allocator) {
    const header::Header<Chars>& h = entries[i].header();
    const std::optional<std::string_view> content = h.text(Slot::content);
    if (!content ||
        !clearing::cleared(view, h.value(Slot::scope), *content, pattern, h.value(Slot::at), at, allocator)) {
        return false;
    }
    for (std::size_t j = 0; j < i; ++j) {
        if (entries[j].header().text(Slot::content) == content) {
            return false;
        }
    }
    return !clearing::held(view, *content, allocator);
}

// The hits settled, in order, the first not cleared refused.
template <class V, platform::Platform P>
void settle(V& view, std::span<const Entry<P>> entries, std::span<const Hit> hits, const AtView& at,
            const Chars& allocator) {
    for (const Hit& hit : hits) {
        const std::string_view pattern = secret::name(hit.hit.pattern);
        if (!cleared(view, entries, hit.index, pattern, at, allocator)) {
            stage::refuse(at, hit.index, {}, "secret", hit.hit.path.view(), pattern);
        }
    }
}

// Part 2 (validator's), its first fault refused.
template <class V, platform::Platform P>
void part2(V& view, std::string_view scope, std::string_view lane, Expected expected,
           std::span<const Entry<P>> entries, const AtView& at, const Chars& allocator) {
    if (const std::optional<validator::RuleError> e =
            validator::try_validate(view, scope, lane, expected, entries, allocator)) {
        stage::refuse(at, e->index, e->id.view(), validator::name(e->code));
    }
}

// A change kind's: a record of it stays whole when its target is forgotten.
[[nodiscard]] constexpr bool change(std::string_view kind) noexcept {
    return std::ranges::find(entry::kinds::CHANGE_KINDS, kind) != entry::kinds::CHANGE_KINDS.end();
}

// At forget, the records each target keeps whole: a record whose target is
// a target of the call (in any scope, in the view's order), not forgotten,
// not itself a target, and of a kind but the change kinds refused
// StoreError whole_record naming the first forget of that target.
template <class V, platform::Platform P>
void whole_record(V& view, std::span<const Entry<P>> entries, const AtView& at, const Chars& allocator) {
    const auto target = [&entries](std::size_t j) {
        return entries[j].header().text(Slot::target).value_or(std::string_view());
    };
    Vector<store::Id> members(allocator);
    Header found(allocator);
    for (std::size_t j = 0; j < entries.size(); ++j) {
        view.targets(target(j), members);
        for (const store::Id& m : members) {
            bool in_set = false;
            for (std::size_t k = 0; k < entries.size() && !in_set; ++k) {
                in_set = target(k) == m.view();
            }
            if (in_set || view.forgotten(m.view())) {
                continue;
            }
            if (view.header(m.view(), found) && !change(found.value(Slot::kind))) {
                refuse_whole(at, j);
            }
        }
    }
}

}  // namespace detail

// One entry of an import in its final view, held there with its content
// or none: part 2's other rules (validator's), forgotten_target among
// them, then forgotten_body, then body_missing; a fault refused.
template <class V>
    requires validator::FinalView<V, Chars>
void import_entry(V& view, const Header& h, std::optional<std::string_view> content, std::size_t index,
                  const AtView& at, const Chars& allocator) {
    const std::string_view id = h.value(Slot::id);
    if (const std::optional<validator::Code> c = validator::try_final(view, h, content, allocator)) {
        stage::refuse(at, index, id, validator::name(*c));
    }
    const bool forgotten = view.forgotten(id);
    if (content && forgotten) {
        stage::refuse(at, index, id, "forgotten_body");
    }
    if (!content && !forgotten && h.text(Slot::content)) {
        stage::refuse(at, index, id, "body_missing");
    }
}

// The source's heads against those its entries give, each in (scope,
// lane) order: the first that differs, or a count that does, refused
// head_mismatch, its index the source's and its id the source head's.
void same_heads(std::span<const LaneHead> source, std::span<const LaneHead> given, const AtView& at);

// The rules as an admit hook (Admit, AdmitImport): given, the hits' room;
// read, nothing (the store's guards run there); kept, at append the secret
// check, its hits kept on the call's allocator; admit, the hits settled,
// part 2 and at forget the whole records; arrived, nothing (the store's chain runs first); settled,
// import_entry; heads, same_heads.
class Rules {
public:
    template <class S>
    void given(const AtView& /*at*/, std::span<const EntryView> /*lines*/, const Chars& kept, S& /*scratch*/) {
        hits_.emplace(kept);
    }
    static void read(const AtView& /*at*/, std::size_t /*index*/, const header::Header<Chars>& /*h*/) noexcept {}
    // The secret check on a scratch of its own, part 1's given back.
    template <platform::Platform P, class S>
    void kept(const AtView& at, std::size_t index, const Entry<P>& e, const Chars& /*kept*/, S& scratch) {
        if (at.op == Operation::append) {
            detail::secrets(at, index, e, *hits_, Chars(scratch()));
        }
    }
    template <class V, platform::Platform P>
    void admit(V& view, std::string_view scope, std::string_view lane, Expected expected,
               std::span<const Entry<P>> entries, const AtView& at, const Chars& allocator) {
        if (at.op != Operation::append && at.op != Operation::forget) {
            memory::fail_fast("stage::admit: an operation but append and forget");
        }
        if (hits_) {
            detail::settle(view, entries, std::span<const Hit>(*hits_), at, allocator);
        }
        detail::part2(view, scope, lane, expected, entries, at, allocator);
        if (at.op == Operation::forget) {
            detail::whole_record(view, entries, at, allocator);
        }
    }
    template <class V>
    static void arrived(V& /*view*/, const Header& /*h*/, std::size_t /*index*/, const AtView& /*at*/) noexcept {}
    template <class V>
    static void settled(V& view, const Header& h, std::optional<std::string_view> content, std::size_t index,
                        const AtView& at, const Chars& allocator) {
        import_entry(view, h, content, index, at, allocator);
    }
    static void heads(std::span<const LaneHead> source, std::span<const LaneHead> given, const AtView& at) {
        same_heads(source, given, at);
    }
    // The hits kept, given up.
    [[nodiscard]] Vector<Hit> take(const Chars& allocator) {
        return hits_ ? std::move(*hits_) : Vector<Hit>(allocator);
    }
    // The hits given back to the call's pages.
    void release() noexcept { hits_.reset(); }

private:
    std::optional<Vector<Hit>> hits_;
};

// Part 1 of each line with the rules: the store's read, its hits returned.
template <platform::Platform P, stage::Scratch S>
Checked<P> check(P& platform, std::span<const EntryView> lines, const AtView& at, const Chars& allocator,
                 S& scratch) {
    Rules rules;
    stage::Checked<P> read = stage::read(platform, lines, at, allocator, scratch, rules);
    return Checked<P>(std::move(read), rules.take(allocator));
}

// The hits settled, part 2 and the maximum, inside the store's lock: the
// rules' admit, then the store's staged; a refusal thrown.
template <stage::View V, platform::Platform P>
stage::Staged admit(V& view, std::string_view scope, std::string_view lane, Expected expected,
                    const Checked<P>& checked, const Limits& limits, const AtView& at, const Chars& allocator) {
    if (at.op != Operation::append && at.op != Operation::forget) {
        memory::fail_fast("stage::admit: an operation but append and forget");
    }
    const std::span<const Entry<P>> entries(checked.entries);
    detail::settle(view, entries, std::span<const Hit>(checked.hits), at, allocator);
    detail::part2(view, scope, lane, expected, entries, at, allocator);
    return stage::staged(view, static_cast<const stage::Checked<P>&>(checked), limits, at, scope, lane,
                         view.head(scope, lane));
}

}  // namespace qiven3::checked
