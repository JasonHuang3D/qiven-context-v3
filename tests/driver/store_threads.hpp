#pragma once

// The store half's requests over a thread and over many entries (S12),
// beside driver/store_ops.hpp's:
//
//   store_appender {state, batches, hold}
//       a thread of its own makes its own store object over the state, by
//       the factory's again on a fake platform of its own (the fake is
//       thread: one), appends the batches in order, each {scope, lane,
//       expected, entries}, then closes its object; after its first hold
//       batches (none where 0 or absent) it holds until released, and the
//       request returns once it holds, or has ended: {"appender"}.
//   appender_release {appender}
//       lets a holding thread go on.
//   appender_join {appender}
//       releases it and waits for its end: {"appended"}, the entries its appends
//       wrote, and "error" where one ended it: a typed error's form
//       (wire::error), else {"kind": "exception", "detail"}.
//   store_fill {store, scope, lane, count, content, mark, writer, minute}
//       count claims like the fixture A after the lane's head, each its
//       content with mark replaced by its seq, the writer given, at minute
//       and a minute more each 60, appended through the object in batches
//       of fill_batch: wire::written of the last, "appended" their count.
//   export_drain {export}
//       the export read whole, each line on a fresh arena of the export's
//       budget: {"count", "peak", "own"}, the lines, the greatest of those
//       arenas' peaks and the export's own counted peak (SQLite's; the
//       in-memory export counts none, "own" absent); "engine_before"
//       and "engine_most", the engine heap's bytes held, its page cache's
//       slots included, at the start and the greatest after a read (0 on
//       the in-memory store); on SQLite "engine_peak", the heap's and the
//       slots' marks since the engine was made.
//
// thread: an Appender's thread uses its own fake and store object alone,
// and reads its factory (again) while the drive's thread leaves it be;
// the rest one.

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "driver/store_held.hpp"
#include "driver/store_wire.hpp"
#include "memory/arena.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/stores/stores.hpp"

namespace qiven3::drive::wire {

inline constexpr std::int64_t fill_batch = 100;

// A fill's claims, made a batch at a time after the head given
// (store_fill.cpp).
class Fill {
public:
    Fill(const test::stage_book::Book& book, ArgView request, const std::optional<store::Head>& head);
    Fill(const Fill&) = delete;
    Fill& operator=(const Fill&) = delete;
    ~Fill() = default;
    // The next batch, empty once count are made; valid until the next call.
    std::span<const store::EntryView> next();
    // The head the batch next() gave last expects.
    [[nodiscard]] store::Expected expected() const noexcept;
    [[nodiscard]] std::int64_t made() const noexcept { return made_; }

private:
    const test::stage_book::Book* book_;
    std::string scope_, lane_, content_, mark_, writer_;
    std::int64_t count_ = 0;
    std::int64_t first_ = 1;  // the first claim's seq
    std::int64_t minute_ = 0;
    std::int64_t made_ = 0;
    std::optional<std::string> expected_;  // the head before the batch
    std::optional<std::string> last_;      // the last claim's id
    std::vector<std::string> lines_;
    std::vector<std::optional<std::string>> bodies_;
    std::vector<store::EntryView> views_;
};

template <class Store>
Value fill(Store& object, const test::stage_book::Book& book, ArgView request) {
    const std::string scope = text(request, "scope");
    const std::string lane = text(request, "lane");
    Fill made(book, request, object.head(scope, lane));
    Value out;
    for (std::span<const store::EntryView> batch = made.next(); !batch.empty(); batch = made.next()) {
        out = written(object.append(scope, lane, made.expected(), batch));
    }
    out.set("appended", made.made());
    return out;
}

// held(): the engine heap's bytes held now, its slots' included, sampled
// at the start and after each read.
template <class Export, class Held>
Value drain(Export& x, const Held& held) {
    // On the heap: the export's budget is past the frame bound.
    std::vector<std::byte> pages(store::export_budget);
    std::int64_t count = 0;
    std::size_t most = 0;
    const std::size_t before = held();
    std::size_t engine = before;
    for (bool more = true; more;) {
        memory::Arena arena(pages);
        store::Line line(arena);
        more = x.next(line);
        most = std::max(most, arena.peak());
        engine = std::max(engine, held());
        count += more ? 1 : 0;
    }
    Value out;
    out.set("count", count);
    out.set("peak", static_cast<std::int64_t>(most));
    if constexpr (requires { x.peak(); }) {
        out.set("own", static_cast<std::int64_t>(x.peak()));
    }
    out.set("engine_before", static_cast<std::int64_t>(before));
    out.set("engine_most", static_cast<std::int64_t>(engine));
    return out;
}

}  // namespace qiven3::drive::wire

