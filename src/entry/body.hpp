#pragma once

// body: an entry's body record and its content's digest. The record is one
// canonical JSON line of exactly three keys,
// {"content":{...},"id":"sha256:...","key":"<64 lowercase hex>"}: the
// content, the entry's id and its key. make builds a body from a content's
// canonical bytes, the entry's key and its id; content_digest gives the content's
// keyed digest under that key, the header's content; line writes the
// record. parse reads a record through canon::parse, the strict parser, the
// first fault deciding: unknown_key, missing_key, then syntax (an id or key
// off its syntax, or a content that is no object), each BodyError(code);
// the parser's own faults are its ParseError. check holds a body against
// its header: an id not the header's, or a content whose digest under the
// key is not the header's content (a forget's header has none), is
// BodyError body_mismatch, both compared by digest::same.
//
// Body holds the content's bytes and the id in one text, beside its Key:
// the key's material lives in the Key alone, in no arena, and reaches
// bytes only as the record's hex, which line writes straight into the
// caller's string. A content is canonical as it stands, so the record's
// bytes are the frame's and the content's, written with no tree; a record
// parsed strictly is canonical too, so its parts are read off its bytes at
// their fixed places once the layout holds.
//
// Allocations: make canon's four and one, the body's text; parse canon's
// four and one; line one, the record's; content_digest one, the caller's
// string's; check none. A size past json::max_input is MemoryError budget.
// Platform calls: wipes alone, each noexcept: key_from_hex's one in parse,
// keyed's one in content_digest and check, and a Key's at each move and
// end (parse three in all). The layout's reader and the record's writer are constexpr
// and templates on the allocator or the string type, so their vectors run
// in constant evaluation too; they read by index, with no pointer
// arithmetic. body logs nothing.
//
// thread: none; a Body, one (its key's platform's).

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "entry/header.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"

namespace qiven3::entry::body {

// The codes, in the order parse reads them, then check's.
enum class Code : std::uint8_t { unknown_key, missing_key, syntax, body_mismatch };
inline constexpr std::array<std::string_view, 4> code_names = {"unknown_key", "missing_key", "syntax",
                                                               "body_mismatch"};
constexpr std::string_view name(Code code) noexcept { return code_names[static_cast<std::size_t>(code)]; }

struct BodyError {
    Code code;
};

// The record's keys in their order, and the frame around the content.
inline constexpr std::array<std::string_view, 3> record_keys = {"content", "id", "key"};
inline constexpr std::size_t id_size = 71;  // sha256: and 64 hex digits
inline constexpr std::size_t key_size = 2 * digest::bytes;
inline constexpr std::size_t digest_size = 76;  // hmac-sha256: and 64 hex digits
inline constexpr std::string_view head = "{\"content\":";
inline constexpr std::string_view id_part = ",\"id\":\"";
inline constexpr std::string_view key_part = "\",\"key\":\"";
inline constexpr std::string_view end_part = "\"}";
inline constexpr std::size_t frame_size =
    head.size() + id_part.size() + id_size + key_part.size() + key_size + end_part.size();
static_assert(frame_size == 164, "the record's frame: its head, 11, its tail's id and key, 153");

namespace detail {

constexpr bool lower_hex(std::string_view text) noexcept {
    for (const char c : text) {
        if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) {
            return false;
        }
    }
    return true;
}

// Whether text is an id: sha256: and 64 lowercase hex digits, header's
// id_syntax.
constexpr bool id_text(std::string_view text) noexcept {
    return text.size() == id_size && text.starts_with("sha256:") && lower_hex(text.substr(7));
}

[[noreturn]] inline void too_large(std::size_t request) {
    throw memory::MemoryError::budget("body", json::max_input, request, 0);
}

// A record's parts, viewing its bytes within the call that lent them.
struct RecordView {
    std::string_view content;
    std::string_view id;
    std::string_view key;
};

// A parsed record's first fault in parse's order, or none.
template <typename A>
constexpr std::optional<Code> layout(const json::Basic<A>& v) {
    if (v.kind(0) != json::Kind::object) {
        return Code::syntax;
    }
    // Each key's value's node, 0 (the root's) while the key is absent.
    std::array<json::Index, record_keys.size()> at{};
    bool unknown = false;
    const json::Index end = v.next(0);
    for (json::Index k = 1; k < end; k = v.next(k + 1)) {
        bool known = false;
        for (std::size_t i = 0; i < record_keys.size(); ++i) {
            if (v.text(k) == record_keys[i]) {
                at[i] = k + 1;
                known = true;
            }
        }
        unknown = unknown || !known;
    }
    if (unknown) {
        return Code::unknown_key;
    }
    if (at[0] == 0 || at[1] == 0 || at[2] == 0) {
        return Code::missing_key;
    }
    if (v.kind(at[0]) != json::Kind::object || v.kind(at[1]) != json::Kind::string ||
        v.kind(at[2]) != json::Kind::string || !id_text(v.text(at[1])) || !digest::key_text(v.text(at[2]))) {
        return Code::syntax;
    }
    return std::nullopt;
}

// A canonical record's parts at their fixed places, its layout held: the
// content between the head and the tail, then the id and the key.
constexpr RecordView parts(std::string_view data) noexcept {
    const std::size_t tail = data.size() - (frame_size - head.size());
    RecordView r;
    r.content = data.substr(head.size(), tail - head.size());
    r.id = data.substr(tail + id_part.size(), id_size);
    r.key = data.substr(tail + id_part.size() + id_size + key_part.size(), key_size);
    return r;
}

// The record's bytes into out, replaced only on success: one allocation.
// put_key writes the key's 64 hex digits into the span it is given, in
// place in the result.
template <typename S, typename PutKey>
constexpr void write(std::string_view content, std::string_view id, const PutKey& put_key, S& out) {
    if (content.size() > json::max_input - frame_size) {
        too_large(content.size());
    }
    S text(out.get_allocator());
    text.reserve(content.size() + frame_size);
    text.append(head);
    text.append(content);
    text.append(id_part);
    text.append(id);
    text.append(key_part);
    const std::size_t at = text.size();
    text.append(key_size, '0');
    put_key(std::span<char>(text).subspan(at).template first<key_size>());
    text.append(end_part);
    out = std::move(text);
}

// A digest's text, held in place: digest's hex writer writes into it with
// no allocation.
class DigestText {
public:
    struct None {};
    constexpr DigestText() noexcept = default;
    constexpr explicit DigestText(None /*none*/) noexcept {}
    [[nodiscard]] constexpr None get_allocator() const noexcept { return {}; }
    constexpr void reserve(std::size_t n) const {
        if (n > bytes_.size()) {
            memory::fail_fast("body: a digest past 76 bytes");
        }
    }
    constexpr void append(std::string_view s) {
        for (const char c : s) {
            bytes_.at(size_++) = c;
        }
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view(bytes_.data(), bytes_.size()).substr(0, size_);
    }

private:
    std::array<char, digest_size> bytes_{};
    std::size_t size_ = 0;
};

}  // namespace detail

