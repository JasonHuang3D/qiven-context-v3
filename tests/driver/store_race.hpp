#pragma once

// The store half's thread requests (S21), beside driver/store_threads.hpp's:
//
//   store_race {state, scope, threads [{lane, writer}], count, content,
//               pause_ms}
//       one thread for each item, each with its own fake, stage book and
//       store object over the state (the factory's again, made one at a
//       time); once every one is made they start together and each makes
//       count successful conditional appends of one claim on its lane's
//       head: the head read, a claim like A after it (its content with
//       "{thread}" and "{number}" replaced by the thread's index and the
//       success's, from 1; its writer given; at seq / 60 minutes), the
//       append expecting that head; a HeadConflict or StoreBusy counted
//       and the head read again; pause_ms after each success. Any other
//       error ends that thread. The request returns once all have ended:
//       {"threads": [{"ids", "conflicts", "busy", "error"?}]}, the ids its
//       successes wrote, in order.
//   "elsewhere": true, on any store request
//       the request's call made on a thread of its own, joined before the
//       reply, whatever it raised raised on the drive's thread (S21's call
//       from another thread than the object's maker's).
//
// thread: each racer its own fake, book and store object; the factory's
// again under the race's lock; the rest one.

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "driver/store_held.hpp"
#include "driver/store_wire.hpp"
#include "memory/arena.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "support/checked/stores.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"

namespace qiven3::drive::wire {

// A claim like the fixture A on (scope, lane) at seq after parent, its
// content and writer given, at seq / 60 minutes (store_fill.cpp).
test::stage_book::Stored claim(const test::stage_book::Book& book, const std::string& scope,
                               const std::string& lane, const std::string& writer, const std::string& content,
                               std::int64_t seq, std::optional<std::string_view> parent);

}  // namespace qiven3::drive::wire

namespace qiven3::drive {

// call() run on a thread of its own, joined; what it raised raised here.
template <class Call>
auto elsewhere(Call&& call) -> decltype(call()) {
    std::optional<decltype(call())> out;
    std::exception_ptr raised;
    std::thread other([&] {
        try {
            out.emplace(call());
        } catch (...) {
            raised = std::current_exception();
        }
    });
    other.join();
    if (raised) {
        std::rethrow_exception(raised);
    }
    return std::move(*out);
}

// The racers over factory F's state: made, started together, joined.
template <class F>
class Race {
public:
    Race(F& factory, std::string root, ArgView request)
        : factory_(&factory), root_(std::move(root)), scope_(text(request, "scope")),
          content_(text(request, "content")), count_(integer(request, "count")),
          pause_(integer(request, "pause_ms", 5)) {
        if (count_ < 0 || pause_ < 0 || content_.find("{thread}") == std::string::npos ||
            content_.find("{number}") == std::string::npos) {
            throw RequestError("store_race: a count or pause below 0, or a content without its marks");
        }
        for (const ArgView t : argument(request, "threads").as_array("threads")) {
            racers_.push_back(std::make_unique<Racer>(text(t, "lane"), text(t, "writer")));
        }
    }
    Race(const Race&) = delete;
    Race& operator=(const Race&) = delete;
    ~Race() = default;

    Value run() {
        std::vector<std::thread> threads;
        threads.reserve(racers_.size());
        for (std::size_t i = 0; i < racers_.size(); ++i) {
            threads.emplace_back([this, i] { race(i); });
        }
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return made_ == racers_.size(); });
            go_ = true;
        }
        ready_.notify_all();
        for (std::thread& t : threads) {
            t.join();
        }
        Value list = array();
        for (const std::unique_ptr<Racer>& r : racers_) {
            Value ids = array();
            for (const std::string& id : r->ids) {
                ids.push(scalar(id));
            }
            Value item;
            item.set("ids", ids);
            item.set("conflicts", r->conflicts);
            item.set("busy", r->busy);
            if (r->error) {
                item.set("error", *r->error);
            }
            list.push(item);
        }
        Value out;
        out.set("threads", list);
        return out;
    }

