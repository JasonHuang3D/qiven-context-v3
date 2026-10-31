// digest's vectors: the framing of the entry's table, Chinese among them,
// and entry A's framed header and id; SHA-256's, the standard's examples
// and the padding's edges among them; RFC 4231's HMAC cases and the keys
// on and past a block; the content digests under the fixtures' key; L1's
// and L2's content and evidence digests under their own keys, and their
// ids; the comparison's; and the key's wipes on the fake platform, at its end, on a
// move and after keyed, with new_key's draw.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "qiven3/digest.hpp"
#include "qiven/runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace {

namespace digest = qiven3::digest;
using digest::Field;
using Fake = qiven3::platform::fake::FakePlatform;
using Key = digest::Key<Fake>;

std::string unhex(std::string_view text) {
    std::string out;
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        out += static_cast<char>((nibble(text[i]) << 4) | nibble(text[i + 1]));
    }
    return out;
}

std::string hex(std::string_view bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        out += digits[b >> 4U];
        out += digits[b & 0x0FU];
    }
    return out;
}

std::string hex(const digest::Bytes& bytes) {
    std::string raw;
    for (const std::uint8_t b : bytes) {
        raw += static_cast<char>(b);
    }
    return hex(raw);
}

std::string framed_hex(std::span<const Field> fields) {
    std::string out;
    digest::frame(fields, out);
    return hex(std::string_view(out));
}

// The bytes of n positions, byte i (a i + b) mod 256.
std::string pattern(std::size_t n, std::size_t a, std::size_t b) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += static_cast<char>((a * i + b) % 256);
    }
    return out;
}

// The fixtures' key: 00 to 1f.
std::array<std::byte, 32> fixture_key() {
    std::array<std::byte, 32> key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::byte>(i);
    }
    return key;
}

// A lesson's key: 32 bytes counting up from first (L1 c0, L2 e0).
std::array<std::byte, 32> lesson_key(unsigned first) {
    std::array<std::byte, 32> key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::byte>(first + i);
    }
    return key;
}

qiven3::platform::fake::Config seeded(std::uint64_t seed) {
    qiven3::platform::fake::Config config;
    config.seed = seed;
    return config;
}

std::size_t wipes(const Fake& fake) {
    std::size_t n = 0;
    for (const std::string& call : fake.calls()) {
        n += call == "wipe" ? 1U : 0U;
    }
    return n;
}

std::string keyed(const Key& key, std::string_view data) {
    std::string out;
    digest::keyed(key, data, out);
    return out;
}

constexpr std::string_view entry_a =
    "000000000000000d716976656e2d656e7472792f31000000000000000d70726f"
    "6a6563743a716976656e000000000000000b696d706c656d656e746572000000"
    "000000000131ffffffffffffffff0000000000000005636c61696d0000000000"
    "00000f696d706c656d656e7465722f7333390000000000000007636c61696d65"
    "640000000000000018323032362d31302d31375430393a33303a30302e303030"
    "5affffffffffffffff000000000000004c686d61632d7368613235363a646630"
    "3338386663313031633463306263303338656132646332333164643566616463"
    "3533373661663834336534383735356465396636333830393662653565";

}  // namespace

QIVEN_CASE(digest, framing) {
    const std::vector<std::pair<Field, std::string_view>> rows = {
        {std::nullopt, "ffffffffffffffff"},
        {"", "0000000000000000"},
        {"person", "0000000000000006706572736f6e"},
        {"12", "00000000000000023132"},
        {"caf\xc3\xa9", "0000000000000005636166c3a9"},
        {"\xf0\x9d\x84\x9e", "0000000000000004f09d849e"},
        {"\xe8\xae\xb0\xe5\xbf\x86", "0000000000000006e8aeb0e5bf86"},  // 记忆
    };
    for (const auto& [field, want] : rows) {
        CHECK_EQ(framed_hex(std::span<const Field>(&field, 1)), std::string(want));
    }
}

