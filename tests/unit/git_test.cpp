// import/git through the fake platform, whose run gives git's output for
// each whole argv: each call's argv, its directory, its environment, its
// deadline and caps; each call read (clean, head, held, show, last,
// added_times, pickaxe), each refusal naming what the text names (not_clean
// the checkout, not_held the document and the revision, failed git's exit
// code and error, argument, not_found, unchecked), the platform's own
// refusals passed unchanged; each parser's malformed input refused by
// output with its reason and record; search_text's three readings; an arena
// too small, MemoryError budget.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "import/git.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace {

namespace fs = std::filesystem;
namespace git = qiven3::import::git;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
using qiven3::platform::Command;
using qiven3::platform::PlatformError;
using qiven3::platform::Ran;
using qiven3::platform::fake::Config;
using qiven3::platform::fake::FakePlatform;
using qiven3::platform::fake::Program;
using namespace std::string_view_literals;

// name under the temporary directory: an absolute path by the fake's rule.
// The temporary directory itself for an empty name: the checkout, which the
// fake's run holds to be a directory.
std::string absolute(std::string_view name) {
    std::string out;
    for (const char8_t c : (name.empty() ? fs::temp_directory_path() : fs::temp_directory_path() / name).u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// The fake, keeping each call's command; a -z call's last LF dropped, since
// the fake ends each line with one and git ends a -z log with a NUL.
class GitFake : public FakePlatform {
public:
    explicit GitFake(const Config& config) : FakePlatform(config) {}

    void run(const Command& command, std::string_view data, Ran& ran) {
        cwd = std::string(command.cwd);
        env.assign(command.env.begin(), command.env.end());
        deadline = command.deadline.count();
        caps = {command.caps.out, command.caps.err};
        FakePlatform::run(command, data, ran);
        if (std::ranges::find(command.argv, "-z"sv) != command.argv.end() && ran.out.ends_with('\n')) {
            ran.out.pop_back();
        }
    }

    std::string cwd;
    std::vector<std::pair<std::string, std::string>> env;
    double deadline = 0;
    std::pair<std::uint64_t, std::uint64_t> caps;
};
static_assert(qiven3::platform::Platform<GitFake>);

// A test's git and checkout, and the programs the fake runs.
struct Bed {
    std::string git = absolute("git.exe");
    std::string checkout = absolute("");
    std::map<std::vector<std::string>, Program> programs;

    // The call's whole argv: git's program, the four options, then args.
    [[nodiscard]] std::vector<std::string> argv(std::vector<std::string> args) const {
        std::vector<std::string> out{git, "--no-pager", "--literal-pathspecs", "-c", "core.quotepath=off"};
        out.insert(out.end(), args.begin(), args.end());
        return out;
    }
    void give(std::vector<std::string> args, std::vector<std::string> lines, int code = 0, std::string err = "") {
        Program program;
        program.lines = std::move(lines);
        program.code = code;
        program.err = std::move(err);
        programs[argv(std::move(args))] = program;
    }
    [[nodiscard]] git::RepoView repo() const { return {git, checkout}; }
};

// The refusal op throws, as "code call subject revision reason record", or
// "ok"; a PlatformError as "platform <operation> <detail>".
template <class Op>
std::string outcome(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const git::GitError& e) {
        return std::string(git::name(e.code)) + " " + std::string(e.call.view()) + " " +
               std::string(e.subject.view()) + " " + std::string(e.revision.view()) + " " +
               std::string(e.detail.view()) + " " + std::to_string(e.record);
    } catch (const PlatformError& e) {
        return "platform " + std::string(e.operation.view()) + " " + std::string(e.detail.view());
    }
    return "ok";
}

// The call op makes on a fake given bed's programs, with an arena of 64 KiB.
template <class Op>
std::string called(const Bed& bed, Op&& op) {
    GitFake fake{Config{}};
    fake.set_programs(bed.programs);
    std::vector<std::byte> pages(64 * 1024);  // on the heap: past the frame bound
    Arena arena(pages);
    return outcome([&] { std::forward<Op>(op)(fake, arena); });
}

const std::string id_a(40, 'a');
const std::string id_b(40, 'b');
const std::string id_c(40, 'c');
const std::string nul(1, '\0');

// A -z log's commit: "\0<id> <author> <committer>\0", "\n" and each name
// ended by a NUL.
std::string logged(const std::string& id, std::string_view times, std::vector<std::string> names) {
    std::string out = nul + id + " " + std::string(times) + nul;
    if (!names.empty()) {
        out += "\n";
    }
    for (const std::string& name : names) {
        out += name + nul;
    }
    return out;
}

}  // namespace

