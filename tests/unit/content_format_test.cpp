// content's cases of the format's changes, at run time: a checkpoint's
// next_action at 2,100 characters, decision_status at 1,600, detail at
// 8,000 and progress still at 1,000, a question at 1,500, each admitted at
// its limit and too_long one past it; for_lane a lane name carried by
// writer import alone (not_allowed for a slot or the loop), syntax past 32
// characters, off the lane syntax or naming owner, loop or import;
// question_id a slug of 64, a question's alone; a decision's closes of at
// most 8 ids, repeating none; the common item, a slug of 64, on served
// kinds but item and map, in a project scope alone (not_allowed in person
// and life, and on item, map, usage and retire); when on retire admitted,
// on check not_allowed; detail and closes not_allowed on each kind but the
// checkpoint's and the decision's. Each refusal by its code and path, and a
// length's by its limit and measured value.

#include <cstddef>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "entry/content.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"

namespace {

namespace content = qiven3::entry::content;
namespace header = qiven3::entry::header;
namespace kinds = qiven3::entry::kinds;
namespace canon = qiven3::canon;
using content::Code;
using content::ContentError;

#define ID "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define ID2 "sha256:1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define DIGEST "hmac-sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define OUTPUT R"([{"command":"python -I probe.py","digest":")" DIGEST R"(","type":"output"}])"
#define WHEN R"({"quality":"exact","time":"2026-10-17T09:00:00.000Z"})"

// A content's members, raw JSON by key; a map writes them in canonical
// order.
using Members = std::map<std::string, std::string>;

std::string text(const Members& m) {
    std::string out = "{";
    for (const auto& [k, v] : m) {
        out += (out.size() == 1 ? "\"" : ",\"") + k + "\":" + v;
    }
    return out + "}";
}
std::string string_of(std::string_view s) { return "\"" + std::string(s) + "\""; }
std::string repeat(std::string_view unit, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += unit;
    }
    return out;
}

// The verdict on a content of a kind by a writer in a scope, basis claimed,
// the header's lane the writer's role.
std::optional<ContentError> verdict(std::string_view kind, std::string_view writer, std::string_view scope,
                                    const Members& m, std::string_view basis = "claimed") {
    header::FieldsView f;
    f.scope = scope;
    f.lane = header::role(writer);
    f.kind = kind;
    f.writer = writer;
    f.basis = basis;
    f.at = "2026-10-17T09:30:00.000Z";
    f.id = ID;
    const header::Header<> h = header::make(f);
    canon::Basic<> v;
    canon::parse(text(m), v);
    return content::try_check(h, v);
}

// A served kind's common members for writer import, and a kind's own.
Members served(Members own) {
    own["admit"] = R"({"mistake":"A reader would trust it.","underivable":"Said in a session."})";
    own["band"] = R"("normal")";
    own["evidence"] = OUTPUT;
    own["subject"] = R"("entity:x")";
    return own;
}
Members checkpoint() { return served({{"next_action", R"("n")"}, {"progress", R"("p")"}}); }
Members with(Members m, const std::string& key, const std::string& value) {
    m[key] = value;
    return m;
}

constexpr std::string_view S = "project:qiven";

// Whether a verdict is none, or the code at the path, and for too_long or
// too_many the limit and the measured value.
bool passes(const std::optional<ContentError>& e) {
    if (e) {
        std::cout << "content.format: refused " << content::name(e->code) << " at " << e->path.view() << "\n";
    }
    return !e.has_value();
}
bool is(const std::optional<ContentError>& e, Code c, std::string_view path, std::size_t limit = 0,
        std::size_t value = 0) {
    const bool ok = e.has_value() && e->code == c && e->path.view() == path && e->limit == limit && e->value == value;
    if (!ok) {
        std::cout << "content.format: wanted " << content::name(c) << " at " << path << ", got "
                  << (e ? content::name(e->code) : std::string_view("none")) << " at "
                  << (e ? e->path.view() : std::string_view()) << "\n";
    }
    return ok;
}

}  // namespace

