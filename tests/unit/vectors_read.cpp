#include "support/fixtures/vectors.hpp"

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
#include "entry/fold.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/labels.hpp"
#include "entry/projectmap.hpp"
#include "entry/served.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "support/fixtures/fixtures.hpp"

namespace qiven3::test::vectors {

namespace body = qiven3::entry::body;
namespace check = qiven3::entry::check;
namespace content = qiven3::entry::content;
namespace digest = qiven3::digest;
namespace fixtures = qiven3::test::fixtures;
namespace fold = qiven3::entry::fold;
namespace header = qiven3::entry::header;
namespace json = qiven3::json;
namespace kinds = qiven3::entry::kinds;
namespace labels = qiven3::entry::labels;
namespace projectmap = qiven3::entry::projectmap;
namespace served = qiven3::entry::served;
using header::Slot;

// A member's value, of its type, or a fault thrown.
std::string_view text(const Value& v, std::string_view key) {
    const std::optional<json::Index> at = v.find(0, key);
    if (!at || v.kind(*at) != json::Kind::string) {
        throw std::runtime_error("member " + std::string(key) + " not a text");
    }
    return v.text(*at);
}
std::optional<std::string_view> maybe_text(const Value& v, std::string_view key) {
    return v.find(0, key) ? std::optional<std::string_view>(text(v, key)) : std::nullopt;
}
std::int64_t integer(const Value& v, std::string_view key) {
    const std::optional<json::Index> at = v.find(0, key);
    if (!at || v.kind(*at) != json::Kind::integer) {
        throw std::runtime_error("member " + std::string(key) + " not an integer");
    }
    return v.integer(*at);
}
std::size_t size(const Value& v, std::string_view key) {
    const std::int64_t n = integer(v, key);
    if (n < 0) {
        throw std::runtime_error("member " + std::string(key) + " negative");
    }
    return static_cast<std::size_t>(n);
}
std::string from_hex(std::string_view hex) {
    std::string out;
    if (!fixtures::from_hex(hex, out)) {
        throw std::runtime_error("not lowercase hex");
    }
    return out;
}
std::string hex_of(std::string_view bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        out += digits[b >> 4U];
        out += digits[b & 15U];
    }
    return out;
}

// Empty when got is want; else what, and what was got.
std::string same(std::string_view what, std::string_view got, std::string_view want) {
    return got == want ? std::string() : std::string(what) + ": " + std::string(got);
}

namespace {

// A refusal thrown by check_entry, as its part and its code's name.
std::string checked(Context& c, std::string_view line, std::optional<std::string_view> record) {
    try {
        static_cast<void>(check::check_entry(c.fake, line, record));
    } catch (const qiven3::ParseError& e) {
        return "parse " + std::string(qiven3::name(e.code));
    } catch (const header::HeaderError& e) {
        return "header " + std::string(header::name(e.code));
    } catch (const body::BodyError& e) {
        return "body " + std::string(body::name(e.code));
    } catch (const content::ContentError& e) {
        return "content " + std::string(content::name(e.code));
    }
    return "none none";
}

}  // namespace

std::string framing(Context& /*c*/, const Value& v) {
    std::string decimal;
    digest::Field field;
    if (const std::optional<json::Index> at = v.find(0, "value")) {
        if (v.kind(*at) == json::Kind::integer) {
            decimal = std::to_string(v.integer(*at));
            field = decimal;
        } else {
            field = text(v, "value");
        }
    }
    const std::array<digest::Field, 1> fields = {field};
    std::string out;
    digest::frame(std::span<const digest::Field>(fields), out);
    return same("framed", hex_of(out), text(v, "framed"));
}

std::string framed_header(Context& /*c*/, const Value& v) {
    const header::Header<> h = header::parse_line(text(v, "header"));
    const auto fields = h.fields();
    std::string out;
    digest::frame(std::span<const digest::Field>(fields), out);
    if (out.size() != size(v, "size")) {
        return "size: " + std::to_string(out.size());
    }
    std::string id;
    digest::entry_id(std::span<const digest::Field>(fields), id);
    const std::string f = same("framed", hex_of(out), text(v, "framed"));
    return f.empty() ? same("id", id, text(v, "id")) : f;
}