// Each call's argv, run in the checkout with GIT_OPTIONAL_LOCKS=0 alone of
// its own, 60 s and 8 MiB caps; find gives program("git")'s path.
QIVEN3_CASE(git, command) {
    Bed bed;
    bed.give({"rev-parse", "HEAD"}, {id_a});
    Config config;
    config.found["git"] = bed.git;
    GitFake fake{config};
    fake.set_programs(bed.programs);
    std::vector<std::byte> pages(64 * 1024);  // on the heap: past the frame bound
    Arena arena(pages);
    qiven3::memory::String found{qiven3::memory::Allocator<char>(arena)};
    git::find(fake, found);
    CHECK_EQ(std::string(found), bed.git);
    CHECK_EQ(git::head(fake, bed.repo(), arena).view(), std::string_view(id_a));
    CHECK_EQ(fake.cwd, bed.checkout);
    CHECK(fake.env == (std::vector<std::pair<std::string, std::string>>{{"GIT_OPTIONAL_LOCKS", "0"}}));
    CHECK_EQ(fake.deadline, 60.0);
    CHECK_EQ(fake.caps.first, std::uint64_t{8} << 20U);
    CHECK_EQ(fake.caps.second, std::uint64_t{8} << 20U);
    GitFake none{Config{}};
    CHECK_EQ(outcome([&] { git::find(none, found); }), std::string("platform program no git on the fake's PATH"));
}

// clean: empty status; a line, not_clean naming the checkout; git failing,
// failed with its exit code and its error's first line.
QIVEN3_CASE(git, clean) {
    Bed bed;
    bed.give({"status", "--porcelain"}, {});
    const auto clean = [&](GitFake& fake, Arena& arena) { git::clean(fake, bed.repo(), arena); };
    CHECK_EQ(called(bed, clean), std::string("ok"));
    bed.give({"status", "--porcelain"}, {"?? new.md"});
    CHECK_EQ(called(bed, clean), "not_clean status " + bed.checkout + "   0");
    bed.give({"status", "--porcelain"}, {}, 128, "fatal: not a git repository\r\nmore\n");
    CHECK_EQ(called(bed, clean), "failed status " + bed.checkout + "  exit 128: fatal: not a git repository 0");
}

// head: the id; malformed, output; failed.
QIVEN3_CASE(git, head) {
    Bed bed;
    const auto head = [&](GitFake& fake, Arena& arena) {
        if (git::head(fake, bed.repo(), arena).view() != id_b) {
            throw PlatformError(qiven3::platform::Code::io, "test", "a wrong id");
        }
    };
    bed.give({"rev-parse", "HEAD"}, {id_b});
    CHECK_EQ(called(bed, head), std::string("ok"));
    bed.give({"rev-parse", "HEAD"}, {"HEAD"});
    CHECK_EQ(called(bed, head), std::string("output rev-parse   not one line of an id 1"));
    bed.give({"rev-parse", "HEAD"}, {"HEAD"}, 128, "fatal: ambiguous argument 'HEAD'");
    CHECK_EQ(called(bed, head),
             "failed rev-parse " + bed.checkout + "  exit 128: fatal: ambiguous argument 'HEAD' 0");
}

// held: exit 0 held; another code not_held naming the document and the
// revision; a revision not 4 to 40 lowercase hex, argument, before any run.
QIVEN3_CASE(git, held) {
    Bed bed;
    bed.give({"cat-file", "-e", id_a + "^{commit}"}, {});
    bed.give({"cat-file", "-e", "abcd^{commit}"}, {}, 1);
    const auto held = [&](std::string revision) {
        return called(bed, [&](GitFake& fake, Arena& arena) { git::held(fake, bed.repo(), "doc.md", revision, arena); });
    };
    CHECK_EQ(held(id_a), std::string("ok"));
    CHECK_EQ(held("abcd"), std::string("not_held cat-file doc.md abcd  0"));
    const std::string reason = " a revision not 4 to 40 lowercase hex 0";
    for (const std::string& bad : {std::string("HEAD"), std::string("abc"), id_a + "a", std::string(40, 'A'),
                                   std::string("-abcd"), std::string("")}) {
        CHECK_EQ(held(bad), "argument cat-file  " + bad.substr(0, 40) + reason);
    }
}

