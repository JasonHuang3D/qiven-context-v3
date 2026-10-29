#pragma once

// write/refuse: the write command's refusals against the fold, product.
// write takes a session's file read (write/entries), its entries built
// (write/build) and the store's fold, and refuses the whole file with a
// Refusal, its code, the field's path, the entry's index and name, or
// returns. In this order, the first that holds deciding:
// - slot, at writer: a writer import, owner, loop or another reserved
//   role, a role not among roles, or an n below the n of the writer of its
//   lane's head in the file's scope;
// - person_scope, at scope: an entry of scope person or life;
// then each entry in the file's order:
// - owner_entry: its target a version of an owner's entry (a retire, a
//   check, an accept, a revision), at target; a closes item naming one, at
//   content.closes[k]; a relation serving one as replaced, retired or
//   refuted (from it with a by_slugs relation, to it with a to_slugs one),
//   it named by entry:<id> of a version of it or by a subject one carries,
//   at content.from or content.to;
// - owner_words, at basis: its basis owner-words;
// - kind, at kind: a kind given to import alone or to neither (kinds);
// - refs_item, at content.refs[0] (content.refs when it has no item): a
//   checkpoint whose first refs item is no version of an item record (an
//   @item:, an @name: of an item entry of the file, or an id the fold
//   holds as an item);
// - cycle, at content.under: an item, the file's last of its item_id, whose
//   under reaches it again among the current items, the file's last of
//   each item_id first, then the fold's.
// An owner's entry is the fold's to tell: any version of a record whose
// root or any version has basis owner-words, the goal record, and every
// entry of scope person. A hole of refs or closes names a version of the
// file's own entries or an item, lane's checkpoint or question, so no
// owner's entry (the file's own are refused before); the review's imported
// decisions and claims, basis claimed, are no owner's entries and get no
// guard here. The reader refuses form and invalid, the builder form and
// invalid against the fold, the store's append secret, head_conflict, busy
// and store_full.
//
// Fold is the caller's, for one scope at a call: writer(scope, lane), the
// writer of the lane's head; kind(scope, id), the kind of an entry held;
// owner(scope, id), whether id is a version of an owner's entry;
// owner_subject(scope, subject), whether an owner's entry carries subject;
// under(scope, item_id), the under of that item record's current version.
// Each gives its views for the call alone; refuse copies them at once.
//
// Allocations: none; a Refusal is thrown by value. Platform calls: none.
// Fold queries: writer once; per entry owner once on a target, once a
// closes item, twice on a relation's named subject, kind once on a
// checkpoint's literal first refs item; on an item the file's last of its
// item_id with an under, at most three under a round of the walk, its
// rounds at most the items it reaches, each of the three first scanning the
// file's entries for that item. It logs nothing.
//
// thread: one (the File's, the built entries' and the fold's)

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "entry/header.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "write/build.hpp"
#include "write/entries.hpp"

