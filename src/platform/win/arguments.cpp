// The binary's command line on Windows (platform/win/windows.hpp): wmain's
// UTF-16 arguments after the program's name as UTF-8, converted strictly
// by WideCharToMultiByte with WC_ERR_INVALID_CHARS, as the adapter converts
// every string, so an unpaired surrogate is refused, never replaced. The
// program's name is not read: argv[0] is empty.

#include "platform/win/windows.hpp"

#include <windows.h>

#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <string_view>

#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"

namespace qiven3::platform::win {
namespace {

constexpr std::string_view operation = "arguments";

// The i-th argument's units; none for the program's name, which is not
// read.
std::wstring_view view(std::span<const wchar_t* const> wide, std::size_t i) noexcept {
    const wchar_t* text = wide[i];
    return i == 0 || text == nullptr ? std::wstring_view{} : std::wstring_view(text);
}

// The argument's UTF-8 bytes: measured where `into` is empty, else written
// into it, which holds them. A refusal is path, naming the index.
std::size_t utf8(std::size_t index, std::wstring_view text, std::span<char> into) {
    if (text.empty()) {
        return 0;
    }
    // The caller holds text and into within the bound, past no int.
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                      into.data(), static_cast<int>(into.size()), nullptr, nullptr);
    if (n <= 0) {
        const DWORD error = GetLastError();
        Detail detail("argument ");
        detail.number(index).add(" not converted to UTF-8: WideCharToMultiByte: ");
        add_error(detail, error);
        throw PlatformError(Code::path, operation, detail);
    }
    return static_cast<std::size_t>(n);
}

}  // namespace

std::size_t WindowsArguments::bytes(std::span<const wchar_t* const> wide) {
    if (wide.size() > most_arguments) {
        throw PlatformError(Code::memory, operation,
                            Detail("more arguments than a command line holds: ").number(wide.size()));
    }
    std::size_t units = 0;
    std::size_t text = 0;
    for (std::size_t i = 0; i < wide.size(); ++i) {
        const std::wstring_view one = view(wide, i);
        if (one.size() > command_line_units - units) {
            throw PlatformError(Code::memory, operation, "a command line past 32,767 UTF-16 units");
        }
        units += one.size();
        // At most 3 bytes a unit and a null: within arguments_bound.
        text += utf8(i, one, {}) + 1;
    }
    return (wide.size() + 1) * sizeof(char*) + text;
}

WindowsArguments::WindowsArguments(std::span<const wchar_t* const> wide, memory::Arena& arena)
    : arena_(&arena), count_(wide.size()) {
    const std::size_t total = bytes(wide);
    const std::size_t pointers = (count_ + 1) * sizeof(char*);
    pointers_ = arena.allocate(pointers, alignof(const char*));
    const std::span<const char*> argv(static_cast<const char**>(pointers_), count_ + 1);
    std::uninitialized_fill(argv.begin(), argv.end(), nullptr);
    try {
        // No argument, no text: the pointers' one allocation alone.
        if (total > pointers) {
            text_ = static_cast<char*>(arena.allocate(total - pointers, 1));
            size_ = total - pointers;
        }
        std::span<char> rest(text_, size_);
        for (std::size_t i = 0; i < count_; ++i) {
            const std::size_t n = utf8(i, view(wide, i), rest.first(rest.size() - 1));
            if (n >= rest.size()) {
                memory::fail_fast("arguments: a conversion past its measure");
            }
            rest[n] = '\0';
            argv[i] = rest.data();
            rest = rest.subspan(n + 1);
        }
    } catch (...) {
        if (text_ != nullptr) {
            arena.deallocate(text_, size_);
        }
        arena.deallocate(pointers_, pointers);
        throw;
    }
}

WindowsArguments::~WindowsArguments() {
    if (text_ != nullptr) {
        arena_->deallocate(text_, size_);
    }
    arena_->deallocate(pointers_, (count_ + 1) * sizeof(char*));
}

namespace {

#if defined(QIVEN3_FAULTS)
using Faults = memory::Countdown*;
#else
using Faults = std::nullptr_t;
#endif

// run_utf8's body, the pages' call and the arena's allocations each asking
// faults first under QIVEN3_FAULTS.
int run(int argc, const wchar_t* const* wide, Utf8Main entry, Utf8Statuses statuses, Faults faults) noexcept {
    if (argc < 0 || (argc > 0 && wide == nullptr) || entry == nullptr) {
        return statuses.refused;
    }
    const std::span<const wchar_t* const> given(wide, static_cast<std::size_t>(argc));
    std::size_t n = 0;
    try {
        n = WindowsArguments::bytes(given);
    } catch (...) {
        return statuses.refused;  // not valid UTF-16, or past a command line's bounds
    }
    // Measured: what fails from here on is an allocation refused, but for
    // a conversion refused, which the measure has already met.
    try {
#if defined(QIVEN3_FAULTS)
        if (faults != nullptr && faults->fail()) {
            throw PlatformError(Code::memory, "pages", "fault injected");
        }
#else
        static_cast<void>(faults);
#endif
        const WindowsPages pages = take_pages(n);
        memory::Arena arena(pages.bytes().first(n));
#if defined(QIVEN3_FAULTS)
        arena.attach(faults);
#endif
        const WindowsArguments arguments(given, arena);
        return entry(arguments.argc(), arguments.argv());
    } catch (const PlatformError& e) {
        return e.code == Code::memory ? statuses.faulted : statuses.refused;
    } catch (const memory::MemoryError&) {
        return statuses.faulted;
    } catch (const std::bad_alloc&) {
        return statuses.faulted;
    } catch (...) {
        return statuses.refused;
    }
}

}  // namespace

int run_utf8(int argc, const wchar_t* const* wide, Utf8Main entry, Utf8Statuses statuses) noexcept {
    return run(argc, wide, entry, statuses, nullptr);
}

#if defined(QIVEN3_FAULTS)
int run_utf8(int argc, const wchar_t* const* wide, Utf8Main entry, Utf8Statuses statuses,
             memory::Countdown* faults) noexcept {
    return run(argc, wide, entry, statuses, faults);
}
#endif

}  // namespace qiven3::platform::win
