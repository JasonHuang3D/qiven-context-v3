#pragma once

// The hardware abstraction layer: the contract for compute (CPU threads,
// vector instructions, GPGPU) that features use. The core never includes
// this header or links qiven3_hal; a feature's targets do. A back end comes
// with the first feature that uses it.
//
// Sizes are bytes; text is UTF-8. A capability's absence is a Gap, a value,
// never an error; HalError is a failed operation.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qiven3::hal {

enum class Vector { sse2, sse4_2, avx, fma, avx2, avx512f, neon };

// The set's name, as the enumerator spells it: "sse2", "sse4_2", ...
std::string_view vector_name(Vector vector) noexcept;

// A device a back end finds; back_end is "cpu" for the CPU.
struct Device {
    std::string id, back_end, name;
    std::uint64_t memory;
};

struct Facts {
    unsigned cpu_threads;
    std::set<Vector> vectors;  // those the CPU has and the system enables
    std::vector<Device> devices;
};

// reason: no_device, no_driver, not_built or not_pinned.
struct Gap {
    std::string capability, reason;
};

// A device's memory; its destructor frees it.
class Buffer {
public:
    virtual ~Buffer() = default;
    virtual std::uint64_t size() const = 0;
};

struct Launch {
    std::string kernel;
    std::vector<Buffer*> buffers;
    std::vector<std::int64_t> scalars;
    std::array<std::uint32_t, 3> grid;
};

class Compute {
public:
    virtual ~Compute() = default;
    // The CPU's threads and vector sets, the devices each back end finds.
    // Never fails: a back end that cannot answer finds no device.
    virtual Facts facts() = 0;
    // A device for "cpu", "gpu" or a back end's name ("cuda"), or the gap.
    virtual std::variant<Device, Gap> device(std::string_view capability) = 0;
    // memory past what the device has free.
    virtual std::unique_ptr<Buffer> allocate(const Device& d, std::uint64_t size) = 0;
    // size for a span not the buffer's size.
    virtual void upload(Buffer& b, std::span<const std::byte> bytes) = 0;
    virtual void download(const Buffer& b, std::span<std::byte> bytes) = 0;
    // Runs a kernel by name and returns at its end: kernel for a name no
    // kernel has, launch for a back end's failure.
    virtual void run(const Device& d, const Launch& launch) = 0;
};

// A kernel's CPU reference, the oracle each back end's code is compared
// with: it reads and writes the launch's buffers as host bytes, in order.
using Reference = std::function<void(std::span<const std::span<std::byte>> buffers,
                                     std::span<const std::int64_t> scalars,
                                     const std::array<std::uint32_t, 3>& grid)>;

// The kernels by name, each with its CPU reference; a back end adds its own
// code beside each.
class Kernels {
public:
    // A name added twice, or an empty reference, is std::invalid_argument.
    void add(std::string name, Reference reference);
    // The name's reference, or null.
    const Reference* find(std::string_view name) const;

private:
    std::map<std::string, Reference, std::less<>> references_;
};

enum class Code { memory, size, kernel, launch };

// The code's name, as the text spells it: "memory", "size", ...
std::string_view code_name(Code code) noexcept;

// A compute operation's typed error, as the platform's PlatformError is.
class HalError : public std::runtime_error {
public:
    HalError(Code code, std::string operation, std::string detail);
    Code code() const noexcept { return code_; }
    const std::string& operation() const noexcept { return operation_; }
    const std::string& detail() const noexcept { return detail_; }

private:
    Code code_;
    std::string operation_;
    std::string detail_;
};

}  // namespace qiven3::hal
