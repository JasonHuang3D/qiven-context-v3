// write/kept over the fake platform's real files in a scratch store and a
// fake store's ids (every scope, writer, id and text invented): a kept
// file's form and names, read back and each corrupt form refused; two
// writes kept whole in order under the lock; the import's file with no
// lock and no settle step; the rebuild's names in order. The steps' faults
// and settling are write_kept_steps_test.cpp's.

#include "runner/runner.hpp"
#include "support/fixtures/kept_bed.hpp"

using namespace qiven3::test::kept_bed;

// The form, its names, and a file read back: its ids, and a forget's no body.
QIVEN3_CASE(write_kept, form) {
    std::vector<std::byte> pages(mib);
    Arena arena(pages);
    const Entries e(arena, "implementer/s7", 3, 4, true);
    String out{Allocator<char>(arena)};
    kept::bytes(kept::Head{12, kept::Command::write, Inline<64>("implementer/s7"), 0}, e.view(), out);
    const std::string_view first = R"({"kept":"qiven3-kept/1","n":12,"command":"write","writer":"implementer/s7","entries":3})";
    CHECK(std::string_view(out).starts_with(std::string(first) + "\n" + std::string(e.built[0].line) + "\n"));
    CHECK(std::string_view(out).ends_with("\n" + std::string(e.built[2].line) + "\n"));
    kept::Head head;
    Vector<kept::LineView> lines{Allocator<kept::LineView>(arena)};
    kept::parse(out, "0000000012.pending", arena, head, lines);
    CHECK(head.n == 12 && head.command == kept::Command::write && head.writer.view() == "implementer/s7");
    REQUIRE(lines.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(lines[i].id.view() == e.built[i].id.view() && lines[i].header == std::string_view(e.built[i].line));
        CHECK(lines[i].has_body == (i < 2) && lines[i].body == std::string_view(e.built[i].record));
    }
    CHECK(kept::file_name(1, kept::Form::kept).view() == "0000000001.jsonl");
    CHECK(kept::file_name(kept::last, kept::Form::pending).view() == "9999999999.pending");
    CHECK(kept::file_name(30, kept::Form::tmp).view() == "0000000030.tmp");
    const auto named = [](std::string_view name) { return kept::parse_name(name); };
    CHECK(named("0000000030.tmp")->n == 30 && named("0000000030.tmp")->form == kept::Form::tmp);
    CHECK(named(".0000000030.tmp.0123456789abcdef.tmp")->form == kept::Form::temporary);
    for (const std::string_view other : {"kept.lock", "0000000001.json", "000000001.jsonl", "0000000001.jsonl.x",
                                         ".0000000030.tmp.0123456789ABCDEF.tmp", ".0000000030.jsonl.0123456789abcdef.tmp"}) {
        CHECK(!named(other));
    }
}

// What parse refuses: each corrupt naming kept/<name>, lines left empty.
QIVEN3_CASE(write_kept, parse_corrupt) {
    std::vector<std::byte> pages(mib);
    Arena arena(pages);
    const Entries e(arena, "implementer/s7", 2, 1);
    String whole{Allocator<char>(arena)};
    kept::bytes(kept::Head{3, kept::Command::write, Inline<64>("implementer/s7"), 0}, e.view(), whole);
    const std::string good(whole);
    const std::string head_line = good.substr(0, good.find('\n') + 1);
    std::string twice = good;
    twice.replace(twice.find("\"entries\":2"), 11, "\"entries\":1");
    const std::vector<std::string> bad = {
        good.substr(0, good.size() - 1),                          // torn: no LF at the end
        good.substr(0, good.size() - e.built[1].record.size() - 1),  // the last body missing
        twice,                                                    // a line past the count
        head_line,                                                // no entry
        "{\"kept\":\"qiven3-kept/2\"" + good.substr(good.find(",\"n\"")),
        head_line + "not json\n",
        std::string(good).replace(good.find("\"n\":3"), 5, "\"n\":4"),  // n not the name's
    };
    for (const std::string& bytes : bad) {
        kept::Head head;
        Vector<kept::LineView> lines{Allocator<kept::LineView>(arena)};
        const auto r = refusal([&] { kept::parse(bytes, "0000000003.pending", arena, head, lines); });
        CHECK(r && r->code == rc::Code::corrupt && r->path.view() == "kept/0000000003.pending" && lines.empty());
    }
    kept::Head head;
    Vector<kept::LineView> lines{Allocator<kept::LineView>(arena)};
    kept::parse(good, "0000000003.jsonl", arena, head, lines);  // the nearest pass
    CHECK(lines.size() == 2);
    CHECK(arena.used() < 64 * 1024);  // the work span given back
}

// Two writes: each kept whole under the lock, 1 then 2; the rebuild's names.
QIVEN3_CASE(write_kept, write_pass) {
    const Scratch scratch{fresh};
    FakePlatform fake(scratch.config());
    std::vector<std::byte> pages(4 * mib);
    Arena arena(pages);
    Store store;
    const Entries a(arena, "implementer/s7", 2, 1, true);
    const Entries b(arena, "reviewer/s8", 1, 1);
    CHECK_EQ(kept::write(fake, scratch.store(), kept::Command::write, "implementer/s7", a.view(), store,
                         appends(store, a), arena),
             std::uint64_t{1});
    CHECK_EQ(scratch.names(), std::string("0000000001.jsonl"));
    String expected{Allocator<char>(arena)};
    kept::bytes(kept::Head{1, kept::Command::write, Inline<64>("implementer/s7"), 0}, a.view(), expected);
    CHECK(bytes_of(scratch.kept() / "0000000001.jsonl") == std::string_view(expected));
    CHECK_EQ(kept::write(fake, scratch.store(), kept::Command::import, "import", b.view(), store, appends(store, b),
                         arena),
             std::uint64_t{2});
    CHECK_EQ(scratch.names(), std::string("0000000001.jsonl|0000000002.jsonl"));
    CHECK(bytes_of(scratch.kept() / "0000000002.jsonl").starts_with(R"({"kept":"qiven3-kept/1","n":2,"command":"import")"));
    CHECK(store.appends == 2 && store.ids.size() == 3);
    CHECK(count(fake.calls(), "lock") == 2);
    write_bytes(scratch.kept() / "0000000003.pending", "x");
    write_bytes(scratch.kept() / "notes", "x");
    Vector<String> names{Allocator<String>(arena)};
    kept::kept_files(fake, scratch.store(), names);
    CHECK(names.size() == 2 && names[0] == "0000000001.jsonl" && names[1] == "0000000002.jsonl");
}

// The import: 0000000001 by steps 2 to 4, no lock, no listing (a pending
// file under its name: write_kept_steps.import_exists).
QIVEN3_CASE(write_kept, import) {
    const Scratch scratch{fresh};
    FakePlatform fake(scratch.config());
    std::vector<std::byte> pages(4 * mib);
    Arena arena(pages);
    Store store;
    const Entries e(arena, "import", 3, 1);
    kept::import(fake, scratch.store(), e.view(), appends(store, e), arena);
    CHECK_EQ(scratch.names(), std::string("0000000001.jsonl"));
    CHECK(bytes_of(scratch.kept() / "0000000001.jsonl")
              .starts_with(R"({"kept":"qiven3-kept/1","n":1,"command":"import","writer":"import","entries":3})"));
    CHECK(count(fake.calls(), "lock") == 0 && count(fake.calls(), "list") == 0 && store.appends == 1);
}
