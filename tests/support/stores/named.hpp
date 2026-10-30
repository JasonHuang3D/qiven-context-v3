#pragma once

// The store's namer (runner/core.hpp): a StoreError a case lets out,
// named by the runner of every binary that includes this header (the
// store's cases, through support/stores/stores.hpp), with nothing of the
// store in the runner.

#include <ostream>
#include <string_view>

#include "runner/core.hpp"
#include "store/errors.hpp"

namespace qiven3::test::stores {

// A StoreError's code by its kind, as message() names it: StoreError's own
// code, StoreLocked's cause, StoreCorrupt's or StoreFull's code,
// EntryInvalid's entry code; empty for a kind with none.
inline std::string_view store_code(const store::StoreError& e) {
    if (const auto code = e.code()) {
        return store::name(*code);
    }
    if (const auto cause = e.cause()) {
        return store::name(*cause);
    }
    if (const auto corrupt = e.corrupt()) {
        return store::name(*corrupt);
    }
    if (const auto full = e.full()) {
        return store::name(*full);
    }
    return e.kind() == store::Kind::entry_invalid ? e.entry_code() : std::string_view();
}

// A StoreError, which has no base: its kind, its kind's code where it has
// one, and its detail.
inline bool store_named(std::ostream& out) {
    try {
        throw;
    } catch (const store::StoreError& e) {
        const std::string_view code = store_code(e);
        out << "store error " << store::name(e.kind()) << (code.empty() ? "" : " ") << code << ": " << e.detail()
            << "\n";
        return true;
    } catch (...) {
        return false;
    }
}

inline const NamerRegistrar store_namer{&store_named};

}  // namespace qiven3::test::stores
