// The per-call wire: a request read from an argument vector within its
// bounds, each bound at its limit and one past, an empty vector, a vector
// it cannot read and arguments that are not strict UTF-8, each refused
// with no request; the views it gives; an answer written whole on the
// fake, none for no bytes, and failed, the wire's exit status 2, for a
// write that throws, whatever it throws, and under QIVEN3_FAULTS for each
// platform call failed in turn.

#include <cstddef>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "wire/wire.hpp"

namespace {

namespace wire = qiven3::wire;
using qiven3::platform::fake::Config;
using qiven3::platform::fake::FakePlatform;

constexpr std::size_t most = std::numeric_limits<std::size_t>::max();

// An argument vector: the program's name, then texts, each a null-ended
// string the vector points into.
class Argv {
public:
    explicit Argv(std::vector<std::string> texts, const char* name = "qiven3") : texts_(std::move(texts)) {
        pointers_.push_back(name);
        for (const std::string& text : texts_) {
            pointers_.push_back(text.c_str());
        }
        pointers_.push_back(nullptr);
    }
    [[nodiscard]] int argc() const { return static_cast<int>(pointers_.size() - 1); }
    [[nodiscard]] const char* const* argv() const { return pointers_.data(); }
    [[nodiscard]] std::optional<wire::RequestView> read(const wire::Bounds& bounds) const {
        return wire::read_request(argc(), argv(), bounds);
    }

private:
    std::vector<std::string> texts_;
    std::vector<const char*> pointers_;
};

// The request's arguments as strings.
std::vector<std::string> texts(const wire::RequestView& request) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < request.size(); ++i) {
        out.emplace_back(request[i]);
    }
    return out;
}

// An output whose write throws E's value.
template <typename E>
struct Throwing {
    E error;
    struct Out {
        const E* error;
        void write(std::string_view /*bytes*/) const { throw *error; }
    };
    Out out{&error};
    Out& standard_output() { return out; }
};

using qiven3::platform::Code;
using qiven3::platform::PlatformError;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;

}  // namespace

static_assert(wire::failed == 2, "the wire's own failures end with exit status 2");
static_assert(wire::Output<FakePlatform>, "every platform is an output");

QIVEN3_CASE(wire, empty) {
    const wire::Bounds none{0, 0, 0};
    const std::optional<wire::RequestView> zero = wire::read_request(0, nullptr, none);
    REQUIRE(zero.has_value());
    CHECK(zero->empty());
    CHECK_EQ(zero->bytes(), std::size_t{0});
    const Argv name({});
    CHECK(wire::read_request(0, name.argv(), none).has_value());
    const std::optional<wire::RequestView> alone = name.read(none);
    REQUIRE(alone.has_value());
    CHECK_EQ(alone->size(), std::size_t{0});
    // The program's name is not read: null or not UTF-8, the request is.
    CHECK(Argv({}, nullptr).read(none).has_value());
    CHECK(Argv({"a"}, "\xff\xfe").read({1, 1, 1}).has_value());
}

QIVEN3_CASE(wire, unreadable) {
    const wire::Bounds wide{most, most, most};
    CHECK(!wire::read_request(-1, nullptr, wide).has_value());
    const Argv given({"location", "show"});
    CHECK(!wire::read_request(-1, given.argv(), wide).has_value());
    CHECK(!wire::read_request(std::numeric_limits<int>::min(), given.argv(), wide).has_value());
    CHECK(!wire::read_request(1, nullptr, wide).has_value());
    CHECK(!wire::read_request(3, nullptr, wide).has_value());
    // An argument null within argc.
    const std::vector<const char*> holed = {"qiven3", "location", nullptr, "show", nullptr};
    CHECK(!wire::read_request(4, holed.data(), wide).has_value());
    CHECK(wire::read_request(2, holed.data(), wide).has_value());
}

