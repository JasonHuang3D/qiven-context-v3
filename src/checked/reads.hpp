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
//   check_body(platform, id, record, h, allocator, at)   a body record
//       against its header: one the parser refuses incoherent, one not its
//       header's body_mismatch
//   copy_header(h, out), copy_record(r, out)   made again on out's
//       allocator
//
// Allocations: the header's parse and check (canon's four and one, the
// header's check's) and the body's parse on the allocator given; a copy's
// on out's. Platform
// calls: the body's parse, its key's (noexcept). thread: none.

#include <optional>
#include <string_view>

#include "entry/body.hpp"
#include "entry/header.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/keys.hpp"

namespace qiven3::checked::reads {

using Chars = memory::Allocator<char>;
using store::Header;

// The read a refusal names: its operation and the store's path.
struct Where {
    store::Operation operation;
    std::string_view path;
};

[[noreturn]] inline void corrupt(const Where& at, store::Corrupt code, std::string_view detail) {
    throw store::StoreError::corrupt(store::name(at.operation), at.path, code, detail);
}

[[nodiscard]] inline Header header_of(std::string_view id, std::string_view line, const Chars& allocator,
                                      const Where& at) {
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

inline void keys_match(const store::Keys& k, const Header& h, const Where& at) {
    using entry::header::Slot;
    if (h.value(Slot::scope) != k.scope || h.value(Slot::lane) != k.lane || h.seq() != k.seq ||
        h.text(Slot::parent) != k.parent || h.value(Slot::kind) != k.kind || h.text(Slot::target) != k.target) {
        corrupt(at, store::Corrupt::incoherent, "an entry's column differs from its line");
    }
}

template <platform::Platform P>
void check_body(P& platform, std::string_view id, std::string_view record, const Header& h, const Chars& allocator,
                const Where& at) {
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

}  // namespace qiven3::checked::reads
