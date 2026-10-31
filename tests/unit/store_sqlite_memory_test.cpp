// store/sqlite's budget and fault runs, on the bed and steps of sqlite_bed.hpp, which describes them.

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "support/faults/faults.hpp"
#include "support/faults/parts.hpp"
#include "support/stores/sqlite_bed.hpp"

namespace qiven3::test {  // A group's fault runs, a part's range of them, by store_sqlite_parts_test.cpp.
void store_sqlite_fault_steps(Context& qiven3_ctx, std::string_view group, FaultRange range); }

using namespace qiven3::test::sqlite_bed;  // NOLINT(google-build-using-namespace)
using qiven3::memory::MemoryError;
using qiven3::platform::PlatformError;
using qiven3::store::StoreError;

QIVEN3_CASE(store_sqlite, budget) {
    const auto b = std::make_unique<Bed>();
    for (const Step& s : steps()) {
        b->fresh_engine(s.sort != Sort::create);
        const std::size_t opened = b->engine->heap().peak();
        const Chars c = b->caller();
        const Ran r = s.run(*b, c);
        const qiven3::store::sqlite::Slots slots = b->engine->slots();
        const std::size_t engine = b->engine->heap().peak() + (slots.peak * slots.size);
        const std::size_t within = budget(*b, s);
        const std::size_t engine_within = qiven3::store::engine_budget * (s.connections == 0 ? 1 : s.connections);
        CHECK(r.store <= within);
        CHECK(r.caller <= call_bytes);
        CHECK(engine <= engine_within);
        CHECK_EQ(b->call->used(), 0U);
        std::cout << "store_sqlite.budget: " << s.name << ": " << r.store << " bytes counted, " << r.caller
                  << " on the caller's arena, within " << within << "; engine peak " << engine << ", "
                  << slots.peak << " slots of it (reopened, the heap's " << opened << ") within " << engine_within
                  << "\n";
    }
    CHECK(b->store->verify() == (qiven3::store::Counts{5, 3, 1, 1}));  // the course's
    CHECK(Store::open(b->path, false, true, b->fake).verify() == b->store->verify());
}

