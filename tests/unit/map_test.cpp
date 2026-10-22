// FA2's map case: a map entry reads stale after its object changes, on M1
// and M2 of the fixtures, the mark computed from the bytes given, as the
// map check does from a blob. The mapped file's version 1 (57 bytes) under
// M1's key is M1's digest: M1 fresh; version 2 (75 bytes) under M1's key is
// another: M1 stale; M2 maps version 2 under its own key: M2 fresh; the
// file deleted: M2 stale, its object missing. The case asserts the four
// marks, the three digests and both ids, each id recomputed from its
// header's fields; then the marks file of the run at 12:06 written and
// read back, M2's mark with its at as served takes it, and M1, superseded
// and so not read, with none (unchecked).

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "entry/header.hpp"
#include "entry/projectmap.hpp"
#include "entry/projectmap_files.hpp"
#include "entry/served.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace projectmap = qiven3::entry::projectmap;
namespace header = qiven3::entry::header;
namespace served = qiven3::entry::served;
namespace canon = qiven3::canon;
namespace digest = qiven3::digest;
namespace fixtures = qiven3::test::fixtures;
using Fake = qiven3::platform::fake::FakePlatform;
using served::Mark;

constexpr std::string_view M1_ID = "sha256:cdc1ccfaaa779c70066a3cd1e3c4da47904c915acb77b83e43de309f7ed36700";
constexpr std::string_view M2_ID = "sha256:4d910a4eee1c81bc7e68ef65f0c7774ec5f020d04c9730f111d1ae6fdfe5b071";
constexpr std::string_view M1_V1 =
    "hmac-sha256:434694c726e2f20586c58c2071b4396cc854d22fd08344a826e58756495ca71d";
constexpr std::string_view M1_V2 =
    "hmac-sha256:ddfce6d48d972a3c2053c70d1f330622397fab820afca10b310a13049b1c6cbd";
constexpr std::string_view M2_V2 =
    "hmac-sha256:e809f12c3d9fdb3386b0dec4db5ea27db09404b31d0b01b8fcd5d49710c04ab6";

// A fixture map: its header, its content's value and its key.
struct Map {
    header::Header<> h;
    canon::Basic<> v;
    fixtures::KeyBytes key{};
};
Map map(const canon::Basic<>& all, std::string_view name) {
    const std::optional<fixtures::EntryView> x = fixtures::find_entry(all, name);
    Map m;
    if (!x || !x->content || !x->key) {
        return m;
    }
    m.h = header::parse_line(x->header);
    canon::parse(*x->content, m.v);
    m.key = fixtures::key_bytes(*x->key).value_or(fixtures::KeyBytes{});
    return m;
}

// The mapped file's version's bytes, or none.
std::optional<std::string> version(const canon::Basic<>& all, std::int64_t n) {
    for (std::size_t i = 0; i < fixtures::mapped_count(all); ++i) {
        const fixtures::FileView f = fixtures::mapped(all, i);
        std::string bytes;
        if (f.version == n && f.repository == "qiven-context-v3" && f.path == "qiven3/entry.py" &&
            fixtures::from_hex(f.hex, bytes)) {
            return bytes;
        }
    }
    return std::nullopt;
}

// The id recomputed from a header's fields.
std::string id_of(const header::Header<>& h) {
    std::string out;
    const auto fields = h.fields();
    digest::entry_id(std::span<const digest::Field>(fields), out);
    return out;
}

}  // namespace

QIVEN3_CASE(map, fa2_stale) {
    canon::Basic<> all;
    fixtures::load_entries(all);
    const Map m1 = map(all, "M1");
    const Map m2 = map(all, "M2");
    const std::optional<std::string> v1 = version(all, 1);
    const std::optional<std::string> v2 = version(all, 2);
    CHECK(v1.has_value() && v2.has_value());
    if (!v1 || !v2) {
        return;
    }
    CHECK_EQ(v1->size(), 57U);
    CHECK_EQ(v2->size(), 75U);
    Fake fake(qiven3::platform::fake::Config{});
    const digest::Key<Fake> k1(fake, std::span<const std::byte, digest::bytes>(m1.key));
    const digest::Key<Fake> k2(fake, std::span<const std::byte, digest::bytes>(m2.key));

    // Both ids, as stored and recomputed.
    CHECK_EQ(m1.h.value(header::Slot::id), M1_ID);
    CHECK_EQ(m2.h.value(header::Slot::id), M2_ID);
    CHECK_EQ(id_of(m1.h), std::string(M1_ID));
    CHECK_EQ(id_of(m2.h), std::string(M2_ID));
    CHECK_EQ(m2.h.value(header::Slot::target), M1_ID);

    // The three digests.
    std::string d;
    digest::keyed(k1, *v1, d);
    CHECK_EQ(d, std::string(M1_V1));
    digest::keyed(k1, *v2, d);
    CHECK_EQ(d, std::string(M1_V2));
    digest::keyed(k2, *v2, d);
    CHECK_EQ(d, std::string(M2_V2));

    // The four marks.
    CHECK(projectmap::mark(m1.h, m1.v, k1, *v1) == Mark::fresh);
    CHECK(projectmap::mark(m1.h, m1.v, k1, *v2) == Mark::stale);
    CHECK(projectmap::mark(m2.h, m2.v, k2, *v2) == Mark::fresh);
    CHECK(projectmap::mark(m2.h, m2.v, k2, projectmap::MISSING) == Mark::stale);
    // No bytes read: unchecked; a version's bytes under another's key: stale.
    CHECK(projectmap::mark(m2.h, m2.v, k2, std::nullopt) == Mark::unchecked);
    CHECK(projectmap::mark(m2.h, m2.v, k1, *v2) == Mark::stale);

    // The run at 12:06 marks M2 alone, M1 being superseded; its file read
    // back gives M2 fresh with its at, and M1 none, read unchecked.
    projectmap::Marks<> run;
    CHECK(run.set_at("2026-10-17T12:06:00.000Z"));
    CHECK(run.put(M2_ID, projectmap::mark(m2.h, m2.v, k2, *v2)));
    CHECK(run.put_read("qiven-context-v3", "0123456789abcdef0123456789abcdef01234567"));
    std::string file;
    projectmap::marks_bytes(run, file);
    const projectmap::Marks<> read = projectmap::parse_marks(file);
    const std::optional<served::MapMark> found = read.find(M2_ID);
    CHECK(found.has_value() && found->mark == Mark::fresh && found->when() == "2026-10-17T12:06:00.000Z");
    CHECK(!read.find(M1_ID).has_value());

    // mark refuses an entry not a map.
    bool kind = false;
    const header::Header<> claim = header::parse_line(R"({"basis":"claimed","kind":"claim"})");
    try {
        static_cast<void>(projectmap::mark(claim, m1.v, k1, *v1));
    } catch (const projectmap::MapError& e) {
        kind = e.code == projectmap::Code::kind;
    }
    CHECK(kind);
    // and a map content with no object digest.
    bool object = false;
    canon::Basic<> bare;
    canon::parse(R"({"object":{"type":"repo"}})", bare);
    try {
        static_cast<void>(projectmap::mark(m1.h, bare, k1, projectmap::MISSING));
    } catch (const projectmap::MapError& e) {
        object = e.code == projectmap::Code::object;
    }
    CHECK(object);
}
