// import/git on this machine's git, through the Windows adapter's run: each
// case builds a synthetic repository in a scratch directory of its own,
// three commits with set author and committer times, by git through run,
// then checks each call against it: clean on a clean and a dirty checkout;
// head; held on revisions held, whole and abbreviated, and on one not held;
// show at two revisions and of a path the revision lacks; last on a file, a
// directory and a path with no commit; added_times over two directories at
// two revisions, a file added in a later commit and a name past ASCII read
// back as its bytes; pickaxe on a text added in a later commit, in the root
// commit, a quote spanning lines searched by search_text's text, a text
// never added and one added after the revision. Each case prints its
// seconds and its arena's peak. machine: they start git, so the full and
// asan presets run them; off Windows, where no adapter has run, each skips.

#if defined(_WIN32)

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
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
#include "support/fixtures/win_process_case.hpp"

namespace {

namespace git = qiven3::import::git;
using namespace qiven3::test::process;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;

// The synthetic history: each commit's author and committer times, and
// the document's text after each.
constexpr std::array<std::pair<std::int64_t, std::int64_t>, 3> times{
    {{1500000000, 1500000100}, {1500086400, 1500090000}, {1500172800, 1500176400}}};
constexpr std::string_view rule = "docs/rule.md";
constexpr std::string_view first_text = "# Rule\n\nFirst line of the rule.\n";
constexpr std::string_view quote = "The quoted sentence that spans\nlines in its document.";
constexpr std::string_view marker = "A marker added in the second commit.";
constexpr std::string_view closing = "Closing words of the third commit.";
// A name past ASCII, as its UTF-8 bytes, with a space.
constexpr std::string_view accented = "seeds/caf\xC3\xA9 one.yaml";

std::string second_text() { return std::string(first_text) + std::string(quote) + "\n" + std::string(marker) + "\n"; }
std::string third_text() { return second_text() + std::string(closing) + "\n"; }

// A refusal of import/git caught whole; thrown false when op returned.
struct Refused {
    bool thrown = false;
    git::Code code = git::Code::argument;
    std::string call, subject, revision, detail;
};

template <class Op>
Refused refused(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const git::GitError& e) {
        return {true, e.code, std::string(e.call.view()), std::string(e.subject.view()),
                std::string(e.revision.view()), std::string(e.detail.view())};
    }
    return {};
}

// The case's repository: a scratch directory, the adapter, an arena, git's
// program as find gives it, and each commit's id as rev-parse printed it.
class Repo {
public:
    explicit Repo(qiven3::test::Context& ctx) : ctx_(ctx) {
        {
            qiven3::memory::String out{Allocator<char>(arena)};
            git::find(platform, out);
            program_.assign(out.begin(), out.end());
        }
        root_ = text(scratch_.root);
        build("init", {"init", "-q"}, 0);
        put("continuity/a.yaml", "id: a\n");
        put("seeds/b.yaml", "id: b\n");
        put(rule, first_text);
        commit(0);
        put("continuity/c.yaml", "id: c\n");
        put(accented, "id: e\n");
        put(rule, second_text());
        commit(1);
        put("seeds/d.yaml", "id: d\n");
        put(rule, third_text());
        commit(2);
    }

    [[nodiscard]] git::RepoView view() const noexcept { return {program_, root_}; }
    [[nodiscard]] const std::string& root() const noexcept { return root_; }
    [[nodiscard]] const std::string& id(std::size_t i) const noexcept { return ids_.at(i); }

    // bytes into the checkout's file at path, its directories made.
    void put(std::string_view path, std::string_view bytes) const {
        const fs::path file = scratch_.root / fs::path(std::u8string(path.begin(), path.end()));
        fs::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    void remove(std::string_view path) const {
        fs::remove(scratch_.root / fs::path(std::u8string(path.begin(), path.end())));
    }

    std::unique_ptr<Pages> pages = std::make_unique<Pages>();
    Arena arena{pages->bytes};
    WindowsPlatform platform{std::nullopt};

private:
    void commit(std::size_t i) {
        build("add", {"add", "-A"}, i);
        build("commit", {"commit", "-q", "-m", "A synthetic commit"}, i);
        std::string id = build("rev-parse", {"rev-parse", "HEAD"}, i);
        while (!id.empty() && (id.back() == '\n' || id.back() == '\r')) {
            id.pop_back();
        }
        ids_.at(i) = id;
    }

    // git with args, no system configuration, a synthetic identity and
    // commit i's times; its output, its exit code checked.
    std::string build(std::string_view what, std::initializer_list<std::string_view> args, std::size_t i) {
        const std::string author = std::to_string(times.at(i).first) + " +0000";
        const std::string committer = std::to_string(times.at(i).second) + " +0000";
        std::vector<std::string_view> argv{program_,
                                           "-c", "user.name=Synthetic Author",
                                           "-c", "user.email=author@example.invalid",
                                           "-c", "init.defaultBranch=main",
                                           "-c", "core.autocrlf=false",
                                           "-c", "commit.gpgsign=false"};
        argv.insert(argv.end(), args.begin(), args.end());
        const std::array<std::pair<std::string_view, std::string_view>, 3> env{
            {{"GIT_AUTHOR_DATE", author}, {"GIT_COMMITTER_DATE", committer}, {"GIT_CONFIG_NOSYSTEM", "1"}}};
        Command command;
        command.argv = argv;
        command.cwd = root_;
        command.env = env;
        command.deadline = git::deadline;
        command.caps = Caps{git::cap, git::cap};
        qiven3::platform::Ran ran(arena);
        platform.run(command, "", ran);
        if (ran.code != 0) {
            std::cout << "git_machine: " << what << " exited " << ran.code << ": " << ran.err << "\n";
        }
        ::qiven3::test::check_eq(ctx_, ran.code, 0, "the build's git call exits 0", __FILE__, __LINE__);
        return {ran.out.begin(), ran.out.end()};
    }

    qiven3::test::Context& ctx_;
    Scratch scratch_{"git-machine"};
    std::string program_;
    std::string root_;
    std::array<std::string, 3> ids_;
};

void done(std::string_view name, Clock::time_point start, const Arena& arena) {
    std::cout << "git_machine." << name << ": " << since(start) << " s, peak " << arena.peak() << " B\n";
}

}  // namespace

