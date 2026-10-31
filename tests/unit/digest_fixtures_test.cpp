// The fixtures' cases: tests/fixtures/entries.json loaded through
// canon::parse, its layout checked, and each entry giving what the text
// pins: its content canonical and of its size, the content's keyed digest
// under the entry's own key equal to its header's, its header canonical
// and its id over its framed header, of its size; the mapped file's two
// versions, each under M1's and M2's keys; the layout's refusals and
// key_bytes' vectors in constant evaluation; the load within canon's
// budget, a byte short a MemoryError budget; and each allocation of the
// load failed in turn, a typed error with nothing held, the accessors
// taking none. The runner's count after each case holds too.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace canon = qiven3::canon;
namespace digest = qiven3::digest;
namespace json = qiven3::json;
namespace fixtures = qiven3::test::fixtures;
using digest::Field;
using fixtures::Code;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;
using Fake = qiven3::platform::fake::FakePlatform;
using Key = digest::Key<Fake>;

// Each entry the text pins, in the file's order: its canonical content's
// size (0 for none) and its framed header's.
struct Pinned {
    std::string_view name;
    std::size_t content;
    std::size_t framed;
};
constexpr std::array<Pinned, 12> pinned{{
    {"A", 511, 253},
    {"B", 512, 324},
    {"C", 276, 319},
    {"D", 360, 332},
    {"ACC", 326, 400},
    {"E", 0, 324},
    {"M1", 670, 316},
    {"M2", 685, 387},
    {"ACC2", 326, 394},
    {"MX", 43065, 319},
    {"L1", 745, 246},
    {"L2", 876, 388},
}};
constexpr std::size_t file_bytes = 61248;

std::optional<std::string_view> text_of(const canon::Basic<>& v, std::string_view name) {
    const std::optional<json::Index> at = v.find(0, name);
    return at ? std::optional<std::string_view>(v.text(*at)) : std::nullopt;
}

// A header line's 11 fields in framing order; seq's decimal held in seq.
std::array<Field, 11> fields_of(const canon::Basic<>& h, std::string& seq) {
    seq = std::to_string(h.integer(*h.find(0, "seq")));
    return {text_of(h, "tag"),    text_of(h, "scope"),  text_of(h, "lane"), Field(seq),
            text_of(h, "parent"), text_of(h, "kind"),   text_of(h, "writer"), text_of(h, "basis"),
            text_of(h, "at"),     text_of(h, "target"), text_of(h, "content")};
}

std::string keyed_under(Fake& fake, std::string_view hex, std::string_view data) {
    std::optional<fixtures::KeyBytes> material = fixtures::key_bytes(hex);
    if (!material) {
        return "no key";
    }
    const Key key(fake, std::span<const std::byte, 32>(*material));
    fake.wipe(std::span<std::byte>(*material));
    std::string out;
    digest::keyed(key, data, out);
    return out;
}

qiven3::platform::fake::Config seeded() {
    qiven3::platform::fake::Config config;
    config.seed = 0;
    return config;
}

// The layout's vectors: a document's first fault, or root 99 where the
// parser refuses it.
constexpr std::optional<fixtures::LoadError> fault_of(std::string_view doc) {
    canon::Basic<> v;
    if (canon::try_parse(doc, v)) {
        return fixtures::LoadError{Code::root, 99};
    }
    return fixtures::layout(v);
}
constexpr bool refused(std::string_view doc, Code code, std::size_t item) {
    const std::optional<fixtures::LoadError> f = fault_of(doc);
    return f && f->code == code && f->item == item;
}

constexpr std::string_view k64 = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
constexpr std::string_view good =
    "{\"entries\":[{\"content\":\"{}\",\"header\":\"{}\",\"key\":\"000102030405060708090a0b0c0d0e0f10111213141516"
    "1718191a1b1c1d1e1f\",\"name\":\"A\"},{\"header\":\"{}\",\"name\":\"E\"}],\"files\":[{\"bytes\":\"0a\",\"pa"
    "th\":\"p\",\"repository\":\"r\",\"version\":1}]}";