std::string entry(Context& c, const Value& v) {
    const std::string_view line = text(v, "header");
    Value strict;
    canon::parse(line, strict);
    const header::Header<> h = header::parse_line(line);
    const auto fields = h.fields();
    if (digest::framed_size(std::span<const digest::Field>(fields)) != size(v, "framed_size")) {
        return "framed_size";
    }
    std::string id;
    digest::entry_id(std::span<const digest::Field>(fields), id);
    if (const std::string f = same("id", id, text(v, "id")); !f.empty() || h.value(Slot::id) != id) {
        return f.empty() ? "the line's id" : f;
    }
    const std::optional<fixtures::EntryView> fx = fixtures::find_entry(c.fixtures, text(v, "name"));
    if (!fx || fx->header != line) {
        return "not the fixtures' entry of its name";
    }
    const std::optional<std::string_view> key = maybe_text(v, "key");
    if (!key) {
        if (v.find(0, "content") || v.find(0, "body") || v.find(0, "content_size") || v.find(0, "body_size") ||
            fx->key || h.present(Slot::content)) {
            return "a content with no key";
        }
        return same("check", checked(c, line, std::nullopt), "none none");
    }
    const std::string_view text_of = text(v, "content");
    Value parsed;
    canon::parse(text_of, parsed);
    if (text_of.size() != size(v, "content_size") || fx->key != key || fx->content != text_of) {
        return "content_size, or not the fixtures' key and content";
    }
    std::string d;
    digest::keyed(digest::key_from_hex(c.fake, *key), text_of, d);
    if (d != h.value(Slot::content)) {
        return "content digest: " + d;
    }
    std::string record;
    body::make(text_of, digest::key_from_hex(c.fake, *key), id).line(record);
    if (record != text(v, "body") || record.size() != size(v, "body_size")) {
        return "body: " + record;
    }
    return same("check", checked(c, line, record), "none none");
}

std::string content_digest(Context& c, const Value& v) {
    const std::string bytes = from_hex(text(v, "bytes"));
    Value shown;
    if (canon::try_parse(text(v, "shown"), shown, canon::Syntax{}, canon::Layout::any)) {
        return "the text shown refused";
    }
    std::string out;
    canon::canonical(shown, out);
    Value strict;
    canon::parse(bytes, strict);
    if (out != bytes || bytes.size() != size(v, "size")) {
        return "canonical: " + hex_of(out);
    }
    std::string d;
    digest::keyed(digest::key_from_hex(c.fake, text(v, "key")), bytes, d);
    return same("digest", d, text(v, "digest"));
}

std::string evidence(Context& c, const Value& v) {
    const Entry& e = c.entry(text(v, "entry"));
    std::string d;
    digest::keyed(digest::key_from_hex(c.fake, e.key), from_hex(text(v, "data")), d);
    if (const std::string f = same("digest", d, text(v, "digest")); !f.empty()) {
        return f;
    }
    Value cv;
    canon::parse(e.content, cv);
    const std::string_view field = text(v, "field");
    const std::optional<json::Index> at = cv.find(0, field);
    if (!at || (field != "evidence" && field != "message")) {
        return "no field " + std::string(field);
    }
    json::Index holder = *at;
    if (field == "evidence") {
        holder = fixtures::detail::item(cv, *at, 0);
    }
    const std::optional<json::Index> held = cv.find(holder, "digest");
    return held ? same("held", cv.text(*held), text(v, "digest")) : "no digest held";
}

std::string parse(Context& /*c*/, const Value& v) {
    const std::optional<json::Index> pieces = v.find(0, "bytes");
    if (!pieces || v.kind(*pieces) != json::Kind::array) {
        return "bytes not a list";
    }
    std::string data;
    for (json::Index k = *pieces + 1; k < v.next(*pieces); k = v.next(k)) {
        const std::optional<json::Index> hex = v.find(k, "hex");
        const std::optional<json::Index> times = v.find(k, "times");
        if (v.count(k) != 2 || !hex || !times || v.integer(*times) < 1) {
            return "a piece not of hex and times";
        }
        const std::string piece = from_hex(v.text(*hex));
        for (std::int64_t i = 0; i < v.integer(*times); ++i) {
            data += piece;
        }
    }
    Value out;
    const std::optional<qiven3::ParseCode> code = canon::try_parse(data, out);
    return same("code", code ? qiven3::name(*code) : "none", text(v, "code"));
}

std::string check_line(Context& c, const Value& v) {
    const std::string want = std::string(text(v, "part")) + " " + std::string(text(v, "code"));
    return same("code", checked(c, text(v, "header"), text(v, "record")), want);
}

