#pragma once

// import/manifest's walk over the manifest's value, the two source files'
// alone: the forms of its texts, the path of the field read, and Reader,
// which checks each member by its table and keeps the manifest's parts.
//
// thread: one (a Reader stays in the call that made it)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "import/manifest.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::import::manifest {

namespace detail {

using JKind = json::Kind;

inline constexpr std::size_t text_chars = 500;  // a note or any other text [proposal]
inline constexpr std::size_t line_chars = 2000;  // a document's line [proposal]
inline constexpr std::size_t max_items = 8;
inline constexpr std::size_t max_listed = 256;  // replacements, amendments [proposal]
inline constexpr std::int64_t max_ordinal = 9999;
inline constexpr std::int64_t max_line = 10000000;

// The key rule: canon's syntax, but a documents member's name and a goal's
// source_reasons key, which this reader checks itself.
struct Keys {
    bool operator()(std::string_view key, const canon::PlaceView& place) const noexcept {
        if (place.level == 2 && place.keys.size() == 1 && place.keys[0] == "documents") {
            return true;
        }
        if (place.level == 3 && place.keys.size() == 2 && place.keys[0] == "goal" &&
            place.keys[1] == "source_reasons") {
            return true;
        }
        return canon::key_syntax(key);
    }
};
static_assert(canon::KeyRule<Keys>);

constexpr bool lower_or_digit(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
constexpr bool alnum(char c) noexcept { return lower_or_digit(c) || (c >= 'A' && c <= 'Z'); }

// Characters: the bytes that begin a UTF-8 sequence.
inline std::size_t characters(std::string_view t) noexcept {
    return static_cast<std::size_t>(
        std::count_if(t.begin(), t.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

inline bool is_name(std::string_view t) noexcept {
    return !t.empty() && t.size() <= 64 && lower_or_digit(t.front()) &&
           std::all_of(t.begin(), t.end(), [](char c) { return lower_or_digit(c) || c == '-'; });
}
inline bool is_repo(std::string_view t) noexcept {
    return !t.empty() && t.size() <= 100 && t != "." && t != ".." &&
           std::all_of(t.begin(), t.end(), [](char c) { return alnum(c) || c == '.' || c == '_' || c == '-'; });
}
inline bool is_path(std::string_view t) noexcept {
    if (t.empty() || t.size() > 240) {
        return false;
    }
    for (const char c : t) {
        const auto b = static_cast<unsigned char>(c);
        if (b < 0x20U || b == 0x7FU || c == '\\' || c == ':') {
            return false;
        }
    }
    std::size_t at = 0;
    while (true) {
        const std::size_t end = std::min(t.find('/', at), t.size());
        const std::string_view segment = t.substr(at, end - at);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (end == t.size()) {
            return true;
        }
        at = end + 1;
    }
}
inline bool is_revision(std::string_view t) noexcept {
    return t.size() >= 4 && t.size() <= 40 &&
           std::all_of(t.begin(), t.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
inline bool is_section(std::string_view t) noexcept {
    return !t.empty() && t.size() <= 32 &&
           std::all_of(t.begin(), t.end(), [](char c) { return alnum(c) || c == '.' || c == '-'; });
}
inline bool is_id(std::string_view t) noexcept {
    return !t.empty() && t.size() <= 64 && lower_or_digit(t.front()) &&
           std::all_of(t.begin(), t.end(), [](char c) { return lower_or_digit(c) || c == '.' || c == '-'; });
}
inline bool is_scope(std::string_view t) noexcept {
    constexpr std::string_view project = "project:";
    return t.starts_with(project) && is_name(t.substr(project.size()));
}

using Form = bool (*)(std::string_view) noexcept;

// The path of the field read, cut past its room; ManifestError keeps 128
// bytes of it, cut at a code point.
class Path {
public:
    std::size_t member(std::string_view key) noexcept {
        const std::size_t mark = size_;
        if (key.empty()) {
            return mark;
        }
        if (size_ > 0) {
            add(".");
        }
        add(key);
        return mark;
    }
    std::size_t member(const memory::Inline<32>& key) noexcept { return member(key.view()); }
    std::size_t index(std::size_t i) noexcept {
        const std::size_t mark = size_;
        std::array<char, 20> digits{};
        std::size_t n = 0;
        do {
            digits.at(n++) = static_cast<char>('0' + (i % 10));
            i /= 10;
        } while (i != 0);
        put('[');
        while (n > 0) {
            put(digits.at(--n));
        }
        put(']');
        return mark;
    }
    void pop(std::size_t mark) noexcept { size_ = mark; }
    [[nodiscard]] std::string_view view() const noexcept { return {bytes_.data(), size_}; }

private:
    void put(char c) noexcept {
        if (size_ < bytes_.size()) {
            bytes_.at(size_++) = c;
        }
    }
    void add(std::string_view s) noexcept {
        for (const char c : s) {
            put(c);
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

// A member of an object read by its table: its value's node, or 0, and
// its key, a copy (the longest a table holds is 25 bytes).
struct Field {
    json::Index at = 0;
    memory::Inline<32> key;
};
template <std::size_t N>
using Table = std::array<std::string_view, N>;

inline std::uint32_t size32(std::size_t n) noexcept { return static_cast<std::uint32_t>(n); }

// The walk over the value, the manifest's parts kept as it goes; m and v
// borrowed for read_manifest's call.
struct Reader {
    Manifest* m = nullptr;
    const json::Value* v = nullptr;
    Path path;
    memory::Inline<64> entry;
    std::array<bool, max_documents> used{};

    [[noreturn]] void refuse(Code code) const {
        throw ManifestError{code, ParseCode::syntax, memory::Inline<128>(path.view()), entry};
    }

    // An object's members by its table; a name not in it, unknown_field.
    template <std::size_t N>
    std::array<Field, N> members(json::Index object, const Table<N>& keys) {
        std::array<Field, N> out{};
        for (std::size_t j = 0; j < N; ++j) {
            out.at(j).key = memory::Inline<32>(keys.at(j));
        }
        for (json::Index k = object + 1; k < v->next(object); k = v->next(k + 1)) {
            const std::string_view key = v->text(k);
            const auto it = std::find(keys.begin(), keys.end(), key);
            if (it == keys.end()) {
                path.member(key);
                refuse(Code::unknown_field);
            }
            out.at(static_cast<std::size_t>(it - keys.begin())).at = k + 1;
        }
        return out;
    }
    json::Index node(const Field& f, JKind kind, bool required) {
        if (f.at == 0) {
            if (!required) {
                return 0;
            }
            path.member(f.key);
            refuse(Code::missing);
        }
        if (v->kind(f.at) != kind) {
            path.member(f.key);
            refuse(Code::type);
        }
        return f.at;
    }
    // A string at the path as it stands, within its characters and form.
    Text checked(json::Index n, std::size_t chars, Form form) {
        if (v->kind(n) != JKind::string) {
            refuse(Code::type);
        }
        const std::string_view t = v->text(n);
        if (t.empty() || characters(t) > chars) {
            refuse(Code::bound);
        }
        if (form != nullptr && !form(t)) {
            refuse(Code::form);
        }
        return n;
    }
    Text text(const Field& f, std::size_t chars, bool required, Form form = nullptr) {
        const json::Index n = node(f, JKind::string, required);
        if (n == 0) {
            return 0;
        }
        const Step s(path, path.member(f.key));
        return checked(n, chars, form);
    }
    Text fixed(const Field& f, std::string_view want) {
        const Text t = text(f, 64, true);
        if (v->text(t) != want) {
            path.member(f.key);
            refuse(Code::value);
        }
        return t;
    }
    std::int64_t integer(const Field& f, std::int64_t most) {
        const json::Index n = node(f, JKind::integer, true);
        if (v->integer(n) < 1 || v->integer(n) > most) {
            path.member(f.key);
            refuse(Code::bound);
        }
        return v->integer(n);
    }
    // An array of lo to hi items, or 0 where absent and not required.
    json::Index list(const Field& f, std::size_t lo, std::size_t hi, bool required) {
        const json::Index n = node(f, JKind::array, required);
        if (n != 0 && (v->count(n) < lo || v->count(n) > hi)) {
            path.member(f.key);
            refuse(Code::bound);
        }
        return n;
    }
    Span texts(const Field& f, std::size_t items, std::size_t chars) {
        const json::Index n = list(f, 1, items, false);
        const Span out{size32(m->texts_.size()), n == 0 ? 0 : size32(v->count(n))};
        const Step s(path, path.member(f.key));
        std::size_t i = 0;
        for (json::Index k = n + 1; n != 0 && k < v->next(n); k = v->next(k), ++i) {
            const Step item(path, path.index(i));
            m->texts_.push_back(checked(k, chars, nullptr));
        }
        return out;
    }
    // A document's name, which documents holds.
    Text doc(const Field& f) {
        const Text t = text(f, 64, true, is_name);
        const std::optional<std::size_t> d = m->document(v->text(t));
        if (!d) {
            path.member(f.key);
            refuse(Code::unknown_document);
        }
        used.at(*d) = true;
        return t;
    }
    // A source item: {doc, section}, or {entry} where entries may be named.
    Source source(json::Index n, bool entries) {
        if (v->kind(n) != JKind::object) {
            refuse(Code::type);
        }
        // A named table, no temporary: MSVC's lifetime rules, given a
        // temporary array of three views in a unit, flag json's own (C26846).
        static constexpr Table<3> keys{"doc", "section", "entry"};
        const std::array<Field, 3> f = members(n, keys);
        Source out;
        if (f[2].at != 0 && entries) {
            for (std::size_t j = 0; j < 2; ++j) {
                if (f.at(j).at != 0) {
                    path.member(f.at(j).key);
                    refuse(Code::unknown_field);
                }
            }
            out.entry = text(f[2], 64, true, is_id);
            return out;
        }
        if (f[2].at != 0) {
            path.member(f[2].key);
            refuse(Code::unknown_field);
        }
        out.doc = doc(f[0]);
        out.section = text(f[1], 32, true, is_section);
        return out;
    }
    Source one_source(const Field& f, bool required) {
        const json::Index n = node(f, JKind::object, required);
        if (n == 0) {
            return {};
        }
        const Step s(path, path.member(f.key));
        return source(n, false);
    }
    Span sources(const Field& f, std::size_t lo, bool entries, bool required) {
        const json::Index n = list(f, lo, max_sources, required);
        const Span out{size32(m->sources_.size()), n == 0 ? 0 : size32(v->count(n))};
        const Step s(path, path.member(f.key));
        std::size_t i = 0;
        for (json::Index k = n + 1; n != 0 && k < v->next(n); k = v->next(k), ++i) {
            const Step item(path, path.index(i));
            m->sources_.push_back(source(k, entries));
        }
        return out;
    }

    void documents(const Field& f);
    void rulings(const Field& f);
    void read_entry(json::Index n);
    void goal(const Field& f);
    void person(const Field& f);
    void replacements(const Field& f);
    void amendments(const Field& f);
    void references();
};

}  // namespace detail

}  // namespace qiven3::import::manifest
