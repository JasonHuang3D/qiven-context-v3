#pragma once

// seams: the store's seams, in the test tree only, none including the
// engine's own header; no subclass, since the model has none: a seam is a
// hooks type the SQLite store takes as its template parameter H
// (stores.hpp's Sqlite<H>), or a call through the store's connection.
//
//   Steps<R>            hooks reporting each step, begun, written,
//                       committed and rebuilt, to R's step(name); the
//                       child's store reports there and waits on stdin
//   NoDeferral          hooks whose import does not defer foreign keys
//                       (S24): the store reads H::defer_foreign_keys
//   write(store, sql)   a statement run through the store's connection,
//                       its engine's code given back: the incoherent rows
//                       of S18, purged_above_vacuumed among them
//   vacuum_and_checkpoint(store)
//                       VACUUM, then a TRUNCATE checkpoint, on a control
//                       store's file (the two-wave store)
//   NaiveStore          M5's and M6's negative control: each lane a JSON
//                       Lines file appended to, its head a file read,
//                       compared and rewritten, with no lock, as F5's
//                       compare-and-swap; a line is the header line, a
//                       tab and the body record, or the header line alone
//
// Not here: secure_delete OFF with a purge that skips VACUUM, and a purge
// that is the TRUNCATE checkpoint alone (the forgetting fixture's
// controls): the store's purge reads no policy from H for its VACUUM step
// yet.
//
// Allocations: Steps, NoDeferral none; write none of the test's; the
// naive store's strings and files' buffers. Platform calls: none; the
// naive store's files by the standard library. thread: one, each object;
// the naive store's processes share its directory without a lock.

#include <concepts>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "store/contract.hpp"

namespace qiven3::test::seams {

template <class R>
concept Reporter = requires(R& r, std::string_view name) { r.step(name); };

template <Reporter R>
class Steps {
public:
    explicit Steps(R& reporter) noexcept : reporter_(&reporter) {}
    void begun() { reporter_->step("begun"); }
    void written() { reporter_->step("written"); }
    void committed() { reporter_->step("committed"); }
    void rebuilt() { reporter_->step("rebuilt"); }

private:
    R* reporter_;
};

struct NoDeferral {
    static constexpr bool defer_foreign_keys = false;
    constexpr void begun() const noexcept {}
    constexpr void written() const noexcept {}
    constexpr void committed() const noexcept {}
    constexpr void rebuilt() const noexcept {}
};

// sql run through the store's own connection; 0 where it ran.
template <class S>
int write(S& store, std::string_view sql) {
    return store.connection().run(sql);
}
inline constexpr std::string_view purged_above_vacuumed =
    "UPDATE meta SET value = (SELECT value FROM meta WHERE name = 'vacuumed') + 1 WHERE name = 'purged'";

// 0 where both ran, else the first's engine code.
template <class S>
int vacuum_and_checkpoint(S& store) {
    const int code = write(store, "VACUUM");
    return code != 0 ? code : write(store, "PRAGMA wal_checkpoint(TRUNCATE)");
}

class NaiveStore {
public:
    explicit NaiveStore(std::filesystem::path directory);

    // The head file's id, or none.
    [[nodiscard]] std::optional<std::string> head(std::string_view scope, std::string_view lane) const;
    // The head compared with expected, unlocked, a mismatch HeadConflict;
    // then each entry a line appended, and the head file rewritten to the
    // last one's id, which it gives.
    std::string append(std::string_view scope, std::string_view lane, store::Expected expected,
                       std::span<const store::EntryView> entries);
    // The lane file's lines as they lie, a torn one among them.
    [[nodiscard]] std::vector<std::string> lines(std::string_view scope, std::string_view lane) const;

private:
    [[nodiscard]] std::filesystem::path file(std::string_view scope, std::string_view lane,
                                             std::string_view suffix) const;
    std::filesystem::path directory_;
};

}  // namespace qiven3::test::seams
