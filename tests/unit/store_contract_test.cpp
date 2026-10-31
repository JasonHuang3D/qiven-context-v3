// store/contract's cases. Evaluated by the compiler: a minimal conforming
// store, a scan and an export that the concepts accept, and one plant each
// that they refuse (a copyable store, a member missing or of another type,
// capabilities that are no constant, a copyable scan, an export that does
// not close, an import of one source type, read_only not const, a hook
// missing); the operations' names, what a read-only store serves and the
// kinds each raises; the capabilities' applicability; the size limits,
// their volume and the maximum; the budgets. At run time: the minimal store driven through
// the concept, each operation once; the gate's three refusals in order;
// a line's strings on the caller's arena; the fault run over none. The
// runner's count after each case holds too.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven/runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace store = qiven3::store;
using store::Operation;
using store::StoreError;

constexpr std::string_view path = "data/store.db";
constexpr std::string_view id_a = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

// A move-only token, never holding anything, and a copyable one in its
// place for a plant.
using Token = std::unique_ptr<int>;
template <bool Copyable>
using TokenOf = std::conditional_t<Copyable, int, Token>;

template <bool Copyable = false>
struct MinimalScan {
    [[nodiscard]] bool next(store::Record& /*out*/) {
        return left-- > 0;
    }
    int left = 0;
    TokenOf<Copyable> token{};
};

template <bool Closes = true>
struct MinimalExport {
    store::Snapshot heads(store::Vector<store::LaneHead>& rows) const {
        rows.clear();
        return {};
    }
    [[nodiscard]] static bool next(store::Line& /*line*/) noexcept { return false; }
    void close() noexcept
        requires Closes
    {
        closed = true;
    }
    bool closed = false;
    Token token;
};

// Each plant changes one thing of the minimal store.
enum class Plant {
    none, copyable, no_verify, append_void, header_bool, scan_copyable, export_no_close, import_one, read_only_mutable,
    limits_ref, no_each_keyed
};

// The minimal conforming store: it holds a count and a gate.
template <Plant P = Plant::none>
class Minimal {
public:
    static constexpr store::Capabilities capabilities{};
    explicit Minimal(bool read_only) noexcept : gate_(read_only) {}

