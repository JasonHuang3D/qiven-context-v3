// labels' cases: the base by basis and an owner-words version's bindings
// (F29: the own writer's accept binds nothing; a re-verified binding before
// an unverified one; F120: a binding adds no suffix too); the strength rows
// alone and by their precedence, the earliest among equals named; the own
// writer's, a dead and an unequal check changing nothing; a reflection
// writer's lesson lifted only by verified or accepted (F156, A10); the
// longest labels at the capacity. Each is evaluated by the compiler.
// At run time: a label past its capacity; FA2's model entry over
// tests/fixtures/ (D alone, after ACC and after ACC2, with no transcript
// reader, a stub that yields the owner's text, and one that yields other
// bytes; a transcript check of D instead; A refused as the owner's; A
// alone, after its own writer's check and after C, the labels' half of
// FA2's status change).

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "entry/header.hpp"
#include "entry/labels.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace labels = qiven3::entry::labels;
namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace digest = qiven3::digest;
namespace fixtures = qiven3::test::fixtures;
using labels::Act;
using labels::Basis;
using labels::Binding;
using labels::CheckView;
using labels::Label;
using labels::Method;
using labels::Result;
using labels::Strength;
using labels::Transcript;
using labels::VersionView;
using Fake = qiven3::platform::fake::FakePlatform;

constexpr std::string_view OWN = "implementer/s39";
constexpr std::string_view OTHER = "reviewer/s70";
constexpr std::string_view THIRD = "tester/s71";
constexpr std::string_view MSG = "00000000-0000-4000-8000-0000000000a2";

constexpr CheckView check(std::string_view writer, Method m, Result r, Transcript t = Transcript::unverified) {
    CheckView c;
    c.writer = writer;
    c.method = m;
    c.result = r;
    c.transcript = t;
    if (m == Method::transcript) {
        c.message = MSG;
    }
    return c;
}
constexpr CheckView accept(std::string_view writer, Transcript t = Transcript::unverified) {
    CheckView c;
    c.writer = writer;
    c.act = Act::accept;
    c.message = MSG;
    c.transcript = t;
    return c;
}
constexpr CheckView dead(CheckView c) {
    c.live = false;
    return c;
}

template <typename... C>
constexpr Label of(Basis basis, std::string_view writer, C... checks) {
    const std::array<CheckView, sizeof...(C)> all = {checks...};
    return labels::label(VersionView{writer, basis}, std::span<const CheckView>(all));
}
template <typename... C>
constexpr bool reads(std::string_view want, Basis basis, C... checks) {
    return of(basis, OWN, checks...).text() == want;
}

constexpr auto OW = Basis::owner_words;
constexpr auto CL = Basis::claimed;
constexpr auto V = Result::verified;
constexpr auto R = Result::refuted;
constexpr auto RE = Transcript::reverified;
constexpr auto UNEQUAL = Transcript::unequal;

constexpr std::string_view UNBOUND = "the model's record of the owner's words, by implementer/s39, unbound";
constexpr std::string_view BOUND = "the model's record of the owner's words, by implementer/s39, bound (unverified) by "
                                   "reviewer/s70 (transcript 00000000-0000-4000-8000-0000000000a2)";
constexpr std::string_view WORD = "the owner's word (transcript 00000000-0000-4000-8000-0000000000a2, bound by "
                                  "reviewer/s70)";
constexpr std::string_view CLAIM = "claim by implementer/s39";

// The base by basis.
static_assert(reads("observed by implementer/s39", Basis::observed));
static_assert(reads(CLAIM, CL));
static_assert(reads("derived by implementer/s39, non-authoritative", Basis::derived));
static_assert(reads(UNBOUND, OW) && of(OW, OWN).binding == Binding::none);

// owner-words: the own writer's accept, or its session's, binds nothing
// (F29); another's binds, unverified with no reader, re-verified with it.
static_assert(reads(UNBOUND, OW, accept(OWN)) && reads(UNBOUND, OW, accept(OWN, RE)));
static_assert(reads(UNBOUND, OW, accept("reflect/s39", RE)));
static_assert(reads(BOUND, OW, accept(OTHER)) && of(OW, OWN, accept(OTHER)).binding == Binding::unverified);
static_assert(reads(BOUND, OW, accept(OWN), accept(OTHER)));
static_assert(reads(WORD, OW, accept(OTHER, RE)) && of(OW, OWN, accept(OTHER, RE)).binding == Binding::reverified);
// A transcript check of result verified binds, and adds no suffix (F120).
static_assert(reads(BOUND, OW, check(OTHER, Method::transcript, V)));
static_assert(of(OW, OWN, check(OTHER, Method::transcript, V)).strength == Strength::none);
static_assert(reads(WORD, OW, check(OTHER, Method::transcript, V, RE)));
static_assert(of(OW, OWN, accept(OTHER, RE)).strength == Strength::none);
// Unequal, dead or refuting: no binding.
static_assert(reads(UNBOUND, OW, accept(OTHER, UNEQUAL)) && reads(UNBOUND, OW, dead(accept(OTHER, RE))));
static_assert(of(OW, OWN, check(OTHER, Method::transcript, R, RE)).text() ==
              "the model's record of the owner's words, by implementer/s39, unbound; refuted by reviewer/s70 "
              "(transcript)");