QIVEN3_CASE(content, format_limits) {
    // A checkpoint's three texts and detail, each at its limit and past it.
    for (const auto& [key, limit] : {std::pair<std::string, std::size_t>{"next_action", 2100}, {"decision_status", 1600},
                                     {"detail", 8000}, {"progress", 1000}}) {
        CHECK(passes(verdict("checkpoint", "import", S, with(checkpoint(), key, string_of(repeat("a", limit))))));
        CHECK(is(verdict("checkpoint", "import", S, with(checkpoint(), key, string_of(repeat("a", limit + 1)))),
                 Code::too_long, key, limit, limit + 1));
    }
    // detail by a slot as by import.
    CHECK(passes(verdict("checkpoint", "implementer/s39", S,
                         with(with(checkpoint(), "episode", R"({"client":"claude-code","session":"s-1"})"), "detail",
                              R"("d")"))));
    // A question's text at 1,500.
    const Members question = served({{"question", string_of(repeat("q", 1500))}});
    CHECK(passes(verdict("question", "import", S, question)));
    CHECK(is(verdict("question", "import", S, with(question, "question", string_of(repeat("q", 1501)))), Code::too_long,
             "question", 1500, 1501));
    std::cout << "content.format: 2100, 1600, 8000, 1000 and 1500 admitted, one past each too_long\n";
}

QIVEN3_CASE(content, format_for_lane) {
    const Members cp = checkpoint();
    CHECK(passes(verdict("checkpoint", "import", S, with(cp, "for_lane", R"("implementer")"))));
    CHECK(passes(verdict("checkpoint", "import", S, with(cp, "for_lane", string_of("a" + repeat("b-9", 10) + "c")))));
    // Writer import alone: a slot's and the loop's not_allowed.
    const Members slot = with(with(cp, "episode", R"({"client":"claude-code","session":"s-1"})"), "for_lane",
                              R"("reviewer")");
    CHECK(is(verdict("checkpoint", "implementer/s39", S, slot), Code::not_allowed, "for_lane"));
    CHECK(is(verdict("checkpoint", "loop", S, with(cp, "for_lane", R"("reviewer")")), Code::not_allowed, "for_lane"));
    // A lane name: 32 characters at most, the lane syntax, none reserved.
    CHECK(is(verdict("checkpoint", "import", S, with(cp, "for_lane", string_of(repeat("a", 33)))), Code::syntax,
             "for_lane"));
    for (const std::string_view bad : {"Implementer", "1lane", "-a", "a_b", "a b", "owner", "loop", "import"}) {
        CHECK(is(verdict("checkpoint", "import", S, with(cp, "for_lane", string_of(bad))), Code::syntax, "for_lane"));
    }
    CHECK(is(verdict("checkpoint", "import", S, with(cp, "for_lane", R"("")")), Code::empty, "for_lane"));
    CHECK(is(verdict("checkpoint", "import", S, with(cp, "for_lane", "7")), Code::type, "for_lane"));
    // Another kind's field.
    CHECK(is(verdict("claim", "import", S, served({{"claim", R"("c")"}, {"for_lane", R"("reviewer")"}})),
             Code::not_allowed, "for_lane"));
    std::cout << "content.format: for_lane by import alone, a lane name, none reserved\n";
}

QIVEN3_CASE(content, format_question_closes) {
    // question_id: a slug of 64.
    const Members question = served({{"question", R"("q")"}});
    CHECK(passes(verdict("question", "import", S, with(question, "question_id", string_of(repeat("q1-", 21) + "x")))));
    CHECK(is(verdict("question", "import", S, with(question, "question_id", string_of(repeat("q", 65)))), Code::too_long,
             "question_id", 64, 65));
    CHECK(is(verdict("question", "import", S, with(question, "question_id", R"("Q1")")), Code::syntax, "question_id"));
    CHECK(is(verdict("claim", "import", S, served({{"claim", R"("c")"}, {"question_id", R"("q1")"}})),
             Code::not_allowed, "question_id"));
    // closes: at most 8 version ids, repeating none, not checked against a
    // store.
    const Members decision = served({{"decides", R"("d")"}});
    std::string eight = "[";
    for (std::size_t i = 0; i < 8; ++i) {
        eight += std::string(i == 0 ? "" : ",") + "\"sha256:" + repeat(std::string(1, static_cast<char>('0' + i)), 64) +
                 "\"";
    }
    CHECK(passes(verdict("decision", "import", S, with(decision, "closes", eight + "]"))));
    CHECK(is(verdict("decision", "import", S, with(decision, "closes", eight + R"(,")" ID R"("])")), Code::too_many,
             "closes", 8, 9));
    CHECK(is(verdict("decision", "import", S, with(decision, "closes", R"([")" ID R"(",")" ID R"("])")),
             Code::repeated, "closes[1]"));
    CHECK(is(verdict("decision", "import", S, with(decision, "closes", R"([")" ID2 R"(","x"])")), Code::syntax,
             "closes[1]"));
    CHECK(is(verdict("decision", "import", S, with(decision, "closes", "[]")), Code::empty, "closes"));
    CHECK(is(verdict("decision", "import", S, with(decision, "closes", R"(")" ID R"(")")), Code::type, "closes"));
    std::cout << "content.format: question_id a slug of 64, closes 8 ids\n";
}

