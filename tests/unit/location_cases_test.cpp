// location's cases on the fake, each over a scratch directory of its own:
// the unit halves of P3 to P8 and P16, the commands run by main on the
// fake as the binary runs them. record: init's record, one canonical line
// of at and dir, read back by store_dir and show; records refused, each
// StoreError location config at show, change and store_dir, unchanged.
// not_chosen: no record at store_dir, show and change. guard: a directory
// under .git (a directory, then a file), under a synced folder, and through
// a link into each, at init and at change: StoreError location checkout or
// synced, nothing recorded, nothing made. warnings: each condition on, then
// unknown, and BitLocker locked, without --accept (exit 4, nothing made)
// and with it. init: chosen; a store there opened, not created; created;
// the configuration directory made; --config's parent missing (io) or a
// file (exists); a refused store, a relative path, a directory the record
// cannot hold. change: change's rules in order. output: ASCII output, the
// record whole. lock: the lock held past its deadline, busy.

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/location.hpp"
#include "qiven3/location_impl.hpp"
#include "qiven/runner/runner.hpp"
#include "store/errors.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace {

namespace fs = std::filesystem;
namespace location = qiven3::location;
using Fake = qiven3::platform::fake::FakePlatform;
using qiven3::platform::State;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

fs::path from(std::string_view utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

// A file's bytes; "absent" where there is none.
std::string bytes_of(const fs::path& path) {
    std::error_code error;
    if (!fs::exists(path, error)) {
        return "absent";
    }
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void put(const fs::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// A JSON string's body: the escapes of the canonical form, UTF-8 as it is.
std::string escaped(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
            out.push_back(c);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// The time the fake gives: 2026-10-24T08:09:10.123456Z.
constexpr auto clock_at = std::chrono::sys_days{std::chrono::year{2026} / 10 / 24} + std::chrono::hours{8} +
                          std::chrono::minutes{9} + std::chrono::seconds{10} + std::chrono::microseconds{123456};
constexpr std::string_view at_text = "2026-10-24T08:09:10.123Z";

std::string record_of(std::string_view at, std::string_view dir) {
    return "{\"at\":\"" + std::string(at) + "\",\"dir\":\"" + escaped(dir) + "\"}\n";
}

// A scratch directory of the case's own, removed at its end, and its fake,
// made again by remake after a change to its configuration.
struct Bed {
    Bed() : root(fresh()), config_dir(root / "config"), record(config_dir / "location.json") {
        c.root = text(root);
        c.config_dir = text(config_dir);
        c.seed = 5;
        c.clock = std::chrono::time_point_cast<std::chrono::microseconds>(clock_at);
        c.default_facts.volume = "V:";
        for (auto* r : {&c.default_facts.protection, &c.default_facts.file_history, &c.default_facts.bitlocker}) {
            r->state = State::off;
        }
        remake();
    }
    Bed(const Bed&) = delete;
    Bed& operator=(const Bed&) = delete;
    Bed(Bed&&) = delete;
    Bed& operator=(Bed&&) = delete;
    ~Bed() {
        fake.reset();
        std::error_code error;
        fs::remove_all(root, error);
    }
    static fs::path fresh() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        fs::path out = fs::temp_directory_path() / ("qiven3-location-c-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    void remake() {
        fake.reset();
        fake = std::make_unique<Fake>(c);
        mark = 0;
    }
    // main on args; its output since the last run kept in out.
    int run(std::initializer_list<std::string_view> args) {
        const std::size_t calls = fake->calls().size();
        const int code = location::main(std::span<const std::string_view>(args.begin(), args.size()), *fake);
        out = fake->written().substr(mark);
        mark = fake->written().size();
        ran = std::vector<std::string>(fake->calls().begin() + static_cast<std::ptrdiff_t>(calls), fake->calls().end());
        return code;
    }
    [[nodiscard]] bool said(std::string_view part) const { return out.find(part) != std::string::npos; }
    [[nodiscard]] bool called(std::string_view op) const {
        for (const std::string& call : ran) {
            if (call == op) {
                return true;
            }
        }
        return false;
    }
    // store_dir's dir, or what it raised: the StoreError's code and detail.
    [[nodiscard]] std::string store_dir() const {
        const auto pages = fake->pages(64 * 1024);
        qiven3::memory::Arena arena(pages.bytes());
        qiven3::memory::String got{qiven3::memory::Allocator<char>(arena)};
        try {
            location::store_dir(*fake, got);
        } catch (const qiven3::store::StoreError& e) {
            return "StoreError " + std::string(e.operation()) + " " + std::string(e.detail());
        }
        return std::string(got);
    }
    // The line main gives for a StoreError location.
    [[nodiscard]] static std::string refusal(std::string_view op, const fs::path& at, std::string_view detail) {
        return "StoreError location: " + std::string(op) + " " + text(at) + "; " + std::string(detail) + "\n";
    }
    // A store made at dir by the store itself, closed.
    void make_store(const fs::path& dir) {
        const qiven3::store::Engine<Fake> engine(*fake, 1);
        auto s = qiven3::store::SQLiteStore<Fake>::open(text(dir / "store.db"), true, false, *fake);
        static_cast<void>(s.close());
    }

    fs::path root;
    fs::path config_dir;
    fs::path record;
    qiven3::platform::fake::Config c;
    std::unique_ptr<Fake> fake;
    std::size_t mark = 0;
    std::string out;
    std::vector<std::string> ran;
};

}  // namespace

QIVEN_CASE(location, record) {
    const auto b = std::make_unique<Bed>();
    const fs::path dir = b->root / "store";
    const std::string d = text(dir);
    CHECK_EQ(b->run({"init", d}), location::recorded);
    const std::string want = record_of(at_text, d);
    CHECK_EQ(bytes_of(b->record), want);
    CHECK(b->out.ends_with(want + "recorded " + d + "\n"));
    CHECK_EQ(b->store_dir(), d);
    CHECK_EQ(b->run({"show"}), location::recorded);
    CHECK(b->out.ends_with(want + "recorded " + d + "\n"));
    CHECK(b->out.starts_with("System Protection: off (fake), volume V:.") && b->called("facts"));
    const std::string abs = escaped(d);
    struct Form {
        std::string_view name;
        std::string bytes;
    };
    const std::array<Form, 13> forms = {{
        {"a key added", "{\"at\":\"" + std::string(at_text) + "\",\"dir\":\"" + abs + "\",\"x\":1}\n"},
        {"dir relative", "{\"at\":\"" + std::string(at_text) + "\",\"dir\":\"store\"}\n"},
        {"a control character", "{\"at\":\"" + std::string(at_text) + "\",\"dir\":\"" + abs + "\\u0001x\"}\n"},
        {"a duplicate key", "{\"at\":\"" + std::string(at_text) + "\",\"dir\":\"" + abs + "\",\"dir\":\"" + abs + "\"}\n"},
        {"no final LF", want.substr(0, want.size() - 1)},
        {"a second line", want + want},
        {"at missing", "{\"dir\":\"" + abs + "\"}\n"},
        {"dir missing", "{\"at\":\"" + std::string(at_text) + "\"}\n"},
        {"at not a time", "{\"at\":\"yesterday\",\"dir\":\"" + abs + "\"}\n"},
        {"dir not a string", "{\"at\":\"" + std::string(at_text) + "\",\"dir\":1}\n"},
        {"not an object", "[]\n"},
        {"empty", ""},
        {"keys out of order", "{\"dir\":\"" + abs + "\",\"at\":\"" + std::string(at_text) + "\"}\n"},
    }};
    for (const Form& f : forms) {
        put(b->record, f.bytes);
        const int show = b->run({"show"});
        const bool show_ok = show == location::refused && b->out == Bed::refusal("show", b->record, "config");
        const int change = b->run({"change", text(b->root / "other"), "--accept"});
        const bool change_ok = change == location::refused && b->out == Bed::refusal("change", b->record, "config");
        const std::string by_dir = b->store_dir();
        std::cout << "location.record: " << f.name << ": show " << show << ", change " << change << ", store_dir "
                  << by_dir << "\n";
        CHECK(show_ok);
        CHECK(change_ok);
        CHECK_EQ(by_dir, std::string("StoreError store_dir config"));
        CHECK_EQ(bytes_of(b->record), f.bytes);
        CHECK(!fs::exists(b->root / "other"));
    }
}

QIVEN_CASE(location, not_chosen) {
    const auto b = std::make_unique<Bed>();
    CHECK_EQ(b->store_dir(), std::string("StoreError store_dir not_chosen"));
    CHECK(!fs::exists(b->config_dir));
    CHECK_EQ(b->run({"show"}), location::refused);
    CHECK_EQ(b->out, Bed::refusal("show", b->record, "not_chosen"));
    CHECK(fs::is_directory(b->config_dir));
    CHECK_EQ(b->run({"change", text(b->root / "store")}), location::refused);
    CHECK_EQ(b->out, Bed::refusal("change", b->record, "not_chosen"));
    CHECK(!fs::exists(b->root / "store") && !fs::exists(b->record));
}

QIVEN_CASE(location, guard) {
    const auto b = std::make_unique<Bed>();
    fs::create_directories(b->root / "repo" / ".git");
    fs::create_directories(b->root / "wt");
    put(b->root / "wt" / ".git", "gitdir: elsewhere\n");
    fs::create_directories(b->root / "cloud");
    b->c.synced = {{text(b->root / "cloud"), "fake drive"}};
    b->c.links = {{text(b->root / "to-repo"), text(b->root / "repo")}, {text(b->root / "to-cloud"), text(b->root / "cloud")}};
    b->remake();
    struct Target {
        fs::path dir;
        std::string_view detail;
    };
    const std::array<Target, 6> targets = {{
        {b->root / "repo" / "s", "checkout"},
        {b->root / "wt" / "s", "checkout"},
        {b->root / "cloud" / "s", "synced"},
        {b->root / "to-repo" / "s", "checkout"},
        {b->root / "to-cloud" / "s", "synced"},
        {b->root / "repo", "checkout"},
    }};
    for (const std::string_view op : {"init", "change"}) {
        if (op == "change") {
            CHECK_EQ(b->run({"init", text(b->root / "store")}), location::recorded);
        }
        const std::string before = bytes_of(b->record);
        for (const Target& t : targets) {
            const int code = b->run({op, text(t.dir), "--accept"});
            std::cout << "location.guard: " << op << " " << text(t.dir.filename()) << ": " << code << ", " << b->out;
            CHECK_EQ(code, location::refused);
            CHECK(b->said("StoreError location: guard ") && b->out.ends_with("; " + std::string(t.detail) + "\n"));
            CHECK(!b->called("facts") && !b->called("create"));
            CHECK_EQ(bytes_of(b->record), before);
            CHECK(t.dir == b->root / "repo" || !fs::exists(t.dir));
        }
    }
}

QIVEN_CASE(location, warnings) {
    const auto b = std::make_unique<Bed>();
    struct Warn {
        std::size_t condition;
        State state;
        std::string_view line;
    };
    constexpr std::array<std::string_view, 3> names = {"warning: System Protection: ", "warning: File History: ",
                                                       "warning: BitLocker: "};
    const std::array<Warn, 7> warns = {{
        {0, State::on, "on"},
        {0, State::unknown, "unknown"},
        {1, State::on, "on"},
        {1, State::unknown, "unknown"},
        {2, State::on, "on"},
        {2, State::unknown, "unknown"},
        {2, State::locked, "locked"},
    }};
    std::size_t n = 0;
    for (const Warn& w : warns) {
        auto& r = w.condition == 0 ? b->c.default_facts.protection
                                   : (w.condition == 1 ? b->c.default_facts.file_history : b->c.default_facts.bitlocker);
        r.state = w.state;
        b->remake();
        std::error_code error;
        fs::remove(b->record, error);
        const fs::path dir = b->root / ("s" + std::to_string(++n));
        CHECK_EQ(b->run({"init", text(dir)}), location::not_accepted);
        std::cout << "location.warnings: " << names.at(w.condition) << w.line << ":\n" << b->out;
        const std::string line = std::string(names.at(w.condition)) + std::string(w.line) + " (fake), volume V:.";
        CHECK(b->said(line));
        CHECK(b->out.find("warning: ") == b->out.rfind("warning: "));
        CHECK(b->out.ends_with(std::string(location::detail::not_accepted_line())));
        std::size_t looks = 0;
        for (std::size_t at = b->out.find("To look:"); at != std::string::npos; at = b->out.find("To look:", at + 1)) {
            ++looks;
        }
        CHECK_EQ(looks, std::size_t{3});
        CHECK(!fs::exists(dir) && !fs::exists(b->record));
        CHECK_EQ(b->run({"init", text(dir), "--accept"}), location::recorded);
        CHECK(b->said(line) && b->out.ends_with("recorded " + text(dir) + "\n"));
        CHECK(fs::exists(dir / "store.db") && bytes_of(b->record) == record_of(at_text, text(dir)));
        r.state = State::off;
    }
    // change warned: nothing changed; then accepted.
    b->c.default_facts.bitlocker.state = State::unknown;
    b->remake();
    const std::string before = bytes_of(b->record);
    const fs::path next = b->root / "next";
    CHECK_EQ(b->run({"change", text(next)}), location::not_accepted);
    CHECK(bytes_of(b->record) == before && !fs::exists(next));
    CHECK_EQ(b->run({"change", text(next), "--accept"}), location::refused);
    CHECK(b->said("; not_moved: store.db\n"));
}

QIVEN_CASE(location, init) {
    const auto b = std::make_unique<Bed>();
    // The configuration directory missing: made, then recorded.
    const fs::path made = b->root / "made";
    CHECK(!fs::exists(b->config_dir));
    CHECK_EQ(b->run({"init", text(made)}), location::recorded);
    CHECK(fs::is_directory(b->config_dir) && fs::exists(made / "store.db"));
    CHECK(b->called("create"));
    // A record there: chosen, the record unchanged.
    const std::string before = bytes_of(b->record);
    CHECK_EQ(b->run({"init", text(b->root / "again")}), location::refused);
    CHECK_EQ(b->out, Bed::refusal("init", b->record, "chosen"));
    CHECK(bytes_of(b->record) == before && !fs::exists(b->root / "again"));
    fs::remove(b->record);
    // A store there: opened, not created, then recorded.
    const fs::path held = b->root / "held";
    b->make_store(held);
    CHECK_EQ(b->run({"init", text(held)}), location::recorded);
    CHECK(!b->called("create") && bytes_of(b->record) == record_of(at_text, text(held)));
    fs::remove(b->record);
    // A 0-byte store.db, as a kill inside a create leaves it: refused as
    // open refuses it, nothing recorded, the file kept.
    const fs::path empty = b->root / "empty";
    fs::create_directories(empty);
    put(empty / "store.db", "");
    CHECK_EQ(b->run({"init", text(empty)}), location::refused);
    std::cout << "location.init: a 0-byte store.db: " << b->out;
    CHECK(b->said("StoreCorrupt") && !fs::exists(b->record) && bytes_of(empty / "store.db").empty());
    // A relative directory: PlatformError path; one the record cannot hold.
    CHECK_EQ(b->run({"init", "relative"}), location::refused);
    std::cout << "location.init: relative: " << b->out;
    CHECK(b->said("PlatformError path") && !b->called("facts") && !fs::exists(b->record));
    const fs::path control = b->root / "a\x01z";
    CHECK_EQ(b->run({"init", text(control), "--accept"}), location::refused);
    std::cout << "location.init: a control character: " << b->out;
    CHECK(b->said("PlatformError path") && !b->called("create") && !fs::exists(b->record));
    // The record's own check, behind resolve's on the fake: a relative dir,
    // a control character, an invalid UTF-8 byte.
    for (const std::string& d : {std::string("relative"), text(b->root) + "\x01", text(b->root) + "\xff"}) {
        const auto pages = b->fake->pages(4096);
        qiven3::memory::Arena arena(pages.bytes());
        qiven3::memory::String bytes{qiven3::memory::Allocator<char>(arena)};
        std::string got = "none";
        try {
            location::detail::record_bytes(std::chrono::floor<std::chrono::milliseconds>(clock_at), d, "init", bytes);
        } catch (const qiven3::platform::PlatformError& e) {
            got = e.code == qiven3::platform::Code::path ? "path" : "another";
        }
        CHECK_EQ(got, std::string("path"));
    }
    // --config whose parent is missing, then naming a file.
    const fs::path lost = b->root / "lost" / "config";
    b->c.config_dir = text(lost);
    b->remake();
    CHECK_EQ(b->run({"init", text(b->root / "s"), "--config", text(lost)}), location::refused);
    std::cout << "location.init: --config's parent missing: " << b->out;
    CHECK(b->said("PlatformError io") && !fs::exists(b->root / "lost") && !fs::exists(b->root / "s"));
    const fs::path file = b->root / "file";
    put(file, "kept");
    b->c.config_dir = text(file);
    b->remake();
    CHECK_EQ(b->run({"init", text(b->root / "s"), "--config", text(file)}), location::refused);
    std::cout << "location.init: --config a file: " << b->out;
    CHECK(b->said("PlatformError exists") && bytes_of(file) == "kept" && !fs::exists(b->root / "s"));
}

QIVEN_CASE(location, change) {
    const auto b = std::make_unique<Bed>();
    const fs::path old = b->root / "old";
    CHECK_EQ(b->run({"init", text(old)}), location::recorded);
    const std::string first = bytes_of(b->record);
    b->fake->set_clock(std::chrono::time_point_cast<std::chrono::microseconds>(clock_at + std::chrono::hours{1}));
    constexpr std::string_view later = "2026-10-24T09:09:10.123Z";
    // 1: the recorded directory again, its case changed where the system
    // folds it: printed, nothing changed.
    std::string upper = text(old);
    for (char& ch : upper) {
        ch = ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
    }
    CHECK_EQ(b->run({"change", upper}), location::recorded);
    CHECK(b->out.ends_with(first + "recorded " + text(old) + "\n"));
    CHECK(bytes_of(b->record) == first && !b->said(location::detail::moved_line()));
    // 2: the old directory still holding v3's files, each alone, then all.
    const fs::path next = b->root / "next";
    CHECK_EQ(b->run({"change", text(next)}), location::refused);
    CHECK_EQ(b->out.substr(b->out.rfind("StoreError")), Bed::refusal("change", old, "not_moved: store.db"));
    fs::remove(old / "store.db");
    constexpr std::array<std::string_view, 5> files = {"store.db", "store.db-wal", "store.db-shm", "checkouts.json",
                                                       "map-marks.json"};
    for (const std::string_view f : files) {
        put(old / f, "x");
        CHECK_EQ(b->run({"change", text(next)}), location::refused);
        CHECK(b->out.ends_with("; not_moved: " + std::string(f) + "\n"));
        fs::remove(old / f);
    }
    for (const std::string_view f : files) {
        put(old / f, "x");
    }
    CHECK_EQ(b->run({"change", text(next)}), location::refused);
    std::cout << "location.change: all five left: " << b->out.substr(b->out.rfind("StoreError"));
    CHECK(b->out.ends_with("; not_moved: store.db, store.db-wal, store.db-shm, checkouts.json, map-marks.json\n"));
    CHECK(bytes_of(b->record) == first && !fs::exists(next));
    // A file of another name left: holds none; 4: neither holds a store:
    // created, the moved line, then recorded.
    for (const std::string_view f : files) {
        fs::remove(old / f);
    }
    put(old / "notes.txt", "x");
    CHECK_EQ(b->run({"change", text(next)}), location::recorded);
    CHECK(b->said(location::detail::moved_line()) && b->called("create"));
    CHECK(b->out.ends_with(record_of(later, text(next)) + "recorded " + text(next) + "\n"));
    CHECK(fs::exists(next / "store.db") && bytes_of(b->record) == record_of(later, text(next)));
    // The old one gone: holds none; 3: the new one holding a store: opened,
    // not created, recorded.
    const fs::path held = b->root / "held";
    b->make_store(held);
    fs::remove_all(next);
    CHECK_EQ(b->run({"change", text(held)}), location::recorded);
    CHECK(!b->called("create") && bytes_of(b->record) == record_of(later, text(held)));
    // A file where the old directory was: holds none.
    fs::remove_all(held);
    put(held, "a file");
    CHECK_EQ(b->run({"change", text(next)}), location::recorded);
    CHECK(bytes_of(b->record) == record_of(later, text(next)));
#if defined(_WIN32)
    std::cout << "location.change: old_unreadable: not applicable on Windows: the fake's status reads are the "
                 "system's, and no scratch directory denies them here\n";
#else
    // The old directory unreadable: its files' status refused.
    fs::permissions(next, fs::perms::none);
    std::error_code denied;
    static_cast<void>(fs::status(next / "store.db", denied));
    if (denied) {
        CHECK_EQ(b->run({"change", text(old)}), location::refused);
        std::cout << "location.change: old_unreadable: " << b->out.substr(b->out.rfind("StoreError"));
        CHECK(b->said("; old_unreadable: ") && bytes_of(b->record) == record_of(later, text(next)));
    } else {
        std::cout << "location.change: old_unreadable: not applicable: this user reads a directory of mode 0\n";
    }
    fs::permissions(next, fs::perms::owner_all);
#endif
}

QIVEN_CASE(location, output) {
    const auto b = std::make_unique<Bed>();
    b->c.encoding = qiven3::platform::fake::Encoding::ascii;
    b->remake();
    const std::string dir = text(b->root) + std::string(1, static_cast<char>(fs::path::preferred_separator)) +
                            "caf\xc3\xab";
    CHECK_EQ(b->run({"init", dir}), location::recorded);
    std::cout << "location.output: " << b->out;
    CHECK(b->out.ends_with("recorded " + text(b->root) + std::string(1, static_cast<char>(fs::path::preferred_separator)) +
                           "caf?\n"));
    CHECK_EQ(bytes_of(b->record), record_of(at_text, dir));
    CHECK(fs::exists(from(dir) / "store.db"));
    CHECK_EQ(b->store_dir(), dir);
}

QIVEN_CASE(location, lock) {
    const auto b = std::make_unique<Bed>();
    fs::create_directories(b->config_dir);
    {
        auto held = b->fake->lock(text(b->config_dir / "location.lock"), qiven3::platform::Seconds{1.0});
        CHECK_EQ(b->run({"init", text(b->root / "s")}), location::refused);
        std::cout << "location.lock: held: " << b->out;
        CHECK(b->said("PlatformError busy") && !fs::exists(b->record) && !fs::exists(b->root / "s"));
    }
    CHECK_EQ(b->run({"init", text(b->root / "s")}), location::recorded);
}
