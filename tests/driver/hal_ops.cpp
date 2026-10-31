// The drive's HAL operations, on the HAL's fake (--hal fake), in a build
// with QIVEN_HAL on alone (driver/hal_ops.hpp). The contract's operations
// are templates on the compute C, instantiated for the fake; hal_fake and
// hal_counts are the fake's own.
//
// hal_fake gives both fakes, the drive's and the other one, its device set:
// "threads", "vectors" by name, "devices" (each {"id", "back_end", "name",
// "memory"}) and "gaps" (a capability's reason), ending every buffer; the
// fakes start with 1 thread and nothing else. A device crosses as that
// object; a buffer as the number allocate gave. hal_allocate with "other"
// true takes the buffer from the other fake; every other operation is the
// drive's fake's, so an upload into that buffer is a caller's mistake and
// ends the process. A device's reply is {"device": {...}}, a gap's
// {"gap": {"reason": ...}}. The kernels are the drive's own, each with its
// CPU reference:
//   add_i64: three buffers of int64 in the host's order, out = a + b.
//
// Each request's devices and facts are written into an arena over the HAL
// state's own pages, the members' budget of 64 KiB unless the request
// gives its "budget", and leave it as the reply's text before the arena
// ends; the arena's record goes to the reply (driver/drive.hpp).

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "driver/drive.hpp"
#include "memory/arena.hpp"