    store::Written append(std::string_view /*scope*/, std::string_view /*lane*/, store::Expected /*expected*/,
                          std::span<const store::EntryView> entries)
        requires(P != Plant::append_void)
    {
        gate_.enter(Operation::append, path);
        count_ += entries.size();
        return {store::Head{store::Id::of(id_a), static_cast<std::int64_t>(count_)}, volume(), {}};
    }
    void append(std::string_view, std::string_view, store::Expected, std::span<const store::EntryView>)
        requires(P == Plant::append_void)
    {}
    std::optional<store::Head> head(std::string_view /*scope*/, std::string_view /*lane*/) {
        gate_.enter(Operation::head, path);
        return count_ == 0 ? std::nullopt : std::optional<store::Head>(store::Head{store::Id::of(id_a), 1});
    }
    store::Heads heads(store::Scopes /*scopes*/, store::Vector<store::LaneHead>& rows) {
        gate_.enter(Operation::heads, path);
        rows.clear();
        return {};
    }
    void header(std::string_view id, store::String& /*line*/)
        requires(P != Plant::header_bool)
    {
        gate_.enter(Operation::header, path);
        throw StoreError::not_found("header", path, id);
    }
    bool header(std::string_view, store::String&)
        requires(P == Plant::header_bool)
    {
        return false;
    }
    bool body(std::string_view /*id*/, store::String& record) {
        gate_.enter(Operation::body, path);
        record.clear();
        return false;
    }
    bool forgotten(std::string_view /*id*/) {
        gate_.enter(Operation::forgotten, path);
        return false;
    }
    bool line(std::string_view /*id*/, store::String& /*line*/) {
        gate_.enter(Operation::header, path);
        return false;
    }
    void targets(std::string_view /*id*/, std::string_view /*scope*/, store::Vector<store::Id>& out) {
        gate_.enter(Operation::scan, path);
        out.clear();
    }
    void keyed(std::string_view /*scope*/, std::string_view /*kind*/, store::Vector<store::Id>& out) {
        gate_.enter(Operation::scan, path);
        out.clear();
    }
    template <class F>
        requires(P != Plant::no_each_keyed)
    bool each_keyed(std::string_view /*scope*/, std::string_view /*kind*/, F&& /*f*/) {
        gate_.enter(Operation::scan, path);
        return false;
    }
    MinimalScan<P == Plant::scan_copyable> scan(const store::RangeView& /*range*/) {
        gate_.enter(Operation::scan, path);
        return {};
    }
    store::Written forget(std::string_view /*scope*/, std::string_view /*lane*/, store::Expected /*expected*/,
                          std::span<const store::EntryView> entries, std::span<const store::GuardView> /*guard*/,
                          store::Vector<store::Id>& erased) {
        gate_.enter(Operation::forget, path);
        erased.clear();
        count_ += entries.size();
        return {store::Head{store::Id::of(id_a), static_cast<std::int64_t>(count_)}, volume(), {}};
    }
    store::Purge purge() {
        gate_.enter(Operation::purge, path);
        return {};
    }
    MinimalExport<P != Plant::export_no_close> export_(store::Scopes /*scopes*/,
                                                       std::span<const store::AfterView> /*after*/) {
        gate_.enter(Operation::export_, path);
        return {};
    }
    template <store::Source R>
    store::Imported import_(R& source, store::Vector<store::LaneHead>& rows)
        requires(P != Plant::import_one)
    {
        gate_.enter(Operation::import_, path);
        source.heads(rows);
        return {};
    }
    store::Imported import_(MinimalExport<>& /*source*/, store::Vector<store::LaneHead>& /*rows*/)
        requires(P == Plant::import_one)
    {
        return {};
    }
    store::Sizes size() {
        gate_.enter(Operation::size, path);
        return {volume(), {}, 0};
    }
    store::Counts verify()
        requires(P != Plant::no_verify)
    {
        gate_.enter(Operation::verify, path);
        return {count_, 0, 0, count_ == 0 ? 0U : 1U};
    }
    store::Purge close() {
        gate_.enter(Operation::close, path);
        gate_.close();
        static_cast<void>(token_);
        return {};
    }
    [[nodiscard]] bool read_only() const noexcept
        requires(P != Plant::read_only_mutable)
    {
        return gate_.read_only();
    }
    [[nodiscard]] bool read_only() noexcept
        requires(P == Plant::read_only_mutable)
    {
        return gate_.read_only();
    }
    [[nodiscard]] store::Limits limits() const noexcept
        requires(P != Plant::limits_ref)
    {
        return limits_;
    }
    [[nodiscard]] const store::Limits& limits() const noexcept
        requires(P == Plant::limits_ref)
    {
        return limits_;
    }

private:
    [[nodiscard]] store::Volume volume() const noexcept { return store::measure(count_ * 100U, count_, limits_); }

    store::Gate gate_;
    store::Limits limits_;
    std::uint64_t count_ = 0;
    TokenOf<P == Plant::copyable> token_{};
};

// The concepts accept the minimal types and refuse each plant.
static_assert(store::Store<Minimal<>>);
static_assert(store::Scan<MinimalScan<>> && store::Export<MinimalExport<>> && store::Source<MinimalExport<>>);
static_assert(store::Source<store::detail::SourceArchetype> && store::Hooks<store::NoHooks>);
static_assert(!store::Store<Minimal<Plant::copyable>>);
static_assert(!store::Store<Minimal<Plant::no_verify>>);
static_assert(!store::Store<Minimal<Plant::append_void>>);
static_assert(!store::Store<Minimal<Plant::header_bool>>);
static_assert(!store::Store<Minimal<Plant::scan_copyable>> && !store::Scan<MinimalScan<true>>);
static_assert(!store::Store<Minimal<Plant::export_no_close>> && !store::Export<MinimalExport<false>>);
static_assert(!store::Store<Minimal<Plant::import_one>>);
static_assert(!store::Store<Minimal<Plant::read_only_mutable>>);
static_assert(!store::Store<Minimal<Plant::limits_ref>>);
static_assert(!store::Store<Minimal<Plant::no_each_keyed>>);

// Capabilities as a constant of their type, or refused.
struct Constant {
    static constexpr store::Capabilities capabilities{true, true, true};
};
struct NotConstant {
    static inline const store::Capabilities capabilities{};
};
struct OtherType {
    static constexpr bool capabilities = true;
};
static_assert(store::detail::Declares<Constant> && store::detail::Declares<Minimal<>>);
static_assert(!store::detail::Declares<NotConstant> && !store::detail::Declares<OtherType>);

// A hook missing.
struct NoRebuilt {
    void begun() {}
    void written() {}
    void committed() {}
};
static_assert(!store::Hooks<NoRebuilt>);