// show: the file's bytes as given; failed naming the path and the revision;
// an empty path or one holding a NUL, argument.
QIVEN3_CASE(git, show) {
    Bed bed;
    bed.give({"show", id_a + ":docs/a b.md"}, {"one", "caf\xc3\xa9 \xff"});
    bed.give({"show", id_b + ":docs/a b.md"}, {}, 128, "fatal: path 'docs/a b.md' does not exist in 'b'");
    std::string shown;
    const auto show = [&](const std::string& revision, std::string_view path) {
        return called(bed, [&](GitFake& fake, Arena& arena) {
            shown = std::string(git::show(fake, bed.repo(), revision, path, arena));
        });
    };
    CHECK_EQ(show(id_a, "docs/a b.md"), std::string("ok"));
    CHECK_EQ(shown, std::string("one\ncaf\xc3\xa9 \xff\n"));
    CHECK_EQ(show(id_b, "docs/a b.md"),
             "failed show docs/a b.md " + id_b + " exit 128: fatal: path 'docs/a b.md' does not exist in 'b' 0");
    CHECK_EQ(show(id_a, ""), std::string("argument show path  empty or holding a NUL 0"));
    CHECK_EQ(show(id_a, "a\0b"sv), std::string("argument show path  empty or holding a NUL 0"));
}

// last: the newest commit's id and times; none, not_found; malformed, output.
QIVEN3_CASE(git, last) {
    Bed bed;
    const std::vector<std::string> call{"log", "-1", "--format=%H %at %ct", id_c, "--", "m.yaml"};
    git::Commit commit;
    const auto last = [&] {
        return called(bed, [&](GitFake& fake, Arena& arena) { commit = git::last(fake, bed.repo(), id_c, "m.yaml", arena); });
    };
    bed.give(call, {id_a + " 981173106 981173107"});
    CHECK_EQ(last(), std::string("ok"));
    CHECK_EQ(commit.id.view(), std::string_view(id_a));
    CHECK_EQ(commit.author, std::int64_t{981173106});
    CHECK_EQ(commit.committer, std::int64_t{981173107});
    bed.give(call, {});
    CHECK_EQ(last(), "not_found log m.yaml " + id_c + "  0");
    bed.give(call, {id_a + " 1"});
    CHECK_EQ(last(), std::string("output log   a commit's times not two decimal seconds 1"));
}

// added_times: each added file with its commit, in the log's order, the
// caller's paths after "--"; failed naming the checkout and the revision.
QIVEN3_CASE(git, added_times) {
    Bed bed;
    const std::vector<std::string> call{"log", "--no-renames", "--diff-filter=A", "--name-only", "-z",
                                        "--format=%x00%H %at %ct", id_c, "--", "records/", "seed"};
    bed.give(call, {logged(id_a, "200 201", {"records/one.md", "records/caf\xc3\xa9 two.md"}) +
                    logged(id_b, "100 0", {"seed"})});
    std::vector<std::string> seen;
    const auto added = [&] {
        return called(bed, [&](GitFake& fake, Arena& arena) {
            const std::array<std::string_view, 2> paths{"records/", "seed"};
            const git::Adds adds = git::added_times(fake, bed.repo(), id_c, paths, arena);
            for (std::size_t i = 0; i < adds.size(); ++i) {
                const git::Commit& c = adds.commit(i);
                seen.push_back(std::string(adds.path(i)) + "|" + std::string(c.id.view().substr(0, 1)) + "|" +
                               std::to_string(c.author) + "|" + std::to_string(c.committer));
            }
        });
    };
    CHECK_EQ(added(), std::string("ok"));
    CHECK(seen == (std::vector<std::string>{"records/one.md|a|200|201", "records/caf\xc3\xa9 two.md|a|200|201",
                                            "seed|b|100|0"}));
    bed.give(call, {}, 128, "fatal: bad revision");
    CHECK_EQ(added(), "failed log " + bed.checkout + " " + id_c + " exit 128: fatal: bad revision 0");
}

// pickaxe: the first commit, its file holding the text and its parent's
// not, or no parent; none, not_found; a commit failing its check, unchecked;
// an empty text, argument.
QIVEN3_CASE(git, pickaxe) {
    Bed bed;
    const std::vector<std::string> call{"log", "-Sthe words", "--reverse", "--format=%H", id_c, "--", "r.md"};
    bed.give(call, {id_a, id_b});
    bed.give({"show", id_a + ":r.md"}, {"# 1", "said the words here"});
    bed.give({"show", id_a + "^:r.md"}, {"# 1"});
    std::string first;
    const auto pickaxe = [&](std::string_view text) {
        return called(bed, [&](GitFake& fake, Arena& arena) {
            first = std::string(git::pickaxe(fake, bed.repo(), id_c, "r.md", text, arena).view());
        });
    };
    CHECK_EQ(pickaxe("the words"), std::string("ok"));
    CHECK_EQ(first, id_a);
    bed.give({"show", id_a + "^:r.md"}, {}, 128, "fatal: invalid object name");
    CHECK_EQ(pickaxe("the words"), std::string("ok"));
    bed.give({"show", id_a + "^:r.md"}, {"the words before"});
    CHECK_EQ(pickaxe("the words"), "unchecked show r.md " + id_a + "  0");
    bed.give({"show", id_a + ":r.md"}, {"other words"});
    CHECK_EQ(pickaxe("the words"), "unchecked show r.md " + id_a + "  0");
    bed.give(call, {});
    CHECK_EQ(pickaxe("the words"), "not_found log r.md " + id_c + "  0");
    CHECK_EQ(pickaxe(""), std::string("argument log text  empty or holding a NUL 0"));
}