QIVEN3_CASE(git_machine, clean) {
    const Clock::time_point start = Clock::now();
    Repo repo(qiven3_ctx);
    CHECK(!refused([&] { git::clean(repo.platform, repo.view(), repo.arena); }).thrown);
    repo.put("notes.txt", "untracked\n");
    const Refused untracked = refused([&] { git::clean(repo.platform, repo.view(), repo.arena); });
    CHECK(untracked.thrown);
    CHECK(untracked.code == git::Code::not_clean);
    CHECK_EQ(untracked.subject, repo.root());
    repo.remove("notes.txt");
    repo.put(rule, first_text);
    const Refused changed = refused([&] { git::clean(repo.platform, repo.view(), repo.arena); });
    CHECK(changed.thrown);
    CHECK(changed.code == git::Code::not_clean);
    repo.put(rule, third_text());
    CHECK(!refused([&] { git::clean(repo.platform, repo.view(), repo.arena); }).thrown);
    done("clean", start, repo.arena);
}

QIVEN3_CASE(git_machine, head_and_held) {
    const Clock::time_point start = Clock::now();
    Repo repo(qiven3_ctx);
    CHECK_EQ(std::string(git::head(repo.platform, repo.view(), repo.arena).view()), repo.id(2));
    for (const std::string& revision : {repo.id(0), repo.id(2), repo.id(1).substr(0, 7)}) {
        CHECK(!refused([&] { git::held(repo.platform, repo.view(), rule, revision, repo.arena); }).thrown);
    }
    const std::string absent = "0123456789abcdef0123456789abcdef01234567";
    const Refused r = refused([&] { git::held(repo.platform, repo.view(), rule, absent, repo.arena); });
    CHECK(r.thrown);
    CHECK(r.code == git::Code::not_held);
    CHECK_EQ(r.subject, std::string(rule));
    CHECK_EQ(r.revision, absent);
    done("head_and_held", start, repo.arena);
}

QIVEN3_CASE(git_machine, show) {
    const Clock::time_point start = Clock::now();
    Repo repo(qiven3_ctx);
    {
        const qiven3::memory::String first = git::show(repo.platform, repo.view(), repo.id(0), rule, repo.arena);
        CHECK_EQ(std::string(first.begin(), first.end()), std::string(first_text));
        const qiven3::memory::String third = git::show(repo.platform, repo.view(), repo.id(2), rule, repo.arena);
        CHECK_EQ(std::string(third.begin(), third.end()), third_text());
    }
    const Refused r = refused([&] {
        static_cast<void>(git::show(repo.platform, repo.view(), repo.id(0), "continuity/c.yaml", repo.arena));
    });
    CHECK(r.thrown);
    CHECK(r.code == git::Code::failed);
    CHECK_EQ(r.subject, std::string("continuity/c.yaml"));
    CHECK_EQ(r.revision, repo.id(0));
    CHECK(!r.detail.empty());
    done("show", start, repo.arena);
}

