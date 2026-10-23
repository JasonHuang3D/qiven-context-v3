#pragma once

// errors: the store's typed errors. StoreError is one value type, thrown by
// value and caught by its own type, with no base class: the text's base
// class and its eight subclasses are its kinds, so that one catch takes
// every error a store operation raises and a mapping returns any of them.
// Each kind carries the base's operation, store path and detail, and its
// own fields: HeadConflict scope, lane, expected and actual; EntryInvalid
// the entry's index, its id, its code, the field's path and, for `secret`,
// the pattern's name; NotFound the id; StoreBusy the seconds waited;
// StoreLocked its cause; StoreCorrupt and StoreFull their codes, StoreFull
// the size, the count and the maximum by size and by count; StoreNotEmpty
// the count. An expected or actual head of none is empty.
//
// Text is held in place, each field cut at a code point past its bytes; an
// EntryInvalid of code `secret` never holds an id, whatever it is given.
// The implementation's own code goes in detail. message() writes the error
// on one line: names, codes, ids, counts and paths, never content or a key.
//
// Allocations: none. thread: none.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "memory/memory.hpp"

namespace qiven3::store {

// The base and its eight subclasses, as the text names them by name().
enum class Kind : std::uint8_t { store_error, head_conflict, entry_invalid, not_found, busy, locked, corrupt, full, not_empty };
// StoreError's own codes.
enum class Code : std::uint8_t {
    closed, exists, location, missing, read_only, thread, settings, engine_version, guard, internal, forget_kind,
    whole_record
};
// StoreLocked's causes, StoreCorrupt's codes and StoreFull's.
enum class Cause : std::uint8_t { held, io, not_writable, permission };
enum class Corrupt : std::uint8_t { not_a_store, corrupt, foreign, version, incoherent, id_mismatch, body_mismatch, forgotten_body };
enum class Full : std::uint8_t { store_full, disk_full };

// Each enumeration's names, in its order, as the text spells them.
inline constexpr std::array<std::string_view, 9> kind_names = {
    "StoreError", "HeadConflict", "EntryInvalid", "NotFound", "StoreBusy",
    "StoreLocked", "StoreCorrupt", "StoreFull", "StoreNotEmpty",
};
inline constexpr std::array<std::string_view, 12> code_names = {
    "closed",   "exists", "location", "missing",  "read_only",   "thread",
    "settings", "engine_version", "guard", "internal", "forget_kind", "whole_record",
};
inline constexpr std::array<std::string_view, 4> cause_names = {"held", "io", "not_writable", "permission"};
inline constexpr std::array<std::string_view, 8> corrupt_names = {
    "not_a_store", "corrupt", "foreign", "version", "incoherent", "id_mismatch", "body_mismatch", "forgotten_body",
};
inline constexpr std::array<std::string_view, 2> full_names = {"store_full", "disk_full"};
constexpr std::string_view name(Kind kind) noexcept { return kind_names[static_cast<std::size_t>(kind)]; }
constexpr std::string_view name(Code code) noexcept { return code_names[static_cast<std::size_t>(code)]; }
constexpr std::string_view name(Cause cause) noexcept { return cause_names[static_cast<std::size_t>(cause)]; }
constexpr std::string_view name(Corrupt code) noexcept { return corrupt_names[static_cast<std::size_t>(code)]; }
constexpr std::string_view name(Full code) noexcept { return full_names[static_cast<std::size_t>(code)]; }

// Each text field's bytes: an operation's name; a store's path; the detail,
// as a platform error's; a scope and a lane by their syntaxes; an id; an
// entry's code; a field's path, as content's; a pattern's name.
inline constexpr std::size_t operation_bytes = 16;
inline constexpr std::size_t path_bytes = 512;
inline constexpr std::size_t detail_bytes = 256;
inline constexpr std::size_t scope_bytes = 56;
inline constexpr std::size_t lane_bytes = 32;
inline constexpr std::size_t id_bytes = 71;
inline constexpr std::size_t code_bytes = 32;
inline constexpr std::size_t field_bytes = 96;
inline constexpr std::size_t pattern_bytes = 32;

class StoreError {
    // The kind's code; defined before its callers, which clang needs in
    // constant evaluation.
    template <typename E>
    [[nodiscard]] constexpr std::optional<E> of(Kind kind) const noexcept {
        return kind_ == kind ? std::optional<E>(static_cast<E>(code_)) : std::nullopt;
    }

public:
    // The base, with one of its own codes.
    constexpr StoreError(Code code, std::string_view operation, std::string_view path,
                         std::string_view detail = {}) noexcept
        : StoreError(Kind::store_error, static_cast<std::uint8_t>(code), operation, path, detail) {}