static_assert(!fault_of(good));
static_assert(!fault_of("{\"entries\":[],\"files\":[]}"));
static_assert(refused("[]", Code::root, 0));
static_assert(refused("{\"entries\":[]}", Code::root, 0));
static_assert(refused("{\"entries\":{},\"files\":[]}", Code::root, 0));
static_assert(refused("{\"entries\":[],\"extra\":1,\"files\":[]}", Code::root, 0));
static_assert(refused("{\"entries\":[{\"header\":\"{}\",\"name\":\"E\"},{\"name\":\"F\"}],\"files\":[]}", Code::entry, 1));
static_assert(refused("{\"entries\":[{\"header\":\"{}\",\"name\":\"E\"},{\"header\":\"{}\",\"name\":\"E\"}],\"files\":[]}",
                      Code::entry, 1));
static_assert(refused("{\"entries\":[{\"header\":\"{}\",\"key\":\"00\",\"name\":\"E\"}],\"files\":[]}", Code::entry, 0));
static_assert(refused("{\"entries\":[{\"header\":1,\"name\":\"E\"}],\"files\":[]}", Code::entry, 0));
static_assert(refused("{\"entries\":[{\"header\":\"{}\",\"name\":\"E\",\"seq\":1}],\"files\":[]}", Code::entry, 0));
static_assert(refused("{\"entries\":[],\"files\":[{\"bytes\":\"0\",\"path\":\"p\",\"repository\":\"r\",\"version\":1}]}",
                      Code::file, 0));
static_assert(refused("{\"entries\":[],\"files\":[{\"bytes\":\"0A\",\"path\":\"p\",\"repository\":\"r\",\"version\":1}]}",
                      Code::file, 0));
static_assert(refused("{\"entries\":[],\"files\":[{\"bytes\":\"\",\"path\":\"p\",\"repository\":\"r\",\"version\":0}]}",
                      Code::file, 0));
static_assert(refused("{\"entries\":[],\"files\":[{\"bytes\":\"\",\"path\":\"p\",\"version\":1}]}", Code::file, 0));
static_assert(refused("{\"entries\": [],\"files\":[]}", Code::root, 99));

// key_bytes' vectors: 64 lowercase digits, nothing else.
static_assert(fixtures::key_bytes(k64).has_value() && (*fixtures::key_bytes(k64))[0] == std::byte{0x00} &&
              (*fixtures::key_bytes(k64))[31] == std::byte{0x1f});
static_assert(!fixtures::key_bytes(k64.substr(1)));
static_assert(!fixtures::key_bytes("000102030405060708090A0B0C0D0E0F101112131415161718191a1b1c1d1e1f"));
static_assert(!fixtures::key_bytes("g00102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1"));

constexpr std::size_t budget_for(std::size_t input) { return 65536 + 4 * input; }

}  // namespace

QIVEN3_CASE(fixtures, files) {
    CHECK_EQ(fixtures::file_count(), 1U);
    CHECK_EQ(fixtures::file_name(0), std::string_view("entries.json"));
    const std::optional<std::string_view> data = fixtures::bytes("entries.json");
    CHECK(data.has_value());
    CHECK_EQ(data->size(), file_bytes);
    CHECK(!fixtures::bytes("absent.json"));
    canon::Basic<> v;
    bool refused_absent = false;
    try {
        fixtures::load("absent.json", v);
    } catch (const fixtures::LoadError& e) {
        refused_absent = e.code == Code::no_file;
    }
    CHECK(refused_absent);
    CHECK(v.empty());
    fixtures::load("entries.json", v);
    CHECK(!fixtures::layout(v));
}

