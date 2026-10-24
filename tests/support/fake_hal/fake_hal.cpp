#include "support/fake_hal/fake_hal.hpp"

#include <algorithm>
#include <array>
#include <new>
#include <span>
#include <stdexcept>

#include "memory/fail.hpp"

namespace qiven3::hal::fake {
namespace {

bool matches(std::string_view capability, const FakeDevice& device) {
    if (capability == "gpu") {
        return device.back_end != "cpu";
    }
    return device.back_end == capability;
}

void write(const FakeDevice& from, Device& out) {
    out.id.assign(from.id.data(), from.id.size());
    out.back_end.assign(from.back_end.data(), from.back_end.size());
    out.name.assign(from.name.data(), from.name.size());
    out.memory = from.memory;
}

// A HalError's detail composed in place, with no heap; the excess dropped.
class Text {
public:
    Text& add(std::string_view text) noexcept {
        const std::span<char> rest = std::span<char>(bytes_).subspan(size_);
        const std::size_t n = std::min(text.size(), rest.size());
        std::ranges::copy_n(text.begin(), static_cast<std::ptrdiff_t>(n), rest.begin());
        size_ += n;
        return *this;
    }
    // In decimal: the digits from the last, written from the end.
    Text& number(std::uint64_t value) noexcept {
        std::array<char, 20> digits{};
        std::size_t first = digits.size();
        do {
            --first;
            digits[first] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        std::string_view text(digits.data(), digits.size());
        text.remove_prefix(first);
        return add(text);
    }
    [[nodiscard]] std::string_view view() const noexcept { return std::string_view(bytes_.data(), size_); }

private:
    std::array<char, detail_bytes> bytes_{};
    std::size_t size_ = 0;
};

HalError sizes(std::string_view operation, std::size_t span, std::uint64_t buffer) noexcept {
    Text text;
    text.add("a span of ").number(span).add(" bytes, a buffer of ").number(buffer);
    return HalError(Code::size, operation, text.view());
}

}  // namespace

FakeBuffer::FakeBuffer(FakeBuffer&& other) noexcept
    : bytes_(std::exchange(other.bytes_, {})), owner_(std::exchange(other.owner_, nullptr)), device_(other.device_) {}

FakeBuffer& FakeBuffer::operator=(FakeBuffer&& other) noexcept {
    if (this != &other) {
        release();
        bytes_ = std::exchange(other.bytes_, {});
        owner_ = std::exchange(other.owner_, nullptr);
        device_ = other.device_;
    }
    return *this;
}

FakeBuffer::~FakeBuffer() { release(); }

void FakeBuffer::release() noexcept {
    if (owner_ != nullptr) {
        owner_->used[device_] -= bytes_.size();
        --owner_->live;
        owner_ = nullptr;
    }
    bytes_ = std::vector<std::byte>();
}

FakeCompute::FakeCompute(Config config) : config_(std::move(config)) {
    for (std::size_t at = 0; at < config_.devices.size(); ++at) {
        for (std::size_t before = 0; before < at; ++before) {
            if (config_.devices[before].id == config_.devices[at].id) {
                memory::fail_fast("hal::fake::FakeCompute: a device id given twice");
            }
        }
    }
    state_.used.assign(config_.devices.size(), 0);
}

FakeCompute::~FakeCompute() {
    if (state_.live != 0) {
        memory::fail_fast("hal::fake::FakeCompute: ended with its buffers live");
    }
}

void FakeCompute::facts(Facts& out) const {
    out.cpu_threads = config_.cpu_threads;
    out.vectors = config_.vectors;
    out.devices.clear();
    out.devices.reserve(config_.devices.size());
    for (const FakeDevice& from : config_.devices) {
        Device& device = out.devices.emplace_back(out.devices.get_allocator().arena());
        write(from, device);
    }
}

std::optional<Gap> FakeCompute::device(std::string_view capability, Device& out) const {
    for (const FakeDevice& device : config_.devices) {
        if (matches(capability, device)) {
            write(device, out);
            return std::nullopt;
        }
    }
    for (const auto& [name, reason] : config_.gaps) {
        if (name == capability) {
            return Gap{reason};
        }
    }
    if (capability == "gpu") {
        for (const auto& [name, reason] : config_.gaps) {
            if (name != "cpu" && name != "gpu") {
                return Gap{reason};
            }
        }
    }
    return Gap{capability == "cpu" || capability == "gpu" ? Reason::no_device : Reason::not_built};
}

std::size_t FakeCompute::known(const Device& d) const noexcept {
    for (std::size_t at = 0; at < config_.devices.size(); ++at) {
        if (std::string_view(config_.devices[at].id) == std::string_view(d.id)) {
            return at;
        }
    }
    memory::fail_fast("hal::fake::FakeCompute: a device this compute was not given");
}

void FakeCompute::ours(const FakeBuffer& b) const noexcept {
    if (b.owner_ != &state_) {
        memory::fail_fast("hal::fake::FakeCompute: a buffer this compute did not give");
    }
}

FakeBuffer FakeCompute::allocate(const Device& d, std::uint64_t size) {
    const std::size_t at = known(d);
    const std::uint64_t given = config_.devices[at].memory;
    const std::uint64_t available = given - std::min(state_.used[at], given);
    if (size > available) {
        Text text;
        text.number(size).add(" bytes, ").number(available).add(" free on ").add(config_.devices[at].id);
        throw HalError(Code::memory, "allocate", text.view());
    }
    std::vector<std::byte> bytes;
    try {
        bytes.resize(static_cast<std::size_t>(size));
    } catch (const std::bad_alloc&) {
        throw HalError(Code::memory, "allocate", "host memory exhausted");
    } catch (const std::length_error&) {
        throw HalError(Code::memory, "allocate", "past the host's size");
    }
    state_.used[at] += size;
    ++state_.live;
    ++state_.counts.allocations;
    return FakeBuffer(state_, at, std::move(bytes));
}

void FakeCompute::upload(FakeBuffer& b, std::span<const std::byte> bytes) {
    ours(b);
    if (bytes.size() != b.size()) {
        throw sizes("upload", bytes.size(), b.size());
    }
    std::copy(bytes.begin(), bytes.end(), b.bytes_.begin());
    ++state_.counts.uploads;
}

void FakeCompute::download(const FakeBuffer& b, std::span<std::byte> bytes) {
    ours(b);
    if (bytes.size() != b.size()) {
        throw sizes("download", bytes.size(), b.size());
    }
    std::copy(b.bytes_.begin(), b.bytes_.end(), bytes.begin());
    ++state_.counts.downloads;
}

void FakeCompute::run(const Device& d, const Launch<FakeBuffer>& launch) {
    const std::size_t at = known(d);
    for (const FakeBuffer* b : launch.buffers) {
        if (b == nullptr) {
            memory::fail_fast("hal::fake::FakeCompute::run: a null buffer");
        }
        ours(*b);
        if (b->device_ != at) {
            memory::fail_fast("hal::fake::FakeCompute::run: a buffer of another device");
        }
    }
    const Reference* reference = config_.kernels.find(launch.kernel);
    if (reference == nullptr) {
        Text text;
        text.add("no kernel ").add(launch.kernel);
        throw HalError(Code::kernel, "run", text.view());
    }
    std::vector<std::span<std::byte>> views;
    views.reserve(launch.buffers.size());
    for (FakeBuffer* b : launch.buffers) {
        views.emplace_back(b->bytes_);
    }
    (*reference)(views, launch.scalars, launch.grid);
    ++state_.counts.runs;
}

std::uint64_t FakeCompute::used(std::string_view device_id) const noexcept {
    for (std::size_t at = 0; at < config_.devices.size(); ++at) {
        if (config_.devices[at].id == device_id) {
            return state_.used[at];
        }
    }
    return 0;
}

}  // namespace qiven3::hal::fake