QIVEN3_CASE(git_machine, last) {
    const Clock::time_point start = Clock::now();
    Repo repo(qiven3_ctx);
    const auto check = [&](std::string_view revision, std::string_view path, std::size_t want) {
        const git::Commit c = git::last(repo.platform, repo.view(), revision, path, repo.arena);
        CHECK_EQ(std::string(c.id.view()), repo.id(want));
        CHECK_EQ(c.author, times.at(want).first);
        CHECK_EQ(c.committer, times.at(want).second);
    };
    check(repo.id(2), rule, 2);
    check(repo.id(1), rule, 1);
    check(repo.id(2), "continuity/c.yaml", 1);
    check(repo.id(2), "continuity", 1);
    check(repo.id(2), "seeds/b.yaml", 0);
    const Refused r = refused([&] {
        static_cast<void>(git::last(repo.platform, repo.view(), repo.id(1), "seeds/d.yaml", repo.arena));
    });
    CHECK(r.thrown);
    CHECK(r.code == git::Code::not_found);
    CHECK_EQ(r.subject, std::string("seeds/d.yaml"));
    done("last", start, repo.arena);
}

QIVEN3_CASE(git_machine, added_times) {
    const Clock::time_point start = Clock::now();
    Repo repo(qiven3_ctx);
    const std::array<std::string_view, 2> paths{"continuity", "seeds"};
    {
        // Newest commit first, a commit's names in git's order.
        const std::array<std::pair<std::string_view, std::size_t>, 5> want{{{"seeds/d.yaml", 2},
                                                                            {"continuity/c.yaml", 1},
                                                                            {accented, 1},
                                                                            {"continuity/a.yaml", 0},
                                                                            {"seeds/b.yaml", 0}}};
        const git::Adds adds = git::added_times(repo.platform, repo.view(), repo.id(2), paths, repo.arena);
        CHECK_EQ(adds.size(), want.size());
        for (std::size_t i = 0; i < std::min(adds.size(), want.size()); ++i) {
            CHECK_EQ(std::string(adds.path(i)), std::string(want.at(i).first));
            CHECK_EQ(std::string(adds.commit(i).id.view()), repo.id(want.at(i).second));
            CHECK_EQ(adds.commit(i).author, times.at(want.at(i).second).first);
            CHECK_EQ(adds.commit(i).committer, times.at(want.at(i).second).second);
        }
    }
    {
        const git::Adds adds = git::added_times(repo.platform, repo.view(), repo.id(0), paths, repo.arena);
        CHECK_EQ(adds.size(), 2U);
        for (std::size_t i = 0; i < adds.size(); ++i) {
            CHECK_EQ(std::string(adds.commit(i).id.view()), repo.id(0));
        }
    }
    {
        const std::array<std::string_view, 1> none{"absent"};
        const git::Adds adds = git::added_times(repo.platform, repo.view(), repo.id(2), none, repo.arena);
        CHECK_EQ(adds.size(), 0U);
    }
    done("added_times", start, repo.arena);
}

QIVEN3_CASE(git_machine, pickaxe) {
    const Clock::time_point start = Clock::now();
    Repo repo(qiven3_ctx);
    const auto first = [&](std::string_view revision, std::string_view text) {
        return std::string(git::pickaxe(repo.platform, repo.view(), revision, rule, text, repo.arena).view());
    };
    CHECK_EQ(first(repo.id(2), marker), repo.id(1));
    CHECK_EQ(first(repo.id(2), closing), repo.id(2));
    // In the root commit: its parent is none, which holds nothing.
    CHECK_EQ(first(repo.id(2), "First line of the rule."), repo.id(0));
    {
        // A quote spanning lines, searched by its text up to its first line break.
        const qiven3::memory::String document = git::show(repo.platform, repo.view(), repo.id(2), rule, repo.arena);
        const std::string_view bytes(document.data(), document.size());
        const std::size_t at = bytes.find(quote);
        CHECK(at != std::string_view::npos);
        const std::string_view search = git::search_text(bytes, at, quote.size());
        CHECK_EQ(std::string(search), std::string("The quoted sentence that spans"));
        CHECK_EQ(first(repo.id(2), search), repo.id(1));
    }
    for (const auto& [revision, text] :
         {std::pair<std::string, std::string_view>{repo.id(2), "A text never written"}, {repo.id(0), marker}}) {
        const Refused r = refused([&] { static_cast<void>(first(revision, text)); });
        CHECK(r.thrown);
        CHECK(r.code == git::Code::not_found);
        CHECK_EQ(r.subject, std::string(rule));
        CHECK_EQ(r.revision, revision);
    }
    done("pickaxe", start, repo.arena);
}

#else

#include <string_view>

#include "runner/runner.hpp"

namespace {
constexpr std::string_view off = "not applicable: only the Windows adapter has run; elsewhere current is unsupported";
}  // namespace

QIVEN3_CASE(git_machine, clean) { QIVEN3_SKIP(off); }
QIVEN3_CASE(git_machine, head_and_held) { QIVEN3_SKIP(off); }
QIVEN3_CASE(git_machine, show) { QIVEN3_SKIP(off); }
QIVEN3_CASE(git_machine, last) { QIVEN3_SKIP(off); }
QIVEN3_CASE(git_machine, added_times) { QIVEN3_SKIP(off); }
QIVEN3_CASE(git_machine, pickaxe) { QIVEN3_SKIP(off); }

#endif