QIVEN3_CASE(fixtures, entries) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    REQUIRE(fixtures::entry_count(v) == pinned.size());
    Fake fake(seeded());
    for (std::size_t i = 0; i < pinned.size(); ++i) {
        const fixtures::EntryView e = fixtures::entry(v, i);
        CHECK_EQ(e.name, pinned[i].name);
        canon::Basic<> h;
        canon::parse(e.header, h);
        if (pinned[i].content == 0) {
            CHECK(!e.key && !e.content && !h.find(0, "content"));
        } else {
            CHECK(e.key.has_value() && e.content.has_value());
            canon::Basic<> c;
            canon::parse(*e.content, c);
            CHECK_EQ(e.content->size(), pinned[i].content);
            CHECK_EQ(keyed_under(fake, *e.key, *e.content), std::string(*text_of(h, "content")));
        }
        std::string seq;
        const std::array<Field, 11> fields = fields_of(h, seq);
        CHECK_EQ(digest::framed_size(fields), pinned[i].framed);
        std::string id;
        digest::entry_id(fields, id);
        CHECK_EQ(id, std::string(*text_of(h, "id")));
        const std::optional<fixtures::EntryView> found = fixtures::find_entry(v, pinned[i].name);
        CHECK(found.has_value() && found->header == e.header);
    }
    CHECK(!fixtures::find_entry(v, "Z"));
    // L1 carries no parent; L2 revises L1, its parent and its target L1's id.
    canon::Basic<> l1;
    canon::Basic<> l2;
    canon::parse(fixtures::find_entry(v, "L1")->header, l1);
    canon::parse(fixtures::find_entry(v, "L2")->header, l2);
    CHECK(!l1.find(0, "parent"));
    CHECK_EQ(*text_of(l2, "parent"), *text_of(l1, "id"));
    CHECK_EQ(*text_of(l2, "target"), *text_of(l1, "id"));
}

QIVEN3_CASE(fixtures, mapped) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    CHECK_EQ(fixtures::mapped_count(v), 2U);
    Fake fake(seeded());
    const fixtures::EntryView m1 = *fixtures::find_entry(v, "M1");
    const fixtures::EntryView m2 = *fixtures::find_entry(v, "M2");
    const auto object_digest = [](std::string_view content) {
        canon::Basic<> c;
        canon::parse(content, c);
        return std::string(c.text(*c.find(*c.find(0, "object"), "digest")));
    };
    struct Version {
        std::int64_t version;
        std::size_t size;
        std::string under_m1;
    };
    const std::vector<Version> versions = {
        {1, 57, object_digest(*m1.content)},
        {2, 75, "hmac-sha256:ddfce6d48d972a3c2053c70d1f330622397fab820afca10b310a13049b1c6cbd"},
    };
    for (std::size_t i = 0; i < versions.size(); ++i) {
        const fixtures::FileView f = fixtures::mapped(v, i);
        CHECK_EQ(f.repository, std::string_view("qiven-context-v3"));
        CHECK_EQ(f.path, std::string_view("qiven3/entry.py"));
        CHECK_EQ(f.version, versions[i].version);
        std::string data;
        CHECK(fixtures::from_hex(f.hex, data));
        CHECK_EQ(data.size(), versions[i].size);
        CHECK_EQ(keyed_under(fake, *m1.key, data), versions[i].under_m1);
    }
    std::string v2;
    CHECK(fixtures::from_hex(fixtures::mapped(v, 1).hex, v2));
    CHECK_EQ(keyed_under(fake, *m2.key, v2), object_digest(*m2.content));
    std::string kept = "kept";
    CHECK(!fixtures::from_hex("0", kept));
    CHECK(!fixtures::from_hex("0G", kept));
    CHECK_EQ(kept, std::string("kept"));
}

QIVEN3_CASE(fixtures, layout_refused) {
    // The constant-evaluated vectors above, once more at run time, and
    // parse_entries' throw, out unchanged.
    CHECK(!fault_of(good));
    CHECK(refused("{\"entries\":[{\"header\":\"{}\",\"name\":\"E\"},{\"header\":\"{}\",\"name\":\"E\"}],\"files\":[]}",
                  Code::entry, 1));
    canon::Basic<> v;
    bool thrown = false;
    try {
        fixtures::parse_entries("{\"entries\":[],\"files\":[{\"bytes\":\"0\",\"path\":\"p\",\"repository\":\"r\",\"versi"
                                "on\":1}]}",
                                v);
    } catch (const fixtures::LoadError& e) {
        thrown = e.code == Code::file && e.item == 0;
    }
    CHECK(thrown);
    CHECK(v.empty());
    bool parse_thrown = false;
    try {
        fixtures::parse_entries("{\"entries\": [],\"files\":[]}", v);
    } catch (const qiven3::ParseError& e) {
        parse_thrown = e.code == qiven3::ParseCode::not_canonical;
    }
    CHECK(parse_thrown);
    CHECK(v.empty());
}

