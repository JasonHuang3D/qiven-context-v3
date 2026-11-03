// digest's vectors: SHA-256's, the standard's examples and the padding's
// edges among them; RFC 4231's HMAC cases and the keys on and past a
// block; keyed's text over a 32-byte key's bytes, its wipe called once over
// the padded key's block; and the comparison's.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "qiven/digest/digest.hpp"
#include "qiven/runner/runner.hpp"

namespace {

namespace digest = qiven::digest;

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

// The bytes of n positions, byte i (a i + b) mod 256.
std::string pattern(std::size_t n, std::size_t a, std::size_t b) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += static_cast<char>((a * i + b) % 256);
    }
    return out;
}

}  // namespace

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
    // A key of 32 bytes, byte i (7 i + 3) mod 256; its padded block the
    // key and 32 zero bytes.
    const std::string material = pattern(32, 7, 3);
    std::array<std::byte, digest::bytes> key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::byte>(material[i]);
    }
    const std::string padded = material + std::string(32, '\0');
    const std::vector<std::pair<std::string, std::string_view>> rows = {
        {"", "feffb2318c2191c4f11e1fc0ee3d808697d6073ae8a1c30be5f1a169ceff13e6"},
        {"qiven \xe6\x91\x98\xe8\xa6\x81", "06ce8735c5323fe38bcfe34b20f53b6e8e3f8e0c0dc75720cd246669aed10fec"},  // 摘要
        {pattern(64, 131, 7), "63cdcb7b3bdf1f02bc1d8c6381e7fa34a1fb88b6e9ce24546ae8edc2f3f9ed7e"},
        {pattern(1120, 131, 7), "170ec20dd51a6920a441359641573f9d053bde76cc15bf29f5560005d807f3d7"},
    };
    for (const auto& [data, want] : rows) {
        // The case's wipe: the bytes it was given, then zeroed.
        std::size_t calls = 0;
        std::string given;
        bool zeroed = false;
        const auto wipe = [&](std::span<std::byte> block) {
            ++calls;
            given.clear();
            for (const std::byte b : block) {
                given += static_cast<char>(b);
            }
            for (std::byte& b : block) {
                b = std::byte{0};
            }
            zeroed = true;
            for (const std::byte b : block) {
                zeroed = zeroed && b == std::byte{0};
            }
        };
        std::string out;
        digest::keyed(std::span<const std::byte, digest::bytes>(key), data, out, wipe);
        CHECK_EQ(out, "hmac-sha256:" + std::string(want));
        CHECK_EQ(calls, 1U);
        CHECK_EQ(hex(given), hex(padded));
        CHECK(zeroed);
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