// The platform's refusals pass unchanged: a hang's deadline, a stream past
// its cap.
QIVEN3_CASE(git, platform_refusals) {
    Bed bed;
    Program hang;
    hang.hang = true;
    bed.programs[bed.argv({"status", "--porcelain"})] = hang;
    Program flood;
    flood.flood = "out";
    bed.programs[bed.argv({"rev-parse", "HEAD"})] = flood;
    CHECK_EQ(called(bed, [&](GitFake& fake, Arena& arena) { git::clean(fake, bed.repo(), arena); }),
             std::string("platform run the fake's program hangs"));
    CHECK_EQ(called(bed, [&](GitFake& fake, Arena& arena) { static_cast<void>(git::head(fake, bed.repo(), arena)); }),
             std::string("platform run out"));
}

namespace {

// parse_head's, parse_commit's and parse_first's reading of bytes: the id
// and times, "none", or the refusal.
std::string head_of(const std::string& bytes) {
    std::string read;
    const std::string refused = outcome([&] { read = std::string(git::parse_head(bytes).view()); });
    return refused == "ok" ? read : refused;
}

std::string commit_of(const std::string& bytes) {
    std::string read = "none";
    const std::string refused = outcome([&] {
        if (const auto commit = git::parse_commit(bytes)) {
            read = std::string(commit->id.view().substr(0, 1)) + " " + std::to_string(commit->author) + " " +
                   std::to_string(commit->committer);
        }
    });
    return refused == "ok" ? read : refused;
}

std::string first_of(const std::string& bytes) {
    std::string read = "none";
    const std::string refused = outcome([&] {
        if (const auto first = git::parse_first(bytes)) {
            read = std::string(first->view());
        }
    });
    return refused == "ok" ? read : refused;
}

// parse_added's reading: each path and its commit's first hex digit, or the
// refusal.
std::string added_of(const std::string& bytes) {
    std::vector<std::byte> pages(16 * 1024);  // on the heap: past the frame bound
    Arena arena(pages);
    qiven3::memory::Vector<git::Added> added{qiven3::memory::Allocator<git::Added>(arena)};
    std::string read;
    const std::string refused = outcome([&] {
        git::parse_added(bytes, added);
        for (const git::Added& a : added) {
            read += bytes.substr(a.path.at, a.path.size) + "|" + a.commit.id.view()[0] + ";";
        }
    });
    return refused == "ok" ? read : refused;
}

const std::string refused_line = "output rev-parse   not one line of an id 1";
const std::string not_hex = "an id not 40 lowercase hex";

}  // namespace

// Each parser: its form read, and each malformed input refused by output
// with its reason and record.
QIVEN3_CASE(git, parse_head) {
    CHECK_EQ(head_of(id_a + "\n"), id_a);
    for (const std::string& bad : {std::string(""), id_a, id_a + "\nx", id_a + "\n\n", std::string("HEAD\n")}) {
        CHECK_EQ(head_of(bad), refused_line);
    }
    CHECK_EQ(head_of(std::string(40, 'A') + "\n"), "output rev-parse   " + not_hex + " 1");
    CHECK_EQ(head_of(std::string(39, 'a') + "g\n"), "output rev-parse   " + not_hex + " 1");
}

QIVEN3_CASE(git, parse_commit) {
    const std::string times = "output log   a commit's times not two decimal seconds 1";
    CHECK_EQ(commit_of(""), std::string("none"));
    CHECK_EQ(commit_of(id_a + " 0 9223372036854775807\n"), std::string("a 0 9223372036854775807"));
    CHECK_EQ(commit_of(id_a + " 1 2"), std::string("output log   not one line 1"));
    CHECK_EQ(commit_of(id_a + " 1 2\n" + id_b + " 1 2\n"), std::string("output log   not one line 1"));
    CHECK_EQ(commit_of(id_a + "\n"), std::string("output log   a commit line not an id and a space 1"));
    CHECK_EQ(commit_of(id_a + "x1 2\n"), std::string("output log   a commit line not an id and a space 1"));
    CHECK_EQ(commit_of(std::string(40, 'g') + " 1 2\n"), "output log   " + not_hex + " 1");
    for (const std::string_view bad : {"01 2"sv, "-1 2"sv, "1 2 3"sv, "1"sv, "1 "sv, " 1 2"sv, "1 9223372036854775808"sv,
                                       "1 12345678901234567890"sv, "1 +2"sv}) {
        CHECK_EQ(commit_of(id_a + " " + std::string(bad) + "\n"), times);
    }
}

