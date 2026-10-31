// The drive's store half (driver/store_ops.hpp): a template on the
// factory F, stores::Memory or stores::Sqlite<>, whose objects it holds by
// id, each on the heap; serve picks one by --store and runs the request
// loop over it and the platform's half.
//
// The objects end in the order they depend on one another: scans, then
// exports, then store objects, then states, then the Site. A state is
// dropped only once no object over it is held, an object only once no
// scan or export of it is.

#include "driver/store_ops.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "driver/store_held.hpp"
#include "driver/store_own.hpp"
#include "driver/store_race.hpp"
#include "driver/store_threads.hpp"
#include "driver/store_wire.hpp"
#include "memory/arena.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "support/checked/stores.hpp"

namespace qiven3::drive {
namespace {

using Fake = platform::fake::FakePlatform;
using Result = std::optional<Value>;
namespace stores = test::ruled;
using held::id_value;
using held::keep;
using held::Kept;
using held::named;
using held::over;
using held::Table;

template <class F>
class StoreDrive {
public:
    using Factory = F;
    using Store = typename F::Store;
    using Scan = decltype(std::declval<Store&>().scan(std::declval<const store::RangeView&>()));
    using Export = decltype(std::declval<Store&>().export_(store::Scopes{}, std::span<const store::AfterView>{}));
    using Operation = Result (*)(StoreDrive&, memory::Arena&, ArgView);
    using Budget = std::size_t (*)(ArgView);

    StoreDrive(Session& session, PlatformDrive<Fake>& platform);
    StoreDrive(const StoreDrive&) = delete;
    StoreDrive& operator=(const StoreDrive&) = delete;
    ~StoreDrive() = default;

    [[nodiscard]] bool serves(std::string_view op) const { return table_.find(op) != table_.end(); }
    // One store request's reply, never throwing.
    Value reply(ArgView request);

    // The Site, made at the first store request.
    stores::Site& site();
    Table<F>& states() noexcept { return states_; }
    Table<Store>& objects() noexcept { return objects_; }
    Table<Scan>& scans() noexcept { return scans_; }
    Table<Export>& exports() noexcept { return exports_; }
    Table<Appender<F>>& appenders() noexcept { return appenders_; }
    std::int64_t next_id() noexcept { return next_++; }

private:
    struct Entry {
        Operation operation = nullptr;
        Budget budget = nullptr;  // null: no arena, no pages
    };
    Value run(const Entry& entry, ArgView request);

