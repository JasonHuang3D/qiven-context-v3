// write/kept's steps over the fake platform's real files in a scratch
// store and a fake store's ids (every scope, writer, id and text invented),
// each with its nearest pass: step 1 removing each temporary unread and
// settling a pending file by its ids (none held removed, all held renamed
// kept, a part corrupt and never settled, and corrupt while its kept name
// is taken); step 2 failing before or after its file is
// whole, no pending file left; step 3's refusal removing the pending file,
// a fault before or after the commit leaving it whole; step 4's rename
// failing busy or exists, the pending file left; each settled by the next
// write; busy under a held lock; past the last name, store_full; and the
// import's step 2 meeting a file under the pending name.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "runner/runner.hpp"
#include "support/fixtures/kept_bed.hpp"

using namespace qiven3::test::kept_bed;

namespace {

constexpr std::string_view writer = "implementer/s7";

// A case's store: its scratch directory, the fake, an arena and the ids.
struct Bed {
    Scratch scratch{fresh};
    FakePlatform fake{scratch.config()};
    std::vector<std::byte> pages = std::vector<std::byte>(4 * mib);
    Arena arena{pages};
    Store store;

    template <class A>
    std::uint64_t write(const Entries& e, A&& append) {
        return kept::write(fake, scratch.store(), kept::Command::write, writer, e.view(), store,
                           std::forward<A>(append), arena);
    }
    std::uint64_t write(const Entries& e) { return write(e, appends(store, e)); }
    // A kept file's bytes for n, as write keeps them.
    std::string file(std::uint64_t n, const Entries& e) {
        String out{Allocator<char>(arena)};
        kept::bytes(kept::Head{n, kept::Command::write, Inline<64>(writer), 0}, e.view(), out);
        return std::string(out);
    }
    void plant(std::string_view name, std::string_view bytes) const {
        fs::create_directories(scratch.kept());
        write_bytes(scratch.kept() / std::string(name), bytes);
    }
    [[nodiscard]] std::string at(std::string_view name) const {
        return bytes_of(scratch.kept() / std::string(name));
    }
};

void hold(Store& store, const Entries& e) {
    for (const Built& b : e.built) {
        store.ids.insert(std::string(b.id.view()));
    }
}

}  // namespace

// Step 1: a torn .tmp and replace's own temporary removed unread, a kept
// file and another name untouched.
QIVEN3_CASE(write_kept_steps, settle_temporaries) {
    const auto bed = std::make_unique<Bed>();
    bed->plant("0000000001.jsonl", "a kept file, not read");
    bed->plant("0000000002.tmp", "{\"kept\":\"qiven3-ke");
    bed->plant(".0000000003.tmp.0123456789abcdef.tmp", "torn");
    bed->plant("notes", "another name");
    const Entries e(bed->arena, writer, 1, 1);
    CHECK_EQ(bed->write(e), std::uint64_t{2});
    CHECK_EQ(bed->scratch.names(), std::string("0000000001.jsonl|0000000002.jsonl|notes"));
    CHECK(count(bed->fake.calls(), "read") == 0);
    CHECK(bed->at("0000000001.jsonl") == "a kept file, not read");
}

// Step 1: a pending file whose ids the store holds none of removed, all of
// renamed kept; a part, corrupt, never settled, nothing appended, until
// the store holds them all.
QIVEN3_CASE(write_kept_steps, settle_by_ids) {
    {
        const auto bed = std::make_unique<Bed>();
        const Entries old(bed->arena, writer, 2, 1);
        bed->plant("0000000001.pending", bed->file(1, old));
        const Entries e(bed->arena, writer, 1, 3);
        CHECK_EQ(bed->write(e), std::uint64_t{1});
        CHECK_EQ(bed->scratch.names(), std::string("0000000001.jsonl"));
        CHECK(bed->at("0000000001.jsonl") == bed->file(1, e));
    }
    {
        const auto bed = std::make_unique<Bed>();
        const Entries old(bed->arena, writer, 2, 1, true);
        const std::string pending = bed->file(1, old);
        bed->plant("0000000001.pending", pending);
        hold(bed->store, old);
        const Entries e(bed->arena, writer, 1, 3);
        CHECK_EQ(bed->write(e), std::uint64_t{2});
        CHECK_EQ(bed->scratch.names(), std::string("0000000001.jsonl|0000000002.jsonl"));
        CHECK(bed->at("0000000001.jsonl") == pending);
    }
    {
        const auto bed = std::make_unique<Bed>();
        const Entries old(bed->arena, writer, 2, 1);
        const std::string pending = bed->file(1, old);
        bed->plant("0000000001.pending", pending);
        bed->store.ids.insert(std::string(old.built[1].id.view()));
        const Entries e(bed->arena, writer, 1, 3);
        const auto r = refusal([&] { bed->write(e); });
        CHECK(r && r->code == rc::Code::corrupt && r->path.view() == "kept/0000000001.pending");
        CHECK(bed->store.appends == 0 && bed->scratch.names() == "0000000001.pending");
        CHECK(bed->at("0000000001.pending") == pending);
        const auto torn = refusal([&] {
            bed->plant("0000000001.pending", pending.substr(0, pending.size() / 2));
            bed->write(e);
        });
        CHECK(torn && torn->code == rc::Code::corrupt && bed->store.appends == 0);
        bed->plant("0000000001.pending", pending);
        hold(bed->store, old);
        CHECK_EQ(bed->write(e), std::uint64_t{2});  // the lock released at each end
    }
}

