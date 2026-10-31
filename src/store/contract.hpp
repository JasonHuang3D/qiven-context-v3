#pragma once

// contract: the store contract, bound at compile time. A store is a
// concrete type satisfying Store, with no base class and no virtual
// function; code that uses a store is a template on it. Two stores meet
// it, SQLite and the in-memory reference store, and a later store passes
// the suite before it is ruled in.
//
// The operations, by the text's names (export_ and import_ for export and
// import, which C++ reserves): append, head, heads, header, body,
// forgotten, scan, forget, purge, export_, import_, size, verify, close;
// read_only() and limits() on a const object; and the type's capabilities,
// durable, files and processes, as a constant. Views in, the caller's
// buffers out: ids, scopes, lanes and lines enter as std::string_view, an
// entry as EntryView (its header line and its body record or none), and a
// result is a value type of values.hpp or is written into the String or
// Vector the caller gives, on the caller's arena. A header line, a body
// record, a scan's records and an export's lines are given as their bytes
// and keys; the store parses none of them.
//
// A scan and an export are handles of their own: move-only values, read
// one entry at a time by next(), each ended by its destructor. An export
// gives its snapshot's heads, forget count, size and count first, then its
// lines; an import reads any Source of that shape, an export among them.
//
// A purge pending is a result with its reason, never an error. The store's
// own errors are StoreError (errors.hpp), of the kinds raises() gives
// (values.hpp);
// beside them an operation raises MemoryError budget for an allocation
// refused within it, the engine's heap's among them, never a StoreError,
// and a platform call's PlatformError as the platform gives it (pages,
// resolve, synced, create). Gate is
// the check each store runs first: the thread that made the object, then
// closed, then read-only. Hooks are the step hooks, begun, written,
// committed and rebuilt, which do nothing in NoHooks; a store takes its
// hooks as a template parameter, so a seam supplies its own type.
//
// Allocations: none. Platform calls: none. thread: Gate reads the calling
// thread's id; the rest none.

#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>

#include "keys/keys.hpp"
#include "memory/memory.hpp"
#include "store/errors.hpp"
#include "store/values.hpp"

