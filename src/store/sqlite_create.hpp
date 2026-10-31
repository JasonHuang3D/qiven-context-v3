#pragma once

// sqlite_create: create's making and its undoing, a template on the
// platform. made() makes the store's directory if absent and its file only
// if absent, by the platform's create (one made meanwhile is StoreError
// exists), then connects (sqlite::connect, steps 2 to 7). A create refused
// past either undoes what it made: the files by sqlite::unmake, through the
// engine's file layer; then the directory, if it made it, by the
// platform's remove_directory, which keeps one not empty; and raises the
// step's error again, its detail naming each removal refused
// (sqlite::reraise). A directory there before create stays.
//
// Allocations: none of its own. thread: the caller's.

#include <string_view>

#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"

namespace qiven3::store::sqlite {

template <platform::Platform P>
[[nodiscard]] Connection made(const memory::String& resolved, P& platform, const WhereView& at, Opened& opened) {
    const std::string_view directory = directory_of(resolved);
    bool own = false;
    if (!directory.empty() && !is_directory(directory)) {
        try {
            platform.create(directory, true);
            own = true;
        } catch (const platform::PlatformError& e) {
            if (e.code != platform::Code::exists) {
                throw;
            }
        }
    }
    bool file = false;
    try {
        try {
            platform.create(resolved, false);
            file = true;
        } catch (const platform::PlatformError& e) {
            if (e.code == platform::Code::exists) {
                throw StoreError(Code::exists, at.operation, at.path, "a file was made meanwhile");
            }
            throw;
        }
        return connect(resolved, true, false, at, opened);
    } catch (...) {
        Refused refused = file ? unmake(resolved) : Refused{0};
        try {
            if (own) {
                static_cast<void>(platform.remove_directory(directory));
            }
        } catch (...) {
            refused = static_cast<Refused>(refused | directory_refused);
        }
        reraise(refused);
    }
}

}  // namespace qiven3::store::sqlite
