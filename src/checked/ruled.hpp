#pragma once

// checked/ruled: a ruled store, the store S of the contract with the rules
// (checked/rules.hpp) as its admit hook, under the calls its callers make
// of a store today.
//
//   Ruled<S>   S with its constructors and every call S gives; open, where
//              S opens, gives a Ruled
//
// Ruled derives from S and adds no member and no virtual call: a call it
// does not name is S's own, and a call it names hides S's of that name.
// Allocations, platform calls and threads: S's.

#include <string_view>
#include <utility>

#include "store/contract.hpp"

namespace qiven3::checked {

template <class S>
class Ruled : public S {
public:
    using S::S;
    // The store an open or a factory gives, ruled.
    Ruled(S&& store) noexcept : S(std::move(store)) {}  // NOLINT(google-explicit-constructor)

    // Opens, or creates, as S::open does, ruled: its path, create and
    // read-only flags, platform and limits, then its hooks if given.
    template <class Platform, class... Hooks>
        requires requires(std::string_view path, bool flag, Platform& platform, const store::Limits& limits,
                          Hooks&&... hooks) { S::open(path, flag, flag, platform, limits, std::forward<Hooks>(hooks)...); }
    static Ruled open(std::string_view path, bool create, bool read_only, Platform& platform,
                      const store::Limits& limits = store::Limits{}, Hooks&&... hooks) {
        return Ruled(S::open(path, create, read_only, platform, limits, std::forward<Hooks>(hooks)...));
    }
};

}  // namespace qiven3::checked