    static constexpr StoreError head_conflict(std::string_view operation, std::string_view path,
                                              std::string_view scope, std::string_view lane,
                                              std::string_view expected, std::string_view actual,
                                              std::string_view detail = {}) noexcept {
        StoreError e = make(Kind::head_conflict, 0, operation, path, detail);
        e.scope_ = Scope(scope);
        e.lane_ = Lane(lane);
        e.expected_ = Id(expected);
        e.actual_ = Id(actual);
        return e;
    }
    // id where computable, else empty; never kept for `secret` (A7).
    static constexpr StoreError entry_invalid(std::string_view operation, std::string_view path, std::size_t index,
                                              std::string_view id, std::string_view code, std::string_view field,
                                              std::string_view pattern = {}, std::string_view detail = {}) noexcept {
        StoreError e = make(Kind::entry_invalid, 0, operation, path, detail);
        e.index_ = index;
        e.entry_code_ = memory::Inline<code_bytes>(code);
        e.id_ = Id(code == "secret" ? std::string_view() : id);
        e.field_ = memory::Inline<field_bytes>(field);
        e.pattern_ = memory::Inline<pattern_bytes>(pattern);
        return e;
    }
    static constexpr StoreError not_found(std::string_view operation, std::string_view path, std::string_view id,
                                          std::string_view detail = {}) noexcept {
        StoreError e = make(Kind::not_found, 0, operation, path, detail);
        e.id_ = Id(id);
        return e;
    }
    static constexpr StoreError busy(std::string_view operation, std::string_view path, double seconds,
                                     std::string_view detail = {}) noexcept {
        StoreError e = make(Kind::busy, 0, operation, path, detail);
        e.seconds_ = seconds;
        return e;
    }
    static constexpr StoreError locked(std::string_view operation, std::string_view path, Cause cause,
                                       std::string_view detail = {}) noexcept {
        return make(Kind::locked, static_cast<std::uint8_t>(cause), operation, path, detail);
    }
    static constexpr StoreError corrupt(std::string_view operation, std::string_view path, Corrupt code,
                                        std::string_view detail = {}) noexcept {
        return make(Kind::corrupt, static_cast<std::uint8_t>(code), operation, path, detail);
    }
    static constexpr StoreError full(std::string_view operation, std::string_view path, Full code, std::uint64_t size,
                                     std::uint64_t count, std::uint64_t max_size, std::uint64_t max_count,
                                     std::string_view detail = {}) noexcept {
        StoreError e = make(Kind::full, static_cast<std::uint8_t>(code), operation, path, detail);
        e.size_ = size;
        e.count_ = count;
        e.max_size_ = max_size;
        e.max_count_ = max_count;
        return e;
    }
    static constexpr StoreError not_empty(std::string_view operation, std::string_view path, std::uint64_t count,
                                          std::string_view detail = {}) noexcept {
        StoreError e = make(Kind::not_empty, 0, operation, path, detail);
        e.count_ = count;
        return e;
    }

    [[nodiscard]] constexpr Kind kind() const noexcept { return kind_; }
    // The kind's code, empty for another kind.
    [[nodiscard]] constexpr std::optional<Code> code() const noexcept { return of<Code>(Kind::store_error); }
    [[nodiscard]] constexpr std::optional<Cause> cause() const noexcept { return of<Cause>(Kind::locked); }
    [[nodiscard]] constexpr std::optional<Corrupt> corrupt() const noexcept { return of<Corrupt>(Kind::corrupt); }
    [[nodiscard]] constexpr std::optional<Full> full() const noexcept { return of<Full>(Kind::full); }
    [[nodiscard]] constexpr std::string_view operation() const noexcept { return operation_.view(); }
    [[nodiscard]] constexpr std::string_view path() const noexcept { return path_.view(); }
    [[nodiscard]] constexpr std::string_view detail() const noexcept { return detail_.view(); }
    [[nodiscard]] constexpr std::string_view scope() const noexcept { return scope_.view(); }
    [[nodiscard]] constexpr std::string_view lane() const noexcept { return lane_.view(); }
    [[nodiscard]] constexpr std::string_view expected() const noexcept { return expected_.view(); }
    [[nodiscard]] constexpr std::string_view actual() const noexcept { return actual_.view(); }
    [[nodiscard]] constexpr std::string_view id() const noexcept { return id_.view(); }
    [[nodiscard]] constexpr std::string_view entry_code() const noexcept { return entry_code_.view(); }
    [[nodiscard]] constexpr std::string_view field() const noexcept { return field_.view(); }
    [[nodiscard]] constexpr std::string_view pattern() const noexcept { return pattern_.view(); }
    [[nodiscard]] constexpr std::size_t index() const noexcept { return index_; }
    [[nodiscard]] constexpr double seconds() const noexcept { return seconds_; }
    [[nodiscard]] constexpr std::uint64_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::uint64_t count() const noexcept { return count_; }
    [[nodiscard]] constexpr std::uint64_t max_size() const noexcept { return max_size_; }
    [[nodiscard]] constexpr std::uint64_t max_count() const noexcept { return max_count_; }

private:
    using Scope = memory::Inline<scope_bytes>;
    using Lane = memory::Inline<lane_bytes>;
    using Id = memory::Inline<id_bytes>;

    constexpr StoreError(Kind kind, std::uint8_t code, std::string_view operation, std::string_view path,
                         std::string_view detail) noexcept
        : kind_(kind), code_(code), operation_(operation), path_(path), detail_(detail) {}
    // A kind's error, made by a return of a list: MSVC refuses, in constant
    // evaluation, a local made by the private constructor directly.
    static constexpr StoreError make(Kind kind, std::uint8_t code, std::string_view operation, std::string_view path,
                                     std::string_view detail) noexcept {
        return {kind, code, operation, path, detail};
    }

    Kind kind_;
    std::uint8_t code_;
    std::size_t index_ = 0;
    double seconds_ = 0;
    std::uint64_t size_ = 0, count_ = 0, max_size_ = 0, max_count_ = 0;
    memory::Inline<operation_bytes> operation_;
    memory::Inline<path_bytes> path_;
    memory::Inline<detail_bytes> detail_;
    Scope scope_;
    Lane lane_;
    Id expected_, actual_, id_;
    memory::Inline<code_bytes> entry_code_;
    memory::Inline<field_bytes> field_;
    memory::Inline<pattern_bytes> pattern_;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(StoreError) == 1392, "StoreError's size is pinned: its kind and code, five counts, twelve texts");
#endif

// The error on one line, as much of it as fits in out; the view is out's.
// "<Kind>[ <code>]: <operation> <path>", then the kind's fields, then
// "; <detail>" where there is one.
std::string_view message(const StoreError& error, std::span<char> out) noexcept;

}  // namespace qiven3::store