// A re-verified binding before an earlier unverified one; the earliest
// among equals.
static_assert(of(OW, OWN, accept(THIRD), accept(OTHER, RE)).text() == WORD);
static_assert(reads(BOUND, OW, accept(OTHER), accept(THIRD)));
static_assert(reads(WORD, OW, accept(OTHER, RE), check(THIRD, Method::transcript, V, RE)));
// A refutation, a test's or a rerun's verification and a review still
// apply on owner-words.
static_assert(of(OW, OWN, check(OTHER, Method::rerun, V)).text() ==
              "the model's record of the owner's words, by implementer/s39, unbound; verified by reviewer/s70 (rerun)");
static_assert(of(OW, OWN, accept(OTHER, RE), check(THIRD, Method::test, R)).text() ==
              "the owner's word (transcript 00000000-0000-4000-8000-0000000000a2, bound by reviewer/s70); refuted by "
              "tester/s71 (test)");
static_assert(of(OW, OWN, accept(OTHER), check(THIRD, Method::review, R)).text() ==
              "the model's record of the owner's words, by implementer/s39, bound (unverified) by reviewer/s70 "
              "(transcript 00000000-0000-4000-8000-0000000000a2); disputed by tester");
static_assert(of(OW, OWN, check(THIRD, Method::review, V)).strength == Strength::checked);
static_assert(of(OW, OWN, check(THIRD, Method::transcript, R)).strength == Strength::disputed);
static_assert(of(OW, OWN, check(THIRD, Method::transcript, R, RE)).strength == Strength::refuted);

// Each strength row alone, on a claim.
static_assert(reads("claim by implementer/s39; refuted by reviewer/s70 (rerun)", CL, check(OTHER, Method::rerun, R)));
static_assert(reads("claim by implementer/s39; refuted by reviewer/s70 (transcript)", CL,
                    check(OTHER, Method::transcript, R, RE)));
static_assert(reads("claim by implementer/s39; accepted by the owner (transcript 00000000-0000-4000-8000-0000000000a2, "
                    "recorded by reviewer/s70)",
                    CL, accept(OTHER, RE)));
static_assert(reads("claim by implementer/s39; verified by reviewer/s70 (test)", CL, check(OTHER, Method::test, V)));
static_assert(reads("claim by implementer/s39; verified by reviewer/s70 (transcript)", CL,
                    check(OTHER, Method::transcript, V, RE)));
static_assert(reads("claim by implementer/s39; accepted (unverified) (transcript 00000000-0000-4000-8000-0000000000a2, "
                    "recorded by reviewer/s70)",
                    CL, accept(OTHER)));
static_assert(reads("claim by implementer/s39; disputed by reviewer", CL, check(OTHER, Method::review, R)));
static_assert(reads("claim by implementer/s39; disputed by reviewer", CL, check(OTHER, Method::transcript, R)));
static_assert(reads("claim by implementer/s39; checked by reviewer", CL, check(OTHER, Method::review, V)));
static_assert(reads("claim by implementer/s39; checked by reviewer", CL, check(OTHER, Method::transcript, V)));
static_assert(reads("observed by implementer/s39; checked by loop", Basis::observed, check("loop", Method::review, V)));