QIVEN3_CASE(wire, count) {
    const wire::Bounds three{3, 100, 100};
    const std::optional<wire::RequestView> at = Argv({"a", "b", "c"}).read(three);
    REQUIRE(at.has_value());
    CHECK_EQ(at->size(), std::size_t{3});
    CHECK(!Argv({"a", "b", "c", "d"}).read(three).has_value());
    CHECK(!Argv({"", "", "", ""}).read(three).has_value());
    const wire::Bounds none{0, 100, 100};
    CHECK(Argv({}).read(none).has_value());
    CHECK(!Argv({""}).read(none).has_value());
    // Past the bound, whatever follows it, read no further.
    const std::vector<const char*> holed = {"qiven3", "a", "b", "c", "d", nullptr};
    CHECK(!wire::read_request(5, holed.data(), three).has_value());
}

QIVEN3_CASE(wire, argument_bytes) {
    const wire::Bounds five{4, 5, 100};
    const std::optional<wire::RequestView> at = Argv({"abcde", "", "xyz"}).read(five);
    REQUIRE(at.has_value());
    CHECK_EQ(at->bytes(), std::size_t{8});
    CHECK(!Argv({"abcdef"}).read(five).has_value());
    CHECK(!Argv({"a", "abcdef", "b"}).read(five).has_value());
    // Bytes, not code points: U+00EB twice is 4 bytes, three times 6.
    const wire::Bounds four{4, 4, 100};
    CHECK(Argv({"\xc3\xab\xc3\xab"}).read(four).has_value());
    CHECK(!Argv({"\xc3\xab\xc3\xab\xc3\xab"}).read(four).has_value());
    const wire::Bounds empty_only{4, 0, 100};
    CHECK(Argv({"", ""}).read(empty_only).has_value());
    CHECK(!Argv({"", "a"}).read(empty_only).has_value());
}

QIVEN3_CASE(wire, total_bytes) {
    const wire::Bounds six{4, 10, 6};
    const std::optional<wire::RequestView> at = Argv({"abc", "def"}).read(six);
    REQUIRE(at.has_value());
    CHECK_EQ(at->bytes(), std::size_t{6});
    CHECK(Argv({"abcdef", "", ""}).read(six).has_value());
    CHECK(!Argv({"abc", "defg"}).read(six).has_value());
    CHECK(!Argv({"abcdef", "g"}).read(six).has_value());
    CHECK(!Argv({"abcdefg"}).read(six).has_value());
    // The total at its limit with each argument at its own.
    const wire::Bounds both{3, 2, 6};
    CHECK(Argv({"ab", "cd", "ef"}).read(both).has_value());
    CHECK(!Argv({"ab", "cd", "efg"}).read(both).has_value());
    const wire::Bounds tight{3, 2, 5};
    CHECK(!Argv({"ab", "cd", "ef"}).read(tight).has_value());
}

QIVEN3_CASE(wire, utf8) {
    const wire::Bounds wide{most, most, most};
    // U+00EB, U+4E2D, U+20AC, U+1F600, U+E000 and the last code point.
    const std::vector<std::string> good = {"caf\xc3\xab", "\xe4\xb8\xad", "\xe2\x82\xac", "\xf0\x9f\x98\x80",
                                           "\xee\x80\x80", "\xf4\x8f\xbf\xbf", "a b\"c"};
    const Argv given(good);  // the request's views are into it
    const std::optional<wire::RequestView> read = given.read(wide);
    REQUIRE(read.has_value());
    CHECK(texts(*read) == good);
    // A continuation alone, an overlong form, a surrogate's three bytes, a
    // form cut short, past U+10FFFF, bytes no UTF-8 holds.
    const std::vector<std::string> bad = {"\x80", "\xc0\xaf", "\xe0\x80\xaf", "\xed\xa0\x80", "\xed\xbf\xbf",
                                          "\xe4\xb8", "a\xf0\x9f\x98", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80",
                                          "\xff", "\xfe"};
    for (const std::string& one : bad) {
        CHECK(!Argv({one}).read(wide).has_value());
        CHECK(!Argv({"location", "show", one}).read(wide).has_value());
        CHECK(!Argv({"ok" + one + "ok"}).read(wide).has_value());
    }
    std::cout << "wire.utf8: " << good.size() << " read, " << bad.size() << " refused\n";
}

