// store/exchange's budget and fault runs, on the bed and steps of exchange_bed.hpp, which describes them.

#include <iostream>

#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "store/errors.hpp"
#include "support/faults/faults.hpp"
#include "support/stores/exchange_bed.hpp"

using namespace qiven3::test::exchange_bed;  // NOLINT(google-build-using-namespace)

namespace {

constexpr std::size_t own_bytes = qiven3::store::operation_base;

}  // namespace

QIVEN3_CASE(store_exchange, budget) {
    const auto b = std::make_unique<Bed>();
    CHECK_EQ(sqlite::Reader::pages_for(b->scopes, b->after), own_bytes + 57U + 98U);
    CHECK_EQ(sqlite::Reader::pages_for(std::nullopt, {}), own_bytes);
    const std::vector<std::string_view> three = {"project:a", "project:b", "project:c"};
    const std::vector<AfterView> two = {AfterView{"project:a", "x", 1}, AfterView{"project:b", "y", 2}};
    CHECK_EQ(sqlite::Reader::pages_for(three, two), own_bytes + (3U * 57U) + (2U * 98U));
    for (const Step& s : steps()) {
        b->fresh_engine(s.side != Side::importing, s.side == Side::importing);
        if (s.side == Side::importing) {
            b->empty_target();
        }
        const std::size_t opened = b->engine->heap().peak();
        if (s.made) {
            b->make_export();
        }
        const Chars c = b->caller();
        const Ran r = s.run(*b, c);
        const sqlite::Slots slots = b->engine->slots();
        const std::size_t engine = b->engine->heap().peak() + (slots.peak * slots.size);
        const std::size_t engine_within = qiven3::store::engine_budget * (s.side == Side::exporting ? 2U : 1U);
        CHECK(r.own <= own_bytes);
        CHECK(r.own > 0 || s.name == "heads");
        CHECK_EQ(r.result, std::string(s.want));
        CHECK(r.caller <= call_bytes);
        CHECK(engine <= engine_within);
        std::cout << "store_exchange.budget: " << s.name << ": " << r.own << " counted within " << own_bytes << ", "
                  << r.caller << " on the caller's arena; engine peak " << engine << ", " << slots.peak
                  << " slots of it (reopened, the heap's " << opened << ") within " << engine_within << "; "
                  << r.result << "\n";
    }
}

