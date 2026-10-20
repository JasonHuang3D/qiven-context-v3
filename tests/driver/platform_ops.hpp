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
// absent. Pages a request takes stay held, by an id, until pages_free.
//
// thread: one

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "driver/drive.hpp"
#include "memory/arena.hpp"
#include "platform/platform.hpp"

namespace qiven3::drive {

template <platform::Platform P>
class PlatformDrive {
public:
    using Pages = decltype(std::declval<P&>().pages(std::size_t{}));
    using Operation = std::optional<Value> (*)(PlatformDrive&, memory::Arena&, const Value&);
    // The operation's stated budget for this request, in bytes.
    using Budget = std::size_t (*)(const Value&);
    // Makes the adapter in place from the options.
    using Make = void (*)(std::optional<P>&, const Options&);
    // The bytes of pages the adapter has lent and not had back, where it
    // counts them (the fake); null to count the pages the drive holds.
    using Lent = std::size_t (*)(const P&);

    PlatformDrive(Session& session, Make make, Lent lent = nullptr);
    PlatformDrive(const PlatformDrive&) = delete;
    PlatformDrive& operator=(const PlatformDrive&) = delete;
    ~PlatformDrive() = default;

    // An operation of the binary's own (the fake's), on no arena.
    void add(std::string name, Operation operation) {
        table_.insert_or_assign(std::move(name), Entry{operation, nullptr});
    }
    // One request's reply, never throwing: the platform's operations, then
    // the common ones; each with "held" and its arena's record.
    Value reply(const Value& request);
    // The adapter, made at the first call.
    P& platform();
    std::map<std::int64_t, Pages>& held() noexcept { return held_; }
    std::int64_t next_id() noexcept { return next_++; }

private:
    struct Entry {
        Operation operation = nullptr;
        Budget budget = nullptr;  // null: no arena, no pages
    };
    Value run(const Entry& entry, const Value& request);
    std::size_t holding() const;

    Session& session_;
    Make make_;
    Lent lent_;
    std::optional<P> platform_;
    std::map<std::string, Entry, std::less<>> table_;
    std::map<std::int64_t, Pages> held_;
    std::int64_t next_ = 1;
};

}  // namespace qiven3::drive
