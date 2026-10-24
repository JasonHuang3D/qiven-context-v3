#include "hal/hal.hpp"

#include "memory/fail.hpp"

namespace qiven3::hal {

std::string_view name(Vector vector) noexcept {
    switch (vector) {
    case Vector::sse2:
        return "sse2";
    case Vector::sse4_2:
        return "sse4_2";
    case Vector::avx:
        return "avx";
    case Vector::fma:
        return "fma";
    case Vector::avx2:
        return "avx2";
    case Vector::avx512f:
        return "avx512f";
    case Vector::neon:
        return "neon";
    }
    return "unknown";
}

std::string_view name(Reason reason) noexcept {
    switch (reason) {
    case Reason::no_device:
        return "no_device";
    case Reason::no_driver:
        return "no_driver";
    case Reason::not_built:
        return "not_built";
    case Reason::not_pinned:
        return "not_pinned";
    }
    return "unknown";
}

std::string_view name(Code code) noexcept {
    switch (code) {
    case Code::memory:
        return "memory";
    case Code::size:
        return "size";
    case Code::kernel:
        return "kernel";
    case Code::launch:
        return "launch";
    }
    return "unknown";
}

void Kernels::add(std::string_view name, Reference reference) noexcept {
    if (reference == nullptr) {
        memory::fail_fast("hal::Kernels::add: a null reference");
    }
    if (name.empty() || name.size() > kernel_name_bytes) {
        memory::fail_fast("hal::Kernels::add: a name empty or past kernel_name_bytes");
    }
    if (find(name) != nullptr) {
        memory::fail_fast("hal::Kernels::add: a name added twice");
    }
    if (size_ == entries_.size()) {
        memory::fail_fast("hal::Kernels::add: a kernel past kernels_max");
    }
    entries_[size_] = Entry{memory::Inline<kernel_name_bytes>(name), reference};
    ++size_;
}

const Reference* Kernels::find(std::string_view name) const noexcept {
    for (std::size_t at = 0; at < size_; ++at) {
        if (entries_[at].name.view() == name) {
            return &entries_[at].reference;
        }
    }
    return nullptr;
}

}  // namespace qiven3::hal
