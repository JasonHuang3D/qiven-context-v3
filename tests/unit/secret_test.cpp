// secret's cases: the twelve patterns, their names content's; a header hit
// refused with its pattern and the field's path, scope's before lane's,
// whatever the content holds; the content's strings and their paths in the
// value's order, evaluated by the compiler; each table fixture and clearing
// fixture built when the case runs, in a content string, its own pattern
// alone hitting it or none, at its path; auth-header, cli-secret and
// tencent-akid in English and Chinese text at several depths, the hits in
// the content's order and each string's in the patterns'; no hit on any
// fixture entry's header or content, the pinned ids and digests, nor on
// 4,000 fresh SHA-256 and HMAC digests.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "entry/content_fields.hpp"
#include "entry/header.hpp"
#include "entry/matcher.hpp"
#include "entry/secret.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "qiven/runner/runner.hpp"
#include "support/fixtures/fixtures.hpp"
#include "support/fixtures/patterns.hpp"

namespace {

namespace secret = qiven3::entry::secret;
namespace header = qiven3::entry::header;
namespace matcher = qiven3::entry::matcher;
namespace content = qiven3::entry::content;
namespace canon = qiven3::canon;
namespace json = qiven3::json;
namespace digest = qiven3::digest;
namespace fixtures = qiven3::test::fixtures;

#define ID "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

constexpr header::FieldsView fields(std::string_view scope, std::string_view lane) {
    header::FieldsView f;
    f.scope = scope;
    f.lane = lane;
    f.kind = "claim";
    f.writer = "implementer";
    f.basis = "observed";
    f.at = "2026-10-17T09:30:00.000Z";
    f.id = ID;
    return f;
}

// The strings a content's walk visits, each path=text, in order.
constexpr std::string visited(std::string_view data) {
    canon::Basic<> v;
    canon::parse(data, v);
    std::string out;
    secret::detail::strings(v, [&](std::string_view text, std::string_view path) {
        out.append(path);
        out.push_back('=');
        out.append(text);
        out.push_back(';');
    });
    return out;
}
static_assert(visited(R"({"a":"x","b":[{"c":"y"},"z",[1,"w"]],"d":{"e":"v","f":true}})") ==
              "a=x;b[0].c=y;b[1]=z;b[2][1]=w;d.e=v;");
static_assert(visited(R"("x")") == "=x;" && visited("[]").empty() && visited(R"({"a":[[["y"]]]})") == "a[0][0][0]=y;");

// Whether the header's first hit is name at path, over an empty content.
bool header_hit(std::string_view scope, std::string_view lane, std::string_view name,
                          std::string_view path) {
    const header::Header<> h = header::make(fields(scope, lane));
    const canon::Basic<> none;
    secret::Hits<std::allocator<char>> out;
    const std::optional<secret::SecretError> e = secret::try_check(h, none, out);
    return e && secret::name(e->pattern) == name && e->path.view() == path && out.empty();
}

// Each hit as name@path, joined by ;.
std::string listed(const secret::Hits<std::allocator<char>>& hits) {
    std::string out;
    for (const secret::Hit& h : hits) {
        out += std::string(secret::name(h.pattern)) + "@" + std::string(h.path.view()) + ";";
    }
    return out;
}

// A content of one string in evidence[0].command, beside a plain words.
canon::Basic<> one(std::string_view text) {
    canon::Basic<> v;
    json::Writer<std::allocator<char>> w(v);
    w.open(json::Kind::object);
    w.string("evidence");
    w.open(json::Kind::array);
    w.open(json::Kind::object);
    w.string("command");
    w.string(text);
    w.string("type");
    w.string("command");
    w.close();
    w.close();
    w.string("words");
    w.string("a plain claim");
    w.close();
    return v;
}

std::string rep(std::string_view s, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += s;
    }
    return out;
}

}  // namespace

QIVEN_CASE(secret, patterns) {
    CHECK_EQ(secret::PATTERNS.size(), 12U);
    for (std::size_t p = 0; p < secret::PATTERNS.size(); ++p) {
        CHECK(secret::name(static_cast<std::uint8_t>(p)) == content::pattern_names.at(p));
        const matcher::Basic<> a = matcher::compile(secret::PATTERNS.at(p).text.view());
        CHECK(!a.whole());
        CHECK(a.positions() <= matcher::max_positions);
    }
}

QIVEN_CASE(secret, header) {
    const canon::Basic<> held = one("--token\x20" + rep("t", 20));
    const auto refused = [&](std::string_view scope, std::string_view lane) -> std::string {
        const header::Header<> h = header::make(fields(scope, lane));
        try {
            (void)secret::check(h, held);
        } catch (const secret::SecretError& e) {
            return std::string(secret::name(e.pattern)) + "@" + std::string(e.path.view());
        }
        return "none";
    };
    const std::string sk_scope = "project:sk-" + rep("a", 32);
    const std::string xox_lane = "xoxb-1-" + rep("a", 8);
    CHECK_EQ(refused(sk_scope, "implementer"), std::string("sk-key@header.scope"));
    CHECK_EQ(refused("project:qiven", xox_lane), std::string("slack-xox@header.lane"));
    // Both: the scope first, the fields in their order.
    CHECK_EQ(refused(sk_scope, xox_lane), std::string("sk-key@header.scope"));
    CHECK_EQ(refused("project:qiven", "implementer"), std::string("none"));
    CHECK(header_hit("project:qiven", xox_lane, "slack-xox", "header.lane"));
    CHECK(!header_hit("project:qiven", "implementer", "slack-xox", "header.lane"));
}