QIVEN3_CASE(fixtures, load_budget) {
    const std::size_t budget = budget_for(file_bytes);
    Arena& arena = qiven3_ctx.counted.arena(budget);
    std::size_t peak = 0;
    {
        canon::Value v{Allocator<char>(arena)};
        fixtures::load_entries(v);
        CHECK_EQ(fixtures::entry_count(v), pinned.size());
        peak = arena.peak();
    }
    std::cout << "fixtures.load_budget: input " << file_bytes << " bytes, peak " << peak << ", budget " << budget
              << "\n";
    CHECK(peak <= budget);
    CHECK(arena.used() == 0);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    // As canon's: under MSVC's debug containers the short budget ends the
    // process in a noexcept constructor, so this half runs elsewhere.
    Arena& shorter = qiven3_ctx.counted.arena(peak - 1);
    canon::Value v{Allocator<char>(shorter)};
    bool refused_short = false;
    try {
        fixtures::load_entries(v);
    } catch (const MemoryError& e) {
        refused_short = e.code == MemoryCode::budget;
    }
    CHECK(refused_short);
    CHECK(v.empty());
#else
    std::cout << "fixtures.load_budget: a byte short not run, under MSVC's debug containers\n";
#endif
}

QIVEN3_CASE(fixtures, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    std::vector<std::byte> pages(budget_for(file_bytes));
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    const auto check = [&](std::size_t) { return arena->used() == 0; };
    std::size_t entries = 0;
    const auto load = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        canon::Value v{Allocator<char>(*arena)};
        fixtures::load_entries(v);
        entries = fixtures::entry_count(v);
    };
    const qiven3::test::FaultRuns loads =
        qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "fixtures.load", countdown, load, check);
    std::cout << "fixtures.faults: load's allocations " << loads.typed << ", completed at " << loads.completed_at
              << "\n";
    // canon's four: the scan's nodes, text and keys' check, then the
    // comparison's sorted members; the layout check none.
    CHECK_EQ(loads.typed, 4U);
    CHECK_EQ(loads.completed_at, 5U);
    CHECK_EQ(loads.untyped, 0U);
    CHECK_EQ(loads.unchecked, 0U);
    CHECK_EQ(entries, pinned.size());

    // The accessors over a loaded value take no allocation; from_hex one.
    canon::Basic<> loaded;
    fixtures::load_entries(loaded);
    std::size_t read = 0;
    const auto access = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        read = 0;
        for (std::size_t i = 0; i < fixtures::entry_count(loaded); ++i) {
            const fixtures::EntryView e = fixtures::entry(loaded, i);
            read += fixtures::find_entry(loaded, e.name).has_value() ? 1U : 0U;
            read += e.key && fixtures::key_bytes(*e.key) ? 1U : 0U;
        }
        read += fixtures::layout(loaded) ? 0U : 1U;
        read += fixtures::mapped(loaded, 1).hex.size();
    };
    const qiven3::test::FaultRuns accesses =
        qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "fixtures.access", countdown, access, check);
    CHECK_EQ(accesses.typed, 0U);
    CHECK_EQ(accesses.completed_at, 1U);
    CHECK_EQ(read, 12U + 11U + 1U + 150U);
    std::size_t bytes = 0;
    const auto decode = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        qiven3::memory::String out{Allocator<char>(*arena)};
        CHECK(fixtures::from_hex(fixtures::mapped(loaded, 0).hex, out));
        bytes = out.size();
    };
    const qiven3::test::FaultRuns decodes =
        qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "fixtures.from_hex", countdown, decode, check);
    CHECK_EQ(decodes.typed, 1U);
    CHECK_EQ(decodes.completed_at, 2U);
    CHECK_EQ(decodes.untyped, 0U);
    CHECK_EQ(bytes, 57U);
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
