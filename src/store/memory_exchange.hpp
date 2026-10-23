#pragma once

// memory_exchange: the in-memory store's export, and the room for its
// forget, import and verify. Until they are built, MemoryExport refuses
// each call, and the store's forget, export_, import_ and verify refuse
// with StoreError internal, detail "not built", after the gate.
//
// Allocations: none. Platform calls: none.
//
// thread: one

#include "memory/memory.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"

namespace qiven3::store::state {

// An operation not built yet: StoreError internal, detail "not built".
[[noreturn]] inline void unbuilt(Operation op) { throw StoreError(Code::internal, name(op), {}, "not built"); }

// The export's handle: a move-only value, closed by its own close or its
// store's.
class MemoryExport {
public:
    MemoryExport() noexcept = default;
    MemoryExport(const MemoryExport&) = delete;
    MemoryExport& operator=(const MemoryExport&) = delete;
    MemoryExport(MemoryExport&&) noexcept = default;
    MemoryExport& operator=(MemoryExport&&) noexcept = default;
    ~MemoryExport() = default;

    // The snapshot's heads into rows, its forget count and its volume.
    static Snapshot heads(Vector<LaneHead>& /*rows*/) { unbuilt(Operation::export_); }
    // The next line, false at the end.
    static bool next(Line& /*line*/) { unbuilt(Operation::export_); }
    void close() noexcept { closed_ = true; }

private:
    bool closed_ = false;
};

}  // namespace qiven3::store::state