namespace qiven3::drive {
namespace {

using Result = std::optional<Value>;
using Fake = hal::fake::FakeCompute;

constexpr std::array<hal::Vector, hal::vector_sets> all_vectors = {
    hal::Vector::sse2, hal::Vector::sse4_2,  hal::Vector::avx,  hal::Vector::fma,
    hal::Vector::avx2, hal::Vector::avx512f, hal::Vector::neon,
};

constexpr std::array<hal::Reason, 4> all_reasons = {hal::Reason::no_device, hal::Reason::no_driver,
                                                    hal::Reason::not_built, hal::Reason::not_pinned};

void add_i64(std::span<const std::span<std::byte>> buffers, std::span<const std::int64_t>,
             const std::array<std::uint32_t, 3>&) {
    if (buffers.size() != 3 || buffers[0].size() != buffers[1].size() || buffers[0].size() != buffers[2].size() ||
        buffers[0].size() % 8 != 0) {
        throw hal::HalError(hal::Code::launch, "run", "add_i64 takes three buffers of one size, of int64");
    }
    for (std::size_t at = 0; at < buffers[0].size(); at += 8) {
        std::int64_t a = 0, b = 0;
        std::memcpy(&a, buffers[0].subspan(at, 8).data(), 8);
        std::memcpy(&b, buffers[1].subspan(at, 8).data(), 8);
        const auto sum = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
        std::memcpy(buffers[2].subspan(at, 8).data(), &sum, 8);
    }
}

hal::fake::Config base_config() {
    hal::fake::Config config;
    config.kernels.add("add_i64", add_i64);
    return config;
}

template <hal::Compute C>
void reset(HalState<C>& h, const hal::fake::Config& config) {
    h.buffers.clear();
    for (std::optional<C>& compute : h.computes) {
        compute.reset();
        compute.emplace(config);
    }
}

Hal& hal_of(Session& s) {
    if (!s.options().hal_fake) {
        throw RequestError("no HAL selected: --hal fake");
    }
    Hal& h = s.hal();
    if (!h.computes[0]) {
        h.pages.resize(member_budget);
        reset(h, base_config());
    }
    return h;
}

std::string_view view(const memory::String& text) { return std::string_view(text.data(), text.size()); }

hal::Device device_of(memory::Arena& arena, ArgView value) {
    hal::Device d(arena);
    const std::string id = text(value, "id"), back_end = text(value, "back_end"), name = text(value, "name");
    d.id.assign(id.data(), id.size());
    d.back_end.assign(back_end.data(), back_end.size());
    d.name.assign(name.data(), name.size());
    d.memory = static_cast<std::uint64_t>(integer(value, "memory"));
    return d;
}

Value device_value(const hal::Device& d) {
    Value value;
    value.set("id", std::string(view(d.id)));
    value.set("back_end", std::string(view(d.back_end)));
    value.set("name", std::string(view(d.name)));
    value.set("memory", static_cast<std::int64_t>(d.memory));
    return value;
}

template <hal::Compute C>
typename HalState<C>::Held& held(HalState<C>& h, ArgView request, std::string_view key) {
    const std::int64_t id = integer(request, key);
    const auto found = h.buffers.find(id);
    if (found == h.buffers.end()) {
        throw RequestError("no buffer " + std::to_string(id));
    }
    return found->second;
}

template <hal::Compute C>
Result facts(HalState<C>& h, memory::Arena& arena, ArgView) {
    hal::Facts f(arena);
    h.computes[0]->facts(f);
    Value value;
    value.set("cpu_threads", static_cast<std::int64_t>(f.cpu_threads));
    Value vectors = array();
    for (const hal::Vector v : all_vectors) {
        if (f.vectors.test(static_cast<std::size_t>(v))) {
            vectors.push(scalar(hal::name(v)));
        }
    }
    value.set("vectors", std::move(vectors));
    Value devices = array();
    for (const hal::Device& d : f.devices) {
        devices.push(device_value(d));
    }
    value.set("devices", std::move(devices));
    return value;
}

template <hal::Compute C>
Result device(HalState<C>& h, memory::Arena& arena, ArgView request) {
    hal::Device d(arena);
    const std::optional<hal::Gap> gap = h.computes[0]->device(text(request, "capability"), d);
    Value value;
    if (gap) {
        Value g;
        g.set("reason", std::string(hal::name(gap->reason)));
        value.set("gap", std::move(g));
    } else {
        value.set("device", device_value(d));
    }
    return value;
}

template <hal::Compute C>
Result allocate(HalState<C>& h, memory::Arena& arena, ArgView request) {
    const std::size_t from = flag(request, "other", false) ? 1 : 0;
    const hal::Device d = device_of(arena, argument(request, "device"));
    auto buffer = h.computes[from]->allocate(d, static_cast<std::uint64_t>(integer(request, "size")));
    const std::int64_t id = h.next++;
    h.buffers.emplace(id, typename HalState<C>::Held{std::move(buffer), from});
    return scalar(id);
}

std::span<const std::byte> bytes_of(const std::string& data) {
    return std::as_bytes(std::span<const char>(data.data(), data.size()));
}

template <hal::Compute C>
Result upload(HalState<C>& h, memory::Arena&, ArgView request) {
    const std::string data = from_hex(text(request, "data"));
    h.computes[0]->upload(held(h, request, "buffer").buffer, bytes_of(data));
    return std::nullopt;
}

// "size", the span's, is the buffer's when absent.
template <hal::Compute C>
Result download(HalState<C>& h, memory::Arena&, ArgView request) {
    const auto& buffer = held(h, request, "buffer").buffer;
    const std::int64_t size = integer(request, "size", static_cast<std::int64_t>(buffer.size()));
    if (size < 0) {
        throw RequestError("size: not negative");
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    h.computes[0]->download(buffer, std::as_writable_bytes(std::span<char>(out.data(), out.size())));
    return scalar(to_hex(out));
}

template <hal::Compute C>
Result free_buffer(HalState<C>& h, memory::Arena&, ArgView request) {
    held(h, request, "buffer");
    h.buffers.erase(integer(request, "buffer"));
    return std::nullopt;
}

template <hal::Compute C>
Result run(HalState<C>& h, memory::Arena& arena, ArgView request) {
    std::vector<typename C::Buffer*> buffers;
    if (const std::optional<ArgView> items = request.find("buffers")) {
        for (const ArgView item : items->as_array("buffers")) {
            const std::int64_t id = item.as_integer("buffers");
            const auto found = h.buffers.find(id);
            if (found == h.buffers.end()) {
                throw RequestError("no buffer " + std::to_string(id));
            }
            buffers.push_back(&found->second.buffer);
        }
    }
    std::vector<std::int64_t> scalars;
    if (const std::optional<ArgView> items = request.find("scalars")) {
        for (const ArgView item : items->as_array("scalars")) {
            scalars.push_back(item.as_integer("scalars"));
        }
    }
    std::array<std::uint32_t, 3> grid = {1, 1, 1};
    if (const std::optional<ArgView> items = request.find("grid")) {
        const auto& values = items->as_array("grid");
        if (values.size() != 3) {
            throw RequestError("grid: three integers");
        }
        for (std::size_t i = 0; i < 3; ++i) {
            grid.at(i) = static_cast<std::uint32_t>(values[i].as_integer("grid"));
        }
    }
    const std::string kernel = text(request, "kernel");
    const hal::Device d = device_of(arena, argument(request, "device"));
    h.computes[0]->run(d, hal::Launch<typename C::Buffer>{kernel, buffers, scalars, grid});
    return std::nullopt;
}

Result hal_fake(Hal& h, memory::Arena&, ArgView request) {
    hal::fake::Config config = base_config();
    config.cpu_threads = static_cast<unsigned>(integer(request, "threads", 1));
    if (const std::optional<ArgView> vectors = request.find("vectors")) {
        for (const ArgView item : vectors->as_array("vectors")) {
            bool known = false;
            for (const hal::Vector v : all_vectors) {
                if (hal::name(v) == item.as_string("vectors")) {
                    config.vectors.set(static_cast<std::size_t>(v));
                    known = true;
                }
            }
            if (!known) {
                throw RequestError("vectors: no set " + item.as_string("vectors"));
            }
        }
    }
    if (const std::optional<ArgView> devices = request.find("devices")) {
        for (const ArgView item : devices->as_array("devices")) {
            config.devices.push_back(hal::fake::FakeDevice{text(item, "id"), text(item, "back_end"),
                                                           text(item, "name"),
                                                           static_cast<std::uint64_t>(integer(item, "memory"))});
        }
    }
    if (const std::optional<ArgView> gaps = request.find("gaps")) {
        for (const auto& [key, member] : gaps->as_object("gaps")) {
            const std::string reason = member.as_string("gaps");
            bool known = false;
            for (const hal::Reason r : all_reasons) {
                if (hal::name(r) == reason) {
                    config.gaps.emplace_back(key, r);
                    known = true;
                }
            }
            if (!known) {
                throw RequestError("gaps: no reason " + reason);
            }
        }
    }
    reset(h, config);
    return std::nullopt;
}

Result hal_counts(Hal& h, memory::Arena&, ArgView) {
    const hal::fake::Counts& c = h.computes[0]->counts();
    Value value;
    value.set("allocations", static_cast<std::int64_t>(c.allocations));
    value.set("uploads", static_cast<std::int64_t>(c.uploads));
    value.set("downloads", static_cast<std::int64_t>(c.downloads));
    value.set("runs", static_cast<std::int64_t>(c.runs));
    return value;
}

using HalOperation = Result (*)(Hal&, memory::Arena&, ArgView);

// The operation on the session's HAL, its results in an arena over the
// HAL's pages: the members' budget, or the request's.
template <HalOperation F>
Result bound(Session& s, ArgView request) {
    Hal& h = hal_of(s);
    const std::size_t bytes = budget(request, member_budget);
    if (h.pages.size() < bytes) {
        h.pages.resize(bytes);
    }
    return on_arena(s, std::span<std::byte>(h.pages).first(bytes),
                    [&](memory::Arena& arena) { return F(h, arena, request); });
}

}  // namespace

void add_hal_operations(Operations& into) {
    const std::pair<const char*, Operation> table[] = {
        {"hal_fake", bound<hal_fake>},
        {"hal_facts", bound<facts<Fake>>},
        {"hal_device", bound<device<Fake>>},
        {"hal_allocate", bound<allocate<Fake>>},
        {"hal_upload", bound<upload<Fake>>},
        {"hal_download", bound<download<Fake>>},
        {"hal_free", bound<free_buffer<Fake>>},
        {"hal_run", bound<run<Fake>>},
        {"hal_counts", bound<hal_counts>},
    };
    for (const auto& [key, operation] : table) {
        into.emplace(key, operation);
    }
}

}  // namespace qiven3::drive
