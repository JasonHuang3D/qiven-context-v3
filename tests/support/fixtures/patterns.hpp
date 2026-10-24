#pragma once

// patterns: the secret check's fixtures, each written as its table writes
// it, a Python expression of literals and counts, and built when a case
// runs (text), so that no file holds a live-looking token. Each names its
// pattern by its index in the secret check's table and says whether it
// hits; the tables' fixtures first, English and Chinese text among them,
// then the clearing fixtures, which their patterns hit. matcher's cases
// and secret's read the same.
//
// thread: none

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace qiven3::test::fixtures {

// A fixture's piece: a literal and its count.
struct PieceView {
    std::string_view text;
    std::size_t count = 0;
};

// A fixture: its pattern's index, whether it hits, its pieces in order.
struct RecipeView {
    std::size_t pattern = 0;
    bool hit = false;
    std::array<PieceView, 6> pieces;
};

inline constexpr std::array recipes = {
    // private-key
    RecipeView{0, true, {{{"-----BEGIN\x20", 1}, {"RSA PRIVATE KEY-----", 1}}}},
    RecipeView{0, true, {{{"-----BEGIN\x20", 1}, {"PGP PRIVATE KEY BLOCK-----", 1}}}},
    RecipeView{0, false, {{{"-----BEGIN\x20", 1}, {"PUBLIC KEY-----", 1}}}},
    // github-ghp
    RecipeView{1, true, {{{"ghp_", 1}, {"A", 36}}}},
    RecipeView{1, true, {{{"\xe4\xbb\xa4\xe7\x89\x8c ghp_", 1}, {"A", 36}, {"\xef\xbc\x8c\xe5\xb7\xb2\xe6\x92\xa4\xe9\x94\x80", 1}}}},
    RecipeView{1, false, {{{"ghp_", 1}, {"A", 35}}}},
    RecipeView{1, false, {{{"xghp_", 1}, {"A", 36}}}},
    // github-pat
    RecipeView{2, true, {{{"github_pat_", 1}, {"A", 22}, {"_", 1}, {"b", 59}}}},
    RecipeView{2, false, {{{"github_pat_", 1}, {"A", 81}}}},
    // sk-key
    RecipeView{3, true, {{{"sk-", 1}, {"0123456789abcdef", 2}}}},
    RecipeView{3, true, {{{"sk-proj-", 1}, {"B", 40}}}},
    RecipeView{3, false, {{{"sk-", 1}, {"a", 31}}}},
    RecipeView{3, false, {{{"task-", 1}, {"a", 40}}}},
    // aws-akia
    RecipeView{4, true, {{{"id\x3d" "AKIA", 1}, {"2", 16}}}},
    RecipeView{4, false, {{{"AKIA", 1}, {"a", 16}}}},
    RecipeView{4, false, {{{"XAKIA", 1}, {"A", 16}}}},
    // slack-xox
    RecipeView{5, true, {{{"xoxb-", 1}, {"1", 12}, {"-", 1}, {"a", 24}}}},
    RecipeView{5, true, {{{"xoxc-", 1}, {"1", 10}, {"-", 1}, {"1", 10}, {"-", 1}, {"a", 64}}}},
    RecipeView{5, false, {{{"xoxo-hugs-and-kisses", 1}}}},
    RecipeView{5, false, {{{"xoxb-", 1}, {"a", 20}}}},
    // slack-xoxd
    RecipeView{6, true, {{{"xoxd-", 1}, {"a", 40}}}},
    RecipeView{6, true, {{{"d\x3dxoxd-", 1}, {"a1%2B", 8}}}},
    RecipeView{6, false, {{{"xoxd-", 1}, {"a", 31}}}},
    RecipeView{6, false, {{{"xxoxd-", 1}, {"a", 40}}}},
    // url-password
    RecipeView{7, true, {{{"postgres://app:", 1}, {"p", 8}, {"@db/x", 1}}}},
    RecipeView{7, true, {{{"redis://:", 1}, {"p", 8}, {"@db", 1}}}},
    RecipeView{7, true, {{{"\xe8\xbf\x9e\xe6\x8e\xa5\xe4\xb8\xb2 postgres://app:", 1}, {"p", 8}, {"@db/x", 1}}}},
    RecipeView{7, false, {{{"https://example.com:8080/a@b", 1}}}},
    RecipeView{7, false, {{{"ssh://git@host/x", 1}}}},
    RecipeView{7, false, {{{"http://user:@host", 1}}}},
    // pair-password
    RecipeView{8, true, {{{"Server\x3d" "db;Password\x3d", 1}, {"p", 8}, {";", 1}}}},
    RecipeView{8, true, {{{"host\x3d" "db pwd\x3d", 1}, {"p", 8}}}},
    RecipeView{8, true, {{{"Server\x3d" "db;Password\x3d\"", 1}, {"p", 8}, {"\";", 1}}}},
    RecipeView{8, true, {{{"Data Source\x3d" "db;Pwd\x3d'", 1}, {"p", 8}, {"'", 1}}}},
    RecipeView{8, true, {{{"DB_PASSWORD\x3d", 1}, {"p", 8}}}},
    RecipeView{8, true, {{{"PGPASSWORD\x3d", 1}, {"p", 8}}}},
    RecipeView{8, true, {{{"\xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93 DB_PASSWORD\x3d", 1}, {"p", 8}}}},
    RecipeView{8, false, {{{"password: see the vault", 1}}}},
    RecipeView{8, false, {{{"keyword\x3d", 1}, {"k", 8}}}},
    RecipeView{8, false, {{{"Password\x3d\"\";", 1}}}},
    RecipeView{8, false, {{{"mypwd\x3d", 1}, {"p", 8}}}},
    RecipeView{8, false, {{{"OLDPWD\x3d", 1}, {"/c/x", 1}}}},
    RecipeView{8, false, {{{"\xe5\xaf\x86\xe7\xa0\x81\xef\xbc\x9a\xe8\xa7\x81\xe4\xbf\x9d\xe9\x99\xa9\xe5\xba\x93", 1}}}},
    // auth-header
    RecipeView{9, true, {{{"Authorization: Bearer\x20", 1}, {"a", 20}}}},
    RecipeView{9, true, {{{"\"authorization\": \"Basic\x20", 1}, {"dXNlcjpw", 2}, {"\"", 1}}}},
    RecipeView{9, true, {{{"Proxy-Authorization: Token\x20", 1}, {"t", 8}}}},
    RecipeView{9, true, {{{"\xe8\xaf\xb7\xe6\xb1\x82\xe5\xa4\xb4 Authorization: Bearer\x20", 1}, {"a1", 10}, {" \xe5\xb7\xb2\xe8\xbf\x87\xe6\x9c\x9f", 1}}}},
    RecipeView{9, false, {{{"Authorization: Bearer <token>", 1}}}},
    RecipeView{9, false, {{{"Authorization: Bearer $GH_TOKEN", 1}}}},
    RecipeView{9, false, {{{"Authorization: Bearer\x20", 1}, {"a", 7}}}},
    RecipeView{9, false, {{{"Authorization: Digest username\x3d", 1}, {"u", 8}}}},
    RecipeView{9, false, {{{"XAuthorization: Bearer\x20", 1}, {"a", 20}}}},
    // cli-secret
    RecipeView{10, true, {{{"mysql --password\x20", 1}, {"p", 8}}}},
    RecipeView{10, true, {{{"--token\x3d", 1}, {"t", 20}}}},
    RecipeView{10, true, {{{"--api-key '", 1}, {"k", 16}, {"'", 1}}}},
    RecipeView{10, true, {{{"--secret\x20", 1}, {"s", 8}}}},
    RecipeView{10, true, {{{"\xe7\x94\xa8 --token\x20", 1}, {"t", 20}, {" \xe7\x99\xbb\xe5\xbd\x95", 1}}}},
    RecipeView{10, true, {{{"--password\x20", 1}, {"\xe7\xa7\x98", 8}}}},
    RecipeView{10, false, {{{"--password-stdin", 1}}}},
    RecipeView{10, false, {{{"--token $GH_TOKEN", 1}}}},
    RecipeView{10, false, {{{"--api-key\x3d<key>", 1}}}},
    RecipeView{10, false, {{{"--secret {secret}", 1}}}},
    RecipeView{10, false, {{{"--password ''", 1}}}},
    RecipeView{10, false, {{{"x--token\x20", 1}, {"t", 8}}}},
    RecipeView{10, false, {{{"--tokens\x20", 1}, {"t", 8}}}},
    // tencent-akid
    RecipeView{11, true, {{{"SecretId\x3d" "AKID", 1}, {"a1B2", 8}}}},
    RecipeView{11, true, {{{"\xe5\xaf\x86\xe9\x92\xa5 AKID", 1}, {"Z", 32}}}},
    RecipeView{11, false, {{{"AKID", 1}, {"a", 31}}}},
    RecipeView{11, false, {{{"XAKID", 1}, {"a", 32}}}},
    RecipeView{11, false, {{{"akid", 1}, {"a", 32}}}},
    // The clearing fixtures.
    RecipeView{8, true, {{{"PWD", 1}, {"=/path/to/repo.git", 1}}}},
    RecipeView{7, true, {{{"https://x-access-token:", 1}, {"${GH_TOKEN}@github.com/o/r", 1}}}},
    RecipeView{7, true, {{{"postgres://<user>:", 1}, {"<password>@<host>/<db>", 1}}}},
    RecipeView{4, true, {{{"AKIA", 1}, {"IOSFODNN7EXAMPLE", 1}}}},
};

// The fixture's text: its pieces, each repeated its count, in order.
inline std::string text(const RecipeView& r) {
    std::string out;
    for (const PieceView& piece : r.pieces) {
        for (std::size_t i = 0; i < piece.count; ++i) {
            out += piece.text;
        }
    }
    return out;
}

}  // namespace qiven3::test::fixtures
