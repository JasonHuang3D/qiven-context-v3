#pragma once

// write/build: the entries of a file read (write/entries) built for the
// store, product. build takes the File, the store's fold and the platform,
// and gives back each entry in the file's order as the store's append or
// import takes it: its header line and body record, its id, at and seq.
// Per entry, in this order:
// - its key, 256 random bits from the platform, one per entry;
// - each hole filled: @name: the id of that earlier entry, of the entry's
//   scope; @item:, @lane: and @question: the current version of that item,
//   lane's checkpoint or question record in the entry's scope, the file's
//   earlier entries first, then the fold; a piece reference's digest, the
//   HMAC under the entry's key over the piece's bytes (a text piece its
//   string's UTF-8 bytes, a path piece the file read by the platform once,
//   at its first reference, at most 4 MiB, every reference taking those
//   bytes);
// - its content, canonical, a slot's episode in it (entries'
//   build_content);
// - its target: the file's, an id or a placeholder, held by the file's
//   earlier entries or the fold in the entry's scope; on a kind with a
//   record identity, the current version of that identity there, the file's
//   earlier entries first, else none (a root);
// - its lane (import, or the slot's role), parent and seq (the head of
//   (scope, lane), the file's earlier entries first, then the fold), at
//   (header's stamp over the platform's clock, the parent's and the
//   target's at), its content's digest under its key (none on a forget,
//   which has no body), its id;
// - its content checked again under its real header (expires against at).
// A hole or target that resolves to nothing (an @name: naming an entry of
// another scope among them, as a literal target), a path piece unread or past
// 4 MiB is EntriesError form (placeholder or piece), naming the entry and
// the field; the content check's fault is invalid under the content's path.
// On any error, a refusal, the arena's or the platform's, no entry is
// built: out is left empty and its memory given back.
// The refusals that need no fold, and the commands' (writer, kinds,
// owner_words, person_scope, owner_entry, refs_item, cycle, secret), are
// others'.
//
// Fold is the caller's: head(scope, lane), the lane's head; current(scope,
// identity), the current version of a record of that identity; find(scope,
// id), an entry held in that scope. Each gives its views for the call
// alone; build copies them at once.
//
// Allocations: all the arena's. The out vector's, each entry's two lines
// and identity key, held; each path piece's bytes and their list, held to
// the build's end; one work span of work_bytes, taken once and given back
// at the build's end, over which each entry's work runs in an arena made
// afresh (its holes, content value and canonical bytes, headers, digest
// and body), so that the arena a file needs grows with its entries' lines
// and its pieces' bytes, not with its entry count; past either MemoryError
// budget. Under QIVEN3_FAULTS each entry's work arena asks hooks.work
// first. Platform calls: random_bytes once an entry, now once an entry,
// read once a path piece, wipe at each key's end. It logs nothing.
//
// thread: one (the File's, the fold's and the platform's)

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "entry/body.hpp"
#include "entry/content.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/kinds_identity.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "qiven3/utc.hpp"
#include "write/entries.hpp"
#include "write/entries_reader.hpp"

