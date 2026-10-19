#include "support/fake_hal/fake_hal.hpp"

#include <algorithm>
#include <exception>
#include <mutex>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace qiven3::hal::fake {

struct FakeCompute::State {
    std::mutex mutex;
    std::map<std::string, std::uint64_t, std::less<>> used;  // by device id
    Counts counts;
};

namespace {

bool known_reason(const std::string& reason) {
    return reason == "no_device" || reason == "no_driver" || reason == "not_built" ||
           reason == "not_pinned";
}

bool matches(std::string_view capability, const Device& device) {
    if (capability == "gpu") {
        return device.back_end != "cpu";
    }
    return device.back_end == capability;
}

class FakeBuffer final : public Buffer {
public:
    FakeBuffer(std::shared_ptr<FakeCompute::State> state, std::string device, std::uint64_t size)
        : state_(std::move(state)), device_(std::move(device)),
          bytes_(static_cast<std::size_t>(size)) {}

    ~FakeBuffer() override {
        std::lock_guard<std::mutex> guard(state_->mutex);
        state_->used[device_] -= bytes_.size();
    }

    FakeBuffer(const FakeBuffer&) = delete;
    FakeBuffer& operator=(const FakeBuffer&) = delete;

    std::uint64_t size() const override { return bytes_.size(); }

    const FakeCompute::State* state() const { return state_.get(); }
    const std::string& device() const { return device_; }
    std::span<std::byte> bytes() { return bytes_; }
    std::span<const std::byte> bytes() const { return bytes_; }

private:
    std::shared_ptr<FakeCompute::State> state_;
    std::string device_;
    std::vector<std::byte> bytes_;
};

}  // namespace

FakeCompute::FakeCompute(Config config)
    : config_(std::move(config)), state_(std::make_shared<State>()) {
    std::set<std::string> ids;
    for (const Device& device : config_.devices) {
        if (!ids.insert(device.id).second) {
            throw std::invalid_argument("fake compute: device " + device.id + " given twice");
        }
        state_->used[device.id] = 0;
    }
    for (const auto& [capability, reason] : config_.gaps) {
        if (!known_reason(reason)) {
            throw std::invalid_argument("fake compute: gap " + capability + ": reason " + reason);
        }
    }
}

FakeCompute::~FakeCompute() = default;

Facts FakeCompute::facts() {
    return Facts{config_.cpu_threads, config_.vectors, config_.devices};
}

std::variant<Device, Gap> FakeCompute::device(std::string_view capability) {
    for (const Device& device : config_.devices) {
        if (matches(capability, device)) {
            return device;
        }
    }
    std::string name(capability);
    if (auto gap = config_.gaps.find(name); gap != config_.gaps.end()) {
        return Gap{name, gap->second};
    }
    if (capability == "gpu") {
        for (const auto& [back_end, reason] : config_.gaps) {
            if (back_end != "cpu" && back_end != "gpu") {
                return Gap{name, reason};
            }
        }
    }
    bool kind = capability == "cpu" || capability == "gpu";
    return Gap{name, kind ? "no_device" : "not_built"};
}

const Device& FakeCompute::known(const Device& d, const char* operation) const {
    for (const Device& device : config_.devices) {
        if (device.id == d.id) {
            return device;
        }
    }
    throw std::invalid_argument(std::string(operation) + ": no device " + d.id);
}

