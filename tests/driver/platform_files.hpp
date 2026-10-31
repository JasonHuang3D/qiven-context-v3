#pragma once

// The drive's requests on the files' handles (driver/platform_ops.hpp),
// which driver/platform_ops_impl.hpp alone includes: stage, a Staged held
// by an id; staged_open, a Sink of a held Staged, held by an id;
// sink_write, its bytes; sink_close; publish and discard, each ending its
// Staged when it succeeds and keeping it when refused; lock, a Lock held
// by an id; lock_release. Each is the contract's member, called as a
// caller calls it, on the member's budget; files take no memory.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "driver/drive.hpp"
#include "driver/platform_ops.hpp"

namespace qiven3::drive::ops {

// A path's bytes: "<key>_hex" in hexadecimal where given, for one that is
// no UTF-8, else "<key>" as text.
inline std::string given_path(ArgView request, std::string_view key) {
    const std::string hex = std::string(key) + "_hex";
    return request.find(hex) ? from_hex(text(request, hex)) : text(request, key);
}

// A handle held by the request's "id", or RequestError naming what.
template <class Map>
auto by_id(Map& map, ArgView request, std::string_view what) {
    const auto found = map.find(integer(request, "id"));
    if (found == map.end()) {
        throw RequestError("no " + std::string(what) + " held by that id");
    }
    return found;
}

// The handle kept by a new id, given as {"id": n}.
template <class P, class Map, class H>
Value keep(PlatformDrive<P>& d, Map& map, H&& handle) {
    const std::int64_t id = d.next_id();
    map.emplace(id, std::forward<H>(handle));
    Value value;
    value.set("id", id);
    return value;
}

template <class P>
std::optional<Value> stage(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    return keep(d, d.staged(), d.platform().stage(given_path(request, "path")));
}

template <class P>
std::optional<Value> staged_open(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    const auto staged = by_id(d.staged(), request, "staging directory");
    return keep(d, d.sinks(), staged->second.open(given_path(request, "path")));
}

template <class P>
std::optional<Value> sink_write(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    by_id(d.sinks(), request, "file")->second.write(from_hex(text(request, "data")));
    return std::nullopt;
}

// The file closed and its handle ended, also when the close is refused.
template <class P>
std::optional<Value> sink_close(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    const auto found = by_id(d.sinks(), request, "file");
    typename PlatformDrive<P>::Sink sink = std::move(found->second);
    d.sinks().erase(found);
    sink.close();
    return std::nullopt;
}

template <class P>
std::optional<Value> publish(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    const auto found = by_id(d.staged(), request, "staging directory");
    found->second.publish(text(request, "name"), deadline(request, "deadline"));
    d.staged().erase(found);
    return std::nullopt;
}

template <class P>
std::optional<Value> discard(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    const auto found = by_id(d.staged(), request, "staging directory");
    found->second.discard();
    d.staged().erase(found);
    return std::nullopt;
}

template <class P>
std::optional<Value> lock(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    return keep(d, d.locks(), d.platform().lock(text(request, "path"), deadline(request, "deadline")));
}

template <class P>
std::optional<Value> lock_release(PlatformDrive<P>& d, memory::Arena&, ArgView request) {
    const auto found = by_id(d.locks(), request, "lock");
    found->second.release();
    d.locks().erase(found);
    return std::nullopt;
}

}  // namespace qiven3::drive::ops