QIVEN_CASE(secret, fixtures) {
    const header::Header<> h = header::make(fields("project:qiven", "implementer"));
    std::size_t hits = 0;
    std::size_t misses = 0;
    for (const fixtures::RecipeView& r : fixtures::recipes) {
        const std::string text = fixtures::text(r);
        const std::string got = listed(secret::check(h, one(text)));
        const std::string want =
            r.hit ? std::string(secret::name(static_cast<std::uint8_t>(r.pattern))) + "@evidence[0].command;" : "";
        if (got != want) {
            std::cout << "secret.fixtures: pattern " << r.pattern << ", fixture " << hits + misses << ": " << got
                      << "\n";
        }
        CHECK_EQ(got, want);
        ++(r.hit ? hits : misses);
    }
    std::cout << "secret.fixtures: " << hits << " hits, " << misses << " misses\n";
    CHECK_EQ(hits + misses, fixtures::recipes.size());
}

QIVEN_CASE(secret, languages) {
    // auth-header, cli-secret and tencent-akid, in English and in Chinese.
    const std::string auth_en = "Authorization: Bearer " + rep("a", 20);
    const std::string auth_zh = "\xe8\xaf\xb7\xe6\xb1\x82\xe5\xa4\xb4 Authorization: Bearer " + rep("a1", 10) +
                                " \xe5\xb7\xb2\xe8\xbf\x87\xe6\x9c\x9f";
    const std::string cli_en = "mysql --password " + rep("p", 8);
    const std::string cli_zh = "\xe7\x94\xa8 --token " + rep("t", 20) + " \xe7\x99\xbb\xe5\xbd\x95";
    const std::string akid_en = "SecretId=AKID" + rep("a1B2", 8);
    const std::string akid_zh = "\xe5\xaf\x86\xe9\x92\xa5 AKID" + rep("Z", 32);
    canon::Basic<> v;
    json::Writer<std::allocator<char>> w(v);
    w.open(json::Kind::object);
    w.string("evidence");
    w.open(json::Kind::array);
    for (const std::string& s : {cli_en, auth_zh, "--token=" + rep("t", 20) + " " + akid_en}) {
        w.open(json::Kind::object);
        w.string("command");
        w.string(s);
        w.close();
    }
    w.close();
    w.string("subject");
    w.string(auth_en);
    w.string("tags");
    w.open(json::Kind::array);
    w.string("plain");
    w.string(cli_zh);
    w.close();
    w.string("words");
    w.string(akid_zh);
    w.close();
    const header::Header<> h = header::make(fields("project:qiven", "implementer"));
    const std::string got = listed(secret::check(h, v));
    std::cout << "secret.languages: " << got << "\n";
    CHECK_EQ(got, std::string("cli-secret@evidence[0].command;auth-header@evidence[1].command;"
                              "cli-secret@evidence[2].command;tencent-akid@evidence[2].command;"
                              "auth-header@subject;cli-secret@tags[1];tencent-akid@words;"));
}

QIVEN_CASE(secret, pinned) {
    // No pinned id or digest, header or content, hits.
    canon::Basic<> all;
    fixtures::load_entries(all);
    std::size_t entries = 0;
    for (std::size_t i = 0; i < fixtures::entry_count(all); ++i) {
        const fixtures::EntryView e = fixtures::entry(all, i);
        const header::Header<> h = header::parse_line(e.header);
        canon::Basic<> c;
        if (e.content) {
            canon::parse(*e.content, c);
        }
        const std::string got = listed(secret::check(h, c));
        if (!got.empty()) {
            std::cout << "secret.pinned: " << e.name << ": " << got << "\n";
        }
        CHECK(got.empty());
        ++entries;
    }
    // 4,000 fresh digests: 2,000 plain and 2,000 keyed.
    canon::Basic<> v;
    json::Writer<std::allocator<char>> w(v);
    w.open(json::Kind::object);
    w.string("refs");
    w.open(json::Kind::array);
    for (std::size_t i = 0; i < 2000; ++i) {
        const std::string data = "fresh " + std::to_string(i);
        std::string plain;
        digest::plain(data, plain);
        w.string(plain);
        std::string keyed;
        digest::detail::write_hex("hmac-sha256:", digest::hmac("key " + std::to_string(i), data), keyed);
        w.string(keyed);
    }
    w.close();
    w.close();
    const header::Header<> h = header::make(fields("project:qiven", "implementer"));
    const std::string got = listed(secret::check(h, v));
    std::cout << "secret.pinned: " << entries << " fixture entries, " << v.count(2) << " fresh digests, hits: "
              << (got.empty() ? "none" : got) << "\n";
    CHECK(entries > 0);
    CHECK_EQ(v.count(2), 4000U);
    CHECK(got.empty());
}
