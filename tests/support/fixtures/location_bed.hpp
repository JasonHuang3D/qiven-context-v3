#pragma once

// location's cases' bed, shared by tests/unit/location_memory_test.cpp and
// location_faults_test.cpp: the scratch directory of the case's own, its
// fake and its directories, and the steps, each from its own state: init
// into a new directory (the store created), into one holding a store
// (opened), init with a record there (chosen), init warned (exit 4); show;
// change to the recorded directory, to a new one (created), to one holding
// a store (opened), and with the old one holding map-marks.json
// (not_moved).

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/location.hpp"
#include "qiven3/location_impl.hpp"
#include "store/engine/sqlite.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::test::location_bed {

namespace fs = std::filesystem;
namespace location = qiven3::location;
namespace sqlite = qiven3::store::sqlite;
using Fake = qiven3::platform::fake::FakePlatform;
using qiven3::platform::State;
#if defined(QIVEN3_FAULTS)
using qiven3::memory::Countdown;
#else
using Countdown = void;  // nothing counts: run's arenas unused
#endif

inline std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

inline std::string bytes_of(const fs::path& path) {
    std::error_code error;
    if (!fs::exists(path, error)) {
        return "absent";
    }
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline void put(const fs::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary);  // out alone: truncated
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

inline std::string record_of(std::string_view dir) {
    std::string d;
    for (const char c : dir) {
        if (c == '\\' || c == '"') {
            d.push_back('\\');
        }
        d.push_back(c);
    }
    return "{\"at\":\"2026-10-24T08:09:10.123Z\",\"dir\":\"" + d + "\"}\n";
}

enum class Step : std::uint8_t { init_new, init_held, init_chosen, init_warned, show, change_same, change_new, change_held, change_left };
inline constexpr std::array<Step, 9> steps = {Step::init_new,    Step::init_held,  Step::init_chosen,
                                              Step::init_warned, Step::show,       Step::change_same,
                                              Step::change_new,  Step::change_held, Step::change_left};
inline constexpr std::array<std::string_view, 9> step_names = {"init new",    "init held",  "init chosen",
                                                               "init warned", "show",       "change same",
                                                               "change new",  "change held", "change left"};
inline std::string_view name_of(Step s) { return step_names.at(static_cast<std::size_t>(s)); }

// The scratch directory, its fake and its directories: old, the recorded
// one a change leaves; held, holding a store made once; fresh, made by a
// step; warm, where the fake's BitLocker is unknown. Made from a Scratch,
// a fresh one each time.
struct Scratch {};
struct Bed {
    explicit Bed(Scratch /*fresh*/) : root(fresh_root()), config_dir(root / "config"), record(config_dir / "location.json") {
        old = root / "old";
        held = root / "held";
        fresh = root / "new";
        warm = root / "warm";
        qiven3::platform::fake::Config c;
        c.root = text(root);
        c.config_dir = text(config_dir);
        c.seed = 5;
        c.clock = std::chrono::time_point_cast<std::chrono::microseconds>(
            std::chrono::sys_days{std::chrono::year{2026} / 10 / 24} + std::chrono::hours{8} + std::chrono::minutes{9} +
            std::chrono::seconds{10} + std::chrono::microseconds{123456});
        c.default_facts.volume = "V:";
        c.default_facts.protection.state = State::off;
        c.default_facts.file_history.state = State::off;
        c.default_facts.bitlocker.state = State::off;
        qiven3::platform::fake::FakeFacts w = c.default_facts;
        w.bitlocker.state = State::unknown;
        c.facts = {{text(warm), w}};
        fake = std::make_unique<Fake>(c);
        {
            const qiven3::store::Engine<Fake> maker(*fake, 1);
            auto s = qiven3::store::SQLiteStore<Fake>::open(text(held / "store.db"), true, false, *fake);
            static_cast<void>(s.close());
        }
        fs::create_directories(config_dir);
    }
    Bed(const Bed&) = delete;
    Bed& operator=(const Bed&) = delete;
    Bed(Bed&&) = delete;
    Bed& operator=(Bed&&) = delete;
    ~Bed() {
        engine.reset();
        fake.reset();
        try {
            std::error_code error;
            fs::remove_all(root, error);
        } catch (const std::exception&) {
            static_cast<void>(std::fputs("location_bed: the scratch directory was not removed\n", stderr));
        }
    }
    // The root's length where the temporary directory leaves room: the strings location makes of its
    // paths grow alike in every run, and counts pins their allocations.
    static constexpr std::size_t root_length = 96;
    static fs::path fresh_root() {
        // On the heap: libstdc++'s random_device is some 5,000 bytes.
        std::vector<std::random_device> device(1);
        std::random_device& draw = device.front();
        fs::path out = fs::temp_directory_path() / "qiven3-location-m-";
        const std::size_t at = text(out).size();
        std::string digits = std::to_string(draw()) + std::to_string(draw());
        digits.resize(at + 8 <= root_length ? root_length - at : 8, '0');
        out += digits;
        fs::create_directories(out);
        return out;
    }
    // The step's state, made again from what any run left.
    void prepare(Step s) {
        std::error_code error;
        fs::remove(record, error);
        fs::remove_all(fresh, error);
        fs::remove_all(old, error);
        fs::remove_all(warm, error);
        if (s == Step::init_chosen || s == Step::change_new || s == Step::change_held || s == Step::change_left) {
            put(record, record_of(text(old)));
        } else if (s == Step::show || s == Step::change_same) {
            put(record, record_of(text(held)));
        }
        if (s == Step::change_left) {
            fs::create_directories(old);
            put(old / "map-marks.json", "{}");
        }
        before = bytes_of(record);
    }
    [[nodiscard]] fs::path target(Step s) const {
        if (s == Step::init_held || s == Step::change_held || s == Step::change_same) {
            return held;
        }
        return s == Step::init_warned ? warm / "s" : fresh;
    }
    [[nodiscard]] static int expected(Step s) {
        if (s == Step::init_chosen || s == Step::change_left) {
            return location::refused;
        }
        return s == Step::init_warned ? location::not_accepted : location::recorded;
    }
    // Whether the step writes a record.
    [[nodiscard]] static bool records(Step s) {
        return s == Step::init_new || s == Step::init_held || s == Step::change_new || s == Step::change_held;
    }
    // main on the step's arguments, its arena under arenas.
    int run(Step s, Countdown* arenas = nullptr, std::size_t* peak = nullptr) {
        const std::string dir = text(target(s));
        location::Hooks hooks;
        hooks.peak = peak;
#if defined(QIVEN3_FAULTS)
        hooks.arenas = arenas;
#else
        static_cast<void>(arenas);
#endif
        const std::string_view op = s <= Step::init_warned ? "init" : (s == Step::show ? "show" : "change");
        std::array<std::string_view, 3> args = {op, dir, "--accept"};
        const std::size_t n = s == Step::show ? 1 : (s == Step::init_warned ? 2 : 3);
        return location::main(std::span<const std::string_view>(args.data(), n), *fake, hooks);
    }
    // The record's state after a run that ended in code: as before, or the
    // step's own at its status 0.
    [[nodiscard]] bool record_held(Step s, int code) const {
        const std::string now = bytes_of(record);
        if (code == location::recorded && records(s)) {
            return now == record_of(text(target(s)));
        }
        return now == before;
    }
    // Nothing but the record in the configuration directory.
    [[nodiscard]] bool config_clean() const {
        std::error_code error;
        for (const fs::directory_entry& e : fs::directory_iterator(config_dir, error)) {
            if (e.path().filename() != "location.json") {
                return false;
            }
        }
        return !error;
    }

    fs::path root;
    fs::path config_dir;
    fs::path record;
    fs::path old;
    fs::path held;
    fs::path fresh;
    fs::path warm;
    std::unique_ptr<Fake> fake;
    std::optional<qiven3::store::Engine<Fake>> engine;
    std::string before;
};

}  // namespace qiven3::test::location_bed
