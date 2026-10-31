#include "write/kept.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::write::kept {

namespace {

constexpr std::size_t digits = 10;
constexpr std::size_t hex_digits = 16;
constexpr std::array<std::string_view, 3> suffixes = {".jsonl", ".pending", ".tmp"};

[[noreturn]] void refuse_corrupt(std::string_view name) { throw Refusal{Code::corrupt, detail::at(name)}; }

constexpr bool digit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool lower_hex(char c) noexcept { return digit(c) || (c >= 'a' && c <= 'f'); }
constexpr bool writer_byte(char c) noexcept { return digit(c) || (c >= 'a' && c <= 'z') || c == '-' || c == '/'; }

// The ten digits' n, or none.
std::optional<std::uint64_t> ten(std::string_view text) noexcept {
    if (text.size() != digits) {
        return std::nullopt;
    }
    std::uint64_t n = 0;
    for (const char c : text) {
        if (!digit(c)) {
            return std::nullopt;
        }
        n = n * 10 + (static_cast<std::uint64_t>(static_cast<unsigned char>(c)) - std::uint64_t{'0'});
    }
    return n;
}

// A decimal integer of at most ten digits, no leading zero, taken from the
// front of text; none if there is none.
std::optional<std::uint64_t> integer(std::string_view& text) noexcept {
    std::size_t i = 0;
    std::uint64_t n = 0;
    while (i < text.size() && digit(text[i]) && i < digits + 1) {
        n = n * 10 + (static_cast<std::uint64_t>(static_cast<unsigned char>(text[i])) - std::uint64_t{'0'});
        ++i;
    }
    if (i == 0 || i > digits || (i > 1 && text[0] == '0')) {
        return std::nullopt;
    }
    text.remove_prefix(i);
    return n;
}

// text's front is word: taken.
bool take(std::string_view& text, std::string_view word) noexcept {
    if (!text.starts_with(word)) {
        return false;
    }
    text.remove_prefix(word.size());
    return true;
}

// The first line's fields, or false.
bool parse_head(std::string_view line, Head& head) noexcept {
    if (!take(line, R"({"kept":")") || !take(line, kept_form) || !take(line, R"(","n":)")) {
        return false;
    }
    const std::optional<std::uint64_t> n = integer(line);
    if (!n || !take(line, R"(,"command":")")) {
        return false;
    }
    if (take(line, name(Command::import))) {
        head.command = Command::import;
    } else if (take(line, name(Command::write))) {
        head.command = Command::write;
    } else {
        return false;
    }
    if (!take(line, R"(","writer":")")) {
        return false;
    }
    std::size_t w = 0;
    while (w < line.size() && writer_byte(line[w])) {
        ++w;
    }
    if (w == 0 || w > 64) {
        return false;
    }
    head.writer = memory::Inline<64>(line.substr(0, w));
    line.remove_prefix(w);
    if (!take(line, R"(","entries":)")) {
        return false;
    }
    const std::optional<std::uint64_t> count = integer(line);
    if (!count || line != "}") {
        return false;
    }
    head.n = *n;
    head.entries = *count;
    return true;
}

// The next line, its LF dropped, taken from the front of rest; none if no
// LF ends it.
std::optional<std::string_view> next_line(std::string_view& rest) noexcept {
    const std::size_t end = rest.find('\n');
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view line = rest.substr(0, end);
    rest.remove_prefix(end + 1);
    return line;
}

void append_decimal(memory::String& out, std::uint64_t n) {
    std::array<char, 20> text{};
    std::size_t at = text.size();
    do {
        text.at(--at) = static_cast<char>('0' + n % 10);
        n /= 10;
    } while (n != 0);
    out.append(std::string_view(text.data(), text.size()).substr(at));
}

}  // namespace

std::string_view name(Command command) noexcept { return command == Command::import ? "import" : "write"; }

std::optional<Name> parse_name(std::string_view name) noexcept {
    // replace's temporary of a <n>.tmp: .<n>.tmp.<16 hex>.tmp
    if (name.starts_with('.')) {
        constexpr std::size_t size = 1 + digits + 4 + 1 + hex_digits + 4;
        if (name.size() != size || name.substr(1 + digits, 5) != ".tmp." || !name.ends_with(".tmp")) {
            return std::nullopt;
        }
        for (const char c : name.substr(1 + digits + 5, hex_digits)) {
            if (!lower_hex(c)) {
                return std::nullopt;
            }
        }
        const std::optional<std::uint64_t> n = ten(name.substr(1, digits));
        return n ? std::optional<Name>(Name{*n, Form::temporary}) : std::nullopt;
    }
    const std::optional<std::uint64_t> n = ten(name.substr(0, digits));
    if (!n) {
        return std::nullopt;
    }
    for (std::size_t f = 0; f < suffixes.size(); ++f) {
        if (name.substr(digits) == suffixes.at(f)) {
            return Name{*n, static_cast<Form>(f)};
        }
    }
    return std::nullopt;
}