namespace qiven3::store {

// Each operation's first check, in this order: a call from another thread
// than the one that made the object is StoreError thread; after close,
// StoreError closed; on a read-only object, an operation it does not serve
// StoreError read_only. Each names the operation and the store's path.
// own is the first alone: an export's close, again after close nothing.
class Gate {
public:
    explicit Gate(bool read_only) noexcept : owner_(std::this_thread::get_id()), read_only_(read_only) {}
    void own(Operation op, std::string_view path) const {
        if (std::this_thread::get_id() != owner_) {
            throw StoreError(Code::thread, name(op), path);
        }
    }
    void enter(Operation op, std::string_view path) const {
        own(op, path);
        if (closed_) {
            throw StoreError(Code::closed, name(op), path);
        }
        if (read_only_ && !serves_read_only(op)) {
            throw StoreError(Code::read_only, name(op), path);
        }
    }
    void close() noexcept { closed_ = true; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] bool read_only() const noexcept { return read_only_; }

private:
    decltype(std::this_thread::get_id()) owner_;
    bool read_only_;
    bool closed_ = false;
};

// A move-only value: a store object, a scan or an export.
template <class H>
concept Handle = std::movable<H> && !std::copyable<H>;

// The step hooks: after BEGIN IMMEDIATE, after the writes, after COMMIT,
// and once a purge's VACUUM returns.
template <class H>
concept Hooks = requires(H& h) {
    h.begun();
    h.written();
    h.committed();
    h.rebuilt();
};
struct NoHooks {
    constexpr void begun() const noexcept {}
    constexpr void written() const noexcept {}
    constexpr void committed() const noexcept {}
    constexpr void rebuilt() const noexcept {}
};

// The call a refusal names: its operation and the store's path.
struct AtView {
    Operation op = Operation::append;
    std::string_view path;
};

// The admit hook of append and forget: a product's checks of the entries a
// call is given, on the store's work pages (kept, the call's; scratch, a
// fresh arena over the same pages each step), each refusal thrown. given
// before any entry is read; read first with each line, its keys into out
// (views valid until release), and kept once the store keeps it, both
// before the store's transaction or lock; admit inside it, after the head
// and the guard, with the store's read view, before any write; release,
// however the call ends, before the store's pages go, gives back what the
// hook holds on them. P is the store's platform, V its view, E its kept
// entry, S its scratch.
template <class A, class P, class V, class E, class S>
concept Admit = requires(A& a, P& platform, V& view, const AtView& at, std::span<const EntryView> lines, std::size_t i,
                         const EntryView& line, KeysView& out, const E& e, std::span<const E> entries,
                         std::string_view v, Expected expected, const memory::Allocator<char>& kept, S& scratch) {
    a.given(at, lines, kept, scratch);
    a.read(platform, at, i, line, out, kept, scratch);
    a.kept(at, i, e, kept, scratch);
    a.admit(view, v, v, expected, entries, at, kept);
    { a.release() } noexcept;
};

// The hook's release at the end of the scope it is made in, after the
// store's pages are made and before they go.
template <class A>
class Released {
public:
    explicit Released(A& a) noexcept : a_(&a) {}
    Released(const Released&) = delete;
    Released& operator=(const Released&) = delete;
    Released(Released&&) = delete;
    Released& operator=(Released&&) = delete;
    ~Released() { a_->release(); }

private:
    A* a_;
};

// The admit hook of import, inside its transaction or lock, each line's
// and entry's work on a fresh arena (work): read first with each line, its
// keys into out (views valid until release); arrived with the line and
// those keys before its insert; settled with each entry in the final view,
// its line, body record and keys as the store holds them; heads with the
// source's heads and those the entries give. release, however each line's
// step ends, before its arena goes, gives back what the hook holds on it.
// P is the store's platform, V its view.
template <class A, class P, class V>
concept AdmitImport = requires(A& a, P& platform, V& view, const AtView& at, std::size_t i, const EntryView& line,
                               KeysView& out, const memory::Allocator<char>& work, std::span<const LaneHead> heads) {
    a.read(platform, at, i, line, out, work);
    a.arrived(view, line, i, at);
    a.settled(view, line, i, at, work);
    a.heads(heads, heads, at);
    { a.release() } noexcept;
};

// The hook that admits what the store's own rules admit: each line's keys
// as its caller gives them.
struct NoAdmit {
    template <class S>
    constexpr void given(const AtView& /*at*/, std::span<const EntryView> /*lines*/,
                         const memory::Allocator<char>& /*kept*/, S& /*scratch*/) const noexcept {}
    template <class P, class S>
    constexpr void read(P& /*platform*/, const AtView& /*at*/, std::size_t /*index*/, const EntryView& line,
                        KeysView& out, const memory::Allocator<char>& /*kept*/, S& /*scratch*/) const noexcept {
        out = line.keys;
    }
    template <class E, class S>
    constexpr void kept(const AtView& /*at*/, std::size_t /*index*/, const E& /*e*/,
                        const memory::Allocator<char>& /*kept*/, S& /*scratch*/) const noexcept {}
    template <class V, class E>
    constexpr void admit(V& /*view*/, std::string_view /*scope*/, std::string_view /*lane*/, Expected /*expected*/,
                         std::span<const E> /*entries*/, const AtView& /*at*/,
                         const memory::Allocator<char>& /*kept*/) const noexcept {}
    template <class P>
    constexpr void read(P& /*platform*/, const AtView& /*at*/, std::size_t /*index*/, const EntryView& line,
                        KeysView& out, const memory::Allocator<char>& /*work*/) const noexcept {
        out = line.keys;
    }
    template <class V>
    constexpr void arrived(V& /*view*/, const EntryView& /*line*/, std::size_t /*index*/,
                           const AtView& /*at*/) const noexcept {}
    template <class V>
    constexpr void settled(V& /*view*/, const EntryView& /*line*/, std::size_t /*index*/, const AtView& /*at*/,
                           const memory::Allocator<char>& /*work*/) const noexcept {}
    constexpr void heads(std::span<const LaneHead> /*source*/, std::span<const LaneHead> /*given*/,
                         const AtView& /*at*/) const noexcept {}
    constexpr void release() const noexcept {}
};

// The hook a store of Rule takes at a write: any where Rule is NoAdmit,
// Rule's alone otherwise, so that no call swaps a ruled store's rules.
template <class A, class Rule>
concept HookOf = std::same_as<Rule, NoAdmit> || std::same_as<std::remove_cvref_t<A>, Rule>;

// A scan's records in (scope, lane, seq) order; false at the end. Every
// entry committed before the scan began comes once, and none after: each
// lane ends at its length at the scan's start.
template <class H>
concept Scan = Handle<H> && requires(H& h, Record& out) {
    { h.next(out) } -> std::same_as<bool>;
};

// An import's source: its heads, then its lines in (scope, lane, seq)
// order; false at the end.
template <class R>
concept Source = requires(R& r, Vector<LaneHead>& rows, Line& line) {
    r.heads(rows);
    { r.next(line) } -> std::same_as<bool>;
};

// An export: a Source whose heads give the snapshot's count and volume,
// and which closes.
template <class E>
concept Export = Handle<E> && Source<E> && requires(E& e, Vector<LaneHead>& rows) {
    { e.heads(rows) } -> std::same_as<Snapshot>;
    e.close();
};

namespace detail {
// The source the concept imports from: an import is a template on Source.
struct SourceArchetype {
    static void heads(Vector<LaneHead>& /*rows*/) noexcept {}
    [[nodiscard]] static bool next(Line& /*line*/) noexcept { return false; }
};
// The capabilities, as a constant of their type.
template <class S>
concept Declares = requires {
    { S::capabilities } -> std::same_as<const Capabilities&>;
    typename std::integral_constant<bool, S::capabilities.durable>;
};
}  // namespace detail

// The contract.
template <class S>
concept Store = Handle<S> && detail::Declares<S> &&
                requires(S& s, const S& c, std::string_view v, Expected expected, std::span<const EntryView> entries,
                         std::span<const GuardView> guard, Scopes scopes, std::span<const AfterView> after,
                         const RangeView& range, String& line, String& record, Vector<LaneHead>& rows,
                         Vector<Id>& erased, detail::SourceArchetype& source) {
                    { s.append(v, v, expected, entries) } -> std::same_as<Written>;
                    { s.head(v, v) } -> std::same_as<std::optional<Head>>;
                    { s.heads(scopes, rows) } -> std::same_as<Heads>;
                    { s.header(v, line) } -> std::same_as<void>;
                    { s.body(v, record) } -> std::same_as<bool>;  // false: forgotten, or a forget
                    { s.forgotten(v) } -> std::same_as<bool>;
                    { s.scan(range) } -> Scan;
                    { s.forget(v, v, expected, entries, guard, erased) } -> std::same_as<Written>;
                    { s.purge() } -> std::same_as<Purge>;
                    { s.export_(scopes, after) } -> Export;
                    { s.import_(source, rows) } -> std::same_as<Imported>;
                    { s.size() } -> std::same_as<Sizes>;
                    { s.verify() } -> std::same_as<Counts>;
                    { s.close() } -> std::same_as<Purge>;
                    { c.read_only() } -> std::same_as<bool>;
                    { c.limits() } -> std::same_as<Limits>;
                };

// The pinned sizes; Gate's on x64 with MSVC's library in its release layout.
static_assert(std::is_empty_v<NoHooks>, "the default hooks take no space");
static_assert(std::is_empty_v<NoAdmit>, "the store's own admit takes no space");
#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(Gate) == 8, "Gate's size is pinned: a thread's id and two flags");
#endif

}  // namespace qiven3::store
