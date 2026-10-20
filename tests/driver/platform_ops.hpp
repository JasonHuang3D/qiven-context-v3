#pragma once

// The drive's platform half, a template on the adapter P: each contract
// operation on the wire, on the adapter the binary instantiates it for
// (driver/instances_fake.cpp, driver/instances_windows.cpp), which alone
// include its definitions (driver/platform_ops_impl.hpp).
//
// Each request's results are written into an arena over the drive's own
// pages, taken from P once at its first platform request, and leave it as
// the reply's text before the arena ends. Bytes cross as hexadecimal; a
// deadline as an integer of milliseconds, 5000 when absent. Pages a request
// takes stay held, by an id, until pages_free.
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

// The bytes of the arena each request's results take.
inline constexpr std::size_t result_bytes = std::size_t{4} << 20;

template <platform::Platform P>
class PlatformDrive {
public:
    using Pages = decltype(std::declval<P&>().pages(std::size_t{}));
    using Operation = std::optional<Value> (*)(PlatformDrive&, memory::Arena&, const Value&);
    // Makes the adapter in place from the options.
    using Make = void (*)(std::optional<P>&, const Options&);

    PlatformDrive(Session& session, Make make);
    PlatformDrive(const PlatformDrive&) = delete;
    PlatformDrive& operator=(const PlatformDrive&) = delete;
    ~PlatformDrive() = default;

    // An operation of the binary's own (the fake's).
    void add(std::string name, Operation operation) { table_.insert_or_assign(std::move(name), operation); }
    // One request's reply, never throwing: the platform's operations, then
    // the common ones.
    Value reply(const Value& request);
    // The adapter, made at the first call.
    P& platform();
    std::map<std::int64_t, Pages>& held() noexcept { return held_; }
    std::int64_t next_id() noexcept { return next_++; }

private:
    Session& session_;
    Make make_;
    std::optional<P> platform_;
    std::optional<Pages> pages_;  // the results' arena's
    std::map<std::string, Operation, std::less<>> table_;
    std::map<std::int64_t, Pages> held_;
    std::int64_t next_ = 1;
};

}  // namespace qiven3::drive
