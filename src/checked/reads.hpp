#pragma once

// checked/reads: the checks a ruled store's reads run over the bytes and
// keys its store gives, each refusal StoreCorrupt in the store's words.
//
//   header_of(id, line, allocator, at)   the line parsed and its header
//       checked, its id recomputed: a line the parser or the header's
//       rules refuse incoherent, an id not the line's or not the one asked
//       id_mismatch
//   keys_match(keys, h, at)              a record's keys against its
//       header's fields: one differing incoherent
//   body_key(keys, h, at)                a record's body key against its
//       header's content: one differing incoherent
//   part1_again(platform, index, line, record, allocator, at)   part 1
//       over a held line and record: a refusal incoherent naming its code
//   check_body(platform, id, record, h, allocator, at)   a body record
//       against its header: one the parser refuses incoherent, one not its
//       header's body_mismatch
//   copy_header(h, out), copy_record(r, out)   made again on out's
//       allocator
//   ParsedView<V>   a store's view (BytesView), which gives bytes, as
//       cognition's readers read it: header(id, out) the line through
//       header_of, body(id, out) the record's content, one shorter than
//       its frame incoherent; false where V's is
//
// Allocations: the header's parse and check (canon's four and one, the
// header's check's), the body's parse and part 1's on the allocator given;
// a copy's on out's; ParsedView's on out's: a header's line and the rest of
// header_of, a body's record in out. Platform calls: the body's parse, its
// key's, part 1's wipes (noexcept). thread: none.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "checked/part1.hpp"
#include "entry/body.hpp"
#include "entry/header.hpp"
#include "keys/keys.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"

namespace qiven3::checked::reads {

using Chars = memory::Allocator<char>;

// The read a refusal names: its operation and the store's path.
struct WhereView {
    store::Operation operation;
    std::string_view path;
};

[[noreturn]] inline void corrupt(const WhereView& at, store::Corrupt code, std::string_view detail) {
    throw store::StoreError::corrupt(store::name(at.operation), at.path, code, detail);
}

[[nodiscard]] inline Header header_of(std::string_view id, std::string_view line, const Chars& allocator,
                                      const WhereView& at) {
    namespace header = entry::header;
    Header h(allocator);
    try {
        h = header::parse_line(line, allocator);
    } catch (const ParseError&) {
        corrupt(at, store::Corrupt::incoherent, "a header line the parser refuses");
    } catch (const header::HeaderError&) {
        corrupt(at, store::Corrupt::incoherent, "a header line that is no object");
    }
    if (const std::optional<header::Code> fault = header::try_check(h)) {
        if (*fault != header::Code::id_mismatch) {
            corrupt(at, store::Corrupt::incoherent, header::name(*fault));
        }
        corrupt(at, store::Corrupt::id_mismatch, id);
    }
    if (h.value(header::Slot::id) != id) {
        corrupt(at, store::Corrupt::id_mismatch, id);
    }
    return h;
}

inline void keys_match(const keys::KeysView& k, const Header& h, const WhereView& at) {
    using entry::header::Slot;
    if (h.value(Slot::scope) != k.scope || h.value(Slot::lane) != k.lane || h.seq() != k.seq ||
        h.text(Slot::parent) != k.parent || h.value(Slot::kind) != k.kind || h.text(Slot::target) != k.target) {
        corrupt(at, store::Corrupt::incoherent, "an entry's column differs from its line");
    }
}

inline void body_key(const keys::KeysView& k, const Header& h, const WhereView& at) {
    if (h.text(entry::header::Slot::content).has_value() != k.body) {
        corrupt(at, store::Corrupt::incoherent, k.id);
    }
}

template <platform::Platform P>
void part1_again(P& platform, std::size_t index, std::string_view line, std::optional<std::string_view> record,
                 const Chars& allocator, const WhereView& at) {
    try {
        (void)checked::part1(platform, store::AtView{at.operation, at.path}, index, store::EntryView{line, record},
                             allocator);
    } catch (const store::StoreError& f) {
        if (f.kind() != store::Kind::entry_invalid) {
            throw;
        }
        corrupt(at, store::Corrupt::incoherent, f.entry_code());
    }
}

template <platform::Platform P>
void check_body(P& platform, std::string_view id, std::string_view record, const Header& h, const Chars& allocator,
                const WhereView& at) {
    namespace body = entry::body;
    try {
        body::check(body::parse(platform, record, allocator), h);
    } catch (const body::BodyError& f) {
        corrupt(at, f.code == body::Code::body_mismatch ? store::Corrupt::body_mismatch : store::Corrupt::incoherent,
                id);
    } catch (const ParseError&) {
        corrupt(at, store::Corrupt::incoherent, id);
    }
}

// h's fields made again on out's allocator.
inline void copy_header(const Header& h, Header& out) {
    namespace header = entry::header;
    using header::Slot;
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
    out = header::make(f, out.get_allocator());
}

// r made again on out's allocator.
inline void copy_record(const store::Record& r, store::Record& out) {
    out.kind.assign(std::string_view(r.kind));
    out.line.assign(std::string_view(r.line));
    out.id = r.id;
    out.scope = r.scope;
    out.lane = r.lane;
    out.seq = r.seq;
    out.parent = r.parent;
    out.target = r.target;
    out.body = r.body;
}

// A store's view: a line's and a record's bytes by id.
template <class V>
concept BytesView = requires(V& v, std::string_view s, store::String& bytes) {
    { v.header(s, bytes) } -> std::same_as<bool>;
    { v.body(s, bytes) } -> std::same_as<bool>;
};

// A store's view for cognition's readers, part 2, the clearing and the whole
// records: the rest V's own.
template <BytesView V>
class ParsedView {
public:
    ParsedView(V& view, const WhereView& at) noexcept : view_(&view), at_(at) {}

    bool header(std::string_view id, Header& out) {
        store::String line(out.get_allocator());
        if (!view_->header(id, line)) {
            return false;
        }
        out = header_of(id, line, out.get_allocator(), at_);
        return true;
    }
    // out, the record's room, cut down to its content; a record shorter than
    // its frame incoherent, as check_body's, before parts reads its places.
    bool body(std::string_view id, store::String& out) {
        if (!view_->body(id, out)) {
            return false;
        }
        if (out.size() < entry::body::frame_size) {
            corrupt(at_, store::Corrupt::incoherent, id);
        }
        const std::string_view content = entry::body::detail::parts(out).content;
        const auto from = static_cast<std::size_t>(content.data() - out.data());
        out.erase(from + content.size());
        out.erase(0, from);
        return true;
    }
    [[nodiscard]] std::optional<keys::Head> head(std::string_view scope, std::string_view lane) {
        return view_->head(scope, lane);
    }
    [[nodiscard]] bool forgotten(std::string_view id) { return view_->forgotten(id); }
    [[nodiscard]] std::optional<keys::Id> forget_of(std::string_view id) { return view_->forget_of(id); }
    void targets(std::string_view id, std::string_view scope, store::Vector<keys::Id>& out) {
        view_->targets(id, scope, out);
    }
    void targets(std::string_view id, store::Vector<keys::Id>& out) { view_->targets(id, out); }
    [[nodiscard]] bool contains(std::string_view bytes) { return view_->contains(bytes); }
    template <class F>
    bool each_keyed(std::string_view scope, std::string_view kind, F&& f) {
        return view_->each_keyed(scope, kind, std::forward<F>(f));
    }
    [[nodiscard]] std::uint64_t count() const { return view_->count(); }
    [[nodiscard]] Chars work() { return view_->work(); }

private:
    V* view_;
    WhereView at_;
};

}  // namespace qiven3::checked::reads
