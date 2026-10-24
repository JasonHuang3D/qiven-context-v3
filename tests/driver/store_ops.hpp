#pragma once

// The drive's store half, the fake's drive alone: with --store sqlite or
// memory, the store contract's operations on the wire, beside the
// platform's, over support/stores' factory on a Site under
// "<root>/stores", its fake seeded by --seed. Without --store the drive
// serves the platform's requests alone.
//
// Each state, store object, scan and export is named by an id in the
// reply that made it, one count for all four, never given twice; a
// request names its object by "state", "store", "scan" or "export". The
// factory's requests: factory_make (its "limits", each threshold
// optional), factory_again, factory_read_only and factory_reopen, each on
// a "state"; factory_drop ends a state no object is held over. The
// store's: store_capabilities, the type's; store_append, store_head,
// store_heads, store_header, store_body, store_forgotten, store_scan,
// store_forget, store_purge, store_export, store_import (from an "export"
// of this drive, or from "heads" and "lines" given), store_size,
// store_verify, store_close, store_read_only and store_limits, each on a
// "store"; store_drop ends an object no handle is held over. A scan's:
// scan_next, a header's line at a time, and scan_close, its end. An
// export's: export_heads, export_next, export_close, and export_drop, its
// end. fixture_entry gives a fixture's stored line and body record, by
// its "name".
//
// Bytes cross as hexadecimal, as the platform's do: an entry's "line" and
// "body", a header's line, an export's lines. Ids, scopes and lanes are
// text. An expected head of none is "expected" absent; a result of none
// (head, body, a handle's end) is "result" absent.
//
// Each store request runs as a caller runs it: its results on an arena
// over pages the drive's fake gives, as many bytes as the contract states
// for the operation (1 MiB plus 4 times the lines given; an export's
// reads 2 MiB), "budget" replacing it; the reply's "memory" is that
// arena's. Under QIVEN3_FAULTS a request's "fault" of "platform" counts
// the calls of the Site's fake, which the stores make. A store's typed
// error is "kind" "store": "class" its kind's name (StoreError,
// HeadConflict, ...), "operation", "path", "detail", and its kind's
// fields; "code" its code where the kind has one (StoreError's,
// EntryInvalid's entry code, StoreLocked's cause, StoreCorrupt's and
// StoreFull's). A tag the store lacks, as reopen on the in-memory store,
// is a "request" error whose detail starts "not applicable:".
//
// thread: one

#include "driver/drive.hpp"
#include "driver/platform_ops.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::drive {

// The fake's request loop: the store's requests where --store selects a
// store, else the platform's alone; the process's exit status.
int serve(Session& session, PlatformDrive<platform::fake::FakePlatform>& drive);

}  // namespace qiven3::drive