#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
namespace {

// The store as its reads give it: its heads, forget count, size, count and
// a scan's ids; or what a read raised.
std::string fingerprint(Store* store) {
    if (store == nullptr) {
        return "none";
    }
    try {
        std::vector<std::byte> pages(std::size_t{1} << 20U);
        qiven3::memory::Arena arena(pages);
        const Chars chars(arena);
        std::string out;
        Vector<LaneHead> rows(chars);
        const Heads hs = store->heads(std::nullopt, rows);
        for (const LaneHead& r : rows) {
            out.append(r.scope.view()).append("/").append(r.lane.view()).append("=").append(r.head.id.view());
            out.append("@").append(std::to_string(r.head.seq)).append(";");
        }
        out.append("forgets ").append(std::to_string(hs.forgets));
        const auto sizes = std::make_unique<Sizes>(store->size());
        out.append(" size ").append(std::to_string(sizes->volume.size));
        out.append(" count ").append(std::to_string(sizes->volume.count)).append(" ids");
        auto scan = store->scan(RangeView{});
        qiven3::store::Record h(chars);
        while (scan.next(h)) {
            out.append(" ").append(h.id.view());
        }
        return out;
    } catch (const StoreError& e) {
        return "raised " + std::string(qiven3::store::name(e.kind()));
    } catch (const MemoryError&) {
        return "raised MemoryError";
    }
}

struct Typed {};  // a fault run's typed end
struct Tally {    // a step's fault runs' tallies
    std::size_t memory = 0;
    std::size_t platform = 0;
    std::size_t engine = 0;    // StoreLocked io, a failed engine call's
    std::size_t absorbed = 0;  // the step completing past a failed allocation
    std::size_t differed = 0;  // of those, a result not the step's
    std::size_t left = 0;      // a create's refused runs leaving its file
};

void attach(Bed& b, qiven3::memory::Countdown* c) {
    if (b.store) {
        b.store->attach(c);
    }
    b.engine->heap().attach(c);
    sqlite::attach_calls(c, c);
    b.call_countdown = c;
    b.fake.attach(c);
}

bool injected_io(const StoreError& e) {
    return qiven3::store::name(e.kind()) == "StoreLocked" && e.cause() &&
           *e.cause() == qiven3::store::Cause::io;
}

// One step's fault runs, those of the part's range; its tallies, and
// whether each run held.
bool fault_step(qiven3::test::Context& qiven3_ctx, Bed& b, const Step& s, qiven3::memory::Countdown& countdown,
                qiven3::test::FaultRange range) {
    const std::string before = fingerprint(b.store.get());
    if (s.sort == Sort::write) {
        b.keep();
    }
    const std::size_t lent = b.fake.lent();
    Tally t;
    qiven3::test::PartRuns part(range);
    bool done = false;
    bool absorbed = false;
    std::string result;
    std::vector<std::string> results;
    const auto op = [&] {
        done = false;
        absorbed = false;
        part.enter<Typed>(countdown);
        const Chars c = b.caller();
        Ran r;
        try {
            r = s.run(b, c);
        } catch (const MemoryError&) {
            ++t.memory;
            throw Typed{};
        } catch (const PlatformError&) {
            ++t.platform;
            throw Typed{};
        } catch (const StoreError& e) {
            if (!injected_io(e)) {
                std::cout << "store_sqlite.faults: " << s.name << ": " << qiven3::store::name(e.kind()) << " "
                          << e.detail() << "\n";
                throw;
            }
            ++t.engine;
            throw Typed{};
        }
        done = true;
        result = r.result;
        if (countdown.fired()) {
            absorbed = true;
            ++t.absorbed;
            results.push_back(r.result);
            throw Typed{};
        }
    };
    const auto check = [&](std::size_t) {
        if (part.skipped()) {
            return true;
        }
        if (s.sort == Sort::create) {
            if (!done) {
                std::error_code error;
                t.left += fs::exists(b.path, error) ? 1U : 0U;
            }
            if (!done || absorbed) {
                b.store.reset();
                std::error_code error;
                fs::remove_all(b.root / "s", error);
            }
            return b.call->used() == 0 && (done && !absorbed ? true : b.fake.lent() == lent);
        }
        if (done && !absorbed) {
            return b.call->used() == 0 && b.fake.lent() == lent;
        }
        if (absorbed && s.sort == Sort::write) {
            b.put_back();
        }
        const bool back = b.call->used() == 0 && b.fake.lent() == lent;
        return back && !b.store->connection().in_transaction() && fingerprint(b.store.get()) == before;
    };
    attach(b, &countdown);
    const qiven3::test::FaultRuns runs =
        qiven3::test::run_faults<Typed>(qiven3_ctx.log, "store_sqlite.step", countdown, op, check);
    attach(b, nullptr);
    for (const std::string& r : results) {
        t.differed += r == result ? 0U : 1U;
    }
    std::cout << "store_sqlite.faults: " << s.name << ": " << runs.typed << " runs refused or absorbed: memory "
              << t.memory << ", platform " << t.platform << ", engine call " << t.engine << ", absorbed "
              << t.absorbed << " (" << t.differed << " of another result), a create's file left " << t.left
              << "; untyped " << runs.untyped << ", unchecked " << runs.unchecked << "; executing " << range.from
              << " to " << part.last(runs) << ", " << part.executed() << " runs\n";
    CHECK(done);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK_EQ(runs.completed_at, runs.typed + 1);
    CHECK(part.held(runs));
    CHECK_EQ(t.memory + t.platform + t.engine + t.absorbed, runs.typed - part.before());
    CHECK_EQ(t.left, 0U);  // a refused create removes its file
    return runs.untyped == 0 && runs.unchecked == 0;
}

}  // namespace

// The steps in order on one store: those of the case's group ("create", "forget" or "rest", the other
// steps) faulted, the runs of the part's range alone, the others run once (store_sqlite_parts_test.cpp).
void qiven3::test::store_sqlite_fault_steps(Context& qiven3_ctx, std::string_view group, FaultRange range) {
    const auto b = std::make_unique<Bed>();
    qiven3::memory::Countdown countdown;
    for (const Step& s : steps()) {
        if ((s.name == "create" || s.name == "forget" ? std::string_view(s.name) : "rest") != group) {
            const Chars c = b->caller();
            (void)s.run(*b, c);
        } else if (!fault_step(qiven3_ctx, *b, s, countdown, range)) {
            std::cout << "store_sqlite.faults: " << s.name << " failed\n";
        }
    }
    CHECK(b->store->verify() == (qiven3::store::Counts{5, 3, 1, 1}));  // the course's
    CHECK(Store::open(b->path, false, true, b->fake).verify() == b->store->verify());
}
#else
void qiven3::test::store_sqlite_fault_steps(Context& /*qiven3_ctx*/, std::string_view /*group*/, FaultRange) {
    QIVEN3_SKIP(unfaulted);
}
#endif
QIVEN3_CASE(store_sqlite, faults) { qiven3::test::store_sqlite_fault_steps(qiven3_ctx, "rest", {}); }