// Step 1, a rename's exists [reading]: a held pending file whose kept name
// is taken corrupt, both left, nothing appended; the name free, the next
// write settles it, its n one past the highest after step 1's removals,
// and every kept file parses under its name.
QIVEN3_CASE(write_kept_steps, settle_exists) {
    const auto bed = std::make_unique<Bed>();
    const Entries one(bed->arena, writer, 1, 4);
    const Entries old(bed->arena, writer, 1, 1);
    const std::string pending = bed->file(2, old);
    bed->plant("0000000001.jsonl", bed->file(1, one));
    bed->plant("0000000002.jsonl", "a file under the pending file's kept name");
    bed->plant("0000000002.pending", pending);
    hold(bed->store, old);
    const Entries e(bed->arena, writer, 1, 2);
    const auto r = refusal([&] { bed->write(e); });
    CHECK(r && r->code == rc::Code::corrupt && r->path.view() == "kept/0000000002.pending");
    CHECK(bed->store.appends == 0 && bed->at("0000000002.pending") == pending);
    CHECK_EQ(bed->scratch.names(), std::string("0000000001.jsonl|0000000002.jsonl|0000000002.pending"));
    fs::remove(bed->scratch.kept() / "0000000002.jsonl");
    bed->plant("0000000007.tmp", "torn");
    CHECK_EQ(bed->write(e), std::uint64_t{3});
    CHECK_EQ(bed->scratch.names(), std::string("0000000001.jsonl|0000000002.jsonl|0000000003.jsonl"));
    Vector<String> names{Allocator<String>(bed->arena)};
    kept::kept_files(bed->fake, bed->scratch.store(), names);
    for (const String& name : names) {
        kept::Head head;
        Vector<kept::LineView> lines{Allocator<kept::LineView>(bed->arena)};
        kept::parse(bed->at(name), name, bed->arena, head, lines);
        CHECK(lines.size() == 1 && kept::file_name(head.n, kept::Form::kept).view() == name);
    }
}

// Step 2: replace failing leaves nothing; the rename to pending failing
// leaves the .tmp whole and no pending file; nothing appended; the next
// write removes the .tmp.
QIVEN3_CASE(write_kept_steps, keep_faults) {
    const auto bed = std::make_unique<Bed>();
    const Entries e(bed->arena, writer, 2, 1);
    bed->fake.set_busy(Busy{.target = never});
    CHECK(fault([&] { bed->write(e); }) == Code::busy);
    CHECK(bed->scratch.names().empty() && bed->store.appends == 0);
    bed->fake.set_busy(Busy{.rename = never});
    CHECK(fault([&] { bed->write(e); }) == Code::busy);
    CHECK_EQ(bed->scratch.names(), std::string("0000000001.tmp"));
    CHECK(bed->at("0000000001.tmp") == bed->file(1, e) && bed->store.appends == 0);
    bed->fake.set_busy(Busy{});
    CHECK_EQ(bed->write(e), std::uint64_t{1});
    CHECK_EQ(bed->scratch.names(), std::string("0000000001.jsonl"));
}

// Step 4 on a refusal: the pending file removed, the refusal thrown; its
// removal failing is dropped, the file left and removed by the next write.
QIVEN3_CASE(write_kept_steps, done_refused) {
    const auto bed = std::make_unique<Bed>();
    const Entries e(bed->arena, writer, 2, 1);
    const auto refuse = [] { throw Refusal{rc::Code::head_conflict}; };
    auto r = refusal([&] { bed->write(e, refuse); });
    CHECK(r && r->code == rc::Code::head_conflict && bed->scratch.names().empty());
    const auto held = [&] {
        bed->fake.set_busy(Busy{.remove = never});
        throw Refusal{rc::Code::store_full};
    };
    r = refusal([&] { bed->write(e, held); });
    CHECK(r && r->code == rc::Code::store_full && bed->scratch.names() == "0000000001.pending");
    bed->fake.set_busy(Busy{});
    CHECK_EQ(bed->write(e), std::uint64_t{1});
    CHECK_EQ(bed->scratch.names(), std::string("0000000001.jsonl"));
    CHECK(bed->store.ids.size() == 2);
}