memory::Inline<32> file_name(std::uint64_t n, Form form) noexcept {
    if (n > last || form == Form::temporary) {
        memory::fail_fast("kept: a file name past ten digits or of no kept form");
    }
    std::array<char, 32> text{};
    for (std::size_t i = digits; i-- > 0;) {
        text.at(i) = static_cast<char>('0' + n % 10);
        n /= 10;
    }
    const std::string_view suffix = suffixes.at(static_cast<std::size_t>(form));
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        text.at(digits + i) = suffix[i];
    }
    return memory::Inline<32>(std::string_view(text.data(), digits + suffix.size()));
}

void bytes(const Head& head, std::span<const build::Built> built, memory::String& out) {
    const std::string_view writer = head.writer.view();
    bool fits = !writer.empty();
    for (const char c : writer) {
        fits = fits && writer_byte(c);
    }
    if (!fits) {
        memory::fail_fast("kept: a writer outside the writer's syntax");
    }
    out.clear();
    std::size_t size = 128 + writer.size();
    for (const build::Built& b : built) {
        size += b.line.size() + 1 + (b.body ? b.record.size() + 1 : 0);
    }
    out.reserve(size);
    out.append(R"({"kept":")").append(kept_form).append(R"(","n":)");
    append_decimal(out, head.n);
    out.append(R"(,"command":")").append(name(head.command)).append(R"(","writer":")").append(writer);
    out.append(R"(","entries":)");
    append_decimal(out, built.size());
    out.append("}\n");
    for (const build::Built& b : built) {
        out.append(b.line).push_back('\n');
        if (b.body) {
            out.append(b.record).push_back('\n');
        }
    }
}

void parse(std::string_view bytes, std::string_view name, memory::Arena& arena, Head& head,
           memory::Vector<LineView>& lines, Hooks hooks) {
    static_cast<void>(hooks);
    lines.clear();
    head = Head{};
    const std::optional<Name> named = parse_name(name);
    std::string_view rest = bytes;
    const std::optional<std::string_view> first = next_line(rest);
    if (!named || named->form == Form::tmp || named->form == Form::temporary || !first || !parse_head(*first, head) ||
        head.n != named->n || head.entries == 0) {
        refuse_corrupt(name);
    }
    std::span<std::byte> work;
    try {
        work = std::span<std::byte>(static_cast<std::byte*>(arena.allocate(work_bytes, alignof(std::max_align_t))),
                                    work_bytes);
        while (!rest.empty()) {
            LineView line;
            const std::optional<std::string_view> header_line = next_line(rest);
            if (!header_line || header_line->size() > header_bytes || lines.size() == head.entries) {
                refuse_corrupt(name);
            }
            line.header = *header_line;
            {
                memory::Arena work_arena(work);  // given back whole at the line's end
#if defined(QIVEN_FAULTS)
                work_arena.attach(hooks.work);
#endif
                namespace header = entry::header;
                try {
                    const auto h = header::parse_line(line.header, memory::Allocator<char>(work_arena));
                    const std::optional<std::string_view> id = h.text(header::Slot::id);
                    if (!id || id->empty() || id->size() > 72) {
                        refuse_corrupt(name);
                    }
                    line.id = memory::Inline<72>(*id);
                    line.has_body = h.present(header::Slot::content);
                } catch (const ParseError&) {
                    refuse_corrupt(name);
                } catch (const header::HeaderError&) {
                    refuse_corrupt(name);
                }
            }
            if (line.has_body) {
                const std::optional<std::string_view> body = next_line(rest);
                if (!body) {
                    refuse_corrupt(name);
                }
                line.body = *body;
            }
            lines.push_back(line);
        }
        if (lines.size() != head.entries) {
            refuse_corrupt(name);
        }
    } catch (...) {
        lines = memory::Vector<LineView>(lines.get_allocator());
        if (!work.empty()) {
            arena.deallocate(work.data(), work.size());
        }
        throw;
    }
    arena.deallocate(work.data(), work.size());
}

namespace detail {

void join(std::string_view store, std::string_view name, memory::String& out) {
    while (!store.empty() && (store.back() == '/' || store.back() == '\\')) {
        store.remove_suffix(1);
    }
    out.clear();
    out.reserve(store.size() + 1 + directory.size() + 1 + name.size());
    out.append(store).append("/").append(directory);
    if (!name.empty()) {
        out.append("/").append(name);
    }
}

std::uint64_t highest(std::span<const memory::String> names) noexcept {
    std::uint64_t top = 0;
    for (const memory::String& name : names) {
        const std::optional<Name> got = parse_name(name);
        if (got && got->n > top) {
            top = got->n;
        }
    }
    return top;
}

memory::Inline<128> at(std::string_view name) noexcept {
    std::array<char, 128> text{};
    std::size_t size = 0;
    for (const std::string_view part : {directory, std::string_view(name.empty() ? "" : "/"), name}) {
        for (const char c : part) {
            if (size < text.size()) {
                text.at(size++) = c;
            }
        }
    }
    return memory::Inline<128>(std::string_view(text.data(), size));
}

}  // namespace detail

}  // namespace qiven3::write::kept