// The precedence, either order: refuted, accepted, verified, accepted
// (unverified), disputed, checked.
constexpr CheckView REFUTED = check(OTHER, Method::test, R);
constexpr CheckView ACCEPTED = accept(OTHER, RE);
constexpr CheckView VERIFIED = check(OTHER, Method::rerun, V);
constexpr CheckView ACCEPTED_UNVERIFIED = accept(OTHER);
constexpr CheckView DISPUTED = check(OTHER, Method::review, R);
constexpr CheckView CHECKED = check(OTHER, Method::review, V);
constexpr std::array<CheckView, 6> ROWS = {CHECKED, DISPUTED, ACCEPTED_UNVERIFIED, VERIFIED, ACCEPTED, REFUTED};
constexpr bool precedence() {
    for (std::size_t i = 0; i + 1 < ROWS.size(); ++i) {
        const Strength stronger = of(CL, OWN, ROWS[i + 1]).strength;
        if (stronger <= of(CL, OWN, ROWS[i]).strength || of(CL, OWN, ROWS[i], ROWS[i + 1]).strength != stronger ||
            of(CL, OWN, ROWS[i + 1], ROWS[i]).strength != stronger) {
            return false;
        }
    }
    return labels::label(VersionView{OWN, CL}, ROWS).strength == Strength::refuted;
}
static_assert(precedence());
static_assert(of(CL, OWN, REFUTED).strength == Strength::refuted && labels::name(Strength::refuted) == "refuted");
static_assert(labels::name(Strength::accepted_unverified) == "accepted, unverified");
// Among equals the earliest is named.
static_assert(reads("claim by implementer/s39; verified by reviewer/s70 (rerun)", CL, VERIFIED,
                    check(THIRD, Method::test, V)));
static_assert(reads("claim by implementer/s39; verified by tester/s71 (test)", CL, check(THIRD, Method::test, V),
                    VERIFIED));
static_assert(reads("claim by implementer/s39; disputed by tester", CL, check(THIRD, Method::review, R), DISPUTED));

// The own writer's check or accept, a dead one, an unequal transcript and
// a check of no method or result change nothing.
static_assert(reads(CLAIM, CL, check(OWN, Method::rerun, R)) && reads(CLAIM, CL, check(OWN, Method::test, V)));
static_assert(reads(CLAIM, CL, check("reflect/s39", Method::test, V)) && reads(CLAIM, CL, accept(OWN, RE)));
static_assert(reads(CLAIM, CL, dead(REFUTED), dead(ACCEPTED), dead(CHECKED)));
static_assert(reads(CLAIM, CL, check(OTHER, Method::transcript, R, UNEQUAL), accept(OTHER, UNEQUAL)));
static_assert(reads(CLAIM, CL, check(OTHER, Method::none, V), check(OTHER, Method::rerun, Result::none)));
static_assert(of(CL, OWN, check(OWN, Method::rerun, R)).strength == Strength::none);

// A reflection writer's lesson, named by the session that runs it, is
// lifted only by verified or accepted, by another session (F156, A10).
template <typename... C>
constexpr bool lifted(C... checks) {
    return of(CL, "reflect/s39", checks...).lifted();
}
static_assert(!lifted() && !lifted(check(OWN, Method::rerun, V)) && !lifted(check("reflect/s39", Method::test, V)));
static_assert(lifted(VERIFIED) && lifted(check(OTHER, Method::test, V)) && lifted(ACCEPTED));
static_assert(lifted(check(OTHER, Method::transcript, V, RE)) && !lifted(check(OTHER, Method::transcript, V)));
static_assert(!lifted(CHECKED) && !lifted(ACCEPTED_UNVERIFIED) && !lifted(DISPUTED) && !lifted(REFUTED));
static_assert(!lifted(VERIFIED, REFUTED) && lifted(CHECKED, VERIFIED));
static_assert(!of(CL, "reflect/s70", VERIFIED).lifted());
static_assert(of(CL, "reflect/s70", VERIFIED).text() == "claim by reflect/s70");

// check_of and version_of over a header and its content: a transcript
// check's first transcript reference in its evidence, an accept's message,
// a rerun's none; another kind is no check; a basis off the set no version.
constexpr bool read_back(std::string_view head, std::string_view body, std::string_view message,
                         std::string_view digest, Act act, Method method, Result result) {
    const header::Header<> h = header::parse_line(head);
    canon::Basic<> content;
    canon::parse(body, content);
    const std::optional<CheckView> c = labels::check_of(h, content, false, RE);
    return c && c->writer == OTHER && c->message == message && c->digest == digest && c->act == act &&
           c->method == method && c->result == result && c->transcript == RE && !c->live;
}
static_assert(read_back(R"({"kind":"check","writer":"reviewer/s70"})",
                        R"({"evidence":[{"command":"x","type":"output"},)"
                        R"({"digest":"d1","message":"m1","type":"transcript"},)"
                        R"({"digest":"d2","message":"m2","type":"transcript"}],)"
                        R"("method":"transcript","result":"verified"})",
                        "m1", "d1", Act::check, Method::transcript, V));