// The names, as the text spells them, export and import among them.
constexpr bool names_vector() {
    for (std::size_t i = 0; i < store::operation_names.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (store::operation_names[i] == store::operation_names[j]) {
                return false;
            }
        }
    }
    return store::name(Operation::append) == "append" && store::name(Operation::export_) == "export" &&
           store::name(Operation::import_) == "import" && store::name(Operation::close) == "close" &&
           store::name(Operation::forgotten) == "forgotten";
}
static_assert(names_vector());

inline constexpr std::array<Operation, 14> operations = {
    Operation::append, Operation::head,    Operation::heads,   Operation::header, Operation::body,
    Operation::forgotten, Operation::scan, Operation::forget,  Operation::purge,  Operation::export_,
    Operation::import_, Operation::size,   Operation::verify,  Operation::close};

// A read-only store serves ten operations and refuses append, forget,
// purge and import.
constexpr bool read_only_vector() {
    std::size_t served = 0;
    for (const Operation op : operations) {
        served += store::serves_read_only(op) ? 1U : 0U;
    }
    return served == 10 && !store::serves_read_only(Operation::append) &&
           !store::serves_read_only(Operation::forget) && !store::serves_read_only(Operation::purge) &&
           !store::serves_read_only(Operation::import_) && store::serves_read_only(Operation::export_) &&
           store::serves_read_only(Operation::verify) && store::serves_read_only(Operation::close);
}
static_assert(read_only_vector());

// The typed errors by operation, as the text lists them.
constexpr bool raises_vector() {
    using store::Full;
    using store::Kind;
    for (const Operation op : operations) {
        if (!store::raises(op, Kind::busy) || !store::raises(op, Kind::locked) || !store::raises(op, Kind::corrupt) ||
            !store::raises(op, Kind::store_error)) {
            return false;
        }
    }
    std::size_t conflict = 0, invalid = 0, full = 0, not_found = 0, not_empty = 0, store_full = 0, disk_full = 0;
    for (const Operation op : operations) {
        conflict += store::raises(op, Kind::head_conflict) ? 1U : 0U;
        invalid += store::raises(op, Kind::entry_invalid) ? 1U : 0U;
        full += store::raises(op, Kind::full) ? 1U : 0U;
        not_found += store::raises(op, Kind::not_found) ? 1U : 0U;
        not_empty += store::raises(op, Kind::not_empty) ? 1U : 0U;
        store_full += store::raises(op, Full::store_full) ? 1U : 0U;
        disk_full += store::raises(op, Full::disk_full) ? 1U : 0U;
    }
    return conflict == 2 && store::raises(Operation::forget, Kind::head_conflict) && invalid == 3 &&
           store::raises(Operation::import_, Kind::entry_invalid) && full == 3 && not_found == 3 &&
           store::raises(Operation::forgotten, Kind::not_found) && not_empty == 1 &&
           store::raises(Operation::import_, Kind::not_empty) && store_full == 1 &&
           store::raises(Operation::append, Full::store_full) && disk_full == 3 &&
           store::raises(Operation::import_, Full::disk_full) && !store::raises(Operation::scan, Kind::not_found);
}
static_assert(raises_vector());

// A case is applicable when the store has each capability it needs.
constexpr bool capabilities_vector() {
    const store::Capabilities none{}, all{true, true, true}, durable{true, false, false};
    const store::Capabilities files{false, true, false}, processes{false, false, true}, no_processes{true, true, false};
    return store::applicable(none, none) && store::applicable(all, all) && !store::applicable(none, durable) &&
           store::applicable(durable, durable) && !store::applicable(durable, files) &&
           store::applicable(all, durable) && !store::applicable(no_processes, processes) &&
           Minimal<>::capabilities == none;
}
static_assert(capabilities_vector());

// The format's numbers by default; the suite's smaller ones valid; ordered.
constexpr bool limits_vector() {
    const store::Limits c15{};
    const store::Limits suite{64U * 1024U, 96U * 1024U, 128U * 1024U, 10, 15, 20};
    store::Limits disordered = suite;
    disordered.warning_count = 9;
    store::Limits zero = suite;
    zero.target_size = 0;
    return c15.target_size == 536'870'912U && c15.warning_size == 671'088'640U && c15.max_size == 805'306'368U &&
           c15.target_count == 400'000U && c15.warning_count == 480'000U && c15.max_count == 550'000U &&
           store::valid(c15) && store::valid(suite) && !store::valid(disordered) && !store::valid(zero);
}
static_assert(limits_vector());

