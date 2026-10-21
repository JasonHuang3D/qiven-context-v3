// body's instantiation for v3's allocator: the layout's reader and the
// record's writer over an arena, compiled once here. Body, make, parse and
// check are templates on the platform too, instantiated where a binary
// links its adapter.

#include "entry/body.hpp"

#include <span>
#include <string_view>

#include "memory/memory.hpp"

namespace qiven3::entry::body {

using Alloc = memory::Allocator<char>;

namespace detail {

namespace {
struct PutNone {
    void operator()(std::span<char, key_size> /*hex*/) const noexcept {}
};
}  // namespace

template std::optional<Code> layout(const json::Basic<Alloc>&);
template void write(std::string_view, std::string_view, const PutNone&, memory::String&);

}  // namespace detail

}  // namespace qiven3::entry::body