static_assert(read_back(R"({"kind":"check","writer":"reviewer/s70"})",
                        R"({"evidence":[{"digest":"d1","message":"m1","type":"transcript"}],)"
                        R"("method":"rerun","result":"refuted"})",
                        "", "", Act::check, Method::rerun, R));
static_assert(read_back(R"({"kind":"accept","writer":"reviewer/s70"})",
                        R"({"message":{"digest":"d3","message":"m3","type":"transcript"},"words":"Yes."})", "m3", "d3",
                        Act::accept, Method::none, Result::none));
constexpr bool no_check(std::string_view head) {
    canon::Basic<> content;
    return !labels::check_of(header::parse_line(head), content, true).has_value();
}
static_assert(no_check(R"({"kind":"claim","writer":"reviewer/s70"})") && no_check(R"({"kind":"retire"})"));
static_assert(labels::version_of(header::parse_line(R"({"basis":"derived","writer":"loop"})"))->basis ==
              Basis::derived);
static_assert(!labels::version_of(header::parse_line(R"({"basis":"owner","writer":"loop"})")).has_value());

// The longest labels: the writers and the message at their syntaxes'
// longest; owner-words bound (unverified) and refuted by transcript fills
// the capacity; a claim's longest, accepted, is shorter.
constexpr std::string_view W1 = "rrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrr/s123456789";
constexpr std::string_view W2 = "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq/s987654321";
constexpr std::array<char, labels::message_max> LONG_BYTES = [] {
    std::array<char, labels::message_max> a{};
    for (char& c : a) {
        c = 'm';
    }
    return a;
}();
constexpr std::string_view LONG(LONG_BYTES.data(), LONG_BYTES.size());
static_assert(W1.size() == labels::writer_max && LONG.size() == labels::message_max);
constexpr CheckView longest(std::string_view writer, Act act, Method m, Result r, Transcript t = RE) {
    CheckView c = check(writer, m, r, t);
    c.act = act;
    c.message = LONG;
    return c;
}
constexpr CheckView ACC_LONG = longest(W2, Act::accept, Method::none, Result::none, Transcript::unverified);
constexpr CheckView REF_LONG = longest(W2, Act::check, Method::transcript, R);
static_assert(of(OW, W1, ACC_LONG, REF_LONG).text().size() == 296 + 69);
static_assert(of(OW, W1, ACC_LONG, REF_LONG).size == labels::capacity);
static_assert(of(Basis::derived, W1, ACC_LONG).size == 73 + 222);
static_assert(labels::basis_of("owner-words") == Basis::owner_words && labels::basis_of("derived") == Basis::derived &&
              !labels::basis_of("owner").has_value());

}  // namespace

namespace {

// A stored line's entry, its content parsed where it has one, and its key.
struct Held {
    header::Header<> h;
    canon::Basic<> v;
    std::string key;
};

Held held(canon::Basic<>& all, std::string_view name) {
    const std::optional<fixtures::EntryView> x = fixtures::find_entry(all, name);
    if (!x) {
        throw std::runtime_error("no fixture entry");
    }
    canon::Basic<> content;
    if (x->content) {
        canon::parse(*x->content, content);
    }
    return Held{header::parse_line(x->header), std::move(content), std::string(x->key.value_or(""))};
}

qiven3::platform::fake::Config seeded() {
    qiven3::platform::fake::Config config;
    config.seed = 0;
    return config;
}

// A stub transcript reader's re-verification: the keyed digest under the
// entry's key over the bytes it yields, against the reference's digest.
Transcript reverify(Fake& fake, const Held& x, const CheckView& c, std::optional<std::string_view> yields) {
    if (!yields) {
        return Transcript::unverified;
    }
    std::optional<fixtures::KeyBytes> material = fixtures::key_bytes(x.key);
    if (!material) {
        throw std::runtime_error("no key");
    }
    const digest::Key key(fake, std::span<const std::byte, 32>(*material));
    fake.wipe(std::span<std::byte>(*material));
    std::string out;
    digest::keyed(key, *yields, out);
    return out == c.digest ? Transcript::reverified : Transcript::unequal;
}

std::string by(const Held& version, std::span<const CheckView> checks) {
    const std::optional<VersionView> v = labels::version_of(version.h);
    return v ? "by: " + std::string(labels::label(*v, checks).text()) : "no version";
}

}  // namespace