// Past a threshold, by size or by count apart; at it, not past.
constexpr bool volume_vector() {
    const store::Limits l{100, 150, 200, 10, 15, 20};
    const store::Volume at = store::measure(100, 10, l);
    const store::Volume by_size = store::measure(101, 1, l);
    const store::Volume by_count = store::measure(1, 11, l);
    const store::Volume warned = store::measure(151, 16, l);
    const store::Volume warned_by_count = store::measure(1, 16, l);
    return at == store::Volume{100, 10, false, false} && by_size == store::Volume{101, 1, true, false} &&
           by_count == store::Volume{1, 11, true, false} && warned == store::Volume{151, 16, true, true} &&
           warned_by_count == store::Volume{1, 16, true, true} &&
           !store::over_maximum(200, 20, l) && store::over_maximum(201, 1, l) && store::over_maximum(1, 21, l);
}
static_assert(volume_vector());

// The budgets: the lines given summed, checked; 1 MiB plus 4 times them.
constexpr bool budget_vector() {
    const std::array<store::EntryView, 3> entries = {store::EntryView{"abc", "de"},
                                                     store::EntryView{"fgh", std::nullopt}, store::EntryView{"", ""}};
    const std::optional<std::size_t> lines = store::given(entries);
    const std::size_t max = SIZE_MAX;
    // Sizes past SIZE_MAX in sum, by lines and by a line and its record,
    // never read; each view at most PTRDIFF_MAX long, as libc++ checks.
    const std::string_view half(id_a.data(), max / 2);
    const std::array<store::EntryView, 3> lines_past = {
        store::EntryView{half, std::nullopt}, store::EntryView{half, std::nullopt},
        store::EntryView{half, std::nullopt}};
    const std::array<store::EntryView, 2> record_past = {store::EntryView{half, std::nullopt},
                                                         store::EntryView{half, half}};
    return lines == 8U && store::operation_budget(*lines) == 1'048'576U + 32U &&
           store::operation_budget(max / 4) == std::nullopt && store::operation_budget((max / 4) + 1) == std::nullopt &&
           store::operation_budget(0) == 1'048'576U &&
           store::engine_budget == 3'096'576U && store::export_budget == 2'097'152U &&
           store::given(std::span<const store::EntryView>{}) == 0U && store::given(lines_past) == std::nullopt &&
           store::given(record_past) == std::nullopt;
}
static_assert(budget_vector());

// A purge: done by default; pending with its reason.
constexpr bool purge_vector() {
    const store::Purge done{};
    const store::Purge pending = store::Purge::pending("SQLITE_IOERR_SHORT_READ");
    return done.done && done.reason.view().empty() && !pending.done &&
           pending.reason.view() == "SQLITE_IOERR_SHORT_READ";
}
static_assert(purge_vector());

// Code bound to the contract alone: each operation once, in the text's
// order, close last.
template <store::Store S>
std::size_t drive(S& s, qiven3::memory::Arena& arena) {
    const qiven3::memory::Allocator<char> chars(arena);
    store::Vector<store::LaneHead> rows{qiven3::memory::Allocator<store::LaneHead>(arena)};
    store::Vector<store::Id> erased{qiven3::memory::Allocator<store::Id>(arena)};
    store::String record(chars);
    store::String line(chars);
    store::Record scanned(chars);
    const std::array<store::EntryView, 2> entries = {store::EntryView{"{}", "{}"},
                                                     store::EntryView{"{}", std::nullopt}};
    std::size_t n = 0;
    n += s.append("person", "implementer", std::nullopt, entries).head.seq == 2 ? 1U : 0U;
    n += s.head("person", "implementer").has_value() ? 1U : 0U;
    n += s.heads(std::nullopt, rows).purge.done ? 1U : 0U;
    try {
        s.header(id_a, line);
    } catch (const StoreError& e) {
        n += e.kind() == store::Kind::not_found && e.id() == id_a ? 1U : 0U;
    }
    n += s.body(id_a, record) ? 0U : 1U;
    n += s.forgotten(id_a) ? 0U : 1U;
    auto scan = s.scan(store::RangeView{});
    n += scan.next(scanned) ? 0U : 1U;
    n += s.forget("person", "implementer", id_a, entries, {}, erased).head.seq == 4 ? 1U : 0U;
    n += s.purge().done ? 1U : 0U;
    auto exported = s.export_(std::nullopt, {});
    n += exported.heads(rows).forgets == 0 ? 1U : 0U;
    n += s.import_(exported, rows).counts == store::Counts{} ? 1U : 0U;
    exported.close();
    n += s.size().volume.count == 4 ? 1U : 0U;
    n += s.verify().entries == 4 ? 1U : 0U;
    n += s.limits() == store::Limits{} && !s.read_only() && !S::capabilities.durable ? 1U : 0U;
    n += s.close().done ? 1U : 0U;
    return n;
}

