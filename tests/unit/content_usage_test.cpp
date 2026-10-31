// content's case of a usage at its limits: every list at its limit, as
// kinds' table gives it, and client and session at theirs (32 and 128
// characters), the content admitted by content's check and measured,
// 50,698 bytes canonical, under 65,536; then one item more on any list, or
// one character more on client or session, refused with its code, so that
// no usage content is longer.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "entry/content.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "qiven3/canon.hpp"
#include "qiven/runner/runner.hpp"

namespace {

namespace content = qiven3::entry::content;
namespace header = qiven3::entry::header;
namespace kinds = qiven3::entry::kinds;
namespace canon = qiven3::canon;

// client's and session's syntaxes allow 32 and 128 characters.
constexpr std::size_t client_max = 32;
constexpr std::size_t session_max = 128;
constexpr std::size_t usage_max = 50698;
constexpr std::size_t content_max = 65536;

// A usage list's most items, by kinds' table; 0 for no list.
std::size_t items_of(std::string_view name) {
    const kinds::Kind& k = kinds::KINDS.at(*kinds::index_of("usage"));
    for (const kinds::Field& f : kinds::fields(k)) {
        if (f.name == name) {
            return f.items;
        }
    }
    return 0;
}

// The id of n: sha256: and n in 64 lowercase hex digits.
std::string id(std::size_t n) {
    std::string hex(64, '0');
    for (std::size_t i = 64; i > 0 && n > 0; --i, n /= 16) {
        hex[i - 1] = "0123456789abcdef"[n % 16];
    }
    return "sha256:" + hex;
}

struct Counts {
    std::size_t served = 0;
    std::size_t cited = 0;
    std::size_t corrected = 0;
    std::size_t client = 0;
    std::size_t session = 0;
};

// A usage's canonical content of these counts: cited the first of served,
// each corrected entry one of served by an id of its own; truncated set.
std::string usage(const Counts& n) {
    std::string out = "{\"cited\":[";
    for (std::size_t i = 0; i < n.cited; ++i) {
        out += (i == 0 ? "\"" : ",\"") + id(i + 1) + "\"";
    }
    out += "],\"corrected\":[";
    for (std::size_t j = 0; j < n.corrected; ++j) {
        out += (j == 0 ? "{\"by\":\"" : ",{\"by\":\"") + id(1001 + j) + "\",\"entry\":\"" + id(j + 1) + "\"}";
    }
    out += "],\"episode\":{\"client\":\"" + std::string(n.client, 'c') + "\",\"session\":\"" +
           std::string(n.session, 's') + "\"},\"served\":[";
    for (std::size_t i = 0; i < n.served; ++i) {
        out += (i == 0 ? "\"" : ",\"") + id(i + 1) + "\"";
    }
    return out + "],\"truncated\":true}";
}

// The content's verdict as the loop's usage, observed.
std::optional<content::ContentError> verdict(std::string_view text) {
    header::FieldsView f;
    f.scope = "project:qiven";
    f.lane = "loop";
    f.kind = "usage";
    f.writer = "loop";
    f.basis = "observed";
    f.at = "2026-10-17T12:00:00.000Z";
    const header::Header<> h = header::make(f);
    canon::Basic<> v;
    canon::parse(text, v);
    return content::try_check(h, v);
}

bool refused_as(const Counts& n, content::Code code, std::string_view path) {
    const std::optional<content::ContentError> e = verdict(usage(n));
    return e && e->code == code && e->path.view() == path;
}

}  // namespace

QIVEN_CASE(content, usage_max) {
    const Counts most{items_of("served"), items_of("cited"), items_of("corrected"), client_max, session_max};
    CHECK_EQ(most.served, 384U);
    CHECK_EQ(most.cited, 192U);
    CHECK_EQ(most.corrected, 48U);
    const std::string text = usage(most);
    CHECK(!verdict(text));
    // Its canonical bytes: the strict parser takes it as canonical, and the
    // encoder writes it back the same.
    canon::Basic<> v;
    canon::parse(text, v);
    std::string again;
    canon::canonical(v, again);
    CHECK(again == text);
    CHECK_EQ(text.size(), usage_max);
    CHECK(text.size() < content_max);
    // One more on any list, or on client or session, is refused.
    Counts more = most;
    ++more.served;
    CHECK(refused_as(more, content::Code::too_many, "served"));
    more = most;
    ++more.cited;
    CHECK(refused_as(more, content::Code::too_many, "cited"));
    more = most;
    ++more.corrected;
    CHECK(refused_as(more, content::Code::too_many, "corrected"));
    more = most;
    ++more.client;
    CHECK(refused_as(more, content::Code::syntax, "episode.client"));
    more = most;
    ++more.session;
    CHECK(refused_as(more, content::Code::syntax, "episode.session"));
    std::cout << "content.usage_max: " << most.served << " served, " << most.cited << " cited, " << most.corrected
              << " corrected, client " << most.client << " and session " << most.session << " characters: "
              << text.size() << " bytes canonical, admitted; one more of each refused\n";
}