QIVEN3_CASE(content, format_item_when) {
    // item: a slug of 64 on a served kind in a project scope.
    const Members claim = served({{"claim", R"("c")"}});
    CHECK(passes(verdict("claim", "import", S, with(claim, "item", string_of(repeat("p", 64))))));
    CHECK(passes(verdict("checkpoint", "import", S, with(checkpoint(), "item", R"("p3")"))));
    CHECK(is(verdict("claim", "import", S, with(claim, "item", string_of(repeat("p", 65)))), Code::too_long, "item", 64,
             65));
    CHECK(is(verdict("claim", "import", S, with(claim, "item", R"("P3")")), Code::syntax, "item"));
    // Elsewhere not_allowed: outside a project scope, on item, map, usage
    // and retire.
    CHECK(is(verdict("claim", "import", "person", with(claim, "item", R"("p3")")), Code::not_allowed, "item"));
    CHECK(is(verdict("claim", "import", "life", with(claim, "item", R"("p3")")), Code::not_allowed, "item"));
    for (const auto& [kind, basis] : {std::pair<std::string_view, std::string_view>{"item", "claimed"},
                                      {"map", "derived"}, {"usage", "observed"}, {"retire", "claimed"}}) {
        CHECK(is(verdict(kind, "import", S, Members{{"item", R"("p3")"}}, basis), Code::not_allowed, "item"));
    }
    // when on retire as on a served kind; on check not_allowed.
    const Members retire{{"evidence", OUTPUT}, {"reason", R"("r")"}, {"when", WHEN}};
    CHECK(passes(verdict("retire", "import", S, retire)));
    CHECK(passes(verdict("retire", "import", "person", retire)));
    CHECK(is(verdict("check", "import", S,
                     Members{{"evidence", OUTPUT}, {"method", R"("review")"}, {"result", R"("verified")"},
                             {"when", WHEN}}),
             Code::not_allowed, "when"));
    std::cout << "content.format: item in a project scope alone, when on retire\n";
}

QIVEN3_CASE(content, format_detail_closes_elsewhere) {
    // detail a checkpoint's alone and closes a decision's alone: on each
    // other kind of the 22, not_allowed at the field, in a project scope by
    // writer import, the kind's basis claimed where it allows that.
    std::size_t refused = 0;
    for (const kinds::Kind& k : kinds::KINDS) {
        const kinds::Bases claimed = kinds::bit(kinds::Basis::claimed);
        std::string_view basis = "claimed";
        if ((k.bases & claimed) == 0) {
            for (std::size_t b = 0; b < header::basis_names.size(); ++b) {
                if ((k.bases & kinds::bit(static_cast<kinds::Basis>(b))) != 0) {
                    basis = header::basis_names[b];
                    break;
                }
            }
        }
        const Members base = k.name == "check" ? Members{{"method", R"("review")"}} : Members{};
        for (const auto& [key, value, owner] :
             {std::tuple<std::string, std::string, std::string_view>{"detail", R"("d")", "checkpoint"},
              {"closes", R"([")" ID R"("])", "decision"}}) {
            if (k.name == owner) {
                continue;
            }
            CHECK(is(verdict(k.name.view(), "import", S, with(base, key, value), basis), Code::not_allowed, key));
            ++refused;
        }
    }
    CHECK(refused == 2 * (kinds::KINDS.size() - 1));
    std::cout << "content.format: detail and closes not_allowed on the other " << kinds::KINDS.size() - 1
              << " kinds\n";
}
