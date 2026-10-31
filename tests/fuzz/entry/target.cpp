// fuzz/entry, package 4's fuzz target: the input split at its first LF
// into a stored header line and a body record, the record none when the
// input holds no LF; each read by the strict parser alone, then both by
// the shared validator's part 1, check_entry, on an arena of its own, 256
// KiB plus 8 times the input, the operation's budget. A typed refusal
// (ParseError, HeaderError, BodyError or ContentError) is a pass; anything
// else is a finding: any other exception, a crash or a hang, and, thrown
// here, a parse fault check_entry does not give first (the line's, then
// the record's), an admitted entry whose header or body does not write
// back the bytes it was read from, or an arena holding bytes once the
// entry is dropped. The digests' key is the fake platform's to wipe.
// entry_input is the regression case's entry; LLVMFuzzerTestOneInput
// libFuzzer's, in the fuzz binary alone.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "entry/body.hpp"
#include "entry/check.hpp"
#include "entry/content.hpp"
#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace {

namespace check = qiven3::entry::check;
namespace canon = qiven3::canon;
using qiven3::ParseCode;
using qiven3::ParseError;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using Fake = qiven3::platform::fake::FakePlatform;

constexpr std::size_t budget_base = 262144;  // the entry's checks' budget beside 8 times the input

struct alignas(4096) Page {
    std::array<std::byte, 4096> bytes{};
};

void finding(std::string_view what) { throw std::logic_error("fuzz/entry: " + std::string(what)); }

// The strict parser's verdict on bytes alone, on the heap.
std::optional<ParseCode> parsed(std::string_view data) {
    canon::Basic<> value;
    return canon::try_parse(data, value);
}

// check_entry's parse fault, or none: its other refusals a pass, the
// entry, admitted, written back.
std::optional<ParseCode> checked(Arena& arena, std::string_view line, std::optional<std::string_view> record) {
    Fake fake{qiven3::platform::fake::Config{}};
    try {
        const check::Entry<Fake, Allocator<char>> e =
            check::check_entry(fake, line, record, Allocator<char>(arena));
        std::string out;
        e.header().line(out);
        if (std::string_view(out) != line) {
            finding("an admitted header that does not write back its line");
        }
        if (e.has_body() != record.has_value()) {
            finding("an admitted entry with a body and no record, or none with one");
        }
        if (record) {
            e.body().line(out);
            if (std::string_view(out) != *record) {
                finding("an admitted body that does not write back its record");
            }
        }
    } catch (const ParseError& error) {
        return error.code;
    } catch (const qiven3::entry::header::HeaderError&) {
    } catch (const qiven3::entry::body::BodyError&) {
    } catch (const qiven3::entry::content::ContentError&) {
    }
    return std::nullopt;
}

}  // namespace

namespace qiven3::fuzz {

int entry_input(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    std::string text(size, '\0');
    for (std::size_t i = 0; i < size; ++i) {
        text[i] = static_cast<char>(input[i]);
    }
    const std::string_view view(text);
    const std::size_t lf = view.find('\n');
    const std::string_view line = view.substr(0, lf);
    const std::optional<std::string_view> record =
        lf == std::string_view::npos ? std::nullopt : std::optional<std::string_view>(view.substr(lf + 1));

    const std::optional<ParseCode> alone_line = parsed(line);
    const std::optional<ParseCode> alone_record = record ? parsed(*record) : std::nullopt;

    if (size > (SIZE_MAX - budget_base) / 8) {
        finding("an input past std::size_t");
    }
    const std::size_t budget = budget_base + 8 * size;
    std::vector<Page> pages((budget + sizeof(Page) - 1) / sizeof(Page));
    Arena arena(std::as_writable_bytes(std::span<Page>(pages)).first(budget));
    const std::optional<ParseCode> got = checked(arena, line, record);
    if (arena.used() != 0) {
        finding("an arena holds bytes once the entry is dropped");
    }
    const std::optional<ParseCode> want = alone_line ? alone_line : alone_record;
    if (got != want) {
        finding("a parse fault check_entry does not give first");
    }
    return 0;
}

}  // namespace qiven3::fuzz

#if defined(QIVEN3_FUZZ_BINARY)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    return qiven3::fuzz::entry_input(data, size);
}
#endif
