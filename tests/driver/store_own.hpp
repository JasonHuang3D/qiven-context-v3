#pragma once

// The SQLite store's own requests over a path or a state's file, beside
// the factory's (driver/store_ops.hpp): templates on the drive D, SQLite
// alone, a RequestError on the in-memory store.
//
//   store_open_at {path, create?, read_only?, refuse? [{of, k}]}
//       an open of the file at path, under the Site's root, its object
//       closed at once: {made: true} or {error}, and under QIVEN3_FAULTS
//       each countdown "refuse" names, armed at k for the open: "platform"
//       (the Site's fake's calls), "arena" (the store's arenas), "engine_call"
//       (the engine calls the wrapper maps), "engine_heap" (the engine
//       heap's allocations), each given back with its calls and fired.
//       The location guard (S22) and a refused create (S28).
//   store_import_undeferred {state, export | heads + lines}
//       an import into the state's file through the seam whose import does
//       not defer foreign keys (S24), its object closed at once: as
//       store_import's result, or the store's typed error.
//
// thread: one

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "driver/drive.hpp"
#include "driver/store_held.hpp"
#include "driver/store_wire.hpp"
#include "memory/memory.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/sqlite_store.hpp"
#include "support/seams/seams.hpp"
#include "support/stores/stores.hpp"

namespace qiven3::drive {

// path, UTF-8 and relative, under the Site's root.
inline std::string under_site(const test::stores::Site& site, std::string_view path) {
    std::u8string given;
    for (const char c : path) {
        given.push_back(static_cast<char8_t>(c));
    }
    return test::stores::text(site.root() / std::filesystem::path(given));
}

#if defined(QIVEN3_FAULTS)
// The kinds of countdown store_open_at arms.
inline constexpr std::array<std::string_view, 4> refusal_kinds = {"platform", "arena", "engine_call", "engine_heap"};

// The countdowns store_open_at arms, attached for the open and detached
// after it.
template <class Site>
class Refusals {
public:
    Refusals(Site& site, ArgView request) : site_(&site) {
        const std::optional<ArgView> list = request.find("refuse");
        if (!list) {
            return;
        }
        for (const ArgView& item : list->as_array("refuse")) {
            const std::string of = text(item, "of");
            std::size_t kind = refusal_kinds.size();
            for (std::size_t j = 0; j < refusal_kinds.size(); ++j) {
                kind = of == refusal_kinds.at(j) ? j : kind;
            }
            const std::int64_t k = integer(item, "k");
            if (kind == refusal_kinds.size() || k < 0 || armed_.at(kind)) {
                throw RequestError("refuse: [{\"of\": platform, arena, engine_call or engine_heap, \"k\": K}], "
                                   "each kind once");
            }
            armed_.at(kind) = true;
            counts_.at(kind).arm(static_cast<std::size_t>(k));
        }
        site.fake().attach(armed_.at(0) ? &counts_.at(0) : nullptr);
        store::sqlite::attach_calls(armed_.at(2) ? &counts_.at(2) : nullptr, armed_.at(1) ? &counts_.at(1) : nullptr);
        site.engine().heap().attach(armed_.at(3) ? &counts_.at(3) : nullptr);
    }
    Refusals(const Refusals&) = delete;
    Refusals& operator=(const Refusals&) = delete;
    ~Refusals() { detach(); }

    void detach() noexcept {
        site_->fake().attach(nullptr);
        store::sqlite::attach_calls(nullptr, nullptr);
        site_->engine().heap().attach(nullptr);
    }
    [[nodiscard]] Value given() const {
        Value out = array();
        for (std::size_t j = 0; j < refusal_kinds.size(); ++j) {
            if (armed_.at(j)) {
                Value item;
                item.set("of", refusal_kinds.at(j));
                item.set("calls", static_cast<std::int64_t>(counts_.at(j).calls()));
                item.set("fired", counts_.at(j).fired());
                out.push(item);
            }
        }
        return out;
    }

private:
    Site* site_;
    std::array<memory::Countdown, 4> counts_{};
    std::array<bool, 4> armed_{};
};
#endif

template <class D>
std::optional<Value> open_at([[maybe_unused]] D& d, memory::Arena&, [[maybe_unused]] ArgView request) {
    if constexpr (D::Factory::own == test::stores::Own::sqlite) {
        using Store = typename D::Store;
        test::stores::Site& site = d.site();
        const std::string path = under_site(site, text(request, "path"));
        const bool create = flag(request, "create", false);
        const bool read_only = flag(request, "read_only", false);
        // The engine made first, in every build: its heap installed for the
        // open, and under QIVEN3_FAULTS its pages taken before any countdown.
        static_cast<void>(site.engine());
#if defined(QIVEN3_FAULTS)
        Refusals<test::stores::Site> refusals(site, request);
#else
        if (request.find("refuse")) {
            throw RequestError("not applicable: built without QIVEN3_FAULTS, so no refusal");
        }
#endif
        Value out;
        try {
            Store made = Store::open(path, create, read_only, site.fake());
            static_cast<void>(made.close());
            out.set("made", true);
        } catch (const store::StoreError& error) {
            out.set("error", wire::error(error));
        } catch (...) {
            out.set("error", caught("store_open_at"));
        }
#if defined(QIVEN3_FAULTS)
        refusals.detach();
        out.set("refused", refusals.given());
#endif
        return out;
    } else {
        throw RequestError("store_open_at: the " + std::string(D::Factory::name) + " store has no file");
    }
}

template <class D>
std::optional<Value> import_undeferred([[maybe_unused]] D& d, [[maybe_unused]] memory::Arena& arena,
                                       [[maybe_unused]] ArgView request) {
    if constexpr (D::Factory::own == test::stores::Own::sqlite) {
        using Undeferred = store::SQLiteStore<test::stores::Fake, test::seams::NoDeferral>;
        const auto& factory = *held::named(d.states(), request, "state").object;
        Undeferred into = Undeferred::open(factory.path(), false, false, d.site().fake());
        store::Vector<store::LaneHead> rows{memory::Allocator<store::LaneHead>(arena)};
        store::Imported done;
        if (request.find("export")) {
            done = into.import_(*held::named(d.exports(), request, "export").object, rows);
        } else {
            wire::Given given(request);
            done = into.import_(given, rows);
        }
        static_cast<void>(into.close());
        Value out;
        out.set("counts", wire::counts(done.counts));
        out.set("volume", wire::volume(done.volume));
        out.set("rows", wire::rows(rows));
        return out;
    } else {
        throw RequestError("store_import_undeferred: the " + std::string(D::Factory::name) + " store has no file");
    }
}

}  // namespace qiven3::drive
