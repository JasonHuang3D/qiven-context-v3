#pragma once

// stage_book: store/stage's cases' entries and helpers. Book reads the
// fixtures' entries as stored lines with their body records, and makes
// entries like a fixture's: its fields changed, a content of its own under
// the fixture's key, its digest and id computed. A secret's fixture is
// built when the case runs, from parts, so no file holds a live-looking
// token. refused runs a call and gives the StoreError it raised, as a
// summary of its fields; check and admit are run so on an arena.
//
// thread: one (the fake's).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "entry/body.hpp"
#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/stage.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/fixtures.hpp"
#include "support/fixtures/view.hpp"

namespace qiven3::test::stage_book {

namespace stage = qiven3::store::stage;
namespace header = qiven3::entry::header;
namespace body = qiven3::entry::body;
namespace fixtures = qiven3::test::fixtures;
using qiven3::memory::Arena;
using qiven3::store::EntryView;
using qiven3::store::Kind;
using qiven3::store::Limits;
using qiven3::store::Operation;
using qiven3::store::StoreError;
using Chars = qiven3::memory::Allocator<char>;
using Fake = qiven3::platform::fake::FakePlatform;
using Slot = header::Slot;

// A stored entry: its header line, its body record and content or none.
struct Stored {
    std::string line;
    std::optional<std::string> record;
    std::optional<std::string> content;
};

// A scratch for check: at each call a fresh arena over its pages, the
// countdown attached where one is given; it keeps the arenas' peak and
// whether one ended holding a block.
class ScratchPages {
public:
    explicit ScratchPages(std::size_t bytes) : pages_(bytes) {}
    Arena& operator()() {
        end();
        arena_.emplace(pages_);
#if defined(QIVEN3_FAULTS)
        if (countdown_ != nullptr) {
            arena_->attach(countdown_);
        }
#endif
        return *arena_;
    }
    // The arena ended, its peak kept; whether it held no block.
    bool end() {
        if (!arena_) {
            return true;
        }
        peak_ = std::max(peak_, arena_->peak());
        const bool empty = arena_->used() == 0;
        leaked_ = leaked_ || !empty;
        arena_.reset();
        return empty;
    }
    [[nodiscard]] std::size_t peak() const noexcept { return peak_; }
    [[nodiscard]] bool leaked() const noexcept { return leaked_; }
#if defined(QIVEN3_FAULTS)
    void attach(qiven3::memory::Countdown* countdown) noexcept { countdown_ = countdown; }
#endif

private:
    std::vector<std::byte> pages_;
    std::optional<Arena> arena_;
    std::size_t peak_ = 0;
    bool leaked_ = false;
#if defined(QIVEN3_FAULTS)
    qiven3::memory::Countdown* countdown_ = nullptr;
#endif
};
static_assert(stage::Scratch<ScratchPages>);

// One entry's scratch, 1 MiB.
inline constexpr std::size_t scratch_bytes = std::size_t{1} << 20U;

// Views of held entries: a temporary list is refused, as its views would
// dangle.
std::vector<EntryView> views(std::vector<Stored>&& list) = delete;
inline std::vector<EntryView> views(const std::vector<Stored>& list) {
    std::vector<EntryView> out;
    for (const Stored& s : list) {
        out.push_back(EntryView{s.line, s.record ? std::optional<std::string_view>(*s.record) : std::nullopt});
    }
    return out;
}

// The bytes the entries add: each line and record with its LF.
inline std::uint64_t added(const std::vector<Stored>& list) {
    std::uint64_t n = 0;
    for (const Stored& s : list) {
        n += s.line.size() + 1 + (s.record ? s.record->size() + 1 : 0);
    }
    return n;
}

inline std::string id_of(const Stored& s) { return std::string(header::parse_line(s.line).value(Slot::id)); }
inline std::string content_of(const Stored& s) {
    return std::string(header::parse_line(s.line).text(Slot::content).value_or(std::string_view()));
}

// Each entry held in the view, with its content.
inline void hold(fixtures::MemoryView& view, const std::vector<Stored>& list) {
    for (const Stored& s : list) {
        view.put(header::parse_line(s.line),
                 s.content ? std::optional<std::string_view>(*s.content) : std::nullopt);
    }
}

inline stage::AtView at(Operation op) { return stage::AtView{op, "store-path"}; }

inline qiven3::store::LaneHead lane_head(std::string_view scope, std::string_view lane, std::string_view id,
                                         std::int64_t seq) {
    qiven3::store::LaneHead h;
    h.head = qiven3::store::Head{qiven3::store::Id::of(id), seq};
    h.scope = qiven3::memory::Inline<qiven3::store::scope_bytes>(scope);
    h.lane = qiven3::memory::Inline<qiven3::store::lane_bytes>(lane);
    return h;
}

// A lane the slack-xox pattern hits, built from parts.
inline std::string slack_lane() { return std::string("xoxb") + "-1-" + "abcdefgh"; }

class Book {
public:
    explicit Book(Fake& fake) : fake_(&fake) { fixtures::load_entries(value_); }