private:
    struct Racer {
        Racer(std::string l, std::string w) : lane(std::move(l)), writer(std::move(w)) {}
        std::string lane;
        std::string writer;
        std::vector<std::string> ids;  // the racer's, read after join
        std::int64_t conflicts = 0;
        std::int64_t busy = 0;
        std::optional<Value> error;
    };

    static std::string marked(std::string text, std::string_view mark, std::int64_t n) {
        text.replace(text.find(mark), mark.size(), std::to_string(n));
        return text;
    }

    // pause_ milliseconds by the steady clock, the thread yielding: a
    // sleep of the system's may round up to its timer's tick (15.6 ms on
    // Windows by default), three times the pause.
    void pause() const {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(pause_);
        while (std::chrono::steady_clock::now() < until) {
            std::this_thread::yield();
        }
    }

    // One racer, made (or failed) and counted, then waiting for the rest.
    void race(std::size_t index) {
        Racer& r = *racers_[index];
        bool made = false;
        try {
            platform::fake::Config config;
            config.root = root_;
            const auto fake = std::make_unique<platform::fake::FakePlatform>(config);
            const auto book = std::make_unique<test::stage_book::Book>(*fake);
            std::unique_ptr<typename F::Store> object;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                object = std::make_unique<typename F::Store>(factory_->again(*fake));
                ++made_;
                made = true;
                ready_.notify_all();
                ready_.wait(lock, [this] { return go_; });
            }
            const std::string content = marked(content_, "{thread}", static_cast<std::int64_t>(index));
            for (std::int64_t k = 1; k <= count_; ++k) {
                append(r, *object, *book, marked(content, "{number}", k));
                pause();
            }
            (void)object->close();
        } catch (const store::StoreError& e) {
            r.error = wire::error(e);
        } catch (const std::exception& e) {
            r.error = plain("exception", e.what());
        }
        if (!made) {
            const std::lock_guard<std::mutex> lock(mutex_);
            ++made_;  // failed before it was made: the rest not held back
            ready_.notify_all();
        }
    }

    // One success: the head read, the claim after it appended expecting it,
    // again after each HeadConflict or StoreBusy.
    void append(Racer& r, typename F::Store& object, const test::stage_book::Book& book,
                const std::string& content) {
        for (;;) {
            try {
                const std::optional<store::Head> head = object.head(scope_, r.lane);
                std::optional<std::string> parent;
                if (head) {
                    parent = std::string(head->id.view());
                }
                const test::stage_book::Stored s =
                    wire::claim(book, scope_, r.lane, r.writer, content, head ? head->seq + 1 : 1, parent);
                const store::EntryView entry{s.line, s.record ? std::optional<std::string_view>(*s.record)
                                                              : std::nullopt};
                const store::Written w = object.append(scope_, r.lane, parent ? store::Expected(*parent) : store::Expected(std::nullopt), {&entry, 1});
                r.ids.emplace_back(w.head.id.view());
                return;
            } catch (const store::StoreError& e) {
                if (e.kind() == store::Kind::head_conflict) {
                    ++r.conflicts;
                } else if (e.kind() == store::Kind::busy) {
                    ++r.busy;
                } else {
                    throw;
                }
            }
        }
    }

    F* factory_;
    std::string root_;
    std::string scope_;
    std::string content_;
    std::int64_t count_;
    std::int64_t pause_;
    std::vector<std::unique_ptr<Racer>> racers_;
    std::mutex mutex_;  // over made_, go_ and the factory's again
    std::condition_variable ready_;
    std::size_t made_ = 0;
    bool go_ = false;
};

template <class D>
std::optional<Value> race(D& d, memory::Arena&, ArgView request) {
    const auto made = std::make_unique<Race<typename D::Factory>>(
        *held::named(d.states(), request, "state").object, test::ruled::text(d.site().root()), request);
    return made->run();
}

}  // namespace qiven3::drive
