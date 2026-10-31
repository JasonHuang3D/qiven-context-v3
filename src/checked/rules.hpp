#pragma once

// checked/rules: cognition's rules over a store's write, as an admit hook
// (the store's contract: Admit, AdmitImport) the stores call at their
// places, and the ruled stores' default (checked/stores.hpp).
//
// The store runs its own guards over the keys the hook gives, whatever its
// hook (store/stage): the keys' bounds, forget_kind, forget_path, the
// chain at import, the order, the targets a forget erases. The hook,
// Rules: given, before the first line, at forget each line through the
// strict parser, a line it refuses EntryInvalid with the parser's code, a
// kind but forget the store's StoreError forget_kind, its detail naming
// the entry; then the call's hits' and entries' room. read, part 1
// (entry/check), a fault refused, the entry kept and the store's keys from
// its header (a content is a body). kept, once the store keeps the entry,
// at append the secret check: a header hit refused EntryInvalid secret
// with no id, the content's hits kept with their entry's
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
// At import, read runs part 1 of each line on the store's work arena for
// it, a fault refused, the entry held until release, and gives the store
// its keys from the header; they pass the store's bounds and chain as it
// arrives (arrived: nothing of the hook's). Once every line is in, settled
// parses each entry's line again (checked/reads' header_of) and takes its
// content from its body record; import_entry(view, header, content, index,
// at, allocator) reads it in the final view: part 2's other rules, target to
// map_evidence, where a revision whose target is forgotten, itself not, is
// forgotten_target; then a body held for a forgotten id forgotten_body; a
// content with no body, not forgotten, body_missing. same_heads (heads)
// compares the source's heads with those its entries give, in order:
// head_mismatch.
//
// A store's view gives bytes; the hook's readers take it through
// checked/reads' ParsedView, which parses each line and body record it
// gives, a line refused as header_of refuses it.
//
// check(platform, lines, at, allocator, scratch) and admit(view, scope,
// lane, expected, checked, limits, at, allocator) are the same steps with
// the rules, called apart: check is the store's read with Rules, its
// entries and hits returned; admit, over a view that gives them parsed,
// the hits settled, part 2, then the store's staged with
// its order, against the view's head of the call's lane.
//
// Refusals are EntryInvalid with the entry's index and its id once part 1
// has read the header through (part 2), never for secret; the field's path
// for a secret.
//
// Allocations. At forget, the line's value, four, on a scratch arena of
// its own. An entry's part 1 on a scratch arena, which the store's
// scratch() gives afresh over the same pages, given back before the next.
// At append, once the entry is kept, the secret check on one of its
// own: the content's value, four with a body, the secret check's 24 and its
// hits' room for a first hit. Each is given back before the next, so the
// scratch peaks at the greater of one entry's part 1 and its secret check.
// On the call's kept allocator, the entries' room, one, each entry's
// header, one, and body, one; the call's hits' room, one where it is
// short; all given back by release. admit: part 2's (validator's) and the
// view's, on its allocator, and at forget the members' room and a held
// header, one each where short, and that header's line, one. At import,
// read's part 1 on the line's
// work arena, held until release; settled's header parse and import_entry's
// (validator's try_final's and the view's) on its allocator. Platform calls:
// part 1's wipes and each kept body's key's, each noexcept. The rules log
// nothing.
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
#include "checked/part1.hpp"
#include "checked/reads.hpp"
#include "entry/body.hpp"
#include "entry/check.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/secret.hpp"
#include "entry/validator.hpp"
#include "keys/keys.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
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
using store::EntryView;
using store::Expected;
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

// What check admits: the store's read, the entries part 1 read, and the
// content hits in order.
template <platform::Platform P>
struct Checked : stage::Checked {
    Checked(stage::Checked&& read, Vector<Entry<P>>&& parsed, Vector<Hit>&& found) noexcept
        : stage::Checked(std::move(read)), entries(std::move(parsed)), hits(std::move(found)) {}
    Vector<Entry<P>> entries;
    Vector<Hit> hits;
};

// whole_record's refusal, its detail naming the forget, "entry <index>".
[[noreturn]] void refuse_whole(const AtView& at, std::size_t index);

