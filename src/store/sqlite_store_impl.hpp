#pragma once

// sqlite_store_impl: SQLiteStore's long members' bodies: open and the guard,
// append's and forget's steps with their pages, a purge's attempt.
// sqlite_store.hpp declares them and states their rules, their budgets,
// platform calls and thread among them, and includes this header at its
// end, after the class, which is its one includer: every file that makes a
// store needs these bodies, each store on the hooks and rule its maker gives.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "store/codes.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/stage.hpp"

namespace qiven3::store {

template <platform::Platform P, Hooks H, class Rule>
SQLiteStore<P, H, Rule> SQLiteStore<P, H, Rule>::open(std::string_view path, bool create, bool read_only, P& platform,
                                                      const Limits& limits, H hooks) {
    if (!valid(limits)) {
        throw StoreError(Code::settings, sqlite::open_name, path, "limits");
    }
    if (create && read_only) {
        throw StoreError(Code::read_only, sqlite::open_name, path, "create");
    }
    SQLiteStore store(platform, read_only, limits, std::move(hooks));
    {
        memory::Arena arena(store.pages_.bytes());
        store.hook(arena);
        String resolved{memory::Allocator<char>(arena)};
        platform.resolve(path, resolved);
        store.path_ = memory::Inline<path_bytes>(resolved);
        const WhereView at = sqlite::site(sqlite::open_name, resolved);
        check(resolved, platform, arena, sqlite::open_name);
        if (sqlite::engine_version() < sqlite::least_version) {
            throw StoreError(Code::engine_version, sqlite::open_name, resolved, "older than 3.37.0");
        }
        if (!sqlite::engine_ready()) {
            throw StoreError(Code::internal, sqlite::open_name, resolved, "no engine heap installed");
        }
        sqlite::presence(resolved, create, at);
        store.connection_ = create ? sqlite::made(resolved, platform, at, store.opened_)
                                   : sqlite::connect(resolved, false, read_only, at, store.opened_);
        store.peak_ = arena.peak();
    }
    if (!read_only && store.opened_.pending()) {
        store.purge_ = store.attempt();
    }
    return store;
}

template <platform::Platform P, Hooks H, class Rule>
void SQLiteStore<P, H, Rule>::guard(std::string_view path, P& platform) {
    Pages pages = platform.pages(operation_base);
    memory::Arena arena(pages.bytes());
#if defined(QIVEN3_FAULTS)
    if (sqlite::arenas_countdown() != nullptr) {
        arena.attach(sqlite::arenas_countdown());
    }
#endif
    String resolved{memory::Allocator<char>(arena)};
    platform.resolve(path, resolved);
    check(resolved, platform, arena, sqlite::guard_name);
}

template <platform::Platform P, Hooks H, class Rule>
template <class A>
Written SQLiteStore<P, H, Rule>::write(Operation op, std::string_view scope, std::string_view lane,
                                       Expected expected, std::span<const EntryView> entries,
                                       std::span<const GuardView> guard, Vector<Id>* erased, A& admit) {
    gate_.enter(op, path_.view());
    std::optional<Pages> extra;
    const std::span<std::byte> span = room(entries, extra);
    const std::size_t half = span.size() / 2;
    memory::Arena kept(span.first(span.size() - half));
    hook(kept);
    sqlite::ScratchView scratch(span.subspan(span.size() - half), countdown_);
    const Released<A> released(admit);
    const sqlite::Chars chars(kept);
    const stage::AtView at{op, path_.view()};
    const stage::Checked checked = stage::read(*platform_, entries, at, chars, scratch, admit);
    Vector<sqlite::RowView> rows(chars);
    Vector<Id> targets(chars);
    rows.reserve(entries.size());
    targets.reserve(erased != nullptr ? entries.size() : 0);
    for (const stage::KeptView& k : checked.kept) {
        const KeysView keys = k.keys();
        rows.push_back(sqlite::RowView{keys.id, keys.scope, keys.lane, keys.kind, keys.parent, keys.target,
                                       keys.seq, k.line, k.record, keys.body});
        if (erased != nullptr) {
            targets.push_back(k.target.value_or(Id{}));
        }
    }
    WhereView where = sqlite::site(name(op), path_.view());
    connection_.exec("BEGIN IMMEDIATE", where);
    Written w;
    try {
        hooks_.begun();
        const std::optional<Head> now = sqlite::expect(connection_, scope, lane, expected, where);
        if (erased != nullptr) {
            sqlite::guard(connection_, guard, where);
        }
        const sqlite::Totals held = sqlite::totals(connection_, where);
        sqlite::WriteView view(connection_, chars, &scratch, where, held);
        admit.admit(view, scope, lane, expected, std::span<const stage::KeptView>(checked.kept), at, chars);
        const stage::Staged staged = stage::guarded(view, checked, limits_, at, scope, lane, now);
        std::uint64_t dropped = 0;
        if (erased != nullptr) {
            dropped = sqlite::erase(connection_, targets, scope, *erased, where);
            stage::erasable(std::span<const stage::KeptView>(checked.kept), std::span<const Id>(*erased), at);
        }
        const std::uint64_t size = stage::detail::sum(static_cast<std::uint64_t>(held.size), staged.size);
        const std::uint64_t count = stage::detail::sum(static_cast<std::uint64_t>(held.count), staged.count);
        where.size = size;
        where.count = count;
        where.max_size = limits_.max_size;
        where.max_count = limits_.max_count;
        sqlite::insert(connection_, rows, expected, where);
        if (!entries.empty()) {
            sqlite::advance(connection_, scope, lane, expected, staged.head, where);
        }
        const std::uint64_t forgets = erased != nullptr ? entries.size() : 0;
        sqlite::add_meta(connection_, delta(staged.size, dropped), delta(staged.count, 0), delta(forgets, 0),
                         where);
        hooks_.written();
        connection_.exec("COMMIT", where);
        w.head = entries.empty() ? now.value_or(Head{}) : staged.head;
        w.volume = measure(size - std::min(size, dropped), count, limits_);
    } catch (...) {
        connection_.rollback();
        throw;
    }
    hooks_.committed();
    peak_ = kept.peak() + scratch.peak();
    w.purge = attempt();
    return w;
}

template <platform::Platform P, Hooks H, class Rule>
std::int64_t SQLiteStore<P, H, Rule>::delta(std::uint64_t added, std::uint64_t dropped) {
    constexpr auto most = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (added > most || dropped > most) {
        throw memory::MemoryError::budget("store/sqlite", most, std::max(added, dropped), 0);
    }
    return static_cast<std::int64_t>(added) - static_cast<std::int64_t>(dropped);
}

template <platform::Platform P, Hooks H, class Rule>
std::span<std::byte> SQLiteStore<P, H, Rule>::room(std::span<const EntryView> entries,
                                                   std::optional<Pages>& extra) {
    const std::optional<std::size_t> lines = given(entries);
    const std::optional<std::size_t> budget = lines ? operation_budget(*lines) : std::nullopt;
    if (!budget) {
        throw memory::MemoryError::budget("store/sqlite", std::numeric_limits<std::size_t>::max(),
                                          entries.size(), 0);
    }
    if (*budget <= operation_base) {
        return pages_.bytes().first(*budget);
    }
    extra.emplace(platform_->pages(*budget));
    if (extra->bytes().size() < *budget) {
        memory::fail_fast("store/sqlite: pages short of their request");
    }
    return extra->bytes().first(*budget);
}

template <platform::Platform P, Hooks H, class Rule>
Purge SQLiteStore<P, H, Rule>::attempt() {
    connection_.wait(0);
    Purge p;
    try {
        p = attempt_steps();
    } catch (...) {
        connection_.wait(sqlite::busy_milliseconds);
        throw;
    }
    connection_.wait(sqlite::busy_milliseconds);
    last_ = p;
    return p;
}

template <platform::Platform P, Hooks H, class Rule>
Purge SQLiteStore<P, H, Rule>::attempt_steps() {
    sqlite::Connection& c = connection_;
    const auto pending = [](int code, Step step) { return Purge::pending(purge_reason(code, step)); };
    std::array<std::int64_t, 3> m{};
    int code = c.run("SELECT (SELECT value FROM meta WHERE name = 'forgets'), (SELECT value FROM meta WHERE "
                     "name = 'vacuumed'), (SELECT value FROM meta WHERE name = 'purged')",
                     m);
    if (code != 0) {
        return pending(code, Step::read);
    }
    const std::int64_t forgets = m[0];
    std::int64_t vacuumed = m[1];
    if (m[2] >= forgets) {
        return Purge{};
    }
    std::array<char, 96> sql{};
    if (vacuumed < forgets) {
        code = c.run("VACUUM");
        if (code != 0) {
            return pending(code, Step::vacuum);
        }
        hooks_.rebuilt();
        code = c.run(sqlite::raise_meta("vacuumed", forgets, sql));
        if (code != 0) {
            return pending(code, Step::vacuumed);
        }
        vacuumed = forgets;
    }
    std::array<std::int64_t, 1> busy{};
    code = c.run("PRAGMA wal_checkpoint(TRUNCATE)", busy);
    if (code != 0) {
        return pending(code, Step::checkpoint);
    }
    if (busy[0] != 0) {
        code = c.run("BEGIN IMMEDIATE");
        c.rollback();
        return pending(code, Step::probe);
    }
    if (sqlite::file_bytes(c.filename(), "-wal").value_or(0) != 0) {
        return Purge::pending("writer");
    }
    code = c.run(sqlite::raise_meta("purged", vacuumed, sql));
    if (code != 0) {
        return pending(code, Step::purged);
    }
    static_cast<void>(c.run("PRAGMA wal_checkpoint(TRUNCATE)"));
    return Purge{};
}

}  // namespace qiven3::store