// A body: its content's canonical bytes and its id in one text, and its
// key. Move-only.
template <platform::Platform P, typename A = std::allocator<char>>
class Body {
public:
    using allocator_type = A;
    using Text = std::basic_string<char, std::char_traits<char>, A>;

    // The parts' bytes copied: one allocation. The id is id_size bytes.
    Body(std::string_view content, std::string_view id, digest::Key<P> key, const A& allocator)
        : text_(allocator), key_(std::move(key)) {
        if (id.size() != id_size) {
            memory::fail_fast("body: an id not of 71 bytes");
        }
        if (content.size() > json::max_input - frame_size) {
            detail::too_large(content.size());
        }
        text_.reserve(content.size() + id_size);
        text_.append(content);
        text_.append(id);
    }
    Body(const Body&) = delete;
    Body& operator=(const Body&) = delete;
    Body(Body&&) noexcept = default;
    Body& operator=(Body&&) noexcept = default;
    ~Body() = default;

    [[nodiscard]] A get_allocator() const { return text_.get_allocator(); }
    [[nodiscard]] std::string_view content() const noexcept {
        return std::string_view(text_).substr(0, text_.size() - id_size);
    }
    [[nodiscard]] std::string_view id() const noexcept { return std::string_view(text_).substr(text_.size() - id_size); }
    // The text's bytes held: its capacity, its terminator apart.
    [[nodiscard]] std::size_t bytes() const noexcept { return text_.capacity(); }

    // The content's keyed digest, the header's content, into out.
    template <typename S>
    void content_digest(S& out) const {
        digest::keyed(key_, content(), out);
    }
    // The record's bytes into out, replaced only on success: one
    // allocation. They hold the key's hex, as the record must.
    template <typename S>
    void line(S& out) const {
        detail::write(content(), id(), [this](std::span<char, key_size> hex) { digest::key_hex(key_, hex); }, out);
    }

private:
    Text text_;
    digest::Key<P> key_;
};

// A body from a content's canonical bytes, the entry's key and its id. The
// content is parsed strictly and dropped: a content that is no object, or
// an id off its syntax, is BodyError syntax; a content past 15 levels,
// which the record's root would take past 16, ParseError depth.
template <platform::Platform P, typename A = std::allocator<char>>
Body<P, A> make(std::string_view content, digest::Key<P> key, std::string_view id, const A& allocator = A()) {
    {
        json::Basic<A> value(allocator);
        canon::parse(content, value);
        if (value.kind(0) != json::Kind::object) {
            throw BodyError{Code::syntax};
        }
        if (value.depth() >= json::max_depth) {
            throw ParseError{ParseCode::depth};
        }
    }
    if (!detail::id_text(id)) {
        throw BodyError{Code::syntax};
    }
    return Body<P, A>(content, id, std::move(key), allocator);
}

// A body read from its record. The record's value is parsed strictly and
// dropped before the body is built; the key is read back by key_from_hex.
template <platform::Platform P, typename A = std::allocator<char>>
Body<P, A> parse(P& platform, std::string_view data, const A& allocator = A()) {
    detail::RecordView r;
    {
        json::Basic<A> value(allocator);
        canon::parse(data, value);
        if (const std::optional<Code> fault = detail::layout(value)) {
            throw BodyError{*fault};
        }
        r = detail::parts(data);
        const std::optional<json::Index> id = value.find(0, "id");
        const std::optional<json::Index> key = value.find(0, "key");
        if (!id.has_value() || !key.has_value() || r.id != value.text(id.value()) || r.key != value.text(key.value())) {
            memory::fail_fast("body: a canonical record's parts not at their places");
        }
    }
    return Body<P, A>(r.content, r.id, digest::key_from_hex(platform, r.key), allocator);
}

// A body against its header: its id the header's, and its content's digest
// under its key the header's content, or BodyError body_mismatch. No
// allocation.
template <platform::Platform P, typename B, typename C>
void check(const Body<P, B>& body, const header::Header<C>& h) {
    if (!digest::same(body.id(), h.value(header::Slot::id)) || !h.present(header::Slot::content)) {
        throw BodyError{Code::body_mismatch};
    }
    detail::DigestText d;
    body.content_digest(d);
    if (!digest::same(d.view(), h.value(header::Slot::content))) {
        throw BodyError{Code::body_mismatch};
    }
}

}  // namespace qiven3::entry::body