    // A fixture as stored.
    [[nodiscard]] Stored stored(std::string_view name) const {
        const fixtures::EntryView e = fixture(name);
        Stored s{std::string(e.header), std::nullopt, std::nullopt};
        if (e.content && e.key) {
            const header::Header<> h = header::parse_line(e.header);
            std::string record;
            body::make(*e.content, digest::key_from_hex(*fake_, *e.key), h.value(Slot::id)).line(record);
            s.record = std::move(record);
            s.content = std::string(*e.content);
        }
        return s;
    }
    // A fixture's header with A's content, an aws-akia key in its mistake,
    // under A's key, its parent changed where one is given: one content
    // digest for each.
    [[nodiscard]] Stored with_secret(std::string_view like, std::optional<std::string> parent) const {
        std::string content(content_named("A"));
        const std::string_view key = "\"mistake\":\"";
        const std::size_t from = content.find(key) + key.size();
        content.replace(from, content.find('"', from) - from, std::string("a key ") + "AKIA" + "IOSFODNN7EXAMPLE");
        return made(like, "A", content, [&](header::FieldsView& f) {
            if (parent) {
                f.parent = *parent;
            }
        });
    }
    // A fixture on another lane, its writer's role the lane's.
    [[nodiscard]] Stored on_lane(std::string_view like, const std::string& lane) const {
        std::string writer;
        return made(like, like, std::string(content_named(like)), [&](header::FieldsView& f) {
            writer = lane + std::string(f.writer.substr(f.writer.find('/')));
            f.lane = lane;
            f.writer = writer;
        });
    }

    // M1's content as a root on reviewer's lane at seq 1, under M1's key,
    // n references added (24 at most: evidence's 16, then source's 8), by
    // turns an output, its command 400 U+0001, each a six-byte escape; a
    // repo with each of its fields; and a transcript; each field within its
    // limit. With an entry's id, the last an entry reference to it, and a
    // relation to a repo subject: every syntax a map's content compiles.
    [[nodiscard]] Stored with_references(std::size_t n, std::string_view entry = {}) const {
        const std::string digest = std::string("\"digest\":\"hmac-sha256:") + std::string(64, '0') + "\"";
        std::string command;
        for (std::size_t i = 0; i < 400; ++i) {
            command += "\\u0001";
        }
        const std::array<std::string, 3> made_refs = {
            "{\"command\":\"" + command + "\"," + digest + ",\"type\":\"output\"}",
            "{\"anchor\":\"f\"," + digest + ",\"name\":\"g\",\"path\":\"src/a.cpp\",\"repository\":\"qiven-context-v3\"," +
                "\"revision\":\"" + std::string(40, 'a') + "\",\"type\":\"repo\"}",
            "{\"client\":\"qiven-loop\"," + digest + ",\"message\":\"m:1\",\"session\":\"s-1\",\"type\":\"transcript\"}"};
        std::array<std::string, 2> lists;  // evidence, source
        for (std::size_t i = 0; i < n; ++i) {
            std::string& list = lists.at(i < 16 ? 0 : 1);
            const std::string item = !entry.empty() && i + 1 == n
                                         ? "{\"id\":\"" + std::string(entry) + "\",\"type\":\"entry\"}"
                                         : made_refs.at(i % 3);
            list += (list.empty() ? "" : ",") + item;
        }
        std::string content(content_named("M1"));
        if (!entry.empty()) {
            content.insert(content.size() - 1,
                           ",\"relations\":[{\"relation\":\"uses\",\"to\":\"repo:qiven-context-v3/src/a.cpp#f\"}]");
        }
        if (!lists[1].empty()) {
            content.insert(content.size() - 1, ",\"source\":[" + lists[1] + "]");
        }
        if (!lists[0].empty()) {
            content.insert(content.find("\"object\":"), "\"evidence\":[" + lists[0] + "],");
        }
        return made("M1", "M1", content, [](header::FieldsView& f) {
            f.seq = 1;
            f.parent = std::nullopt;
        });
    }