namespace qiven3::write::build {

using entries::EntriesError;
using entries::File;

// A version the fold holds: its id and at, and its seq for a head.
struct VersionView {
    std::string_view id;
    std::string_view at;
    std::int64_t seq = 0;
};

template <class F>
concept Fold = requires(F& f, std::string_view s, const entry::kinds::IdentityView& identity) {
    { f.head(s, s) } -> std::same_as<std::optional<VersionView>>;
    { f.current(s, identity) } -> std::same_as<std::optional<VersionView>>;
    { f.find(s, s) } -> std::same_as<std::optional<VersionView>>;
};

// An entry built, its texts on the arena.
struct Built {
    explicit Built(memory::Arena& arena)
        : line(memory::Allocator<char>(arena)), record(memory::Allocator<char>(arena)),
          identity(memory::Allocator<char>(arena)) {}
    memory::String line;    // the stored header line
    memory::String record;  // the body record; empty on a forget
    memory::Inline<72> id{};
    memory::Inline<72> target{};  // its target's id; empty for a root
    memory::Inline<32> at{};
    memory::Inline<64> scope{};
    memory::Inline<64> lane{};
    std::int64_t seq = 0;
    bool body = false;
    bool derived = false;     // its target the fold's or the file's current version
    memory::String identity;  // its record identity's key; empty for none
};

namespace detail {

// The id or at's room, as Inline holds it.
inline constexpr std::size_t at_room = 32;

// Each entry's work span, the reader's size [proposal]: a content of at
// most 65,536 canonical bytes, its value, holes, two headers and body.
inline constexpr std::size_t work_bytes = entries::detail::work_bytes;

// A path piece's bytes, read at its first reference and held to the
// build's end, every reference taking them.
struct PieceBytes {
    explicit PieceBytes(memory::Arena& arena) : bytes(memory::Allocator<char>(arena)) {}
    memory::String bytes;
    bool read = false;
};

// identity's key: the kind, then each part's length and bytes.
void identity_key(const entry::kinds::IdentityView& identity, memory::String& out);
// The identity a placeholder names: an item, a lane's checkpoint, a question.
entry::kinds::IdentityView placeholder_identity(entries::Hole hole, std::string_view key);
// The refusal for entry e, its field's path: the target, a site's node
// (and piece below a piece reference), or the content's path below it.
[[noreturn]] void refuse_target(const File& file, std::size_t e);
[[noreturn]] void refuse_site(const File& file, std::size_t e, const entries::Site& site);
[[noreturn]] void refuse_content(const File& file, std::size_t e, const entry::content::ContentError& fault);
// The latest of the file's built entries in scope with this identity key,
// or of this (scope, lane); none for none.
const Built* latest(std::span<const Built> built, std::string_view scope, std::string_view identity);
const Built* head(std::span<const Built> built, std::string_view scope, std::string_view lane);
const Built* by_id(std::span<const Built> built, std::string_view scope, std::string_view id);

// A version held, the file's first, copied into id and at.
struct Held {
    memory::Inline<72> id{};
    memory::Inline<at_room> at{};
    std::int64_t seq = 0;
};
inline std::optional<Held> held(const Built* b) {
    return b == nullptr ? std::nullopt : std::optional<Held>(Held{b->id, b->at, b->seq});
}
inline std::optional<Held> held(const std::optional<VersionView>& v) {
    return v ? std::optional<Held>(Held{memory::Inline<72>(v->id), memory::Inline<at_room>(v->at), v->seq})
             : std::nullopt;
}

template <Fold F>
std::optional<Held> current(std::span<const Built> built, F& fold, std::string_view scope,
                            const entry::kinds::IdentityView& identity, memory::String& key) {
    identity_key(identity, key);
    if (const Built* b = latest(built, scope, key)) {
        return held(b);
    }
    return held(fold.current(scope, identity));
}

// Each entry built into out, empty, as build says, its work over pages.
template <platform::Platform P, Fold F>
void build_each(const File& file, F& fold, P& platform, memory::Arena& arena, std::span<std::byte> pages,
                [[maybe_unused]] entries::Hooks hooks, memory::Vector<Built>& out) {
    namespace header = entry::header;
    using header::Slot;
    out.reserve(file.entries().size());
    const std::string_view lane = file.import() ? std::string_view("import") : header::role(file.writer());
    memory::Vector<PieceBytes> paths{memory::Allocator<PieceBytes>(arena)};
    for (std::size_t i = 0; i < file.entries().size(); ++i) {
        const entries::Entry& e = file.entries()[i];
        const std::string_view scope = file.text(e.scope);
        const std::string_view kind = file.text(e.kind);
        const std::span<const Built> before(out.data(), out.size());
        memory::Arena work_arena(pages);  // given back whole at the entry's end
#if defined(QIVEN3_FAULTS)
        work_arena.attach(hooks.work);
#endif
        const memory::Allocator<char> alloc(work_arena);
        digest::Key<P> key = digest::new_key(platform);
        memory::String work(alloc);

        // A placeholder's version: @name:'s entry if of this scope, else
        // the current version it names, the file's first.
        const auto resolve = [&](const entries::Site& s) {
            std::optional<detail::Held> to;
            if (s.hole != entries::Hole::name) {
                to = detail::current(before, fold, scope, detail::placeholder_identity(s.hole, file.key(s)), work);
            } else if (out[s.named].scope.view() == scope) {
                to = detail::held(&out[s.named]);
            }
            if (!to) {
                detail::refuse_site(file, i, s);
            }
            return *to;
        };

        // The holes, in document order; fills view texts that outlive them.
        const std::span<const entries::Site> sites = file.sites(e);
        memory::Vector<memory::String> filled{memory::Allocator<memory::String>(work_arena)};
        filled.reserve(sites.size());
        for (const entries::Site& s : sites) {
            filled.emplace_back(alloc);
            memory::String& fill = filled.back();
            if (s.hole == entries::Hole::piece) {
                const entries::Piece& p = file.pieces()[s.named];
                if (p.path) {
                    if (paths.empty()) {
                        paths.reserve(file.pieces().size());
                        for (std::size_t k = 0; k < file.pieces().size(); ++k) {
                            paths.emplace_back(arena);
                        }
                    }
                    PieceBytes& got = paths[s.named];
                    if (!got.read) {
                        try {
                            platform.read(file.text(p.value), got.bytes);
                        } catch (const platform::PlatformError&) {
                            detail::refuse_site(file, i, s);
                        }
                        if (got.bytes.size() > entries::max_piece_bytes) {
                            detail::refuse_site(file, i, s);
                        }
                        got.read = true;
                    }
                    digest::keyed(key, got.bytes, fill);
                } else {
                    digest::keyed(key, file.text(p.value), fill);
                }
                continue;
            }
            fill.assign(resolve(s).id.view());
        }
        memory::Vector<std::string_view> fills{memory::Allocator<std::string_view>(work_arena)};
        fills.reserve(filled.size());
        for (const memory::String& f : filled) {
            fills.push_back(f);
        }
        json::Value value{alloc};
        entries::build_content(file, i, fills, value);
        memory::String content(alloc);
        canon::canonical(value, content);

        // The target: the file's, held; or the current version of the
        // entry's record identity, if any, its key built in the work span. b's
        // texts are the arena's.
        Built b(arena);
        memory::String identity(alloc);
        std::optional<detail::Held> target;
        if (e.target_site != entries::none) {
            target = resolve(file.sites()[e.target_site]);
        } else if (e.target != 0) {
            const std::string_view given = file.text(e.target);
            target = detail::held(detail::by_id(before, scope, given));
            if (!target) {
                target = detail::held(fold.find(scope, given));
            }
            if (!target) {
                detail::refuse_target(file, i);
            }
        } else {
            const header::FieldsView stand{.scope = scope, .lane = lane, .parent = {}, .kind = kind,
                                           .writer = file.writer(), .basis = file.text(e.basis),
                                           .at = entries::detail::stand_at, .target = {}, .content = {},
                                           .id = entries::detail::stand_id};
            const header::Header<memory::Allocator<char>> h = header::make(stand, alloc);
            if (const std::optional<entry::kinds::IdentityView> id = entry::kinds::identity(h, value)) {
                target = detail::current(before, fold, scope, *id, identity);
                b.derived = target.has_value();
            }
        }

        // The chain, the time, the digest and the id.
        std::optional<detail::Held> parent = detail::held(detail::head(before, scope, lane));
        if (!parent) {
            parent = detail::held(fold.head(scope, lane));
        }
        const std::array<char, utc::at_size> at = header::stamp(
            platform, parent ? std::optional<std::string_view>(parent->at.view()) : std::nullopt,
            target ? std::optional<std::string_view>(target->at.view()) : std::nullopt);
        const std::string_view at_text(at.data(), at.size());
        b.body = kind != "forget";
        memory::String content_digest(alloc);
        if (b.body) {
            digest::keyed(key, content, content_digest);
        }
        const header::FieldsView f{
            .scope = scope, .lane = lane, .seq = parent ? parent->seq + 1 : 1,
            .parent = parent ? std::optional<std::string_view>(parent->id.view()) : std::nullopt,
            .kind = kind, .writer = file.writer(), .basis = file.text(e.basis), .at = at_text,
            .target = target ? std::optional<std::string_view>(target->id.view()) : std::nullopt,
            .content = b.body ? std::optional<std::string_view>(content_digest) : std::nullopt, .id = {}};
        const header::Header<memory::Allocator<char>> h = header::make(f, alloc);
        if (const std::optional<entry::content::ContentError> c = entry::content::try_check(h, value)) {
            detail::refuse_content(file, i, *c);
        }
        if (identity.empty()) {
            if (const std::optional<entry::kinds::IdentityView> id = entry::kinds::identity(h, value)) {
                detail::identity_key(*id, identity);
            }
        }
        b.identity.assign(identity.data(), identity.size());  // copied out in one allocation
        // The line written in the work span, whose encoder's order is given
        // back with it, then copied out in one allocation.
        memory::String line(alloc);
        h.line(line);
        b.line.assign(line.data(), line.size());  // by its bytes: the allocator stays the arena's
        b.id = memory::Inline<72>(h.value(Slot::id));
        if (target) {
            b.target = target->id;
        }
        b.at = memory::Inline<detail::at_room>(at_text);
        b.scope = memory::Inline<64>(scope);
        b.lane = memory::Inline<64>(lane);
        b.seq = f.seq;
        if (b.body) {
            entry::body::make(std::string_view(content), std::move(key), h.value(Slot::id), alloc).line(b.record);
        }
        out.push_back(std::move(b));
    }
}

}  // namespace detail

// The file's entries built into out in the file's order, or EntriesError as
// above; MemoryError budget past the arena, the platform's errors but
// read's as they come. On any error out is left empty, its memory and the
// work span given back: no entry of a file refused is built.
template <platform::Platform P, Fold F>
void build(const File& file, F& fold, P& platform, memory::Arena& arena, memory::Vector<Built>& out,
           entries::Hooks hooks = {}) {
    out.clear();
    std::span<std::byte> pages;
    try {
        pages = std::span<std::byte>(
            static_cast<std::byte*>(arena.allocate(detail::work_bytes, alignof(std::max_align_t))),
            detail::work_bytes);
        detail::build_each(file, fold, platform, arena, pages, hooks, out);
    } catch (...) {
        out = memory::Vector<Built>(out.get_allocator());
        if (!pages.empty()) {
            arena.deallocate(pages.data(), pages.size());
        }
        throw;
    }
    arena.deallocate(pages.data(), pages.size());
}

}  // namespace qiven3::write::build