// The minimal store through the concept: fifteen answers as given.
QIVEN_CASE(store_contract, smoke) {
    std::vector<std::byte> pages(16384);
    qiven3::memory::Arena arena(pages);
    Minimal<> s(false);
    CHECK_EQ(drive(s, arena), 15U);
    // The hooks run as calls that do nothing; the partial set too.
    const store::NoHooks hooks;
    hooks.begun();
    hooks.written();
    hooks.committed();
    hooks.rebuilt();
    NoRebuilt partial;
    partial.begun();
    partial.written();
    partial.committed();
    // The handles move.
    MinimalScan<> scan;
    MinimalScan<> moved(std::move(scan));
    scan = std::move(moved);
    CHECK_EQ(scan.left, 0);
}

// The gate: another thread first, then closed, then read-only; each names
// its operation and the path.
QIVEN_CASE(store_contract, gate) {
    store::Gate writable(false);
    store::Gate reading(true);
    std::size_t refused = 0, served = 0;
    for (const Operation op : operations) {
        writable.enter(op, path);
        try {
            reading.enter(op, path);
            ++served;
        } catch (const StoreError& e) {
            CHECK(e.code() == store::Code::read_only);
            CHECK_EQ(e.operation(), store::name(op));
            CHECK_EQ(e.path(), path);
            ++refused;
        }
    }
    CHECK_EQ(served, 10U);
    CHECK_EQ(refused, 4U);
    // From another thread, the thread first: before read-only, and after
    // close before closed.
    const auto from_other = [&reading] {
        std::optional<store::Code> other;
        {
            // std::thread, joined here: libc++ before 20 keeps std::jthread
            // experimental.
            std::thread other_thread([&] {
                try {
                    reading.enter(Operation::append, path);
                } catch (const StoreError& e) {
                    other = e.code();
                }
            });
            other_thread.join();
        }
        return other;
    };
    CHECK(from_other() == store::Code::thread);
    reading.close();
    for (const Operation op : operations) {
        try {
            reading.enter(op, path);
            CHECK(false);
        } catch (const StoreError& e) {
            CHECK(e.code() == store::Code::closed);
            CHECK_EQ(e.operation(), store::name(op));
        }
    }
    CHECK(from_other() == store::Code::thread);
    CHECK(reading.closed() && reading.read_only() && !writable.closed());
}

// A line takes nothing from the arena until written.
QIVEN_CASE(store_contract, line) {
    std::vector<std::byte> pages(4096);
    qiven3::memory::Arena arena(pages);
    store::Line line(arena);
    CHECK_EQ(arena.used(), 0U);
    line.header.assign(200, 'h');
    line.has_body = true;
    CHECK(arena.used() >= 200U);
    CHECK(!line.forgotten && line.body.empty());
}

// No allocation and no platform call: with an arena's countdown armed at
// its first allocation, the gate, the volume and the budgets run, and the
// run completes at once.
QIVEN_CASE(store_contract, faults) {
#if defined(QIVEN_FAULTS)
    std::vector<std::byte> pages(4096);
    qiven3::memory::Countdown countdown;
    std::optional<qiven3::memory::Arena> arena;
    std::uint64_t sum = 0;
    const auto op = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        const store::Gate gate(false);
        const store::Line line(*arena);
        for (const Operation o : operations) {
            gate.enter(o, path);
            sum +=store::measure(sum, 1, store::Limits{}).count + store::operation_budget(1).value_or(0);
        }
        sum += line.header.size();
        arena->attach(nullptr);
    };
    const auto empty = [&](std::size_t) { return true; };
    const qiven3::test::FaultRuns runs =
        qiven3::test::run_faults<StoreError>(qiven_ctx.log, "store_contract.gate", countdown, op, empty);
    CHECK_EQ(runs.typed, 0U);
    CHECK_EQ(runs.completed_at, 1U);
    CHECK_EQ(runs.untyped, 0U);
    CHECK(sum > 0);
    CHECK_EQ(arena->used(), 0U);
    arena.reset();
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}

}  // namespace
