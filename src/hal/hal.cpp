#include "hal/hal.hpp"

#include <utility>

namespace qiven3::hal {
namespace {

std::string message(Code code, const std::string& operation, const std::string& detail) {
    std::string text = operation + ": " + std::string(code_name(code));
    if (!detail.empty()) {
        text += ": " + detail;
    }
    return text;
}

}  // namespace

std::string_view vector_name(Vector vector) noexcept {
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

std::string_view code_name(Code code) noexcept {
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

void Kernels::add(std::string name, Reference reference) {
    if (!reference) {
        throw std::invalid_argument("kernel " + name + ": no reference");
    }
    if (references_.contains(name)) {
        throw std::invalid_argument("kernel " + name + ": added twice");
    }
    references_.emplace(std::move(name), std::move(reference));
}

const Reference* Kernels::find(std::string_view name) const {
    auto found = references_.find(name);
    return found == references_.end() ? nullptr : &found->second;
}

HalError::HalError(Code code, std::string operation, std::string detail)
    : std::runtime_error(message(code, operation, detail)),
      code_(code),
      operation_(std::move(operation)),
      detail_(std::move(detail)) {}

}  // namespace qiven3::hal
