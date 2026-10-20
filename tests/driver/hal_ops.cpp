// The drive's HAL operations, on the HAL's fake (--hal fake), in a build
// with QIVEN3_HAL on alone.
//
// hal_fake gives the fake its device set: "threads", "vectors" by name,
// "devices" (each {"id", "back_end", "name", "memory"}) and "gaps" (a
// capability's reason); the fake starts with 1 thread and nothing else. A
// device crosses as that object; a buffer as the number allocate gave. The
// kernels are the drive's own, each with its CPU reference:
//   add_i64: three buffers of int64 in the host's order, out = a + b.

#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "driver/drive.hpp"
#include "support/fake_hal/fake_hal.hpp"

namespace qiven3::drive {
namespace {

using Result = std::optional<Value>;

constexpr hal::Vector all_vectors[] = {hal::Vector::sse2, hal::Vector::sse4_2, hal::Vector::avx,
                                       hal::Vector::fma,  hal::Vector::avx2,   hal::Vector::avx512f,
                                       hal::Vector::neon};

struct Hal {
    std::unique_ptr<hal::fake::FakeCompute> compute;
    std::map<std::int64_t, std::unique_ptr<hal::Buffer>> buffers;
    std::int64_t next = 1;
};

hal::Kernels kernels() {
    hal::Kernels k;
    k.add("add_i64", [](std::span<const std::span<std::byte>> buffers, std::span<const std::int64_t>,
                        const std::array<std::uint32_t, 3>&) {
        if (buffers.size() != 3 || buffers[0].size() != buffers[1].size() ||
            buffers[0].size() != buffers[2].size() || buffers[0].size() % 8 != 0) {
            throw std::invalid_argument("add_i64 takes three buffers of one size, of int64");
        }
        for (std::size_t at = 0; at < buffers[0].size(); at += 8) {
            std::int64_t a = 0, b = 0;
            std::memcpy(&a, buffers[0].data() + at, 8);
            std::memcpy(&b, buffers[1].data() + at, 8);
            const auto sum = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
            std::memcpy(buffers[2].data() + at, &sum, 8);
        }
    });
    return k;
}

Hal& hal_of(Session& s) {
    if (!s.options().hal_fake) {
        throw RequestError("no HAL selected: --hal fake");
    }
    std::shared_ptr<void>& slot = s.state("hal");
    if (!slot) {
        auto made = std::make_shared<Hal>();
        hal::fake::Config config;
        config.kernels = kernels();
        made->compute = std::make_unique<hal::fake::FakeCompute>(std::move(config));
        slot = made;
    }
    return *static_cast<Hal*>(slot.get());
}

hal::Device device_of(const Value& value) {
    hal::Device d;
    d.id = text(value, "id");
    d.back_end = text(value, "back_end");
    d.name = text(value, "name");
    d.memory = static_cast<std::uint64_t>(integer(value, "memory"));
    return d;
}

Value device_value(const hal::Device& d) {
    Value value;
    value.set("id", d.id);
    value.set("back_end", d.back_end);
    value.set("name", d.name);
    value.set("memory", static_cast<std::int64_t>(d.memory));
    return value;
}

hal::Buffer& buffer_of(Hal& h, std::int64_t id) {
    const auto found = h.buffers.find(id);
    if (found == h.buffers.end()) {
        throw RequestError("no buffer " + std::to_string(id));
    }
    return *found->second;
}

std::span<const std::byte> bytes_of(const std::string& data) {
    return std::as_bytes(std::span<const char>(data.data(), data.size()));
}

Result hal_fake(Session& s, const Value& request) {
    Hal& h = hal_of(s);
    hal::fake::Config config;
    config.cpu_threads = static_cast<unsigned>(integer(request, "threads", 1));
    if (const Value* vectors = request.find("vectors")) {
        for (const Value& item : vectors->as_array("vectors")) {
            bool known = false;
            for (const hal::Vector v : all_vectors) {
                if (hal::vector_name(v) == item.as_string("vectors")) {
                    config.vectors.insert(v);
                    known = true;
                }
            }
            if (!known) {
                throw RequestError("vectors: no set " + item.as_string("vectors"));
            }
        }
    }
    if (const Value* devices = request.find("devices")) {
        for (const Value& item : devices->as_array("devices")) {
            config.devices.push_back(device_of(item));
        }
    }
    if (const Value* gaps = request.find("gaps")) {
        for (const std::string& key : gaps->keys()) {
            config.gaps[key] = gaps->find(key)->as_string("gaps");
        }
    }
    config.kernels = kernels();
    h.buffers.clear();
    h.compute = std::make_unique<hal::fake::FakeCompute>(std::move(config));
    return std::nullopt;
}

Result hal_facts(Session& s, const Value&) {
    const hal::Facts f = hal_of(s).compute->facts();
    Value value;
    value.set("cpu_threads", static_cast<std::int64_t>(f.cpu_threads));
    Value vectors = Value::array();
    for (const hal::Vector v : f.vectors) {
        vectors.push(Value(std::string(hal::vector_name(v))));
    }
    value.set("vectors", std::move(vectors));
    Value devices = Value::array();
    for (const hal::Device& d : f.devices) {
        devices.push(device_value(d));
    }
    value.set("devices", std::move(devices));
    return value;
}

Result hal_device(Session& s, const Value& request) {
    const auto found = hal_of(s).compute->device(text(request, "capability"));
    Value value;
    if (const auto* d = std::get_if<hal::Device>(&found)) {
        value.set("device", device_value(*d));
    } else {
        const auto& gap = std::get<hal::Gap>(found);
        Value g;
        g.set("capability", gap.capability);
        g.set("reason", gap.reason);
        value.set("gap", std::move(g));
    }
    return value;
}

Result hal_allocate(Session& s, const Value& request) {
    Hal& h = hal_of(s);
    auto buffer = h.compute->allocate(device_of(argument(request, "device")),
                                      static_cast<std::uint64_t>(integer(request, "size")));
    const std::int64_t id = h.next++;
    h.buffers.emplace(id, std::move(buffer));
    return Value(id);
}

Result hal_upload(Session& s, const Value& request) {
    Hal& h = hal_of(s);
    const std::string data = from_hex(text(request, "data"));
    h.compute->upload(buffer_of(h, integer(request, "buffer")), bytes_of(data));
    return std::nullopt;
}

// "size", the span's, is the buffer's when absent.
Result hal_download(Session& s, const Value& request) {
    Hal& h = hal_of(s);
    const hal::Buffer& buffer = buffer_of(h, integer(request, "buffer"));
    const std::int64_t size = integer(request, "size", static_cast<std::int64_t>(buffer.size()));
    if (size < 0) {
        throw RequestError("size: not negative");
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    h.compute->download(buffer, std::as_writable_bytes(std::span<char>(out.data(), out.size())));
    return Value(to_hex(out));
}

Result hal_free(Session& s, const Value& request) {
    Hal& h = hal_of(s);
    buffer_of(h, integer(request, "buffer"));
    h.buffers.erase(integer(request, "buffer"));
    return std::nullopt;
}

Result hal_run(Session& s, const Value& request) {
    Hal& h = hal_of(s);
    hal::Launch launch;
    launch.kernel = text(request, "kernel");
    if (const Value* buffers = request.find("buffers")) {
        for (const Value& item : buffers->as_array("buffers")) {
            launch.buffers.push_back(&buffer_of(h, item.as_integer("buffers")));
        }
    }
    if (const Value* scalars = request.find("scalars")) {
        for (const Value& item : scalars->as_array("scalars")) {
            launch.scalars.push_back(item.as_integer("scalars"));
        }
    }
    launch.grid = {1, 1, 1};
    if (const Value* grid = request.find("grid")) {
        const auto& items = grid->as_array("grid");
        if (items.size() != 3) {
            throw RequestError("grid: three integers");
        }
        for (std::size_t i = 0; i < 3; ++i) {
            launch.grid[i] = static_cast<std::uint32_t>(items[i].as_integer("grid"));
        }
    }
    h.compute->run(device_of(argument(request, "device")), launch);
    return std::nullopt;
}

Result hal_counts(Session& s, const Value&) {
    const hal::fake::Counts c = hal_of(s).compute->counts();
    Value value;
    value.set("allocations", static_cast<std::int64_t>(c.allocations));
    value.set("uploads", static_cast<std::int64_t>(c.uploads));
    value.set("downloads", static_cast<std::int64_t>(c.downloads));
    value.set("runs", static_cast<std::int64_t>(c.runs));
    return value;
}

}  // namespace

void add_hal_operations(Operations& into) {
    const std::pair<const char*, Result (*)(Session&, const Value&)> table[] = {
        {"hal_fake", hal_fake},         {"hal_facts", hal_facts}, {"hal_device", hal_device},
        {"hal_allocate", hal_allocate}, {"hal_upload", hal_upload}, {"hal_download", hal_download},
        {"hal_free", hal_free},         {"hal_run", hal_run},     {"hal_counts", hal_counts},
    };
    for (const auto& [key, operation] : table) {
        into.emplace(key, operation);
    }
}

}  // namespace qiven3::drive
