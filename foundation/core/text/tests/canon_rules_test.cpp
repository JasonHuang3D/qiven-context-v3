// canon's two routes beside the default: a key rule by place, and a layout
// taken whole, step 6 skipped; each with the codes the other steps still
// give. The rules and texts here are the cases' own, a small catalogue's.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "qiven/runner/runner.hpp"
#include "qiven/text/canon.hpp"

namespace {

using qiven::text::ParseCode;
namespace canon = qiven::text::canon;
using Std = canon::Basic<>;

// A shelf label, as the cases take one: a letter or digit, then letters,
// digits, '.', '_' or '-'.
constexpr bool label(std::string_view key) noexcept {
    const auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
    if (key.empty() || !alnum(key.front())) {
        return false;
    }
    for (const char c : key) {
        if (!alnum(c) && c != '.' && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

// The catalogue's keys: shelf labels, then room and title.
struct Catalogue {
    constexpr bool operator()(std::string_view key, const canon::PlaceView& place) const noexcept {
        return place.level == 1 ? label(key) : place.level == 2 && (key == "room" || key == "title");
    }
};

// The catalogue's index keys: at, cards and rooms; then hashes in cards
// and shelf labels in rooms.
struct Index {
    constexpr bool operator()(std::string_view key, const canon::PlaceView& place) const noexcept {
        if (place.level == 1) {
            return key == "at" || key == "cards" || key == "rooms";
        }
        if (place.level != 2) {
            return false;
        }
        return place.keys.front() == "cards" ? key.starts_with("sha256:") : place.keys.front() == "rooms" && label(key);
    }
};

// Every key allowed, each place written down as key@level/holder/holder.
struct Record {
    std::vector<std::string>* seen = nullptr;
    bool operator()(std::string_view key, const canon::PlaceView& place) const {
        std::string line = std::string(key) + "@" + std::to_string(place.level);
        for (const std::string_view holder : place.keys) {
            line += "/" + std::string(holder);
        }
        seen->push_back(line);
        return true;
    }
};

static_assert(canon::KeyRule<Catalogue> && canon::KeyRule<Index> && canon::KeyRule<Record>);

template <canon::KeyRule K>
std::optional<ParseCode> code(std::string_view data, const K& rule, canon::Layout layout = canon::Layout::canonical) {
    Std v;
    return canon::try_parse(data, v, rule, qiven::text::json::max_depth, layout);
}

constexpr std::string_view catalogue_text =
    "{\r\n  \"knuth-1\": {\r\n    \"room\": \"north\",\r\n    \"title\": \"Volume 1\"\r\n  },\r\n"
    "  \"ewd.249\": {\"title\": \"Notes on Structured Programming\", \"room\": \"east/two\"}\r\n}\r\n";

}  // namespace

QIVEN_CASE(canon, key_by_place) {
    std::vector<std::string> seen;
    Std v;
    REQUIRE(!canon::try_parse(R"({"a":[{"b":1}],"c":{"d":{"e":[]}}})", v, Record{&seen}, qiven::text::json::max_depth));
    const std::vector<std::string> want = {"a@1", "b@3/a/", "c@1", "d@2/c", "e@3/c/d"};
    CHECK(seen == want);
    // The index, canonical, its keys by place: accepted under its rule,
    // key_syntax under the syntax; a key off its place key_syntax.
    const std::string index_text = R"({"at":"2026-10-17T09:30:00.000Z","cards":{"sha256:)" + std::string(64, 'a') +
                                   R"(":"on-shelf"},"rooms":{"east.v2-wing":")" + std::string(40, '0') + R"("}})";
    CHECK(!code(index_text, Index{}));
    CHECK(code(index_text, canon::Syntax{}) == ParseCode::key_syntax);
    CHECK(code(R"({"at":"x","cards":{"rooms":"on-shelf"}})", Index{}) == ParseCode::key_syntax);
    CHECK(code(R"({"at":"x","rooms":{"sha256:a":"0"}})", Index{}) == ParseCode::key_syntax);
    CHECK(code(R"({"at":"x","when":1})", Index{}) == ParseCode::key_syntax);
    // The rule replaces the syntax alone: the key's characters come first,
    // and every other step stays.
    constexpr std::string_view surrogate_key = R"({"\ud800":1})";
    constexpr std::string_view nonchar_key = R"({"\ufdd0":1})";
    CHECK(code(surrogate_key, Record{&seen}) == ParseCode::surrogate);
    CHECK(code(nonchar_key, Record{&seen}) == ParseCode::nonchar);
    CHECK(code(R"({"rooms":{"a":null}})", Index{}) == ParseCode::null);
    CHECK(code(R"({"at":1,"at":2})", Index{}) == ParseCode::duplicate_key);
    CHECK(code(R"({"cards":{},"at":"x"})", Index{}) == ParseCode::not_canonical);
    // canonical writes such a value under the same rule, and refuses it
    // under the syntax.
    Std value;
    REQUIRE(!canon::try_parse(index_text, value, Index{}, qiven::text::json::max_depth));
    std::string out;
    CHECK(canon::try_canonical(value, out, canon::Syntax{}) == ParseCode::key_syntax);
    CHECK(out.empty());
    CHECK(!canon::try_canonical(value, out, Index{}));
    CHECK(out == index_text);
}

QIVEN_CASE(canon, layout_any) {
    // The catalogue in any layout: CRLF, indented, a final line end, keys
    // out of order; refused when step 6 runs.
    CHECK(!code(catalogue_text, Catalogue{}, canon::Layout::any));
    CHECK(code(catalogue_text, Catalogue{}) == ParseCode::not_canonical);
    CHECK(code(catalogue_text, canon::Syntax{}, canon::Layout::any) == ParseCode::key_syntax);
    Std v;
    canon::parse(catalogue_text, v, Catalogue{}, qiven::text::json::max_depth, canon::Layout::any);
    const std::optional<qiven::text::json::Index> book = v.find(0, "ewd.249");
    REQUIRE(book.has_value());
    CHECK(v.text(*v.find(*book, "room")) == "east/two");
    // A key off its place, and every step but the sixth, still refused.
    CHECK(code(R"({"a":{"room":"x","shelf":"9"}})", Catalogue{}, canon::Layout::any) == ParseCode::key_syntax);
    CHECK(code(R"( {"a-b":{"room":"x","room":"y"}} )", Catalogue{}, canon::Layout::any) ==
          ParseCode::duplicate_key);
    CHECK(code("\xef\xbb\xbf{}", Catalogue{}, canon::Layout::any) == ParseCode::bom);
    CHECK(code("{\"a\":\"\xff\"}", Catalogue{}, canon::Layout::any) == ParseCode::utf8);
    CHECK(code(std::string(17, '['), Catalogue{}, canon::Layout::any) == ParseCode::depth);
    CHECK(code(R"({"a": {"room": null}})", Catalogue{}, canon::Layout::any) == ParseCode::null);
    CHECK(code(R"({"a": {"room": 1.5}})", Catalogue{}, canon::Layout::any) == ParseCode::floating);
    CHECK(code("{\"a\": {\"title\": \"\xef\xbf\xbe\"}}", Catalogue{}, canon::Layout::any) == ParseCode::nonchar);
    CHECK(code("{\"a\":1}\r\n", canon::Syntax{}, canon::Layout::any) == std::nullopt);
    CHECK(code("{\"a\":1} x", canon::Syntax{}, canon::Layout::any) == ParseCode::syntax);
}
