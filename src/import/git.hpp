#pragma once

// import/git: the import's git route, product. Each call runs git through
// the platform's run, never another process route, as
//   <git> --no-pager --literal-pathspecs -c core.quotepath=off <call>...
// in the checkout's directory, with GIT_OPTIONAL_LOCKS=0 in its
// environment, a deadline of 60 s and caps of 8 MiB for each stream. A
// path the caller passes is literal, never a pattern. The module names no
// path of either checkout but what its caller passes.
//
// The calls, each over a checkout:
//   clean          status --porcelain: empty, else not_clean naming the
//                  checkout;
//   head           rev-parse HEAD: 40 hex;
//   held           cat-file -e <revision>^{commit}: a revision not held is
//                  not_held, naming the document and the revision;
//   show           show <revision>:<path>: the file's bytes at the revision;
//   last           log -1 on a path from a revision: its newest commit,
//                  with the author's and the committer's times;
//   added_times    log --no-renames --diff-filter=A --name-only -z from a
//                  revision over the caller's paths: each added file with
//                  its commit and times, newest commit first, a commit's
//                  names in git's order; a renamed file is added at its
//                  rename's commit, whatever the user's diff.renames;
//   pickaxe        log -S<text> --reverse on a path from a revision: the
//                  first commit, checked: its file holds the text and its
//                  parent's does not (a parent without the file, or none,
//                  holds nothing).
// search_text gives a quote's text to search by: up to its first line break
// in its document, at least 12 code points, else the document's line the
// quote begins on, whole.
//
// Refused, by code: argument, a revision not 4 to 40 lowercase hex, an
// empty path or text, or one holding a NUL; failed, git's exit code not 0,
// with that code and its error's first line; not_clean; not_held;
// output, git's output malformed, the parser's reason and the record (a
// line, or a commit of the log) from 1; not_found, no commit (last, pickaxe);
// unchecked, a pickaxe's commit that fails its check. The platform's own
// refusals pass unchanged: PlatformError deadline, output (a stream past
// its cap), start.
//
// The parsers are pure functions of git's bytes, each refusing by output.
// Allocations: each call's argv, one vector, and its two streams, on the
// caller's arena as the platform's run grows them; added_times one vector
// more, reserved for the output's NUL count; a pickaxe's check two shows'
// streams. A request past the arena is MemoryError budget. It logs nothing.
//
// thread: one

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "memory/memory.hpp"
#include "platform/platform.hpp"

namespace qiven3::import::git {

// Each call's bounds [proposal].
inline constexpr platform::Seconds deadline{60.0};
inline constexpr std::uint64_t cap = std::uint64_t{8} * 1024 * 1024;

enum class Code : std::uint8_t { argument, failed, not_clean, not_held, output, not_found, unchecked };
std::string_view name(Code code) noexcept;

// The refusal: a plain value, thrown by value; it allocates nothing.
struct GitError {
    Code code;
    memory::Inline<16> call;       // git's command: status, rev-parse, cat-file, show, log
    memory::Inline<192> subject;   // the checkout, or the document or path read
    memory::Inline<40> revision;   // the revision read at, if any
    memory::Inline<128> detail;    // failed: the exit code and git's error; output: the reason
    std::size_t record = 0;        // output: the faulty line or commit, from 1
};

// A commit's id, 40 lowercase hex.
struct Revision {
    std::array<char, 40> hex{};
    [[nodiscard]] std::string_view view() const noexcept { return {hex.data(), hex.size()}; }
    friend bool operator==(const Revision&, const Revision&) = default;
};

// A commit and its times, in seconds since 1970 in UTC, as git records them.
struct Commit {
    Revision id;
    std::int64_t author = 0;
    std::int64_t committer = 0;
};

// A place in a text: its first byte and its size.
struct Place {
    std::uint32_t at = 0;
    std::uint32_t size = 0;
};

// An added file: its path's place in the log's bytes, and the commit.
struct Added {
    Place path;
    Commit commit;
};

// The parsers, each over the bytes git wrote. parse_head: rev-parse's one
// line. parse_commit: log -1's line "<id> <author> <committer>", none when
// the log is empty. parse_first: the first of log's lines of one id each,
// none when it is empty. parse_added: added_times' log, appended to out,
// each path a place in bytes, after one reserve.
[[nodiscard]] Revision parse_head(std::string_view bytes);
[[nodiscard]] std::optional<Commit> parse_commit(std::string_view bytes);
[[nodiscard]] std::optional<Revision> parse_first(std::string_view bytes);
void parse_added(std::string_view bytes, memory::Vector<Added>& out);

// A quote's text to search by, as a view of document: the quote is the
// bytes [at, at + size) of the document.
[[nodiscard]] std::string_view search_text(std::string_view document, std::size_t at, std::size_t size) noexcept;

// The checkout and git's program, as find gave it: views the call reads.
struct RepoView {
    std::string_view git;
    std::string_view checkout;
};

class Adds;
template <platform::Platform P>
[[nodiscard]] Adds added_times(P& p, RepoView repo, std::string_view revision,
                               std::span<const std::string_view> paths, memory::Arena& arena);

// added_times' result: the log's bytes and each added file's place in them.
// Asking past them is a caller's mistake: fail_fast.
class Adds {
public:
    explicit Adds(memory::Arena& arena) noexcept
        : text_(memory::Allocator<char>(arena)), added_(memory::Allocator<Added>(arena)) {}