QIVEN3_CASE(git, parse_first) {
    CHECK_EQ(first_of(""), std::string("none"));
    CHECK_EQ(first_of(id_a + "\n" + id_b + "\n"), id_a);
    CHECK_EQ(first_of(id_a + "\nx\n"), "output log   " + not_hex + " 2");
    CHECK_EQ(first_of(id_a + "\n" + id_b), std::string("output log   a line not ended 2"));
    CHECK_EQ(first_of("\n"), "output log   " + not_hex + " 1");
}

QIVEN3_CASE(git, parse_added) {
    const std::string a = nul + id_a + " 1 2" + nul;
    const std::string b = nul + id_b + " 3 4" + nul;
    CHECK_EQ(added_of(""), std::string(""));
    CHECK_EQ(added_of(a + "\nx" + nul + "y z" + nul + b + "\nw" + nul), std::string("x|a;y z|a;w|b;"));
    CHECK_EQ(added_of(a + b + "\nw" + nul), std::string("w|b;"));  // a commit naming no file
    CHECK_EQ(added_of(a), std::string(""));
    const std::string log = "output log   ";
    CHECK_EQ(added_of("x"), log + "a commit not opened by a NUL 1");
    // A name may begin with a line break, so a stray one after the names is
    // a name never closed.
    CHECK_EQ(added_of(a + "\nx" + nul + "\n"), log + "a name not closed by a NUL 1");
    CHECK_EQ(added_of(nul + id_a + " 1 2"), log + "a commit line not closed by a NUL 1");
    CHECK_EQ(added_of(a + "x"), log + "a commit line not followed by a line break 1");
    CHECK_EQ(added_of(a + "\nx"), log + "a name not closed by a NUL 1");
    CHECK_EQ(added_of(a + "\n" + nul), log + "an empty name 1");
    CHECK_EQ(added_of(a + "\nx\xff" + nul), log + "a name not UTF-8 1");
    CHECK_EQ(added_of(a + "\nx" + nul + nul + id_b + " 3" + nul), log + "a commit's times not two decimal seconds 2");
    CHECK_EQ(added_of(nul + id_a.substr(1) + "G 1 2" + nul), log + not_hex + " 1");
}

// search_text: a quote on one line whole; one spanning lines, its text up to
// its line break when that holds 12 code points, else its first line in its
// document whole.
QIVEN3_CASE(git, search_text) {
    const auto search = [](std::string_view document, std::string_view quote) {
        return std::string(git::search_text(document, document.find(quote), quote.size()));
    };
    CHECK_EQ(search("abc the words here xyz", "the words"), std::string("the words"));
    CHECK_EQ(search("x: we keep every record\nand more\n", "we keep every record\nand"),
             std::string("we keep every record"));
    CHECK_EQ(search("x: the owner said: go\nnow and fast\n", "go\nnow"), std::string("x: the owner said: go"));
    CHECK_EQ(search("one\ngo\nnow\n", "go\nnow"), std::string("go"));
    CHECK_EQ(search("go\nnow", "go\nnow"), std::string("go"));
    std::string eleven;
    for (int i = 0; i < 11; ++i) {
        eleven += "\xc3\xa9";
    }
    CHECK_EQ(search("> " + eleven + "\nx\n", eleven + "\nx"), "> " + eleven);
    CHECK_EQ(search("> " + eleven + "\xc3\xa9\nx\n", eleven + "\xc3\xa9\nx"), eleven + "\xc3\xa9");
}

// An arena too small for the call's argv and output: MemoryError budget.
QIVEN3_CASE(git, budget) {
    Bed bed;
    bed.give({"show", id_a + ":r.md"}, {std::string(4096, 'x')});
    GitFake fake{Config{}};
    fake.set_programs(bed.programs);
    std::vector<std::byte> pages(1024);
    Arena arena(pages);
    bool budget = false;
    try {
        static_cast<void>(git::show(fake, bed.repo(), id_a, "r.md", arena));
    } catch (const MemoryError&) {
        budget = true;
    }
    CHECK(budget);
}