// Step 3 faulted, as a kill or the store's io: before the commit the
// pending file is left whole and the next write removes it; after it, left
// whole and the next write renames it kept.
QIVEN3_CASE(write_kept_steps, append_faulted) {
    for (const bool committed : {false, true}) {
        const auto bed = std::make_unique<Bed>();
        const Entries e(bed->arena, writer, 2, 1);
        const auto killed = [&] {
            if (committed) {
                hold(bed->store, e);
            }
            throw Killed{};
        };
        bool thrown = false;
        try {
            bed->write(e, killed);
        } catch (const Killed&) {
            thrown = true;
        }
        CHECK(thrown && bed->scratch.names() == "0000000001.pending");
        CHECK(bed->at("0000000001.pending") == bed->file(1, e));
        const Entries next(bed->arena, writer, 1, 3);
        CHECK_EQ(bed->write(next), committed ? std::uint64_t{2} : std::uint64_t{1});
        CHECK_EQ(bed->scratch.names(),
                 std::string(committed ? "0000000001.jsonl|0000000002.jsonl" : "0000000001.jsonl"));
        CHECK(bed->at("0000000001.jsonl") == bed->file(1, committed ? e : next));
    }
}

// Step 4's rename failing after the commit: busy, or exists [reading]
// with a file under the kept name; the pending file left whole, never
// removed, and the next write renames it kept, refused corrupt while its
// own name is taken.
QIVEN3_CASE(write_kept_steps, done_rename_faults) {
    for (const bool exists : {false, true}) {
        const auto bed = std::make_unique<Bed>();
        const Entries e(bed->arena, writer, 2, 1);
        const auto append = [&] {
            hold(bed->store, e);
            if (exists) {
                bed->plant("0000000001.jsonl", "a file under the kept name");
            } else {
                bed->fake.set_busy(Busy{.rename = never});
            }
        };
        CHECK(fault([&] { bed->write(e, append); }) == (exists ? Code::exists : Code::busy));
        CHECK_EQ(bed->scratch.names(),
                 std::string(exists ? "0000000001.jsonl|0000000001.pending" : "0000000001.pending"));
        CHECK(bed->at("0000000001.pending") == bed->file(1, e));
        bed->fake.set_busy(Busy{});
        const Entries next(bed->arena, writer, 1, 3);
        if (exists) {  // the kept name taken: corrupt, never settled, until it is free
            const auto r = refusal([&] { bed->write(next); });
            CHECK(r && r->code == rc::Code::corrupt && bed->store.appends == 0);
            fs::remove(bed->scratch.kept() / "0000000001.jsonl");
        }
        CHECK_EQ(bed->write(next), std::uint64_t{2});
        CHECK(bed->at("0000000001.jsonl") == bed->file(1, e));
    }
}

// The lock held: busy naming it, nothing kept or appended; released, the
// nearest pass.
QIVEN3_CASE(write_kept_steps, busy) {
    const auto bed = std::make_unique<Bed>();
    const Entries e(bed->arena, writer, 1, 1);
    {
        auto held = bed->fake.lock(bed->scratch.store() + "/kept/kept.lock", kept::deadline);
        const auto r = refusal([&] { bed->write(e); });
        CHECK(r && r->code == rc::Code::busy && r->path.view() == "kept/kept.lock");
        CHECK(bed->store.appends == 0 && bed->scratch.names().empty());
    }
    CHECK_EQ(bed->write(e), std::uint64_t{1});
}

// Past the last name: store_full, nothing kept or appended [reading].
QIVEN3_CASE(write_kept_steps, past_last) {
    const auto bed = std::make_unique<Bed>();
    bed->plant("9999999999.jsonl", "the last");
    const Entries e(bed->arena, writer, 1, 1);
    const auto r = refusal([&] { bed->write(e); });
    CHECK(r && r->code == rc::Code::store_full && bed->store.appends == 0);
    CHECK_EQ(bed->scratch.names(), std::string("9999999999.jsonl"));
}

// The import's step 2 meeting a file under the pending name, which only
// a directory that held something gives: exists, both kept, nothing
// appended, no settle step.
QIVEN3_CASE(write_kept_steps, import_exists) {
    const auto bed = std::make_unique<Bed>();
    bed->plant("0000000001.pending", "a file under the pending name");
    const Entries e(bed->arena, "import", 2, 1);
    CHECK(fault([&] { kept::import(bed->fake, bed->scratch.store(), e.view(), appends(bed->store, e), bed->arena); }) ==
          Code::exists);
    CHECK_EQ(bed->scratch.names(), std::string("0000000001.pending|0000000001.tmp"));
    CHECK(bed->at("0000000001.pending") == "a file under the pending name" && bed->store.appends == 0);
    CHECK(count(bed->fake.calls(), "list") == 0 && count(bed->fake.calls(), "lock") == 0);
}
