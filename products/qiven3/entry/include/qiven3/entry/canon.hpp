#pragma once

// qiven3/entry/canon: text's canon bound once for the product, which every
// product site reaches as canon. Its namespace re-exports canon's names
// (the value, the key's place, the key rule and the syntax's, the layout)
// and gives parse, try_parse, canonical and try_canonical with the
// product's own defaults: the key rule Syntax, and the depth max_depth,
// the format's limit, a value of at most 16 levels. Each forwards to text's
// with max_depth, which text's canon takes with no default of its own.

#include <cstddef>
#include <optional>
#include <string_view>

#include "qiven/text/canon.hpp"

namespace qiven3::entry::canon {

using qiven::text::canon::Basic;
using qiven::text::canon::key_syntax;
using qiven::text::canon::KeyRule;
using qiven::text::canon::Layout;
using qiven::text::canon::PlaceView;
using qiven::text::canon::Syntax;
using qiven::text::canon::Value;

// The format's limit: a value of at most 16 levels. With Syntax, the
// product's own defaults; json's max_depth stays json's own capacity.
inline constexpr std::size_t max_depth = 16;

// text's try_canonical, the rule Syntax by default.
template <typename A, typename S, KeyRule K = Syntax>
constexpr std::optional<qiven::text::ParseCode> try_canonical(const qiven::text::json::Basic<A>& value, S& out,
                                                              const K& rule = K{}) {
    return qiven::text::canon::try_canonical(value, out, rule);
}

// text's canonical, the rule Syntax by default.
template <typename A, typename S, KeyRule K = Syntax>
constexpr void canonical(const qiven::text::json::Basic<A>& value, S& out, const K& rule = K{}) {
    qiven::text::canon::canonical(value, out, rule);
}

// text's try_parse at max_depth, the rule Syntax by default.
template <typename A, KeyRule K = Syntax>
constexpr std::optional<qiven::text::ParseCode> try_parse(std::string_view data, qiven::text::json::Basic<A>& out,
                                                          const K& rule = K{}, Layout layout = Layout::canonical) {
    return qiven::text::canon::try_parse(data, out, rule, max_depth, layout);
}

// text's parse at max_depth, the rule Syntax by default.
template <typename A, KeyRule K = Syntax>
constexpr void parse(std::string_view data, qiven::text::json::Basic<A>& out, const K& rule = K{},
                     Layout layout = Layout::canonical) {
    qiven::text::canon::parse(data, out, rule, max_depth, layout);
}

}  // namespace qiven3::entry::canon