    [[nodiscard]] std::size_t size() const noexcept { return added_.size(); }
    [[nodiscard]] std::string_view path(std::size_t i) const noexcept {
        return std::string_view(text_).substr(at(i).path.at, at(i).path.size);
    }
    [[nodiscard]] const Commit& commit(std::size_t i) const noexcept { return at(i).commit; }

private:
    template <platform::Platform P>
    friend Adds added_times(P& p, RepoView repo, std::string_view revision, std::span<const std::string_view> paths,
                            memory::Arena& arena);

    [[nodiscard]] const Added& at(std::size_t i) const noexcept;

    memory::String text_;
    memory::Vector<Added> added_;
};

namespace detail {

// argument, naming the call and the revision, unless it is 4 to 40 lowercase hex.
void check_revision(std::string_view call, std::string_view revision);
// argument, naming the call and what the text is, if it is empty or holds a NUL.
void check_text(std::string_view call, std::string_view what, std::string_view text);
// failed, unless ran's code is 0.
void check_ran(const platform::Ran& ran, std::string_view call, std::string_view subject,
               std::string_view revision);
[[noreturn]] void refuse(Code code, std::string_view call, std::string_view subject, std::string_view revision,
                         std::string_view reason = "");
[[nodiscard]] bool holds(std::string_view bytes, std::string_view text) noexcept;

// Runs git with the call's arguments into ran: its argv one vector on the
// arena.
template <platform::Platform P>
void run(P& p, RepoView repo, std::span<const std::string_view> args, memory::Arena& arena, platform::Ran& ran) {
    const std::array<std::string_view, 5> head{repo.git, "--no-pager", "--literal-pathspecs", "-c",
                                               "core.quotepath=off"};
    memory::Vector<std::string_view> argv{memory::Allocator<std::string_view>(arena)};
    argv.reserve(head.size() + args.size());
    argv.insert(argv.end(), head.begin(), head.end());
    argv.insert(argv.end(), args.begin(), args.end());
    const std::array<std::pair<std::string_view, std::string_view>, 1> env{{{"GIT_OPTIONAL_LOCKS", "0"}}};
    platform::Command command;
    command.argv = argv;
    command.cwd = repo.checkout;
    command.env = env;
    command.deadline = deadline;
    command.caps = platform::Caps{cap, cap};
    p.run(command, "", ran);
}

// A text joined of parts, on the arena.
template <std::size_t N>
memory::String join(const std::array<std::string_view, N>& parts, memory::Arena& arena) {
    memory::String out{memory::Allocator<char>(arena)};
    std::size_t size = 0;
    for (const std::string_view part : parts) {
        size += part.size();
    }
    out.reserve(size);
    for (const std::string_view part : parts) {
        out.append(part);
    }
    return out;
}

}  // namespace detail

// git's program by the platform's program: its absolute path, into out.
template <platform::Platform P>
void find(P& p, memory::String& out) {
    p.program("git", out);
}

template <platform::Platform P>
void clean(P& p, RepoView repo, memory::Arena& arena) {
    const std::array<std::string_view, 2> args{"status", "--porcelain"};
    platform::Ran ran(arena);
    detail::run(p, repo, args, arena, ran);
    detail::check_ran(ran, "status", repo.checkout, "");
    if (!ran.out.empty()) {
        detail::refuse(Code::not_clean, "status", repo.checkout, "");
    }
}

template <platform::Platform P>
[[nodiscard]] Revision head(P& p, RepoView repo, memory::Arena& arena) {
    const std::array<std::string_view, 2> args{"rev-parse", "HEAD"};
    platform::Ran ran(arena);
    detail::run(p, repo, args, arena, ran);
    detail::check_ran(ran, "rev-parse", repo.checkout, "");
    return parse_head(ran.out);
}

template <platform::Platform P>
void held(P& p, RepoView repo, std::string_view document, std::string_view revision, memory::Arena& arena) {
    detail::check_revision("cat-file", revision);
    const memory::String object = detail::join(std::array<std::string_view, 2>{revision, "^{commit}"}, arena);
    const std::array<std::string_view, 3> args{"cat-file", "-e", object};
    platform::Ran ran(arena);
    detail::run(p, repo, args, arena, ran);
    if (ran.code != 0) {
        detail::refuse(Code::not_held, "cat-file", document, revision);
    }
}

template <platform::Platform P>
[[nodiscard]] memory::String show(P& p, RepoView repo, std::string_view revision, std::string_view path,
                                  memory::Arena& arena) {
    detail::check_revision("show", revision);
    detail::check_text("show", "path", path);
    const memory::String object = detail::join(std::array<std::string_view, 3>{revision, ":", path}, arena);
    const std::array<std::string_view, 2> args{"show", object};
    platform::Ran ran(arena);
    detail::run(p, repo, args, arena, ran);
    detail::check_ran(ran, "show", path, revision);
    return std::move(ran.out);
}

template <platform::Platform P>
[[nodiscard]] Commit last(P& p, RepoView repo, std::string_view revision, std::string_view path,
                          memory::Arena& arena) {
    detail::check_revision("log", revision);
    detail::check_text("log", "path", path);
    const std::array<std::string_view, 6> args{"log", "-1", "--format=%H %at %ct", revision, "--", path};
    platform::Ran ran(arena);
    detail::run(p, repo, args, arena, ran);
    detail::check_ran(ran, "log", path, revision);
    const std::optional<Commit> commit = parse_commit(ran.out);
    if (!commit) {
        detail::refuse(Code::not_found, "log", path, revision);
    }
    return *commit;
}

template <platform::Platform P>
[[nodiscard]] Adds added_times(P& p, RepoView repo, std::string_view revision,
                               std::span<const std::string_view> paths, memory::Arena& arena) {
    detail::check_revision("log", revision);
    for (const std::string_view path : paths) {
        detail::check_text("log", "path", path);
    }
    const std::array<std::string_view, 8> lead{"log", "--no-renames", "--diff-filter=A", "--name-only", "-z",
                                               "--format=%x00%H %at %ct", revision, "--"};
    memory::Vector<std::string_view> args{memory::Allocator<std::string_view>(arena)};
    args.reserve(lead.size() + paths.size());
    args.insert(args.end(), lead.begin(), lead.end());
    args.insert(args.end(), paths.begin(), paths.end());
    platform::Ran ran(arena);
    detail::run(p, repo, args, arena, ran);
    detail::check_ran(ran, "log", repo.checkout, revision);
    Adds adds(arena);
    adds.text_ = std::move(ran.out);
    parse_added(adds.text_, adds.added_);
    return adds;
}

template <platform::Platform P>
[[nodiscard]] Revision pickaxe(P& p, RepoView repo, std::string_view revision, std::string_view path,
                               std::string_view text, memory::Arena& arena) {
    detail::check_revision("log", revision);
    detail::check_text("log", "path", path);
    detail::check_text("log", "text", text);
    const memory::String search = detail::join(std::array<std::string_view, 2>{"-S", text}, arena);
    const std::array<std::string_view, 7> args{"log", search, "--reverse", "--format=%H", revision, "--", path};
    platform::Ran ran(arena);
    detail::run(p, repo, args, arena, ran);
    detail::check_ran(ran, "log", path, revision);
    const std::optional<Revision> first = parse_first(ran.out);
    if (!first) {
        detail::refuse(Code::not_found, "log", path, revision);
    }
    if (!detail::holds(show(p, repo, first->view(), path, arena), text)) {
        detail::refuse(Code::unchecked, "show", path, first->view());
    }
    const memory::String parent = detail::join(std::array<std::string_view, 3>{first->view(), "^:", path}, arena);
    const std::array<std::string_view, 2> show_parent{"show", parent};
    platform::Ran before(arena);
    detail::run(p, repo, show_parent, arena, before);
    if (before.code == 0 && detail::holds(before.out, text)) {
        detail::refuse(Code::unchecked, "show", path, first->view());
    }
    return *first;
}

}  // namespace qiven3::import::git
