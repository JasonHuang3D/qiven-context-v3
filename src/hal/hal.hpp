#pragma once

// The hardware abstraction layer: the contract for compute (CPU threads,
// vector instructions, GPGPU) that features use. The core never includes
// this header or links qiven3_hal; a feature's targets do. A back end comes
// with the first feature that uses it.
//
// The contract is a pair of concepts bound at compile time: a back end or
// the fake is a concrete type satisfying Compute, with no base class and no
// virtual function, and a feature's code is a template on it. Views go in,
// results into the caller's out; a buffer is a move-only value that frees
// its memory at its end.
//
// Sizes are bytes; text is UTF-8. A capability's absence is a Gap, a value,
// never an error; HalError is a failed operation. A caller's mistake (a
// device or buffer this compute did not give, a null buffer, a name added
// twice to Kernels, a null reference) is a broken invariant: it stops the
// process through memory::fail_fast.
//
// thread: none (the contract); each back end states its own.

#include <array>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "memory/memory.hpp"

namespace qiven3::hal {

enum class Vector : std::uint8_t { sse2, sse4_2, avx, fma, avx2, avx512f, neon };

// The count of Vector's sets.
inline constexpr std::size_t vector_sets = 7;

// Indexed by Vector.
using Vectors = std::bitset<vector_sets>;

// The set's name, as the enumerator spells it: "sse2", "sse4_2", ...
std::string_view name(Vector vector) noexcept;

// A device a back end finds, its text on the caller's arena; back_end is
// "cpu" for the CPU.
struct Device {
    explicit Device(memory::Arena& arena)
        : id(memory::Allocator<char>(arena)), back_end(memory::Allocator<char>(arena)),
          name(memory::Allocator<char>(arena)) {}
    memory::String id, back_end, name;
    std::uint64_t memory = 0;
};

struct Facts {
    explicit Facts(memory::Arena& arena) : devices(memory::Allocator<Device>(arena)) {}
    unsigned cpu_threads = 0;
    Vectors vectors;  // those the CPU has and the system enables
    memory::Vector<Device> devices;
};

enum class Reason : std::uint8_t { no_device, no_driver, not_built, not_pinned };

// The reason's name: "no_device", "no_driver", "not_built", "not_pinned".
std::string_view name(Reason reason) noexcept;

struct Gap {
    Reason reason;
};

// A device's memory, a move-only value; its destructor frees it.
template <class B>
concept Buffer = std::movable<B> && !std::copyable<B> && requires(const B& b) {
    { b.size() } -> std::same_as<std::uint64_t>;
};

// A kernel's launch: views of the caller's name, buffers and scalars, used
// within the call.
template <class B>
struct Launch {
    std::string_view kernel;
    std::span<B* const> buffers;
    std::span<const std::int64_t> scalars;
    std::array<std::uint32_t, 3> grid;
};

// facts(out): the CPU's threads and vector sets, the devices each back end
// finds; never fails: a back end that cannot answer finds no device.
// device(capability, out): for "cpu", the device whose back end is "cpu";
// for "gpu", the first in facts()'s order whose back end is not "cpu"; for a
// back end's name ("cuda"), that back end's first; written into out, or the
// gap. allocate: memory past what the device has free. upload, download:
// size for a span not the buffer's size. run: runs a kernel by name and
// returns at its end: kernel for a name no kernel has, launch for a back
// end's failure.
template <class C>
concept Compute = Buffer<typename C::Buffer> &&
    requires(C& c, std::string_view s, Facts& f, Device& d, std::uint64_t n, typename C::Buffer& b,
             std::span<const std::byte> in, std::span<std::byte> out, const Launch<typename C::Buffer>& l) {
        c.facts(f);
        { c.device(s, d) } -> std::same_as<std::optional<Gap>>;
        { c.allocate(d, n) } -> std::same_as<typename C::Buffer>;
        c.upload(b, in);
        c.download(std::as_const(b), out);
        c.run(d, l);
    };

// A kernel's CPU reference, the oracle each back end's code is compared
// with: it reads and writes the launch's buffers as host bytes, in order. A
// plain function; its own failure is HalError launch.
using Reference = void (*)(std::span<const std::span<std::byte>> buffers, std::span<const std::int64_t> scalars,
                           const std::array<std::uint32_t, 3>& grid);

// The most kernels a Kernels holds, and the bytes of a kernel's name.
inline constexpr std::size_t kernels_max = 16;
inline constexpr std::size_t kernel_name_bytes = 32;

// The kernels by name, each with its CPU reference, held in place; a back
// end adds its own code beside each. A name added twice, a null reference,
// an empty name or one past kernel_name_bytes, or a kernel past kernels_max
// is a caller's mistake.
class Kernels {
public:
    void add(std::string_view name, Reference reference) noexcept;
    // The name's reference, or null.
    [[nodiscard]] const Reference* find(std::string_view name) const noexcept;

private:
    struct Entry {
        memory::Inline<kernel_name_bytes> name;
        Reference reference = nullptr;
    };
    std::array<Entry, kernels_max> entries_{};
    std::size_t size_ = 0;
};

enum class Code : std::uint8_t { memory, size, kernel, launch };

// The code's name, as the text spells it: "memory", "size", ...
std::string_view name(Code code) noexcept;

// The bytes a HalError's detail holds in place.
inline constexpr std::size_t detail_bytes = 256;

// A compute operation's typed error, as the platform's PlatformError is: a
// plain value, thrown by value and caught by its own type, with no base
// class and no heap allocation.
struct HalError {
    HalError(Code code_, std::string_view operation_, std::string_view detail_) noexcept
        : code(code_), operation(operation_), detail(detail_) {}

    Code code;
    memory::Inline<32> operation;
    memory::Inline<detail_bytes> detail;
};

// The pinned sizes: on a 64-bit system; the containers' on x64 with MSVC's
// library without its debug iterators, which add a pointer to each.
static_assert(sizeof(Gap) == 1, "Gap's size is pinned");
#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(Launch<int>) == 64, "Launch's size is pinned: 4 bytes of padding after grid");
static_assert(sizeof(Kernels) == kernels_max * 48 + 8, "Kernels' size is pinned");
static_assert(sizeof(HalError) == 312, "HalError's size is pinned: 7 bytes of padding after code");
#endif
#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(Device) == 128, "Device's size is pinned");
static_assert(sizeof(Facts) == 40, "Facts' size is pinned");
#endif

}  // namespace qiven3::hal