namespace detail {

// forget_kind: the line through the strict parser, a line it refuses
// EntryInvalid with the parser's code, then its kind, a kind but forget
// the store's StoreError forget_kind.
inline void forget_kind(const AtView& at, std::size_t index, std::string_view line, const Chars& allocator) {
    canon::Basic<Chars> value(allocator);
    if (const std::optional<ParseCode> code = canon::try_parse(line, value)) {
        refuse_part1(at, index, {}, entry::check::detail::fault(entry::check::Part::parse, *code));
    }
    if (entry::kinds::detail::member(value, 0, "kind") != "forget") {
        stage::refuse_kind(at, index);
    }
}

// An entry part 1 read, kept on the call's allocator: its header made again
// from its fields, its id given, and its body from the record's parts.
template <platform::Platform P>
Entry<P> keep(P& platform, const Entry<P>& e, const EntryView& line, const Chars& allocator) {
    const header::Header<Chars>& h = e.header();
    header::FieldsView f;
    f.tag = h.value(Slot::tag);
    f.scope = h.value(Slot::scope);
    f.lane = h.value(Slot::lane);
    f.seq = h.seq();
    f.parent = h.text(Slot::parent);
    f.kind = h.value(Slot::kind);
    f.writer = h.value(Slot::writer);
    f.basis = h.value(Slot::basis);
    f.at = h.value(Slot::at);
    f.target = h.text(Slot::target);
    f.content = h.text(Slot::content);
    f.id = h.text(Slot::id);
    header::Header<Chars> kept = header::make(f, allocator);
    std::optional<entry::body::Body<P, Chars>> b;
    if (e.has_body() && line.body) {
        const entry::body::detail::RecordView parts = entry::body::detail::parts(*line.body);
        b.emplace(parts.content, parts.id, digest::key_from_hex(platform, parts.key), allocator);
    }
    return Entry<P>(std::move(kept), std::move(b));
}

// A header's keys for the store, views of it: a content is a body.
[[nodiscard]] inline keys::KeysView keys_of(const header::Header<Chars>& h) {
    return keys::KeysView{h.value(Slot::id),    h.value(Slot::scope),  h.value(Slot::lane),
                           h.value(Slot::kind),  h.seq(),               h.text(Slot::parent),
                           h.text(Slot::target), h.text(Slot::content).has_value()};
}

// The secret check of one admitted entry: a header hit refused with no id,
// the content's hits added to out.
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

// The hits settled, in order, the first not cleared refused with no id.
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
    Vector<keys::Id> members(allocator);
    Header found(allocator);
    for (std::size_t j = 0; j < entries.size(); ++j) {
        view.targets(target(j), members);
        for (const keys::Id& m : members) {
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

// The rules as an admit hook (Admit, AdmitImport) of a store on P: given,
// at forget each line's parse and kind first (forget_kind), then the hits'
// and the entries' room; read, part 1 of the line on a scratch arena, a
// fault refused, the entry kept on the call's allocator and its keys
// given from its header; kept, at append the secret check, its hits kept
// on the call's allocator; admit, the hits settled, part 2 and at forget
// the whole records, over the entries read; at import read, part 1 of the
// line on its work arena, the entry held until release and its keys given;
// arrived, nothing (the store's bounds and chain run first); settled, the
// line parsed again, then import_entry; heads, same_heads.
template <platform::Platform P>
class Rules {
public:
    template <class S>
    void given(const AtView& at, std::span<const EntryView> lines, const Chars& kept, S& scratch) {
        if (at.op == Operation::forget) {
            for (std::size_t i = 0; i < lines.size(); ++i) {
                detail::forget_kind(at, i, lines[i].line, Chars(scratch()));
            }
        }
        hits_.emplace(kept);
        entries_.emplace(kept);
        entries_->reserve(lines.size());
    }
    template <stage::Scratch S>
    void read(P& platform, const AtView& at, std::size_t index, const EntryView& line, keys::KeysView& out,
              const Chars& kept, S& scratch) {
        {
            const Entry<P> e = part1(platform, at, index, line, Chars(scratch()));
            entries_->push_back(detail::keep(platform, e, line, kept));
        }
        out = detail::keys_of(entries_->back().header());
    }
    // The secret check on a scratch of its own, part 1's given back.
    template <class S>
    void kept(const AtView& at, std::size_t index, const stage::KeptView& /*e*/, const Chars& /*kept*/, S& scratch) {
        if (at.op == Operation::append) {
            detail::secrets(at, index, (*entries_)[index], *hits_, Chars(scratch()));
        }
    }
    template <class V>
    void admit(V& bytes, std::string_view scope, std::string_view lane, Expected expected,
               std::span<const stage::KeptView> /*kept*/, const AtView& at, const Chars& allocator) {
        if (at.op != Operation::append && at.op != Operation::forget) {
            memory::fail_fast("stage::admit: an operation but append and forget");
        }
        reads::ParsedView<V> view(bytes, reads::WhereView{at.op, at.path});
        const std::span<const Entry<P>> entries = entries_ ? std::span<const Entry<P>>(*entries_)
                                                           : std::span<const Entry<P>>();
        if (hits_) {
            detail::settle(view, entries, std::span<const Hit>(*hits_), at, allocator);
        }
        detail::part2(view, scope, lane, expected, entries, at, allocator);
        if (at.op == Operation::forget) {
            detail::whole_record(view, entries, at, allocator);
        }
    }
    // At import: part 1 of the line on its work arena, a fault refused, the
    // entry held until release and the store's keys given from its header.
    void read(P& platform, const AtView& at, std::size_t index, const EntryView& line, keys::KeysView& out,
              const Chars& work) {
        line_.emplace(part1(platform, at, index, line, work));
        out = detail::keys_of(line_->header());
    }
    template <class V>
    static void arrived(V& /*view*/, const EntryView& /*line*/, std::size_t /*index*/,
                        const AtView& /*at*/) noexcept {}
    // The entry's line parsed again (its id the key's) and its content
    // from its record, then import_entry.
    template <class V>
    static void settled(V& bytes, const EntryView& line, std::size_t index, const AtView& at, const Chars& allocator) {
        const reads::WhereView where{at.op, at.path};
        const Header h = reads::header_of(line.keys.id, line.line, allocator, where);
        const std::optional<std::string_view> content =
            line.body ? std::optional<std::string_view>(entry::body::detail::parts(*line.body).content)
                      : std::nullopt;
        reads::ParsedView<V> view(bytes, where);
        import_entry(view, h, content, index, at, allocator);
    }
    static void heads(std::span<const LaneHead> source, std::span<const LaneHead> given, const AtView& at) {
        same_heads(source, given, at);
    }
    // The hits kept, given up.
    [[nodiscard]] Vector<Hit> take(const Chars& allocator) {
        return hits_ ? std::move(*hits_) : Vector<Hit>(allocator);
    }
    // The entries read, given up.
    [[nodiscard]] Vector<Entry<P>> take_entries(const Chars& allocator) {
        return entries_ ? std::move(*entries_) : Vector<Entry<P>>(allocator);
    }
    // The hits and the entries given back to the call's pages.
    void release() noexcept {
        hits_.reset();
        entries_.reset();
        line_.reset();
    }

private:
    std::optional<Vector<Hit>> hits_;
    std::optional<Vector<Entry<P>>> entries_;
    std::optional<Entry<P>> line_;  // an import's line in hand
};

// Part 1 of each line with the rules: the store's read, its entries and
// hits returned.
template <platform::Platform P, stage::Scratch S>
Checked<P> check(P& platform, std::span<const EntryView> lines, const AtView& at, const Chars& allocator,
                 S& scratch) {
    Rules<P> rules;
    stage::Checked read = stage::read(platform, lines, at, allocator, scratch, rules);
    Vector<Entry<P>> entries = rules.take_entries(allocator);
    return Checked<P>(std::move(read), std::move(entries), rules.take(allocator));
}

// The hits settled, part 2 and the maximum, inside the store's lock: the
// rules' admit, then the store's staged; a refusal thrown. view gives
// headers and contents parsed.
template <stage::View V, platform::Platform P>
    requires validator::View<V, Chars>
stage::Staged admit(V& view, std::string_view scope, std::string_view lane, Expected expected,
                    const Checked<P>& checked, const Limits& limits, const AtView& at, const Chars& allocator) {
    if (at.op != Operation::append && at.op != Operation::forget) {
        memory::fail_fast("stage::admit: an operation but append and forget");
    }
    const std::span<const Entry<P>> entries(checked.entries);
    detail::settle(view, entries, std::span<const Hit>(checked.hits), at, allocator);
    detail::part2(view, scope, lane, expected, entries, at, allocator);
    return stage::staged(view, static_cast<const stage::Checked&>(checked), limits, at, scope, lane,
                         view.head(scope, lane));
}

}  // namespace qiven3::checked
