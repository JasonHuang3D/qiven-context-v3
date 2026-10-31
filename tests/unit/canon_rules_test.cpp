// canon's two routes beside the default: a key rule by place, as a map
// file's parser gives one, and the layout the registry takes, step 6
// skipped; each with the codes the other steps still give. The rules here
// are the cases' own, shaped as the map files' are.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "qiven3/canon.hpp"
#include "qiven/runner/runner.hpp"

namespace {

using qiven3::ParseCode;
namespace canon = qiven3::canon;
using Std = canon::Basic<>;

// A repository name, as the cases take one: a letter or digit, then
// letters, digits, '.', '_' or '-'.
constexpr bool repository(std::string_view key) noexcept {
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

// The registry's keys: repository names, then path and ref.
struct Registry {
    constexpr bool operator()(std::string_view key, const canon::PlaceView& place) const noexcept {
        return place.level == 1 ? repository(key) : place.level == 2 && (key == "path" || key == "ref");
    }
};

// The marks file's keys: at, marks and read; then ids in marks and
// repository names in read.
struct Marks {
    constexpr bool operator()(std::string_view key, const canon::PlaceView& place) const noexcept {
        if (place.level == 1) {
            return key == "at" || key == "marks" || key == "read";
        }
        if (place.level != 2) {
            return false;
        }
        return place.keys.front() == "marks" ? key.starts_with("sha256:") : place.keys.front() == "read" && repository(key);
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

static_assert(canon::KeyRule<Registry> && canon::KeyRule<Marks> && canon::KeyRule<Record>);

template <canon::KeyRule K>
std::optional<ParseCode> code(std::string_view data, const K& rule, canon::Layout layout = canon::Layout::canonical) {
    Std v;
    return canon::try_parse(data, v, rule, layout);
}

constexpr std::string_view registry_text =
    "{\r\n  \"alpha-repo\": {\r\n    \"path\": \"/home/owner/a\",\r\n    \"ref\": \"main\"\r\n  },\r\n"
    "  \"beta.v2\": {\"ref\": \"topic/one\", \"path\": \"/home/owner/b\"}\r\n}\r\n";

}  // namespace

QIVEN_CASE(canon, key_by_place) {
    std::vector<std::string> seen;
    Std v;
    REQUIRE(!canon::try_parse(R"({"a":[{"b":1}],"c":{"d":{"e":[]}}})", v, Record{&seen}));
    const std::vector<std::string> want = {"a@1", "b@3/a/", "c@1", "d@2/c", "e@3/c/d"};
    CHECK(seen == want);
    // The marks file, canonical, its keys by place: accepted under its
    // rule, key_syntax under the syntax; a key off its place key_syntax.
    const std::string marks = R"({"at":"2026-10-17T09:30:00.000Z","marks":{"sha256:)" + std::string(64, 'a') +
                              R"(":"fresh"},"read":{"beta.v2-repo":")" + std::string(40, '0') + R"("}})";
    CHECK(!code(marks, Marks{}));
    CHECK(code(marks, canon::Syntax{}) == ParseCode::key_syntax);
    CHECK(code(R"({"at":"x","marks":{"read":"fresh"}})", Marks{}) == ParseCode::key_syntax);
    CHECK(code(R"({"at":"x","read":{"sha256:a":"0"}})", Marks{}) == ParseCode::key_syntax);
    CHECK(code(R"({"at":"x","when":1})", Marks{}) == ParseCode::key_syntax);
    // The rule replaces the syntax alone: the key's characters come first,
    // and every other step stays.
    constexpr std::string_view surrogate_key = R"({"\ud800":1})";
    constexpr std::string_view nonchar_key = R"({"\ufdd0":1})";
    CHECK(code(surrogate_key, Record{&seen}) == ParseCode::surrogate);
    CHECK(code(nonchar_key, Record{&seen}) == ParseCode::nonchar);
    CHECK(code(R"({"read":{"a":null}})", Marks{}) == ParseCode::null);
    CHECK(code(R"({"at":1,"at":2})", Marks{}) == ParseCode::duplicate_key);
    CHECK(code(R"({"marks":{},"at":"x"})", Marks{}) == ParseCode::not_canonical);
    // canonical writes such a value under the same rule, and refuses it
    // under the syntax.
    Std value;
    REQUIRE(!canon::try_parse(marks, value, Marks{}));
    std::string out;
    CHECK(canon::try_canonical(value, out) == ParseCode::key_syntax);
    CHECK(out.empty());
    CHECK(!canon::try_canonical(value, out, Marks{}));
    CHECK(out == marks);
}

QIVEN_CASE(canon, layout_any) {
    // The registry in any layout: CRLF, indented, a final line end, keys
    // out of order; refused when step 6 runs.
    CHECK(!code(registry_text, Registry{}, canon::Layout::any));
    CHECK(code(registry_text, Registry{}) == ParseCode::not_canonical);
    CHECK(code(registry_text, canon::Syntax{}, canon::Layout::any) == ParseCode::key_syntax);
    Std v;
    canon::parse(registry_text, v, Registry{}, canon::Layout::any);
    const std::optional<qiven3::json::Index> entry = v.find(0, "beta.v2");
    REQUIRE(entry.has_value());
    CHECK(v.text(*v.find(*entry, "ref")) == "topic/one");
    // A key off its place, and every step but the sixth, still refused.
    CHECK(code(R"({"a":{"path":"/x","branch":"main"}})", Registry{}, canon::Layout::any) == ParseCode::key_syntax);
    CHECK(code(R"( {"a-b":{"path":"/x","path":"/y"}} )", Registry{}, canon::Layout::any) ==
          ParseCode::duplicate_key);
    CHECK(code("\xef\xbb\xbf{}", Registry{}, canon::Layout::any) == ParseCode::bom);
    CHECK(code("{\"a\":\"\xff\"}", Registry{}, canon::Layout::any) == ParseCode::utf8);
    CHECK(code(std::string(17, '['), Registry{}, canon::Layout::any) == ParseCode::depth);
    CHECK(code(R"({"a": {"ref": null}})", Registry{}, canon::Layout::any) == ParseCode::null);
    CHECK(code(R"({"a": {"ref": 1.5}})", Registry{}, canon::Layout::any) == ParseCode::floating);
    CHECK(code("{\"a\": {\"ref\": \"\xef\xbf\xbe\"}}", Registry{}, canon::Layout::any) == ParseCode::nonchar);
    CHECK(code("{\"a\":1}\r\n", canon::Syntax{}, canon::Layout::any) == std::nullopt);
    CHECK(code("{\"a\":1} x", canon::Syntax{}, canon::Layout::any) == ParseCode::syntax);
}