QIVEN_CASE(digest, entry_a) {
    // Entry A's eleven fields, parent and target absent.
    const std::array<Field, 11> fields = {
        "qiven-entry/1", "project:qiven", "implementer", "1", std::nullopt, "claim", "implementer/s39", "claimed",
        "2026-10-17T09:30:00.000Z", std::nullopt,
        "hmac-sha256:df0388fc101c4c0bc038ea2dc231dd5fadc5376af843e48755de9f638096be5e"};
    CHECK_EQ(framed_hex(fields), std::string(entry_a));
    CHECK_EQ(digest::framed_size(fields), 253U);
    std::string id;
    digest::entry_id(fields, id);
    CHECK_EQ(id, std::string("sha256:902380391ae283f1a88e038cf525aea22d5e4ed0ca3f852c8acdf43457b3b119"));
    std::string plain;
    digest::plain(unhex(entry_a), plain);
    CHECK_EQ(plain, id);
}

QIVEN_CASE(digest, sha256) {
    const std::string cavp_1304 = unhex(
        "451101250ec6f26652249d59dc974b7361d571a8101cdfd36aba3b5854d3ae086b5fdd4597721b66e3c0dc5d8c606d9657d0"
        "e323283a5217d1f53f2f284f57b85c8a61ac8924711f895c5ed90ef17745ed2d728abd22a5f7a13479a462d71b56c19a74a4"
        "0b655c58edfe0a188ad2cf46cbf30524f65d423c837dd1ff2bf462ac4198007345bb44dbb7b1c861298cdf61982a833afc72"
        "8fae1eda2f87aa2c9480858bec");
    const std::vector<std::pair<std::string, std::string_view>> rows = {
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {unhex("d3"), "28969cdfa74a12c82f3bad960b0b000aca2ac329deea5c2328ebc6f2ba9802c1"},
        {unhex("11af"), "5ca7133fa735326081558ac312c620eeca9970d1e70a4b95533d956f072d1f98"},
        {unhex("5a86b737eaea8ee976a0a24da63e7ed7eefad18a101c1211e2b3650c5187c2a8"
               "a650547208251f6d4237e661c7bf4c77f335390394c37fa1a9f9be836ac28509"),
         "42e61e174fbb3897d6dd6cef3dd2802fe67b331953b06114a65c772859dfc1aa"},
        {cavp_1304, "3c593aa539fdcdae516cdf2f15000f6634185c88f505b39775fb9ab137a10aa2"},
        {std::string(55, 'a'), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {std::string(56, 'a'), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {std::string(63, 'a'), "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {std::string(64, 'a'), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {std::string(65, 'a'), "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {std::string(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
    };
    for (const auto& [message, want] : rows) {
        CHECK_EQ(hex(digest::sha256(message)), std::string(want));
    }
    // A digest in parts is the digest whole.
    digest::Sha256 parts;
    parts.update(std::string_view(cavp_1304).substr(0, 70));
    parts.update(std::string_view(cavp_1304).substr(70));
    CHECK_EQ(hex(parts.finish()), hex(digest::sha256(cavp_1304)));
}

QIVEN_CASE(digest, hmac) {
    const std::string case7 =
        "This is a test using a larger than block-size key and a larger than block-size data. The key needs to "
        "be hashed before being used by the HMAC algorithm.";
    CHECK_EQ(case7.size(), 152U);
    const std::vector<std::tuple<std::string, std::string, std::string_view>> rows = {
        {std::string(20, '\x0b'), "Hi There", "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"},
        {"Jefe", "what do ya want for nothing?", "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"},
        {std::string(20, '\xaa'), std::string(50, '\xdd'),
         "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"},
        {pattern(25, 1, 1), std::string(50, '\xcd'), "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b"},
        {std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First",
         "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"},
        {std::string(131, '\xaa'), case7, "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2"},
        // The key's length on and past the block, over "qiven".
        {pattern(0, 7, 1), "qiven", "7a6606c5ffe88dae24a359756e5229ef7a6fa1515c86036817800a2bc7a65c30"},
        {pattern(32, 7, 1), "qiven", "94701393eda0101c49cd7c868b8ec835aff001f89a7e3697b1fdb734ae4f9ef4"},
        {pattern(63, 7, 1), "qiven", "812b527044162dbada0633d1f8aec771ce465c6e4c3f26bf47bc140360e4a9c0"},
        {pattern(64, 7, 1), "qiven", "7894ba505808627c9396bd3fb53a4f806fa1fdc71d3001dab0ad30cbaa2ad8bb"},
        {pattern(65, 7, 1), "qiven", "0a025e717c9838b0c94630ab0c70b0a42f024a72fa6df57b951a1709a1e8b57d"},
        {pattern(131, 7, 1), "qiven", "e8674da110643e4e3b3dd41c5de5e896fb8d16f528895a091073af29bbc06a46"},
    };
    for (const auto& [key, data, want] : rows) {
        CHECK_EQ(hex(digest::hmac(key, data)), std::string(want));
    }
    // Case 5, truncated to its first 128 bits.
    CHECK_EQ(hex(digest::hmac(std::string(20, '\x0c'), "Test With Truncation")).substr(0, 32),
             std::string("a3b6167473100ee06e0c796c2955552b"));
}

QIVEN_CASE(digest, keyed) {
    Fake fake(seeded(0));
    const std::array<std::byte, 32> material = fixture_key();
    const Key key(fake, material);
    const std::vector<std::pair<std::string, std::string_view>> rows = {
        {"{}", "86227fe96722a2247dcfee12c07d89c1b0567bbc9aa6a8d804838f66a3da843f"},
        {unhex("7b2261223a22636166c3a9222c22615f62223a5b747275652c66616c73655d2c"
               "226161223a2d393030373139393235343734303939312c2262223a3930303731"
               "39393235343734303939317d"),
         "a1d260ab7630300afa3722b8d897fba06856b2f9b94b881636ae44104cb03f73"},
        {unhex("7b2274657874223a22615c7530303030625c62635c74645c6e655c66665c7267"
               "5c7530303166685c22695c5c6a7f6be280a86ce280a96defbbbf6ef09d849e6f"
               "227d"),
         "7862e029b4b1cb2283feddef2b0cc2b2dedf7bdaefce012e36482b70b210423c"},
        {pattern(0, 131, 7), "d38b42096d80f45f826b44a9d5607de72496a415d3f4a1a8c88e3bb9da8dc1cb"},
        {pattern(64, 131, 7), "74139f4b63139647cfb34557dddc55c0e9d57087c653cc82a220ad6e6213107e"},
        {pattern(1120, 131, 7), "500bbe529377d0cf68e4273f432fd16545ab41eacf6a790e5055fb0aabc8ad23"},
    };
    CHECK_EQ(rows[1].first.size(), 76U);
    CHECK_EQ(rows[2].first.size(), 66U);
    for (const auto& [data, want] : rows) {
        CHECK_EQ(keyed(key, data), "hmac-sha256:" + std::string(want));
    }
}

QIVEN_CASE(digest, lessons) {
    // L1 and L2, each under its own key: the content digest over its
    // canonical content, the evidence digest over the interpreter's output,
    // and the id over its framed header, parent and target absent on L1.
    struct LessonView {
        std::array<std::byte, 32> key;
        std::string_view content;
        std::array<Field, 11> header;
        std::string_view output;
        std::string_view evidence;
        std::string_view digest;
        std::string_view id;
        std::size_t content_bytes;
        std::size_t framed_bytes;
    };
    constexpr std::string_view l1_id = "sha256:dbc3708cf045a0a4032f26cdc403298992821d648136a203a419002cf218334c";
    constexpr std::string_view l1_digest = "hmac-sha256:ca3d1e31fad36dce7c2a27c4015091cf85bf069b5929103d6169bc1b2dca8cf3";
    constexpr std::string_view l2_digest = "hmac-sha256:c5d45ae33ec18accb198b6f5a70d5b5421d84e96047d0f2c632ba932c542c356";
    constexpr std::string_view l1_content =
        "{\"admit\":{\"mistake\":\"A test would pass on one interpreter and fail on the next pin.\","
        "\"underivable\":\"\xe5\x9c\xa8\xe4\xbc\x9a\xe8\xaf\x9d\xe4\xb8\xad\xe8\xa7\x82\xe5\xaf\x9f"
        "\xe5\x88\xb0\xe7\x9a\x84\xe7\xbb\x93\xe6\x9e\x9c\xef\xbc\x8c\xe6\xb2\xa1\xe6\x9c\x89\xe5\x86"
        "\x99\xe8\xbf\x9b\xe4\xbb\xbb\xe4\xbd\x95\xe4\xbb\x93\xe5\xba\x93\xe3\x80\x82\"},\"band\":\"n"
        "ormal\",\"episode\":{\"client\":\"claude-code\",\"session\":\"00000000-0000-4000-8000-000000"
        "000042\"},\"evidence\":[{\"command\":\"python -I --version\",\"digest\":\"hmac-sha256:042796"
        "5c77d19ad3af90f88a2e288afa35e68082d468838b50d416e1e5e38474\",\"type\":\"output\"}],\"lesson"
        "\":\"\xe5\x9c\xa8 3.14.7 \xe4\xb8\x8a\xef\xbc\x8cstr.isspace \xe6\x8c\x89 Unicode 16.0.0 "
        "\xe5\x88\xa4\xe6\x96\xad\xef\xbc\x9bpin the interpreter in every test.\",\"pins\":[{\"name\""
        ":\"python\",\"type\":\"tool\",\"version\":\"3.14.7\"},{\"name\":\"PYTHONUTF8\",\"type\":\"en"
        "v\"}],\"subject\":\"entity:cpython\",\"trigger\":{\"activities\":[\"pin-interpreter\"]},\"wh"
        "en\":{\"quality\":\"exact\",\"time\":\"2026-10-17T05:00:00.000Z\"}}";
    constexpr std::string_view l2_content =
        "{\"admit\":{\"mistake\":\"A lesson pinned to 3.14.7 would be served after the re-pin to 3.15"
        ".0.\",\"underivable\":\"\xe6\x8d\xa2\xe7\x89\x88\xe6\x9c\xac\xe5\x90\x8e\xe9\x87\x8d\xe6\x96"
        "\xb0\xe8\xbf\x90\xe8\xa1\x8c\xe7\x9a\x84\xe8\xbe\x93\xe5\x87\xba\xef\xbc\x8c\xe6\xb2\xa1\xe6"
        "\x9c\x89\xe5\x86\x99\xe8\xbf\x9b\xe4\xbb\xbb\xe4\xbd\x95\xe4\xbb\x93\xe5\xba\x93\xe3\x80\x82"
        "\"},\"band\":\"normal\",\"episode\":{\"client\":\"claude-code\",\"session\":\"00000000-0000-"
        "4000-8000-000000000043\"},\"evidence\":[{\"id\":\"sha256:dbc3708cf045a0a4032f26cdc4032989928"
        "21d648136a203a419002cf218334c\",\"type\":\"entry\"},{\"command\":\"python -I --version\",\"d"
        "igest\":\"hmac-sha256:9b2b49164905577f1312bffc2f690e450a2152c95c432fa0c52cc01047bef814\",\"t"
        "ype\":\"output\"}],\"lesson\":\"On 3.15.0, str.isspace follows Unicode 17.0.0 and holds the "
        "same 29 code points; \xe6\xaf\x8f\xe4\xb8\xaa\xe6\xb5\x8b\xe8\xaf\x95\xe4\xbb\x8d\xe5\x9b"
        "\xba\xe5\xae\x9a\xe8\xa7\xa3\xe9\x87\x8a\xe5\x99\xa8\xe3\x80\x82\",\"pins\":[{\"name\":\"pyt"
        "hon\",\"type\":\"tool\",\"version\":\"3.15.0\"},{\"name\":\"PYTHONUTF8\",\"type\":\"env\"}],"
        "\"subject\":\"entity:cpython\",\"trigger\":{\"activities\":[\"pin-interpreter\"]},\"when\":{"
        "\"quality\":\"session\",\"time\":\"2026-10-17T09:00:00.000Z\"}}";
    const std::vector<LessonView> lessons = {
        {lesson_key(0xc0), l1_content,
         {"qiven-entry/1", "project:qiven", "learner", "1", std::nullopt, "lesson", "learner/s42", "claimed",
          "2026-10-17T05:02:10.000Z", std::nullopt, l1_digest},
         "Python 3.14.7\n", "hmac-sha256:0427965c77d19ad3af90f88a2e288afa35e68082d468838b50d416e1e5e38474", l1_digest,
         l1_id, 745, 246},
        {lesson_key(0xe0), l2_content,
         {"qiven-entry/1", "project:qiven", "learner", "2", l1_id, "lesson", "learner/s43", "claimed",
          "2026-10-17T09:40:00.000Z", l1_id, l2_digest},
         "Python 3.15.0\n", "hmac-sha256:9b2b49164905577f1312bffc2f690e450a2152c95c432fa0c52cc01047bef814", l2_digest,
         "sha256:566240684567f8e0289106449dddd847ec44a665313c8e09b32c5d48480f31c7", 876, 388},
    };
    Fake fake(seeded(0));
    for (const LessonView& lesson : lessons) {
        const Key key(fake, lesson.key);
        CHECK_EQ(lesson.content.size(), lesson.content_bytes);
        CHECK_EQ(digest::framed_size(lesson.header), lesson.framed_bytes);
        CHECK_EQ(keyed(key, lesson.content), std::string(lesson.digest));
        CHECK_EQ(keyed(key, lesson.output), std::string(lesson.evidence));
        std::string id;
        digest::entry_id(lesson.header, id);
        CHECK_EQ(id, std::string(lesson.id));
    }
}

QIVEN_CASE(digest, same) {
    const std::string key = pattern(32, 1, 0);
    std::string first = key;
    first[0] = '\xff';
    std::string last = key;
    last[31] = '\xff';
    CHECK(digest::same(key, key));
    CHECK(!digest::same(key, first));
    CHECK(!digest::same(key, last));
    CHECK(!digest::same(key.substr(0, 31), key));
    CHECK(digest::same("", ""));
}

QIVEN_CASE(digest, key_wipes) {
    Fake fake(seeded(0));
    const std::string data = "qiven";
    const std::string zero = "hmac-sha256:" + hex(digest::hmac(std::string(32, '\0'), data));
    const std::string fixed = "hmac-sha256:" + hex(digest::hmac(pattern(32, 1, 0), data));
    {
        Key key(fake, fixture_key());
        const Key& from = key;  // read after the move: its bytes read zero
        CHECK_EQ(wipes(fake), 0U);
        CHECK_EQ(keyed(key, data), fixed);
        CHECK_EQ(wipes(fake), 1U);  // keyed's padded block
        Key moved(std::move(key));
        CHECK_EQ(wipes(fake), 2U);  // the moved-from key
        CHECK_EQ(keyed(moved, data), fixed);
        CHECK_EQ(keyed(from, data), zero);
        Key other(fake, fixture_key());
        const Key& assigned = moved;
        other = std::move(moved);
        CHECK_EQ(wipes(fake), 6U);  // the assignment's two: the old key's, the moved-from's
        CHECK_EQ(keyed(other, data), fixed);
        CHECK_EQ(keyed(assigned, data), zero);
        CHECK_EQ(wipes(fake), 8U);
    }
    CHECK_EQ(wipes(fake), 11U);  // each key at its end
}

QIVEN_CASE(digest, new_key) {
    // The fake seeded 1 draws its first block: the key is those bytes, and
    // the drawn copy is wiped.
    Fake fake(seeded(1));
    const Key key = digest::new_key(fake);
    const std::vector<std::string> calls = {"random_bytes", "wipe"};
    CHECK(fake.calls() == calls);
    const std::string drawn = unhex("783825822a6f9e62da2190e828e4c9d2576e5977e3a0b3620b092dfb9e9996fa");
    CHECK_EQ(keyed(key, "qiven"), "hmac-sha256:" + hex(digest::hmac(drawn, "qiven")));
    const Key second = digest::new_key(fake);
    const std::string next = unhex("532deabf88729cb43995ab5a9cd49bf9b90a079904dc0645ecda9e47ce7345a9");
    CHECK_EQ(keyed(second, "qiven"), "hmac-sha256:" + hex(digest::hmac(next, "qiven")));
}