QIVEN3_CASE(labels, capacity) {
    // A writer past its syntax's length overflows the label: its budget.
    const std::string writer(labels::capacity, 'w');
    try {
        static_cast<void>(labels::label(VersionView{writer, Basis::claimed}, {}));
        CHECK(false);
    } catch (const qiven3::memory::MemoryError& e) {
        CHECK(e.code == qiven3::memory::MemoryCode::budget);
        std::cout << "labels.capacity: " << e.operation.view() << ": " << e.detail.view() << "\n";
    }
    // A claim's label at the capacity.
    const std::string fits(labels::capacity - 9, 'w');
    CHECK_EQ(labels::label(VersionView{fits, Basis::claimed}, {}).text().size(), labels::capacity);
}

QIVEN3_CASE(labels, fa2_model_entry) {
    canon::Basic<> all;
    fixtures::load_entries(all);
    const Held a = held(all, "A");
    const Held c = held(all, "C");
    const Held d = held(all, "D");
    const Held acc = held(all, "ACC");
    const Held acc2 = held(all, "ACC2");
    Fake fake(seeded());
    const std::string unbound = "by: the model's record of the owner's words, by implementer/s39, unbound";
    const std::string bound = "by: the model's record of the owner's words, by implementer/s39, bound (unverified) "
                              "by reviewer/s70 (transcript 00000000-0000-4000-8000-0000000000a2)";
    const std::string word = "by: the owner's word (transcript 00000000-0000-4000-8000-0000000000a2, bound by "
                             "reviewer/s70)";
    // D alone, and after ACC, an accept by D's own writer (F29).
    const CheckView own = labels::check_of(acc.h, acc.v, true).value();
    CHECK_EQ(own.act == Act::accept, true);
    CHECK_EQ(std::string(own.message), "00000000-0000-4000-8000-0000000000a2");
    CHECK_EQ(by(d, {}), unbound);
    CHECK_EQ(by(d, std::array{own}), unbound);
    // After ACC2, by reviewer/s70: with no transcript reader, a stub that
    // yields the owner's text, and one that yields other bytes.
    CheckView other = labels::check_of(acc2.h, acc2.v, true).value();
    CHECK_EQ(by(d, std::array{own, other}), bound);
    other.transcript = reverify(fake, acc2, other, "Yes, keep that.");
    CHECK(other.transcript == Transcript::reverified);
    CHECK_EQ(by(d, std::array{own, other}), word);
    other.transcript = reverify(fake, acc2, other, "Yes, keep this.");
    CHECK(other.transcript == Transcript::unequal);
    CHECK_EQ(by(d, std::array{own, other}), unbound);
    CHECK(reverify(fake, acc, own, "Yes, keep that.") == Transcript::reverified);
    // Instead of ACC2, its check of D, method transcript, result verified:
    // bound, adding no suffix (F120).
    CheckView transcript_check = check(OTHER, Method::transcript, Result::verified);
    CHECK_EQ(by(d, std::array{own, transcript_check}), bound);
    transcript_check.transcript = Transcript::reverified;
    CHECK_EQ(by(d, std::array{own, transcript_check}), word);
    // A with writer owner/s1 and lane owner is refused with owner.
    std::string line(fixtures::find_entry(all, "A")->header);
    for (const auto& [from, to] : {std::pair<std::string_view, std::string_view>{"\"writer\":\"implementer/s39\"",
                                                                                 "\"writer\":\"owner/s1\""},
                                   {"\"lane\":\"implementer\"", "\"lane\":\"owner\""}}) {
        const std::size_t at = line.find(from);
        REQUIRE(at != std::string::npos);
        line.replace(at, from.size(), to);
    }
    CHECK(header::try_check(header::parse_line(line)) == header::Code::owner);
    // A alone, after a check by its own writer, and after C, reviewer/s70's
    // rerun: the labels' half of FA2's status change.
    CHECK_EQ(by(a, {}), "by: claim by implementer/s39");
    CHECK_EQ(by(a, std::array{check(OWN, Method::rerun, Result::verified)}), "by: claim by implementer/s39");
    const CheckView rerun = labels::check_of(c.h, c.v, true).value();
    CHECK(rerun.method == Method::rerun && rerun.result == Result::verified && rerun.message.empty());
    CHECK_EQ(by(a, std::array{rerun}), "by: claim by implementer/s39; verified by reviewer/s70 (rerun)");
    CHECK(labels::label(labels::version_of(a.h).value(), std::array{rerun}).lifted());
    CHECK(!labels::check_of(a.h, a.v, true).has_value());
    std::cout << "labels.fa2_model_entry: " << by(d, std::array{own, transcript_check}) << "\n";
}
