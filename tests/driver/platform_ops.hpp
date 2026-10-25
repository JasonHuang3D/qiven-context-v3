#pragma once

// The drive's platform half, a template on the adapter P: each contract
// operation on the wire, on the adapter the binary instantiates it for
// (driver/instances_fake.cpp, driver/instances_windows.cpp), which alone
// include its definitions (driver/platform_ops_impl.hpp).
//
// Each request is an operation as a caller runs one: the drive takes
// pages(budget) from P, the operation's stated budget or the request's
// "budget", and the operation writes its results into an arena over just
// those bytes, so a request past the budget is MemoryError budget; the
// results leave it as the reply's text before the arena ends, and the
// pages go back. The stated budgets: 64 KiB for each member; read's adds
// the request's "size", the file's bytes as its caller knows them; run's
// its caps; pages and the drive's own operations take none. Bytes cross
// as hexadecimal; a deadline as an integer of milliseconds, 5000 when
// absent; a path as "path", or as "path_hex", its bytes in hexadecimal,
// for one that is no UTF-8 (resolve, replace, stage, staged_open). Pages a
// request takes stay held, by an id, until pages_free.
//
// The files' handles stay held by an id too (driver/platform_files.hpp):
// stage's Staged until publish or discard ends it, staged_open's Sink
// until sink_close, lock's Lock until lock_release; a publish or a discard
// refused keeps its Staged, so that a later discard tries again. Each is
// a member, on the member's budget.
//
// thread: one

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "driver/drive.hpp"
#include "memory/arena.hpp"
#include "platform/platform.hpp"

namespace qiven3::drive {

template <platform::Platform P>
class PlatformDrive {
public:
    using Pages = decltype(std::declval<P&>().pages(std::size_t{}));
    using Staged = decltype(std::declval<P&>().stage(std::string_view{}));
    using Sink = decltype(std::declval<Staged&>().open(std::string_view{}));
    using Lock = decltype(std::declval<P&>().lock(std::string_view{}, platform::Seconds{}));
    using Operation = std::optional<Value> (*)(PlatformDrive&, memory::Arena&, ArgView);
    // The operation's stated budget for this request, in bytes.
    using Budget = std::size_t (*)(ArgView);
    // Makes the adapter in place from the options.
    using Make = void (*)(std::optional<P>&, const Options&);
    // The bytes of pages the adapter has lent and not had back, where it
    // counts them (the fake); null to count the pages the drive holds.
    using Lent = std::size_t (*)(const P&);

    PlatformDrive(Session& session, Make make, Lent lent = nullptr);
    PlatformDrive(const PlatformDrive&) = delete;
    PlatformDrive& operator=(const PlatformDrive&) = delete;
    ~PlatformDrive() = default;

    // An operation of the binary's own (the fake's, the adapter's): on no
    // arena, or, given its budget, on an arena of it as a member's.
    void add(std::string name, Operation operation, Budget budget = nullptr) {
        table_.insert_or_assign(std::move(name), Entry{operation, budget});
    }
    // One request's reply, never throwing: the platform's operations, then
    // the common ones; each with "held" and its arena's record.
    Value reply(ArgView request);
    // The adapter, made at the first call.
    P& platform();
    std::map<std::int64_t, Pages>& held() noexcept { return held_; }
    std::map<std::int64_t, Staged>& staged() noexcept { return staged_; }
    std::map<std::int64_t, Sink>& sinks() noexcept { return sinks_; }
    std::map<std::int64_t, Lock>& locks() noexcept { return locks_; }
    std::int64_t next_id() noexcept { return next_++; }

private:
    struct Entry {
        Operation operation = nullptr;
        Budget budget = nullptr;  // null: no arena, no pages
    };
    Value run(const Entry& entry, ArgView request);
    std::size_t holding() const;

    Session& session_;
    Make make_;
    Lent lent_;
    std::optional<P> platform_;
    std::map<std::string, Entry, std::less<>> table_;
    std::map<std::int64_t, Pages> held_;
    // After the adapter, so that each handle ends before it; a file before
    // its staging directory.
    std::map<std::int64_t, Staged> staged_;
    std::map<std::int64_t, Sink> sinks_;
    std::map<std::int64_t, Lock> locks_;
    std::int64_t next_ = 1;
};

}  // namespace qiven3::drive
