// location's smoke case on the fake over a scratch directory of its own:
// the command line read; init refused for its warnings, then accepted,
// the store created and the record read back by store_dir; init again
// chosen; show; change to the same directory, then to another while the
// old one still holds the store, not_moved; a usage error. usage: each
// usage error through main, exit 2.

#include <array>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <iostream>
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

// A scratch directory of the case's own, removed at its end.
struct Scratch {
    Scratch() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        root = fs::temp_directory_path() / ("qiven3-location-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(root);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    fs::path root;
};

qiven3::platform::fake::Config config(const fs::path& root, State facts) {
    qiven3::platform::fake::Config c;
    c.root = text(root);
    c.config_dir = text(root / "config");
    c.seed = 5;
    c.default_facts.volume = "V:";
    c.default_facts.protection.state = facts;
    c.default_facts.file_history.state = facts;
    c.default_facts.bitlocker.state = facts;
    return c;
}

int run(Fake& fake, std::initializer_list<std::string_view> args, std::size_t* peak = nullptr) {
    location::Hooks hooks;
    hooks.peak = peak;
    return location::main(std::span<const std::string_view>(args.begin(), args.size()), fake, hooks);
}

// The fake's output since mark, and the mark moved past it.
std::string since(const Fake& fake, std::size_t& mark) {
    std::string out = fake.written().substr(mark);
    mark = fake.written().size();
    return out;
}

}  // namespace

QIVEN_CASE(location, arguments) {
    const std::array<std::string_view, 5> init = {"init", "--config", "/c", "/d", "--accept"};
    const auto a = location::arguments(init);
    REQUIRE(a.has_value());
    CHECK(a->command == location::Command::init && a->directory == "/d" && a->config == "/c" && a->accept);
    CHECK(location::config_option(init) == std::optional<std::string_view>("/c"));
    const std::array<std::string_view, 2> show_dir = {"show", "/d"};
    CHECK(!location::arguments(show_dir));
    const std::array<std::string_view, 2> show_accept = {"show", "--accept"};
    CHECK(!location::arguments(show_accept));
    const std::array<std::string_view, 2> no_value = {"change", "--config"};
    CHECK(!location::arguments(no_value));
    const std::array<std::string_view, 1> no_dir = {"init"};
    CHECK(!location::arguments(no_dir));
    const std::array<std::string_view, 3> unknown = {"init", "/d", "--force"};
    CHECK(!location::arguments(unknown));
    CHECK(!location::arguments({}));
}

QIVEN_CASE(location, smoke) {
    const Scratch scratch;
    const std::string dir = text(scratch.root / "store");
    const std::string other = text(scratch.root / "other");
    const fs::path record = scratch.root / "config" / "location.json";
    {
        Fake warned(config(scratch.root, State::unknown));
        CHECK_EQ(run(warned, {"init", dir}), location::not_accepted);
        CHECK(warned.written().find("warning: BitLocker") != std::string::npos);
        CHECK(!fs::exists(record) && !fs::exists(scratch.root / "store"));
    }
    Fake fake(config(scratch.root, State::off));
    std::size_t mark = 0;
    std::size_t peak = 0;
    CHECK_EQ(run(fake, {"init", dir}, &peak), location::recorded);
    std::cout << "location.smoke: init's arena peak " << peak << " of " << location::budget << " bytes\n";
    CHECK(peak > 0 && peak <= location::budget);
    CHECK_EQ(fake.lent(), std::size_t{0});
    const std::string out = since(fake, mark);
    std::cout << "location.smoke: init's output: " << out;
    CHECK(out.find("Nothing encrypts the store at rest.") != std::string::npos);
    CHECK(out.ends_with("recorded " + dir + "\n"));
    CHECK(fs::exists(scratch.root / "store" / "store.db"));
    {
        const auto pages = fake.pages(64 * 1024);
        qiven3::memory::Arena arena(pages.bytes());
        qiven3::memory::String got{qiven3::memory::Allocator<char>(arena)};
        location::store_dir(fake, got);
        CHECK_EQ(std::string(got), dir);
    }
    CHECK_EQ(run(fake, {"init", dir}), location::refused);
    CHECK(since(fake, mark).find("chosen") != std::string::npos);
    CHECK_EQ(run(fake, {"show"}), location::recorded);
    CHECK(since(fake, mark).ends_with("recorded " + dir + "\n"));
    CHECK_EQ(run(fake, {"change", dir}), location::recorded);
    since(fake, mark);
    CHECK_EQ(run(fake, {"change", other}), location::refused);
    const std::string moved = since(fake, mark);
    std::cout << "location.smoke: change's refusal: " << moved;
    CHECK(moved.find("not_moved: store.db") != std::string::npos);
    CHECK(!fs::exists(scratch.root / "other"));
    CHECK_EQ(run(fake, {"move", dir}), location::usage);
}

// Each usage error through main: exit 2, the usage line alone, the platform
// called for standard_output alone, nothing made; the statuses the text's.
QIVEN_CASE(location, usage) {
    static_assert(location::recorded == 0 && location::refused == 1 && location::usage == 2 &&
                  location::not_accepted == 4);
    const Scratch scratch;
    Fake fake(config(scratch.root, State::off));
    const std::vector<std::vector<std::string_view>> errors = {
        {},
        {"move", "/d"},
        {"init"},
        {"init", "/d", "/e"},
        {"init", "/d", "--accept", "--accept"},
        {"init", "/d", "--config", "/c", "--config", "/c"},
        {"init", "/d", "--config"},
        {"init", "/d", "--config", ""},
        {"show", "/d"},
        {"show", "--accept"},
        {"change", "--accept"},
        {"change", "/d", "--force"},
        {"--accept", "init", "/d"},
        {"Init", "/d"},
    };
    std::size_t mark = 0;
    for (const auto& e : errors) {
        const std::size_t calls = fake.calls().size();
        CHECK_EQ(location::main(e, fake), location::usage);
        CHECK_EQ(since(fake, mark), std::string(location::detail::usage_line()));
        CHECK_EQ(fake.calls().size(), calls + 1);
        CHECK_EQ(fake.calls().back(), std::string("standard_output"));
        CHECK(!location::config_option(e));
    }
    CHECK(!fs::exists(scratch.root / "config"));
    std::cout << "location.usage: " << location::detail::usage_line();
    const std::array<std::string_view, 4> late = {"change", "--accept", "--config", "/c"};
    CHECK(!location::arguments(late));
    const std::array<std::string_view, 4> given = {"change", "--accept", "/d", "--config=/c"};
    CHECK(!location::arguments(given));
    const std::array<std::string_view, 3> show = {"show", "--config", "/c"};
    const auto s = location::arguments(show);
    REQUIRE(s.has_value());
    CHECK(s->command == location::Command::show && s->directory.empty() && s->config == "/c" && !s->accept);
    CHECK(location::config_option(show) == std::optional<std::string_view>("/c"));
    const std::array<std::string_view, 2> none = {"change", "/d"};
    CHECK(location::arguments(none).has_value() && !location::config_option(none));
}
