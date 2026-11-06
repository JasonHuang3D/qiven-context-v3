#pragma once

// The drive's requests on the files' handles (driver/platform_ops.hpp),
// which driver/platform_ops_impl.hpp alone includes: stage, a Staged held
// by an id; staged_open, a Sink of a held Staged, held by an id;
// sink_write, its bytes; publish and discard, each ending its
// Staged when it succeeds and keeping it when refused. Each is the
// contract's member, called as a caller calls it, on the member's budget;
// files take no memory.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "driver/drive.hpp"
#include "driver/platform_ops.hpp"

namespace qiven3::drive::ops {

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
std::optional<Value> stage(PlatformDrive<P>& d, qiven::memory::Arena&, ArgView request) {
    return keep(d, d.staged(), d.platform().stage(text(request, "path")));
}

template <class P>
std::optional<Value> staged_open(PlatformDrive<P>& d, qiven::memory::Arena&, ArgView request) {
    const auto staged = by_id(d.staged(), request, "staging directory");
    return keep(d, d.sinks(), staged->second.open(text(request, "path")));
}

template <class P>
std::optional<Value> sink_write(PlatformDrive<P>& d, qiven::memory::Arena&, ArgView request) {
    by_id(d.sinks(), request, "file")->second.write(from_hex(text(request, "data")));
    return std::nullopt;
}

template <class P>
std::optional<Value> publish(PlatformDrive<P>& d, qiven::memory::Arena&, ArgView request) {
    const auto found = by_id(d.staged(), request, "staging directory");
    found->second.publish(text(request, "name"), deadline(request, "deadline"));
    d.staged().erase(found);
    return std::nullopt;
}

template <class P>
std::optional<Value> discard(PlatformDrive<P>& d, qiven::memory::Arena&, ArgView request) {
    const auto found = by_id(d.staged(), request, "staging directory");
    found->second.discard();
    d.staged().erase(found);
    return std::nullopt;
}

}  // namespace qiven3::drive::ops