    // A fixture's header with its fields as change leaves them, the content
    // given under key_of's key: its content digest and id computed.
    template <class F>
    [[nodiscard]] Stored made(std::string_view like, std::string_view key_of, const std::string& content,
                              const F& change) const {
        const fixtures::EntryView e = fixture(like);
        const std::string_view key = key_named(key_of);
        const header::Header<> h = header::parse_line(e.header);
        header::FieldsView f;
        f.scope = h.value(Slot::scope);
        f.lane = h.value(Slot::lane);
        f.seq = h.seq();
        f.parent = h.text(Slot::parent);
        f.kind = h.value(Slot::kind);
        f.writer = h.value(Slot::writer);
        f.basis = h.value(Slot::basis);
        f.at = h.value(Slot::at);
        f.target = h.text(Slot::target);
        change(f);
        std::string digest_text;
        // The digest alone is read: any id of the syntax serves.
        body::make(content, digest::key_from_hex(*fake_, key), h.value(Slot::id)).content_digest(digest_text);
        f.content = digest_text;
        const header::Header<> made_header = header::make(f);
        Stored s{std::string(), std::string(), content};
        made_header.line(s.line);
        body::make(content, digest::key_from_hex(*fake_, key), made_header.value(Slot::id)).line(*s.record);
        return s;
    }

private:
    // A fixture by name, read by index; a name not among them ends the
    // process.
    [[nodiscard]] fixtures::EntryView fixture(std::string_view name) const {
        for (std::size_t i = 0; i < fixtures::entry_count(value_); ++i) {
            const fixtures::EntryView e = fixtures::entry(value_, i);
            if (e.name == name) {
                return e;
            }
        }
        memory::fail_fast("stage_book: no fixture of the name");
    }
    // A fixture's content, and its key; one without it ends the process.
    [[nodiscard]] std::string_view content_named(std::string_view name) const {
        const fixtures::EntryView e = fixture(name);
        if (!e.content) {
            memory::fail_fast("stage_book: a fixture without its content");
        }
        return *e.content;
    }
    [[nodiscard]] std::string_view key_named(std::string_view name) const {
        const fixtures::EntryView e = fixture(name);
        if (!e.key) {
            memory::fail_fast("stage_book: a fixture without its key");
        }
        return *e.key;
    }

    Fake* fake_;
    qiven3::canon::Basic<> value_;
};

// A case's fake, book, operation arena and scratch, made on the heap so
// that a case's frame stays within the bound.
struct Bench {
    explicit Bench(std::size_t bytes)
        : fake(qiven3::platform::fake::Config{}), book(fake), pages(bytes), arena(pages), chars(arena),
          scratch(scratch_bytes) {}
    Fake fake;
    Book book;
    std::vector<std::byte> pages;
    Arena arena;
    Chars chars;
    ScratchPages scratch;
};

// A StoreError's fields.
struct Refusal {
    Kind kind = Kind::store_error;
    std::string code;
    std::string operation;
    std::string id;
    std::string field;
    std::string pattern;
    std::string detail;
    std::size_t index = 0;
    std::uint64_t size = 0;
    std::uint64_t count = 0;
};

template <class F>
std::optional<Refusal> refused(const F& f) {
    try {
        f();
    } catch (const StoreError& e) {
        Refusal r;
        r.kind = e.kind();
        if (const std::optional<qiven3::store::Code> code = e.code()) {
            r.code = qiven3::store::name(*code);
        } else if (const std::optional<qiven3::store::Full> full = e.full()) {
            r.code = qiven3::store::name(*full);
        } else {
            r.code = e.entry_code();
        }
        r.operation = e.operation();
        r.id = e.id();
        r.field = e.field();
        r.pattern = e.pattern();
        r.detail = e.detail();
        r.index = e.index();
        r.size = e.size();
        r.count = e.count();
        return r;
    }
    return std::nullopt;
}

inline std::optional<Refusal> refused_check(Fake& fake, const std::vector<Stored>& list, Operation op,
                                            const Chars& chars) {
    const std::vector<EntryView> lines = views(list);
    ScratchPages scratch(scratch_bytes);
    return refused([&] { (void)stage::check(fake, std::span<const EntryView>(lines), at(op), chars, scratch); });
}

inline std::optional<Refusal> refused_admit(fixtures::MemoryView& view, const stage::Checked<Fake>& checked,
                                            std::optional<std::string_view> expected, const Chars& chars,
                                            const Limits& limits = Limits{}) {
    const std::string_view scope = "project:qiven";
    const std::string_view lane =
        checked.entries.empty() ? std::string_view() : checked.entries[0].header().value(Slot::lane);
    return refused([&] {
        std::ignore = stage::admit(view, scope, lane, expected, checked, limits, at(Operation::append), chars);
    });
}

}  // namespace qiven3::test::stage_book
