#pragma once

// The store half's wire forms (driver/store_ops.hpp): the contract's
// arguments read from a request, its results and typed errors written as
// a reply's values. Bytes as hexadecimal; counts as integers within
// 2^53 - 1, as every store's sizes and counts are.
//
//   head        {"id", "seq"}
//   volume      {"size", "count", "over_target", "over_warning"}
//   purge       {"done"}, and "reason" when pending
//   written     {"head", "volume", "purge"}; forget's adds "erased", ids
//   rows        [{"scope", "lane", "id", "seq"}, ...]
//   counts      {"entries", "bodies", "forgets", "lanes"}
//   limits      {"target_size", "warning_size", "max_size",
//                "target_count", "warning_count", "max_count"}
//   sizes       {"volume", "detail": {name: value, ...}}
//   an entry    {"line": hex, "body": hex}, "body" absent for none
//   a line      {"line": hex, "body": hex, "forgotten"}, an export's or
//               an import's, "body" absent for none
//
// thread: none

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "driver/drive.hpp"
#include "memory/arena.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"

namespace qiven3::drive::wire {

Value head(const store::Head& h);
Value volume(const store::Volume& v);
Value purge(const store::Purge& p);
Value written(const store::Written& w);
Value rows(std::span<const store::LaneHead> list);
Value counts(const store::Counts& c);
Value limits(const store::Limits& l);
Value sizes(const store::Sizes& s);
// A header's stored line, as hexadecimal, written on the arena.
std::string header_line(const store::Header& header, memory::Arena& arena);
// An export's or an import's line.
Value line(const store::Line& l);

// A store's typed error: "kind" "store" with its kind's fields.
Value error(const store::StoreError& e);

// The request's "limits", each threshold the format's where absent.
store::Limits limits_of(ArgView request);

// The request's entries under key: their bytes and their views, valid
// while this lives.
class Entries {
public:
    Entries(ArgView request, std::string_view key);
    Entries(const Entries&) = delete;
    Entries& operator=(const Entries&) = delete;
    ~Entries() = default;
    [[nodiscard]] std::span<const store::EntryView> views() const noexcept { return views_; }

private:
    std::vector<std::string> lines_;
    std::vector<std::optional<std::string>> bodies_;
    std::vector<store::EntryView> views_;
};

// The request's scope set, "scopes", or none for the whole store; its
// seqs to start after, "after"; forget's guard, "guard".
class Ranges {
public:
    explicit Ranges(ArgView request);
    Ranges(const Ranges&) = delete;
    Ranges& operator=(const Ranges&) = delete;
    ~Ranges() = default;
    [[nodiscard]] store::Scopes scopes() const noexcept;
    [[nodiscard]] std::span<const store::AfterView> after() const noexcept { return after_; }
    [[nodiscard]] std::span<const store::GuardView> guard() const noexcept { return guard_; }

private:
    bool whole_ = true;
    std::vector<std::string> texts_;
    std::vector<std::string_view> scopes_;
    std::vector<store::AfterView> after_;
    std::vector<store::GuardView> guard_;
};

// An import's source given in the request: "heads", as rows, then
// "lines", each a line's form; it satisfies the contract's Source.
class Given {
public:
    explicit Given(ArgView request);
    Given(const Given&) = delete;
    Given& operator=(const Given&) = delete;
    ~Given() = default;
    void heads(store::Vector<store::LaneHead>& rows) const;
    bool next(store::Line& line);

private:
    struct Held {
        std::string line;
        std::optional<std::string> body;
        bool forgotten = false;
    };
    std::vector<store::LaneHead> heads_;
    std::vector<Held> lines_;
    std::size_t at_ = 0;
};
static_assert(store::Source<Given>);

// The bytes of the lines a request gives under key, each line and body;
// the operation's budget over them.
std::size_t lines_budget(ArgView request, std::string_view key);

}  // namespace qiven3::drive::wire
