// The binary's command line on Windows (platform/win/windows.hpp): wmain's
// UTF-16 arguments as UTF-8, converted strictly by WideCharToMultiByte with
// WC_ERR_INVALID_CHARS, as the adapter converts every string, so an
// unpaired surrogate is refused, never replaced.

#include "platform/win/windows.hpp"

#include <windows.h>

#include <memory>
#include <span>
#include <string_view>

#include "memory/arena.hpp"
#include "memory/fail.hpp"

namespace qiven3::platform::win {
namespace {

constexpr std::string_view operation = "arguments";

std::wstring_view view(const wchar_t* text) noexcept {
    return text == nullptr ? std::wstring_view{} : std::wstring_view(text);
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
        const std::wstring_view one = view(wide[i]);
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
            const std::size_t n = utf8(i, view(wide[i]), rest.first(rest.size() - 1));
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

int run_utf8(int argc, const wchar_t* const* wide, Utf8Main entry, int refused) noexcept {
    if (argc < 0 || (argc > 0 && wide == nullptr) || entry == nullptr) {
        return refused;
    }
    try {
        const std::span<const wchar_t* const> given(wide, static_cast<std::size_t>(argc));
        const std::size_t n = WindowsArguments::bytes(given);
        const WindowsPages pages = take_pages(n);
        memory::Arena arena(pages.bytes().first(n));
        const WindowsArguments arguments(given, arena);
        return entry(arguments.argc(), arguments.argv());
    } catch (...) {
        return refused;
    }
}

}  // namespace qiven3::platform::win
