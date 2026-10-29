#pragma once

// write/entries' walk over the file's value, its source files' alone.
//
// thread: one (a Reader stays in the call that made it)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "entry/content_fields.hpp"
#include "entry/matcher.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "write/entries.hpp"

namespace qiven3::write::entries::detail {

// Each content check's span, an arena made over it afresh [proposal]: the
// stand-in of at most 65,536 bytes, its header and the check's own.
inline constexpr std::size_t work_bytes = std::size_t{4} << 20U;

inline constexpr std::string_view stand_id =
    "sha256:0000000000000000000000000000000000000000000000000000000000000000";
inline constexpr std::string_view stand_digest =
    "hmac-sha256:0000000000000000000000000000000000000000000000000000000000000000";
inline constexpr std::string_view stand_at = "1970-01-01T00:00:00.000Z";

// The key rule: canon's syntax, but a piece's name, the reader's own.
struct Keys {
    bool operator()(std::string_view key, const canon::PlaceView& place) const noexcept {
        if (place.level == 2 && place.keys.size() == 1 && place.keys[0] == "pieces") {
            return true;
        }
        return canon::key_syntax(key);
    }
};
static_assert(canon::KeyRule<Keys>);

// The path of the field read, cut past its room.
class Path {
public:
    std::size_t member(std::string_view key) noexcept;
    std::size_t index(std::size_t i) noexcept;
    void pop(std::size_t mark) noexcept { size_ = mark; }
    [[nodiscard]] std::string_view view() const noexcept { return {bytes_.data(), size_}; }

private:
    void put(char c) noexcept {
        if (size_ < bytes_.size()) {
            bytes_.at(size_++) = c;
        }
    }
    std::array<char, 160> bytes_{};
    std::size_t size_ = 0;
};

// A step into the path, taken back at its scope's end.
class Step {
public:
    Step(Path& path, std::size_t mark) noexcept : path_(path), mark_(mark) {}
    Step(const Step&) = delete;
    Step& operator=(const Step&) = delete;
    Step(Step&&) = delete;
    Step& operator=(Step&&) = delete;
    ~Step() { path_.pop(mark_); }

private:
    Path& path_;
    std::size_t mark_;
};

// A member read by its table: its value's node, or 0, and its key.
struct Field {
    json::Index at = 0;
    memory::Inline<16> key{};
};
template <std::size_t N>
using Table = std::array<std::string_view, N>;

// The walk over the value, the file's parts kept as it goes; m borrowed
// for read's call. The syntaxes are compiled once, over the arena.
struct Reader {
    Reader(File& file, memory::Arena& arena);

    File* m;
    const json::Value* v;
    Path path;
    std::uint32_t entry = none;
    memory::Inline<64> entry_name;
    entry::matcher::Automaton slug;
    entry::matcher::Automaton scope;
    entry::matcher::Automaton writer;
    entry::matcher::Automaton id;
    entry::matcher::Automaton lane;

    [[noreturn]] void refuse(Why why) const;
    [[noreturn]] void refuse_at(std::string_view key, Why why);
    [[noreturn]] void fault(entry::content::Code code, std::size_t limit, std::size_t value, std::string_view at);
    // The first of by_name_ not before name.
    [[nodiscard]] memory::Vector<std::uint32_t>::iterator named(std::string_view name) const;

    template <std::size_t N>
    std::array<Field, N> members(json::Index object, const Table<N>& keys) {
        std::array<Field, N> out{};
        for (std::size_t j = 0; j < N; ++j) {
            out.at(j).key = memory::Inline<16>(keys.at(j));
        }
        for (json::Index k = object + 1; k < v->next(object); k = v->next(k + 1)) {
            const std::string_view key = v->text(k);
            const auto it = std::find(keys.begin(), keys.end(), key);
            if (it == keys.end()) {
                refuse_at(key, Why::unknown_member);
            }
            out.at(static_cast<std::size_t>(it - keys.begin())).at = k + 1;
        }
        return out;
    }
    // A member's node of a JSON kind, or 0 where absent and not required.
    json::Index node(const Field& f, json::Kind kind, bool required);
    // A string member, not empty, within bytes and, given one, a syntax.
    Text text(const Field& f, bool required, std::size_t bytes, const entry::matcher::Automaton* syntax);
    // The node absent, else not_allowed.
    void refused(const Field& f);

    void file();
    void episode(const Field& f);
    void pieces(const Field& f);
    void read_entry(json::Index object);
    void content(Entry& e, json::Index c);
    void references(json::Index list);
    void hole(json::Index node);
    std::uint32_t piece(std::string_view name) const;
    void pieces_used();
    [[nodiscard]] std::size_t standing_size(const Entry& e) const;
    void check_contents(memory::Arena& arena, Hooks hooks);
};

}  // namespace qiven3::write::entries::detail