QIVEN3_CASE(wire, views) {
    const std::vector<std::string> given = {"location", "init", "", " spaced out ", "--config", "x"};
    const Argv vector(given);
    const std::optional<wire::RequestView> read = vector.read({6, 12, 33});
    REQUIRE(read.has_value());
    CHECK(texts(*read) == given);
    CHECK_EQ(read->bytes(), std::size_t{33});
    CHECK(!vector.read({6, 12, 32}).has_value());
    // Views into the vector the request was read from, not copies.
    CHECK((*read)[0].data() == vector.argv()[1]);
    const wire::RequestView copied = *read;
    CHECK_EQ(copied[5], std::string_view("x"));
    CHECK(!read->empty());
}

QIVEN3_CASE(wire, answer_whole) {
    FakePlatform fake{Config{}};
    const std::string bytes = "first line\nsecond, caf\xc3\xab\n";
    CHECK_EQ(wire::answer(fake, bytes, 7), 7);
    CHECK_EQ(fake.written(), bytes);
    CHECK(fake.calls() == std::vector<std::string>{"standard_output"});
    // An answer of many bytes, whole.
    FakePlatform big{Config{}};
    const std::string many(100000, 'x');
    CHECK_EQ(wire::answer(big, many, 0), 0);
    CHECK_EQ(big.written(), many);
    // write: the same bytes, its caller's status its own.
    FakePlatform line{Config{}};
    wire::write(line, bytes);
    CHECK_EQ(line.written(), bytes);
}

QIVEN3_CASE(wire, answer_empty) {
    FakePlatform fake{Config{}};
    CHECK_EQ(wire::answer(fake, "", 3), 3);
    CHECK_EQ(wire::answer(fake, std::string_view{}, 0), 0);
    CHECK(fake.written().empty());
    CHECK(fake.calls().empty());
    // write calls the platform whatever its bytes.
    wire::write(fake, "");
    CHECK(fake.calls() == std::vector<std::string>{"standard_output"});
}

QIVEN3_CASE(wire, answer_failed) {
    Throwing<PlatformError> platform{PlatformError(Code::io, "stdout", "WriteFile: broken pipe")};
    CHECK_EQ(wire::answer(platform, "a line\n", 0), wire::failed);
    Throwing<MemoryError> memory{MemoryError(MemoryCode::budget, "stdout", "budget 1, request 2, held 0")};
    CHECK_EQ(wire::answer(memory, "a line\n", 1), wire::failed);
    Throwing<std::bad_alloc> heap{std::bad_alloc()};
    CHECK_EQ(wire::answer(heap, "a line\n", 0), wire::failed);
    Throwing<int> other{5};
    CHECK_EQ(wire::answer(other, "a line\n", 0), wire::failed);
    // No bytes, no write, so no failure.
    CHECK_EQ(wire::answer(platform, "", 0), 0);
    // write leaves the platform's error to its caller, as it is.
    bool typed = false;
    try {
        wire::write(platform, "a line\n");
    } catch (const PlatformError& e) {
        typed = e.code == Code::io;
    }
    CHECK(typed);
}

QIVEN3_CASE(wire, answer_faults) {
#if defined(QIVEN3_FAULTS)
    const std::string bytes = "the answer\n";
    qiven3::memory::Countdown countdown;
    std::size_t k = 1;
    for (; k <= 4; ++k) {
        FakePlatform fake{Config{}};
        fake.attach(&countdown);
        countdown.arm(k);
        const int status = wire::answer(fake, bytes, 0);
        const bool fired = countdown.fired();
        countdown.disarm();
        fake.attach(nullptr);
        if (!fired) {
            CHECK_EQ(status, 0);
            CHECK_EQ(fake.written(), bytes);
            break;
        }
        // Failed: the wire's status, and no answer.
        CHECK_EQ(status, wire::failed);
        CHECK(fake.written().empty());
    }
    std::cout << "wire.answer_faults: platform calls " << k - 1 << "\n";
    CHECK_EQ(k, std::size_t{2});
#else
    QIVEN3_SKIP("not applicable: QIVEN3_FAULTS is off in this build");
#endif
}