#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
namespace {

using qiven3::memory::MemoryError;
using qiven3::platform::PlatformError;
using qiven3::checked::Header;
using qiven3::store::Heads;
using qiven3::store::RangeView;
using qiven3::store::Sizes;

std::uintmax_t wal_bytes(const std::string& path) {
    std::error_code error;
    const std::uintmax_t n = fs::file_size(fs::path(path + "-wal"), error);
    return error ? 0 : n;
}

// The source as its reads give it: its heads, forget count, size, count
// and a scan's ids; or what a read raised.
std::string fingerprint(Store& store) {
    try {
        std::vector<std::byte> pages(std::size_t{1} << 20U);
        Arena arena(pages);
        const Chars chars(arena);
        std::string out;
        Vector<LaneHead> rows(chars);
        const Heads hs = store.heads(std::nullopt, rows);
        for (const LaneHead& r : rows) {
            out.append(r.scope.view()).append("/").append(r.lane.view()).append("=").append(r.head.id.view());
            out.append("@").append(std::to_string(r.head.seq)).append(";");
        }
        out.append("forgets ").append(std::to_string(hs.forgets));
        const auto sizes = std::make_unique<Sizes>(store.size());
        out.append(" size ").append(std::to_string(sizes->volume.size));
        out.append(" count ").append(std::to_string(sizes->volume.count)).append(" ids");
        auto scan = store.scan(RangeView{});
        Header h(chars);
        while (scan.next(h)) {
            out.append(" ").append(h.value(Slot::id));
        }
        return out;
    } catch (const qiven3::store::StoreError& e) {
        return "raised " + std::string(qiven3::store::name(e.kind()));
    } catch (const MemoryError&) {
        return "raised MemoryError";
    }
}

struct Typed {};  // a fault run's typed end
struct Tally {  // a step's fault runs' tallies
    std::size_t memory = 0;
    std::size_t platform = 0;
    std::size_t engine = 0;    // StoreLocked io, a failed engine call's
    std::size_t absorbed = 0;  // the step completing past a failure
    std::size_t differed = 0;  // of those, a result not the step's
};

void attach(Bed& b, qiven3::memory::Countdown* c) {
    b.attached = c;
    b.source->attach(c);
    b.target->attach(c);
    b.engine->heap().attach(c);
    sqlite::attach_calls(c, c);
    b.call_countdown = c;
    b.fake.attach(c);
}

bool injected_io(const qiven3::store::StoreError& e) {
    return qiven3::store::name(e.kind()) == "StoreLocked" && e.cause() && *e.cause() == qiven3::store::Cause::io;
}

struct After {
    std::string source;
    std::size_t lent = 0;
    std::size_t heap = 0;
};

bool held_after(Bed& b, const Step& s, const After& before, bool done) {
    b.x.reset();
    bool good = b.call->used() == 0 && b.fake.lent() == before.lent;
    if (s.side == Side::importing) {
        good = good && !b.target->connection().in_transaction() && b.heads_left == 0;
        if (done) {
            b.empty_target();
        }
        return good && b.target->verify() == Counts{} && wal_bytes(b.into) == 0;
    }
    good = good && (s.side != Side::exporting || b.engine->heap().used() == before.heap);
    good = good && !b.source->connection().in_transaction();
    return good && fingerprint(*b.source) == before.source;
}

// One step's fault runs.
void fault_step(qiven3::test::Context& qiven3_ctx, Bed& b, const Step& s, qiven3::memory::Countdown& countdown) {
    if (s.side == Side::importing) {
        b.empty_target();
    }
    After before{fingerprint(*b.source), b.fake.lent(), 0};
    before.heap = b.engine->heap().used();
    Tally t;
    bool done = false;
    std::size_t k = 0;
    std::string result;
    std::vector<std::string> results;
    const auto op = [&] {
        done = false;
        ++k;
        countdown.disarm();
        const Chars c = b.caller();
        if (s.made) {
            b.make_export();
        }
        countdown.arm(k);
        Ran r;
        try {
            r = s.run(b, c);
        } catch (const MemoryError&) {
            ++t.memory;
            throw Typed{};
        } catch (const PlatformError&) {
            ++t.platform;
            throw Typed{};
        } catch (const qiven3::store::StoreError& e) {
            if (!injected_io(e)) {
                std::cout << "store_exchange.faults: " << s.name << ": " << qiven3::store::name(e.kind()) << " "
                          << e.detail() << "\n";
                throw;
            }
            ++t.engine;
            throw Typed{};
        }
        done = true;
        result = r.result;
        if (countdown.fired()) {
            ++t.absorbed;
            results.push_back(r.result);
            throw Typed{};
        }
    };
    const auto check = [&](std::size_t) {
        const bool good = held_after(b, s, before, done);
        if (!good) {
            std::cout << "store_exchange.faults: " << s.name << ": run " << k << " left the state changed\n";
        }
        return good;
    };
    attach(b, &countdown);
    const qiven3::test::FaultRuns runs =
        qiven3::test::run_faults<Typed>(qiven3_ctx.log, "store_exchange.step", countdown, op, check);
    attach(b, nullptr);
    for (const std::string& r : results) {
        t.differed += r == result ? 0U : 1U;
    }
    std::cout << "store_exchange.faults: " << s.name << ": " << runs.typed << " runs refused or absorbed: memory "
              << t.memory << ", platform " << t.platform << ", engine call " << t.engine << ", absorbed "
              << t.absorbed << " (" << t.differed << " of another result); untyped " << runs.untyped
              << ", unchecked " << runs.unchecked << "; completed: " << result << "\n";
    CHECK(done);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK_EQ(runs.completed_at, runs.typed + 1);
    CHECK_EQ(t.memory + t.platform + t.engine + t.absorbed, runs.typed);
    CHECK_EQ(t.differed, 0U);
}
void fault_steps(qiven3::test::Context& qiven3_ctx, Side side) {
    const auto b = std::make_unique<Bed>();
    qiven3::memory::Countdown countdown;
    for (const Step& s : steps()) {
        if (s.side == side) {
            fault_step(qiven3_ctx, *b, s, countdown);
        }
    }
}

}  // namespace

QIVEN3_CASE(store_exchange, faults_export) { fault_steps(qiven3_ctx, Side::exporting); }
QIVEN3_CASE(store_exchange, faults_import) { fault_steps(qiven3_ctx, Side::importing); }
QIVEN3_CASE(store_exchange, faults_verify) { fault_steps(qiven3_ctx, Side::verifying); }
#else
QIVEN3_CASE(store_exchange, faults_export) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(store_exchange, faults_import) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(store_exchange, faults_verify) { QIVEN3_SKIP(unfaulted); }
#endif
