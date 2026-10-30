#pragma once

// checked/stores: the ruled stores, each store of the contract with the
// rules (checked/rules.hpp) as the admit hook its append, forget and
// import take, and no other: a call given another hook does not compile
// (the store's HookOf). Each is a Ruled (checked/ruled.hpp) over the store.
//
//   MemoryStore<P>         the in-memory store on platform P
//   SQLiteStore<P, H>      the SQLite store on P, its hooks H
//
// Allocations, platform calls and threads: the store's and the rules'.

#include "checked/ruled.hpp"
#include "checked/rules.hpp"
#include "platform/platform.hpp"
#include "store/contract.hpp"
#include "store/memory.hpp"
#include "store/sqlite_store.hpp"

namespace qiven3::checked {

template <platform::Platform P>
using MemoryStore = Ruled<store::MemoryStore<P, Rules<P>>>;

template <platform::Platform P, store::Hooks H = store::NoHooks>
using SQLiteStore = Ruled<store::SQLiteStore<P, H, Rules<P>>>;

}  // namespace qiven3::checked
