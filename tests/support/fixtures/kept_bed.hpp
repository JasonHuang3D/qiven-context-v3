#pragma once

// write/kept's cases' bed, shared by tests/unit/write_kept_test.cpp and
// write_kept_steps_test.cpp: a scratch store of the case's own, the fake
// store's ids, a file's entries built by hand as the builder gives them,
// the append, and the catchers of a refusal and a platform fault.

#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "write/build.hpp"
#include "write/kept.hpp"
#include "write/refuse.hpp"

namespace qiven3::test::kept_bed {


namespace fs = std::filesystem;
namespace kept = qiven3::write::kept;
namespace header = qiven3::entry::header;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::Inline;
using qiven3::memory::String;
using qiven3::memory::Vector;
using qiven3::platform::Code;
using qiven3::platform::PlatformError;
using qiven3::platform::fake::Busy;
using qiven3::platform::fake::Config;
using qiven3::platform::fake::FakePlatform;
using qiven3::platform::fake::never;
using qiven3::write::build::Built;
using qiven3::write::refuse::Refusal;
namespace rc = qiven3::write::refuse;

inline std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

inline std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline void write_bytes(const fs::path& path, std::string_view bytes) {
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// A scratch store's directory of the case's own, removed at its end; made
// from the tag fresh.
struct Fresh {};
inline constexpr Fresh fresh{};
struct Scratch {
    explicit Scratch(Fresh /*fresh*/) {
        // On the heap: libstdc++'s random_device is some 5,000 bytes.
        std::vector<std::random_device> device(1);
        std::random_device& draw = device.front();
        root = fs::temp_directory_path() / ("qiven3-kept-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(root / "store");
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        try {
            std::error_code error;
            fs::remove_all(root, error);
        } catch (const std::exception&) {
            static_cast<void>(std::fputs("kept_bed: the scratch directory was not removed\n", stderr));
        }
    }
    [[nodiscard]] Config config() const {
        Config config;
        config.root = text(root);
        config.seed = 7;
        return config;
    }
    [[nodiscard]] std::string store() const { return text(root / "store"); }
    [[nodiscard]] fs::path kept() const { return root / "store" / "kept"; }
    // kept/'s names, sorted, '|'-joined.
    [[nodiscard]] std::string names() const {
        std::set<std::string> all;
        std::error_code error;
        for (const auto& e : fs::directory_iterator(kept(), error)) {
            all.insert(text(e.path().filename()));
        }
        std::string out;
        for (const std::string& name : all) {
            out += (out.empty() ? "" : "|") + name;
        }
        return out;
    }
    fs::path root;
};

// The fake store: the ids it holds.
struct Store {
    std::set<std::string, std::less<>> ids;
    int appends = 0;
    bool operator()(std::string_view id) const { return ids.contains(id); }
};
static_assert(kept::Holds<Store>);

// A fault that is no refusal, as a kill or the store's io is.
struct Killed {};

// count entries of writer in scope project:example, seq from first, a
// forget last when forget; each id the header's own.
struct Entries {
    Entries(Arena& arena, std::string_view writer, int count, int first, bool forget = false)
        : built(Allocator<Built>(arena)) {
        const std::string lane = writer == "import" ? "import" : std::string(header::role(writer));
        for (int i = 0; i < count; ++i) {
            const bool last = forget && i == count - 1;
            const std::string at = "2026-01-01T00:00:0" + std::to_string(i % 10) + ".000Z";
            const std::string content = "sha256:" + std::string(63, 'a') + std::to_string(i % 10);
            header::FieldsView f;
            f.scope = "project:example";
            f.lane = lane;
            f.seq = static_cast<decltype(f.seq)>(first) + static_cast<decltype(f.seq)>(i);
            f.kind = last ? "forget" : "claim";
            f.writer = writer;
            f.basis = "claimed";
            f.at = at;
            const std::string target = "sha256:" + std::string(64, 'b');
            if (last) {
                f.target = target;
            } else {
                f.content = content;
            }
            const auto h = header::make(f, Allocator<char>(arena));
            Built& b = built.emplace_back(arena);
            h.line(b.line);
            b.id = Inline<72>(h.text(header::Slot::id).value_or(std::string_view{}));
            b.body = !last;
            if (b.body) {
                b.record = R"({"body":"an invented record )" + std::to_string(first + i) + R"("})";
            }
        }
    }
    [[nodiscard]] std::span<const Built> view() const { return {built.data(), built.size()}; }
    Vector<Built> built;
};

// The append: every id the store's.
inline auto appends(Store& store, const Entries& entries) {
    return [&store, &entries] {
        ++store.appends;
        for (const Built& b : entries.built) {
            store.ids.insert(std::string(b.id.view()));
        }
    };
}

template <class Op>
std::optional<Refusal> refusal(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const Refusal& r) {
        return r;
    }
    return std::nullopt;
}

template <class Op>
std::optional<Code> fault(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const PlatformError& e) {
        return e.code;
    }
    return std::nullopt;
}

inline std::size_t count(const std::vector<std::string>& calls, std::string_view name) {
    std::size_t n = 0;
    for (const std::string& call : calls) {
        n += call == name ? 1U : 0U;
    }
    return n;
}

inline constexpr std::size_t mib = std::size_t{1} << 20U;

}  // namespace qiven3::test::kept_bed