namespace qiven3::write::refuse {

// Every refusal code of the entries' commands, one list: the reader's and
// the builder's (form, invalid), the store's append (secret, head_conflict,
// busy, store_full), this module's (owner_words to cycle), the later file's
// and the kept files' (not_empty, identity, corrupt).
enum class Code : std::uint8_t {
    form, invalid, secret, owner_words, person_scope, owner_entry, slot, kind, refs_item, cycle,
    head_conflict, busy, store_full, not_empty, identity, corrupt
};
std::string_view name(Code code) noexcept;

// The refusal, thrown by value, allocating nothing: entry none for the
// file's own fields.
struct Refusal {
    Code code = Code::slot;
    memory::Inline<128> path{};
    std::uint32_t entry = entries::none;
    memory::Inline<64> name{};
};

// The roles a session's slot may take [proposal: a new role is added here
// first].
inline constexpr std::array<std::string_view, 2> roles = {"reviewer", "implementer"};
// The kinds a session may not write: import's alone, or neither command's.
inline constexpr std::array<std::string_view, 6> kinds = {"goal", "clause", "map", "module", "episode", "usage"};
// The relations that serve their from as replaced, retired or refuted, and
// those that serve their to so.
inline constexpr std::array<std::string_view, 4> by_slugs = {"replaced-by", "superseded-by", "retired-by",
                                                             "refuted-by"};
inline constexpr std::array<std::string_view, 4> to_slugs = {"replaces", "supersedes", "retires", "refutes"};

template <class F>
concept Fold = requires(F& f, std::string_view s) {
    { f.writer(s, s) } -> std::same_as<std::optional<std::string_view>>;
    { f.kind(s, s) } -> std::same_as<std::optional<std::string_view>>;
    { f.owner(s, s) } -> std::same_as<bool>;
    { f.owner_subject(s, s) } -> std::same_as<bool>;
    { f.under(s, s) } -> std::same_as<std::optional<std::string_view>>;
};

namespace detail {

// An item id held across the fold's calls.
using Id = memory::Inline<64>;

// The refusal at the file's member, or at entries[e]'s member, below it
// field and below that the item'th of field's array (none for none).
[[noreturn]] void at_file(Code code, std::string_view member);
[[noreturn]] void at_entry(Code code, const entries::File& file, std::size_t e, std::string_view member,
                           std::string_view field = {}, std::size_t item = entries::none);
// The string member key of entry e's content, or none; its array member.
std::optional<std::string_view> text(const entries::File& file, std::size_t e, std::string_view key);
std::optional<json::Index> array(const entries::File& file, std::size_t e, std::string_view key);
// The site at node among entry e's holes, or none.
const entries::Site* site(const entries::File& file, std::size_t e, json::Index node);
// The writer's slot number n, or none off <role>/s<n>.
std::optional<std::uint64_t> slot_number(std::string_view writer) noexcept;
// The index of the file's last item entry of item_id, or none.
std::optional<std::size_t> last_item(const entries::File& file, std::string_view item_id);

template <Fold F>
bool names_owner(F& fold, std::string_view scope, std::string_view subject) {
    constexpr std::string_view entry_type = "entry:";
    return (subject.starts_with(entry_type) && fold.owner(scope, subject.substr(entry_type.size()))) ||
           fold.owner_subject(scope, subject);
}

// The under of item_id among the current items: the file's last first.
template <Fold F>
std::optional<Id> under(const entries::File& file, F& fold, std::string_view scope, std::string_view item_id) {
    if (const std::optional<std::size_t> j = last_item(file, item_id)) {
        const std::optional<std::string_view> u = text(file, *j, "under");
        return u ? std::optional<Id>(Id(*u)) : std::nullopt;
    }
    const std::optional<std::string_view> u = fold.under(scope, item_id);
    return u ? std::optional<Id>(Id(*u)) : std::nullopt;
}

}  // namespace detail

// slot, at writer.
template <Fold F>
void slot(const entries::File& file, F& fold) {
    namespace header = entry::header;
    const std::string_view writer = file.writer();
    const std::string_view role = header::role(writer);
    const std::optional<std::uint64_t> n = detail::slot_number(writer);
    if (file.import() || !n || !header::among(roles, role)) {
        detail::at_file(Code::slot, "writer");
    }
    if (const std::optional<std::string_view> head = fold.writer(file.scope(), role)) {
        if (const std::optional<std::uint64_t> m = detail::slot_number(*head); m && *n < *m) {
            detail::at_file(Code::slot, "writer");
        }
    }
}

// person_scope, at scope.
inline void person_scope(const entries::File& file) {
    for (const entries::Entry& e : file.entries()) {
        const std::string_view scope = file.text(e.scope);
        if (scope == "person" || scope == "life") {
            detail::at_file(Code::person_scope, "scope");
        }
    }
}

// owner_entry, for entry e built as b.
template <Fold F>
void owner_entry(const entries::File& file, const build::Built& b, F& fold, std::size_t e) {
    const std::string_view scope = b.scope.view();
    if (!b.target.view().empty() && fold.owner(scope, b.target.view())) {
        detail::at_entry(Code::owner_entry, file, e, "target");
    }
    const json::Value& v = file.value();
    if (const std::optional<json::Index> closes = detail::array(file, e, "closes")) {
        std::size_t k = 0;
        for (json::Index n = *closes + 1; n < v.next(*closes); n = v.next(n), ++k) {
            if (!v.text(n).starts_with('@') && fold.owner(scope, v.text(n))) {
                detail::at_entry(Code::owner_entry, file, e, "content", "closes", k);
            }
        }
    }
    if (file.text(file.entries()[e].kind) != "relation") {
        return;
    }
    const std::string_view slug = detail::text(file, e, "relation").value_or(std::string_view{});
    const std::string_view side = entry::header::among(by_slugs, slug)   ? "from"
                                  : entry::header::among(to_slugs, slug) ? "to"
                                                                         : std::string_view{};
    if (!side.empty()) {
        const std::optional<std::string_view> subject = detail::text(file, e, side);
        if (subject && detail::names_owner(fold, scope, *subject)) {
            detail::at_entry(Code::owner_entry, file, e, "content", side);
        }
    }
}

// refs_item, for entry e; built holds the file's entries built, in order.
template <Fold F>
void refs_item(const entries::File& file, std::span<const build::Built> built, F& fold, std::size_t e) {
    if (file.text(file.entries()[e].kind) != "checkpoint") {
        return;
    }
    const json::Value& v = file.value();
    const std::optional<json::Index> refs = detail::array(file, e, "refs");
    if (!refs || v.count(*refs) == 0) {
        detail::at_entry(Code::refs_item, file, e, "content", "refs");
    }
    const json::Index first = *refs + 1;
    const std::string_view scope = built[e].scope.view();
    bool item = false;
    if (const entries::Site* s = detail::site(file, e, first)) {
        item = s->hole == entries::Hole::item ||
               (s->hole == entries::Hole::name && file.text(file.entries()[s->named].kind) == "item");
    } else {
        const std::string_view id = v.text(first);
        for (std::size_t j = 0; j < e; ++j) {
            item = item || (built[j].id.view() == id && file.text(file.entries()[j].kind) == "item");
        }
        item = item || fold.kind(scope, id) == std::optional<std::string_view>("item");
    }
    if (!item) {
        detail::at_entry(Code::refs_item, file, e, "content", "refs", 0);
    }
}

// cycle, for entry e in scope: an item, the file's last of its item_id,
// whose under reaches it again; a loop that does not hold it ends the walk
// (two walkers, one a step at a time, one two).
template <Fold F>
void cycle(const entries::File& file, F& fold, std::string_view scope, std::size_t e) {
    if (file.text(file.entries()[e].kind) != "item") {
        return;
    }
    const std::string_view self = detail::text(file, e, "item_id").value_or(std::string_view{});
    if (detail::last_item(file, self) != std::optional<std::size_t>(e)) {
        return;
    }
    const std::optional<std::string_view> start = detail::text(file, e, "under");
    std::optional<detail::Id> slow = start ? std::optional<detail::Id>(detail::Id(*start)) : std::nullopt;
    std::optional<detail::Id> fast = slow;
    while (fast) {
        for (int step = 0; step < 2 && fast; ++step) {
            if (fast->view() == self) {
                detail::at_entry(Code::cycle, file, e, "content", "under");
            }
            fast = detail::under(file, fold, scope, fast->view());
        }
        slow = detail::under(file, fold, scope, slow->view());
        if (fast && slow && fast->view() == slow->view()) {
            return;
        }
    }
}

// The write command's refusals of a session's file, its entries built in
// built, as above, or a Refusal.
template <Fold F>
void write(const entries::File& file, std::span<const build::Built> built, F& fold) {
    if (built.size() != file.entries().size()) {
        memory::fail_fast("refuse: built entries other than the file's");
    }
    slot(file, fold);
    person_scope(file);
    for (std::size_t e = 0; e < built.size(); ++e) {
        owner_entry(file, built[e], fold, e);
        if (file.text(file.entries()[e].basis) == "owner-words") {
            detail::at_entry(Code::owner_words, file, e, "basis");
        }
        if (entry::header::among(kinds, file.text(file.entries()[e].kind))) {
            detail::at_entry(Code::kind, file, e, "kind");
        }
        refs_item(file, built, fold, e);
        cycle(file, fold, built[e].scope.view(), e);
    }
}

}  // namespace qiven3::write::refuse