namespace qiven3::drive {

// A thread appending through a store object of its own over factory F's
// state, holding after its first hold batches until released; released
// and joined by join() or at its end.
template <class F>
class Appender {
public:
    // Returns once the thread holds, or has ended, where a hold is given.
    Appender(F& factory, std::string root, ArgView request)
        : root_(std::move(root)), hold_(integer(request, "hold", 0)) {
        for (const ArgView b : argument(request, "batches").as_array("batches")) {
            batches_.push_back(std::make_unique<Batch>(b));
        }
        thread_ = std::thread([this, &factory] { run(factory); });
        if (hold_ > 0) {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait(lock, [this] { return holding_ || ended_; });
        }
    }
    Appender(const Appender&) = delete;
    Appender& operator=(const Appender&) = delete;
    Appender(Appender&&) = delete;
    Appender& operator=(Appender&&) = delete;
    ~Appender() {
        if (thread_.joinable()) {
            release();
            thread_.join();
        }
    }

    void release() {
        const std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }
    Value join() {
        release();
        thread_.join();
        Value out;
        out.set("appended", appended_);
        if (error_) {
            out.set("error", *error_);
        }
        return out;
    }

private:
    struct Batch {
        explicit Batch(ArgView b) : scope(text(b, "scope")), lane(text(b, "lane")), entries(b, "entries") {
            if (b.find("expected")) {
                expected = text(b, "expected");
            }
        }
        std::string scope;
        std::string lane;
        std::optional<std::string> expected;
        wire::Entries entries;
    };

    void run(F& factory) {
        try {
            platform::fake::Config config;
            config.root = root_;
            const auto fake = std::make_unique<platform::fake::FakePlatform>(config);
            const auto object = std::make_unique<typename F::Store>(factory.again(*fake));
            std::int64_t done = 0;
            for (const std::unique_ptr<Batch>& b : batches_) {
                const store::Expected expected =
                    b->expected ? store::Expected(*b->expected) : store::Expected(std::nullopt);
                (void)object->append(b->scope, b->lane, expected, b->entries.views());
                appended_ += static_cast<std::int64_t>(b->entries.views().size());
                if (++done == hold_) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    holding_ = true;
                    changed_.notify_all();
                    changed_.wait(lock, [this] { return released_; });
                }
            }
            (void)object->close();
        } catch (const store::StoreError& e) {
            error_ = wire::error(e);
        } catch (const std::exception& e) {
            error_ = plain("exception", e.what());
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        ended_ = true;
        changed_.notify_all();
    }

    std::string root_;
    std::int64_t hold_;
    std::vector<std::unique_ptr<Batch>> batches_;
    std::int64_t appended_ = 0;   // the thread's, read after join
    std::optional<Value> error_;  // the thread's, read after join
    std::mutex mutex_;            // over the three flags
    std::condition_variable changed_;
    bool holding_ = false;
    bool released_ = false;
    bool ended_ = false;
    std::thread thread_;  // last: started once the rest is made
};

// The requests, on the drive D's tables (driver/store_held.hpp), its
// factory D::Factory.
template <class D>
std::optional<Value> start_appender(D& d, memory::Arena&, ArgView request) {
    const std::int64_t state = integer(request, "state");
    auto made = std::make_unique<Appender<typename D::Factory>>(*held::named(d.states(), request, "state").object,
                                                                 test::stores::text(d.site().root()), request);
    return held::id_value("appender", held::keep(d, d.appenders(), std::move(made), state));
}

template <class D>
std::optional<Value> release_appender(D& d, memory::Arena&, ArgView request) {
    held::named(d.appenders(), request, "appender").object->release();
    return std::nullopt;
}

template <class D>
std::optional<Value> join_appender(D& d, memory::Arena&, ArgView request) {
    Value out = held::named(d.appenders(), request, "appender").object->join();
    d.appenders().erase(integer(request, "appender"));
    return out;
}

template <class D>
std::optional<Value> fill_lane(D& d, memory::Arena&, ArgView request) {
    return wire::fill(*held::named(d.objects(), request, "store").object, d.site().book(), request);
}

template <class D>
std::optional<Value> drain_export(D& d, memory::Arena&, ArgView request) {
    auto& x = *held::named(d.exports(), request, "export").object;
    if constexpr (D::Factory::own == test::stores::Own::sqlite) {
        auto& engine = d.site().engine();
        const auto held = [&engine] {
            const store::sqlite::Slots s = engine.slots();
            return engine.heap().used() + (s.used * s.size);
        };
        Value out = wire::drain(x, held);
        const store::sqlite::Slots s = engine.slots();
        out.set("engine_peak", static_cast<std::int64_t>(engine.heap().peak() + (s.peak * s.size)));
        return out;
    } else {
        return wire::drain(x, [] { return std::size_t{0}; });
    }
}

}  // namespace qiven3::drive