std::string usage(Context& /*c*/, const Value& v) {
    const std::string_view t = text(v, "content");
    Value cv;
    canon::parse(t, cv);
    if (t.size() != size(v, "size")) {
        return "size: " + std::to_string(t.size());
    }
    header::FieldsView f;
    f.scope = "project:qiven";
    f.lane = "loop";
    f.kind = "usage";
    f.writer = "loop";
    f.basis = "observed";
    f.at = "2026-10-17T12:00:00.000Z";
    const header::Header<> h = header::make(f);
    if (const std::optional<content::ContentError> e = content::try_check(h, cv)) {
        return "refused: " + std::string(content::name(e->code));
    }
    const kinds::Kind& k = kinds::KINDS.at(*kinds::index_of("usage"));
    std::size_t lists = 0;
    for (const kinds::Field& field : kinds::fields(k)) {
        if (field.items == 0) {
            continue;
        }
        const std::optional<json::Index> at = cv.find(0, field.name.view());
        if (!at || cv.count(*at) != field.items || size(v, field.name.view()) != field.items) {
            return "a list not at its limit: " + std::string(field.name.view());
        }
        ++lists;
    }
    const std::optional<json::Index> episode = cv.find(0, "episode");
    const std::optional<json::Index> truncated = cv.find(0, "truncated");
    if (lists != 3 || !episode || !truncated || !cv.boolean(*truncated)) {
        return "not every list, the episode and truncated";
    }
    const std::size_t client = cv.text(*cv.find(*episode, "client")).size();
    const std::size_t session = cv.text(*cv.find(*episode, "session")).size();
    return client == size(v, "client") && client == 32 && session == size(v, "session") && session == 128
               ? std::string()
               : "client and session not at their limits";
}

std::string file(Context& c, const Value& v) {
    const std::string bytes = from_hex(text(v, "bytes"));
    if (bytes.size() != size(v, "size")) {
        return "size: " + std::to_string(bytes.size());
    }
    for (std::size_t i = 0; i < fixtures::mapped_count(c.fixtures); ++i) {
        const fixtures::FileView f = fixtures::mapped(c.fixtures, i);
        if (f.version == integer(v, "version")) {
            return f.repository == text(v, "repository") && f.path == text(v, "path") && f.hex == text(v, "bytes")
                       ? std::string()
                       : "not the fixtures' file";
        }
    }
    return "no fixtures' file of its version";
}

std::string map(Context& c, const Value& v) {
    const Entry& e = c.entry(text(v, "entry"));
    const header::Header<> h = header::parse_line(e.header);
    Value cv;
    canon::parse(e.content, cv);
    const digest::Key<Fake> key = digest::key_from_hex(c.fake, e.key);
    served::Mark mark = served::Mark::unchecked;
    if (integer(v, "version") == 0) {
        if (v.find(0, "digest")) {
            return "a digest of no file";
        }
        mark = projectmap::mark(h, cv, key, projectmap::MISSING);
    } else {
        const std::string& bytes = c.file(integer(v, "version"));
        std::string d;
        digest::keyed(key, bytes, d);
        if (const std::string f = same("digest", d, text(v, "digest")); !f.empty()) {
            return f;
        }
        mark = projectmap::mark(h, cv, key, std::string_view(bytes));
    }
    return same("mark", served::name(mark), text(v, "mark"));
}

std::string block(Context& c, const Value& v) {
    const std::optional<json::Index> record = v.find(0, "record");
    if (!record || v.kind(*record) != json::Kind::array) {
        return "record not a list";
    }
    const std::size_t n = v.count(*record);
    std::vector<header::Header<>> heads;
    std::vector<Value> values;
    std::vector<fold::ItemView> items;
    heads.reserve(n);
    values.reserve(n);
    std::optional<std::size_t> self;
    for (json::Index k = *record + 1; k < v.next(*record); k = v.next(k)) {
        const Entry& e = c.entry(v.text(k));
        self = e.name == text(v, "entry") ? std::optional<std::size_t>(heads.size()) : self;
        heads.push_back(header::parse_line(e.header));
        values.emplace_back();
        canon::parse(e.content, values.back());
    }
    for (std::size_t i = 0; i < n; ++i) {
        items.push_back(fold::item(heads[i], values[i], true));
    }
    const std::string_view at = text(v, "at");
    const fold::State state = fold::fold(std::span<const fold::ItemView>(items), at);
    if (!self || !state.served() || state.current != *self) {
        return "the entry not its record's current";
    }
    const std::string_view mark = text(v, "mark");
    const served::Mark m = mark == served::name(served::Mark::fresh) ? served::Mark::fresh : served::Mark::stale;
    const header::Header<>& h = heads[*self];
    std::string out;
    served::block(h, values[*self], served::State{state, 0}, labels::label(*labels::version_of(h, values[*self]), {}),
                  served::map_mark(m, at), out);
    if (const std::string f = same("block", out, text(v, "block")); !f.empty()) {
        return f;
    }
    return served::cost(out) == size(v, "cost") ? std::string() : "cost: " + std::to_string(served::cost(out));
}

}  // namespace qiven3::test::vectors