    Session& session_;
    PlatformDrive<Fake>& platform_;
    std::map<std::string, Entry, std::less<>> table_;
    std::int64_t next_ = 1;
    // In the order they are made, so ended in the order they depend.
    std::unique_ptr<stores::Site> site_;
    Table<F> states_;
    Table<Store> objects_;
    Table<Export> exports_;
    Table<Scan> scans_;
    Table<Appender<F>> appenders_;  // last: each joined before its state ends
};

template <class F>
typename F::Store& object(StoreDrive<F>& d, ArgView request) {
    return *named(d.objects(), request, "store").object;
}

// The factory's requests.

template <class F>
Result factory_make(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    const store::Limits given = wire::limits_of(request);
    std::unique_ptr<F> factory;
    if constexpr (F::own == stores::Own::none) {
        // The in-memory state's heap, its "heap" bytes where given (S12's
        // 20,000 entries).
        const std::int64_t heap = integer(request, "heap", static_cast<std::int64_t>(stores::Memory::default_heap));
        factory = std::make_unique<F>(d.site(), static_cast<std::size_t>(std::max<std::int64_t>(heap, 0)));
    } else {
        factory = std::make_unique<F>(d.site());
    }
    auto made = std::make_unique<typename F::Store>(factory->make(given));
    Value out;
    if constexpr (requires { factory->directory(); }) {
        out.set("directory", stores::text(factory->directory()));
        out.set("path", factory->path());
    }
    const std::int64_t state = keep(d, d.states(), std::move(factory), 0);
    out.set("state", state);
    out.set("store", keep(d, d.objects(), std::move(made), state));
    return out;
}

template <class F, typename F::Store (F::*open)()>
Result factory_open(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    const std::int64_t state = integer(request, "state");
    F& factory = *named(d.states(), request, "state").object;
    auto made = std::make_unique<typename F::Store>((factory.*open)());
    return id_value("store", keep(d, d.objects(), std::move(made), state));
}

// reopen, a static member on the in-memory factory, which is not durable.
template <class F>
Result factory_reopen(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    const std::int64_t state = integer(request, "state");
    F& factory = *named(d.states(), request, "state").object;
    auto made = std::make_unique<typename F::Store>(factory.reopen());
    return id_value("store", keep(d, d.objects(), std::move(made), state));
}

template <class F>
Result factory_drop(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    const std::int64_t state = integer(request, "state");
    (void)named(d.states(), request, "state");
    if (over(d.objects(), state) || over(d.appenders(), state)) {
        throw RequestError("the state still has store objects or appenders held over it");
    }
    d.states().erase(state);
    return std::nullopt;
}

// The fixtures' entries (store_fixtures.cpp), from the Site's book.
template <class F>
Result fixture_entry(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    return wire::fixture_entry(d.site().book(), request);
}
template <class F>
Result fixture_made(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    return wire::fixture_made(d.site().book(), request);
}

// The SQLite store's own: its plans and the seam's write on its connection
// (wire::plans, wire::seam_write), and create over its state's file (S17).
template <class F, Value (*own)(store::sqlite::Connection&, ArgView)>
Result own_sqlite([[maybe_unused]] StoreDrive<F>& d, memory::Arena&, [[maybe_unused]] ArgView request) {
    if constexpr (F::own == stores::Own::sqlite) {
        return own(object(d, request).connection(), request);
    } else {
        throw RequestError("the " + std::string(F::name) + " store has no connection");
    }
}
template <class F>
Result factory_create([[maybe_unused]] StoreDrive<F>& d, [[maybe_unused]] memory::Arena& a, ArgView request) {
    if constexpr (F::own == stores::Own::sqlite) {
        return factory_open<F, &F::create>(d, a, request);
    } else {
        throw RequestError("factory_create: the " + std::string(F::name) + " store has no file");
    }
}

template <class F>
Result capabilities(StoreDrive<F>&, memory::Arena&, ArgView) {
    constexpr store::Capabilities have = F::Store::capabilities;
    Value out;
    out.set("name", F::name);
    out.set("own", F::own == stores::Own::sqlite ? std::string_view("sqlite") : std::string_view("none"));
    out.set("durable", have.durable);
    out.set("files", have.files);
    out.set("processes", have.processes);
    return out;
}

// The store's requests.

store::Expected expected_of(ArgView request, std::optional<std::string>& held) {
    if (request.find("expected")) {
        held = text(request, "expected");
        return std::string_view(*held);
    }
    return std::nullopt;
}

template <class F>
Result append(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    const std::string scope = text(request, "scope");
    const std::string lane = text(request, "lane");
    std::optional<std::string> held;
    const store::Expected expected = expected_of(request, held);
    const wire::Entries entries(request, "entries");
    return wire::written(object(d, request).append(scope, lane, expected, entries.views()));
}

template <class F>
Result head(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    const std::optional<store::Head> h = object(d, request).head(text(request, "scope"), text(request, "lane"));
    return h ? Result(wire::head(*h)) : std::nullopt;
}

template <class F>
Result heads(StoreDrive<F>& d, memory::Arena& arena, ArgView request) {
    const wire::Ranges ranges(request);
    store::Vector<store::LaneHead> rows{memory::Allocator<store::LaneHead>(arena)};
    const store::Heads h = object(d, request).heads(ranges.scopes(), rows);
    Value out;
    out.set("rows", wire::rows(rows));
    out.set("forgets", static_cast<std::int64_t>(h.forgets));
    out.set("purge", wire::purge(h.purge));
    return out;
}

template <class F>
Result header(StoreDrive<F>& d, memory::Arena& arena, ArgView request) {
    checked::Header h{memory::Allocator<char>(arena)};
    object(d, request).header(text(request, "id"), h);
    return scalar(wire::header_line(h, arena));
}

template <class F>
Result body(StoreDrive<F>& d, memory::Arena& arena, ArgView request) {
    store::String record{memory::Allocator<char>(arena)};
    if (!object(d, request).body(text(request, "id"), record)) {
        return std::nullopt;
    }
    return scalar(to_hex(std::string_view(record.data(), record.size())));
}

template <class F>
Result forgotten(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    return scalar(object(d, request).forgotten(text(request, "id")));
}

template <class F>
Result scan(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    const std::int64_t of = integer(request, "store");
    const std::optional<std::string> scope = request.find("scope") ? std::optional(text(request, "scope")) : std::nullopt;
    const std::optional<std::string> lane = request.find("lane") ? std::optional(text(request, "lane")) : std::nullopt;
    store::RangeView range;
    if (scope) {
        range.scope = *scope;
    }
    if (lane) {
        range.lane = *lane;
    }
    if (request.find("after")) {
        range.after = integer(request, "after");
    }
    using Scan = typename StoreDrive<F>::Scan;
    auto made = std::make_unique<Scan>(object(d, request).scan(range));
    return id_value("scan", keep(d, d.scans(), std::move(made), of));
}

template <class F>
Result forget(StoreDrive<F>& d, memory::Arena& arena, ArgView request) {
    const std::string scope = text(request, "scope");
    const std::string lane = text(request, "lane");
    std::optional<std::string> held;
    const store::Expected expected = expected_of(request, held);
    const wire::Entries entries(request, "entries");
    const wire::Ranges ranges(request);
    store::Vector<store::Id> erased{memory::Allocator<store::Id>(arena)};
    Value out = wire::written(object(d, request).forget(scope, lane, expected, entries.views(), ranges.guard(), erased));
    Value ids = array();
    for (const store::Id& id : erased) {
        ids.push(scalar(id.view()));
    }
    out.set("erased", ids);
    return out;
}

template <class F>
Result purge(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    return wire::purge(object(d, request).purge());
}

template <class F>
Result export_(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    const std::int64_t of = integer(request, "store");
    const wire::Ranges ranges(request);
    using Export = typename StoreDrive<F>::Export;
    auto made = std::make_unique<Export>(object(d, request).export_(ranges.scopes(), ranges.after()));
    return id_value("export", keep(d, d.exports(), std::move(made), of));
}

template <class F>
Result import_(StoreDrive<F>& d, memory::Arena& arena, ArgView request) {
    auto& into = object(d, request);
    store::Vector<store::LaneHead> rows{memory::Allocator<store::LaneHead>(arena)};
    store::Imported done;
    if (request.find("export")) {
        done = into.import_(*named(d.exports(), request, "export").object, rows);
    } else {
        wire::Given given(request);
        done = into.import_(given, rows);
    }
    Value out;
    out.set("counts", wire::counts(done.counts));
    out.set("volume", wire::volume(done.volume));
    out.set("rows", wire::rows(rows));
    return out;
}

template <class F>
Result size(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    return wire::sizes(object(d, request).size());
}

template <class F>
Result verify(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    return wire::counts(object(d, request).verify());
}

template <class F>
Result close(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    return wire::purge(object(d, request).close());
}

template <class F>
Result read_only(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    return scalar(static_cast<const typename F::Store&>(object(d, request)).read_only());
}

template <class F>
Result limits(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    return wire::limits(static_cast<const typename F::Store&>(object(d, request)).limits());
}

template <class F>
Result store_drop(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    const std::int64_t id = integer(request, "store");
    (void)named(d.objects(), request, "store");
    if (over(d.scans(), id) || over(d.exports(), id)) {
        throw RequestError("the store object still has a scan or an export held");
    }
    d.objects().erase(id);
    return std::nullopt;
}

// The handles' requests.

template <class F>
Result scan_next(StoreDrive<F>& d, memory::Arena& arena, ArgView request) {
    checked::Header h{memory::Allocator<char>(arena)};
    if (!named(d.scans(), request, "scan").object->next(h)) {
        return std::nullopt;
    }
    return scalar(wire::header_line(h, arena));
}

template <class F>
Result scan_close(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    (void)named(d.scans(), request, "scan");
    d.scans().erase(integer(request, "scan"));
    return std::nullopt;
}

template <class F>
Result export_heads(StoreDrive<F>& d, memory::Arena& arena, ArgView request) {
    store::Vector<store::LaneHead> rows{memory::Allocator<store::LaneHead>(arena)};
    const store::Snapshot s = named(d.exports(), request, "export").object->heads(rows);
    Value out;
    out.set("rows", wire::rows(rows));
    out.set("forgets", static_cast<std::int64_t>(s.forgets));
    out.set("volume", wire::volume(s.volume));
    return out;
}

template <class F>
Result export_next(StoreDrive<F>& d, memory::Arena& arena, ArgView request) {
    store::Line l(arena);
    if (!named(d.exports(), request, "export").object->next(l)) {
        return std::nullopt;
    }
    return wire::line(l);
}

template <class F>
Result export_close(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    named(d.exports(), request, "export").object->close();
    return std::nullopt;
}

template <class F>
Result export_drop(StoreDrive<F>& d, memory::Arena&, ArgView request) {
    (void)named(d.exports(), request, "export");
    d.exports().erase(integer(request, "export"));
    return std::nullopt;
}

// The budgets: the contract's for an operation, 1 MiB plus 4 times the
// lines given; an export's reads 2 MiB.
std::size_t base(ArgView) { return store::operation_base; }
std::size_t entries_budget(ArgView request) { return wire::lines_budget(request, "entries"); }
std::size_t import_budget(ArgView request) { return wire::lines_budget(request, "lines"); }
std::size_t export_budget(ArgView) { return store::export_budget; }

template <class F>
StoreDrive<F>::StoreDrive(Session& session, PlatformDrive<Fake>& platform) : session_(session), platform_(platform) {
    const std::pair<const char*, Entry> table[] = {
        {"factory_make", {factory_make<F>, nullptr}},
        {"factory_again", {factory_open<F, &F::again>, nullptr}},
        {"factory_read_only", {factory_open<F, &F::read_only>, nullptr}},
        {"factory_reopen", {factory_reopen<F>, nullptr}},
        {"factory_create", {factory_create<F>, nullptr}},
        {"factory_drop", {factory_drop<F>, nullptr}},
        {"fixture_entry", {fixture_entry<F>, nullptr}},
        {"fixture_made", {fixture_made<F>, nullptr}},
        {"store_capabilities", {capabilities<F>, nullptr}},
        {"store_append", {append<F>, entries_budget}},
        {"store_head", {head<F>, base}},
        {"store_heads", {heads<F>, base}},
        {"store_header", {header<F>, base}},
        {"store_body", {body<F>, base}},
        {"store_forgotten", {forgotten<F>, base}},
        {"store_scan", {scan<F>, base}},
        {"store_forget", {forget<F>, entries_budget}},
        {"store_purge", {purge<F>, base}},
        {"store_export", {export_<F>, base}},
        {"store_import", {import_<F>, import_budget}},
        {"store_size", {size<F>, base}},
        {"store_verify", {verify<F>, base}},
        {"store_plans", {own_sqlite<F, wire::plans>, nullptr}},
        {"store_seam_write", {own_sqlite<F, wire::seam_write>, nullptr}},
        {"store_settings", {own_sqlite<F, wire::settings>, nullptr}},
        {"store_open_at", {open_at<StoreDrive>, nullptr}},
        {"store_import_undeferred", {import_undeferred<StoreDrive>, import_budget}},
        {"store_close", {close<F>, base}},
        {"store_read_only", {read_only<F>, nullptr}},
        {"store_limits", {limits<F>, nullptr}},
        {"store_drop", {store_drop<F>, nullptr}},
        {"scan_next", {scan_next<F>, base}},
        {"scan_close", {scan_close<F>, nullptr}},
        {"export_heads", {export_heads<F>, export_budget}},
        {"export_next", {export_next<F>, export_budget}},
        {"export_close", {export_close<F>, base}},
        {"export_drop", {export_drop<F>, nullptr}},
        {"store_appender", {start_appender<StoreDrive>, nullptr}},
        {"appender_release", {release_appender<StoreDrive>, nullptr}},
        {"appender_join", {join_appender<StoreDrive>, nullptr}},
        {"store_fill", {fill_lane<StoreDrive>, nullptr}},
        {"export_drain", {drain_export<StoreDrive>, nullptr}},
        {"store_race", {race<StoreDrive>, nullptr}},
    };
    for (const auto& [key, entry] : table) {
        table_.emplace(key, entry);
    }
}

template <class F>
stores::Site& StoreDrive<F>::site() {
    if (!site_) {
        const Options& options = session_.options();
        std::u8string root;
        for (const char c : *options.root) {
            root.push_back(static_cast<char8_t>(c));
        }
        site_ = std::make_unique<stores::Site>(options.seed, std::filesystem::path(root) / "stores");
    }
    return *site_;
}

template <class F>
Value StoreDrive<F>::run(const Entry& entry, ArgView request) {
    stores::Site& s = site();
#if defined(QIVEN_FAULTS)
    s.fake().attach(&session_.faults().platform);
#else
    static_cast<void>(s);
#endif
    // "elsewhere": the call on a thread of its own (driver/store_race.hpp).
    const auto call = [&, other = flag(request, "elsewhere", false)](memory::Arena& arena) {
        return other ? elsewhere([&] { return entry.operation(*this, arena, request); })
                     : entry.operation(*this, arena, request);
    };
    if (entry.budget == nullptr) {
        memory::Arena none{std::span<std::byte>{}};
        return ok_reply(call(none));
    }
    const std::size_t bytes = budget(request, entry.budget(request));
    std::optional<platform::fake::FakePages> pages;
    if (bytes > 0) {
        pages.emplace(platform_.platform().pages(bytes));
    }
    const std::span<std::byte> span = pages ? pages->bytes().first(bytes) : std::span<std::byte>{};
    return ok_reply(on_arena(session_, span, call));
}

template <class F>
Value StoreDrive<F>::reply(ArgView request) {
    std::string op;
    Value answer;
    try {
        op = text(request, "op");
        begin(session_, request);
        answer = run(table_.find(op)->second, request);
    } catch (const store::StoreError& error) {
        answer = error_reply(wire::error(error));
    } catch (const test::Skip& skip) {
        answer = error_reply(plain("request", skip.reason));
    } catch (...) {
        try {
            answer = caught(op);
        } catch (...) {
            answer = error_reply(plain("exception", op + ": an exception of no type the drive names"));
        }
    }
#if defined(QIVEN_FAULTS)
    if (site_) {
        site_->fake().attach(nullptr);
    }
#endif
    return finish(session_, std::move(answer), platform_.platform().lent());
}

// The request loop over both halves: the store's requests to it, the rest
// to the platform's.
template <class F>
struct Both {
    PlatformDrive<Fake>& platform;
    StoreDrive<F>& stores;

    Value reply(ArgView request) {
        const std::optional<ArgView> op = request.find("op");
        if (op && op->kind() == json::Kind::string && stores.serves(op->as_string("op"))) {
            return stores.reply(request);
        }
        return platform.reply(request);
    }
};

template <class F>
int serve_with(Session& session, PlatformDrive<Fake>& drive) {
    // On the heap: the halves hold the store objects' tables.
    const auto stores = std::make_unique<StoreDrive<F>>(session, drive);
    Both<F> both{drive, *stores};
    return loop(both);
}

}  // namespace

int serve(Session& session, PlatformDrive<Fake>& drive) {
    const std::optional<std::string>& store = session.options().store;
    if (!store) {
        return loop(drive);
    }
    if (*store == "sqlite") {
        return serve_with<stores::Sqlite<>>(session, drive);
    }
    return serve_with<stores::Memory>(session, drive);
}

}  // namespace qiven3::drive