std::unique_ptr<Buffer> FakeCompute::allocate(const Device& d, std::uint64_t size) {
    const Device& device = known(d, "allocate");
    std::lock_guard<std::mutex> guard(state_->mutex);
    std::uint64_t used = state_->used[device.id];
    std::uint64_t available = device.memory - std::min(used, device.memory);
    if (size > available) {
        throw HalError(Code::memory, "allocate",
                       std::to_string(size) + " bytes, " + std::to_string(available) +
                           " free on " + device.id);
    }
    if (static_cast<std::uint64_t>(static_cast<std::size_t>(size)) != size) {
        throw HalError(Code::memory, "allocate",
                       std::to_string(size) + " bytes: past the host's size");
    }
    std::unique_ptr<Buffer> buffer;
    try {
        buffer = std::make_unique<FakeBuffer>(state_, device.id, size);
    } catch (const std::bad_alloc&) {
        throw HalError(Code::memory, "allocate",
                       std::to_string(size) + " bytes: host memory exhausted");
    } catch (const std::length_error&) {
        throw HalError(Code::memory, "allocate",
                       std::to_string(size) + " bytes: past the host's size");
    }
    state_->used[device.id] = used + size;
    ++state_->counts.allocations;
    return buffer;
}

namespace {

template <typename B>
auto& ours(B& b, const FakeCompute::State* state, const char* operation) {
    using Fake = std::conditional_t<std::is_const_v<B>, const FakeBuffer, FakeBuffer>;
    auto* fake = dynamic_cast<Fake*>(&b);
    if (fake == nullptr || fake->state() != state) {
        throw std::invalid_argument(std::string(operation) + ": not this compute's buffer");
    }
    return *fake;
}

std::string sizes(std::size_t span, std::uint64_t buffer) {
    return "a span of " + std::to_string(span) + " bytes, a buffer of " +
           std::to_string(buffer);
}

}  // namespace

void FakeCompute::upload(Buffer& b, std::span<const std::byte> bytes) {
    FakeBuffer& buffer = ours(b, state_.get(), "upload");
    if (bytes.size() != buffer.size()) {
        throw HalError(Code::size, "upload", sizes(bytes.size(), buffer.size()));
    }
    std::copy(bytes.begin(), bytes.end(), buffer.bytes().begin());
    std::lock_guard<std::mutex> guard(state_->mutex);
    ++state_->counts.uploads;
}

void FakeCompute::download(const Buffer& b, std::span<std::byte> bytes) {
    const FakeBuffer& buffer = ours(b, state_.get(), "download");
    if (bytes.size() != buffer.size()) {
        throw HalError(Code::size, "download", sizes(bytes.size(), buffer.size()));
    }
    std::span<const std::byte> source = buffer.bytes();
    std::copy(source.begin(), source.end(), bytes.begin());
    std::lock_guard<std::mutex> guard(state_->mutex);
    ++state_->counts.downloads;
}

void FakeCompute::run(const Device& d, const Launch& launch) {
    const Device& device = known(d, "run");
    const Reference* reference = config_.kernels.find(launch.kernel);
    if (reference == nullptr) {
        throw HalError(Code::kernel, "run", "no kernel " + launch.kernel);
    }
    std::vector<std::span<std::byte>> views;
    views.reserve(launch.buffers.size());
    for (Buffer* b : launch.buffers) {
        if (b == nullptr) {
            throw std::invalid_argument("run: a null buffer");
        }
        FakeBuffer& buffer = ours(*b, state_.get(), "run");
        if (buffer.device() != device.id) {
            throw std::invalid_argument("run: a buffer of " + buffer.device() + " on " +
                                        device.id);
        }
        views.push_back(buffer.bytes());
    }
    try {
        (*reference)(views, launch.scalars, launch.grid);
    } catch (const HalError&) {
        throw;
    } catch (const std::exception& error) {
        throw HalError(Code::launch, "run", launch.kernel + ": " + error.what());
    }
    std::lock_guard<std::mutex> guard(state_->mutex);
    ++state_->counts.runs;
}

Counts FakeCompute::counts() const {
    std::lock_guard<std::mutex> guard(state_->mutex);
    return state_->counts;
}

std::uint64_t FakeCompute::used(std::string_view device_id) const {
    std::lock_guard<std::mutex> guard(state_->mutex);
    auto found = state_->used.find(device_id);
    return found == state_->used.end() ? 0 : found->second;
}

}  // namespace qiven3::hal::fake
