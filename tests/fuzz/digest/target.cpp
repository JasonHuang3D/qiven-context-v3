// fuzz/digest, package 2's fuzz target: the input split at each of its
// first ten LFs into an entry's eleven fields, the last taking the rest and
// those it does not reach absent; then frame, entry_id, plain and keyed
// under the fixtures' key, each into a string on an arena of its own,
// 4 KiB beside its input, the operation's budget. A finding is any
// exception, a crash or a hang; thrown here, a framed size but
// framed_size's, an id but the plain digest of the framed bytes, a plain
// digest's text of another length, a keyed digest but hmac's over the
// key's bytes, or an arena holding bytes after its result's end.
// LLVMFuzzerTestOneInput is libFuzzer's entry, and the regression case's,
// which calls it without libFuzzer.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/digest.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace {

namespace digest = qiven3::digest;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::String;
using Fake = qiven3::platform::fake::FakePlatform;

constexpr std::size_t beside = 4096;  // each operation's budget beside its input
constexpr std::size_t entry_fields = 11;

struct alignas(4096) Page {
    std::array<std::byte, 4096> bytes{};
};

void finding(std::string_view what) { throw std::logic_error("fuzz/digest: " + std::string(what)); }

// op's result, written on an arena of its budget and copied out; past the
// budget the arena's MemoryError, which nothing here catches.
template <typename Op>
std::string within(std::size_t input, const Op& op) {
    const std::optional<std::size_t> budget = qiven3::memory::add(beside, input);
    if (!budget) {
        finding("an input past std::size_t");
    }
    std::vector<Page> pages((*budget + sizeof(Page) - 1) / sizeof(Page));
    Arena arena(std::as_writable_bytes(std::span<Page>(pages)).first(*budget));
    std::string text;
    {
        String out{Allocator<char>(arena)};
        op(out);
        text.assign(out.begin(), out.end());
    }
    if (arena.used() != 0) {
        finding("an arena holds bytes after its result's end");
    }
    return text;
}

std::string hex(const digest::Bytes& bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t b : bytes) {
        out += digits[b >> 4U];
        out += digits[b & 0x0FU];
    }
    return out;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    std::string text(size, '\0');
    for (std::size_t i = 0; i < size; ++i) {
        text[i] = static_cast<char>(input[i]);
    }
    const std::string_view view(text);
    std::array<digest::Field, entry_fields> fields{};
    std::size_t start = 0;
    std::size_t n = 0;
    for (std::size_t lf = view.find('\n'); lf != std::string_view::npos && n + 1 < entry_fields;
         lf = view.find('\n', start)) {
        fields[n++] = view.substr(start, lf - start);
        start = lf + 1;
    }
    fields[n] = view.substr(start);
    const std::size_t field_bytes = size - n;  // the LFs that split them are no field's

    const std::string framed = within(field_bytes, [&](String& out) { digest::frame(fields, out); });
    if (framed.size() != digest::framed_size(fields)) {
        finding("a framed size but framed_size's");
    }
    const std::string id = within(field_bytes, [&](String& out) { digest::entry_id(fields, out); });
    if (id != within(framed.size(), [&](String& out) { digest::plain(framed, out); })) {
        finding("an id but the plain digest of the framed bytes");
    }
    if (within(size, [&](String& out) { digest::plain(view, out); }).size() != 7 + 2 * digest::bytes) {
        finding("a plain digest's text but sha256: and 64 digits");
    }

    std::array<std::byte, digest::bytes> material{};
    std::string material_text(digest::bytes, '\0');
    for (std::size_t i = 0; i < digest::bytes; ++i) {
        material[i] = static_cast<std::byte>(i);
        material_text[i] = static_cast<char>(i);
    }
    Fake fake{qiven3::platform::fake::Config{}};
    const digest::Key<Fake> key(fake, material);
    const std::string keyed = within(size, [&](String& out) { digest::keyed(key, view, out); });
    if (keyed != "hmac-sha256:" + hex(digest::hmac(material_text, view))) {
        finding("a keyed digest but hmac's over the key's bytes");
    }
    return 0;
}
