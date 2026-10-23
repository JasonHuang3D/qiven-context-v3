#pragma once

// view: a store's view in memory for the validator's cases, and an entry
// of a call. MemoryView holds headers and contents on std::allocator,
// each lane's head, the forgotten set, the live clearings, and the size
// and count held; put appends an entry and moves its lane's head, forget
// erases a body and marks its id. It reads into the caller's buffers on
// their own allocator: a header copied by make, its id given, one
// allocation; a content assigned, one where the buffer's capacity is
// short. It counts its reads. Item is an
// entry of a call: a header and its content or none, on one allocator.
// Each is constexpr and a template on the allocator, so its vectors run in
// constant evaluation too.
//
// thread: none

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "entry/header.hpp"
#include "entry/validator.hpp"

namespace qiven3::test::fixtures {

namespace header = qiven3::entry::header;
namespace validator = qiven3::entry::validator;

// A header copied onto an allocator, its id kept.
template <typename A, typename B>
constexpr header::Header<B> copy(const header::Header<A>& h, const B& allocator) {
    using header::Slot;
    header::FieldsView f;
    f.tag = h.value(Slot::tag);
    f.scope = h.value(Slot::scope);
    f.lane = h.value(Slot::lane);
    f.seq = h.seq();
    f.parent = h.text(Slot::parent);
    f.kind = h.value(Slot::kind);
    f.writer = h.value(Slot::writer);
    f.basis = h.value(Slot::basis);
    f.at = h.value(Slot::at);
    f.target = h.text(Slot::target);
    f.content = h.text(Slot::content);
    f.id = h.text(Slot::id);
    return header::make(f, allocator);
}

// A body's content, as the call's entries give it.
struct ContentView {
    std::string_view text;
    [[nodiscard]] constexpr std::string_view content() const noexcept { return text; }
};

// An entry of a call.
template <typename A = std::allocator<char>>
struct Item {
    header::Header<A> head;
    std::optional<validator::Text<A>> text;

    [[nodiscard]] constexpr const header::Header<A>& header() const noexcept { return head; }
    [[nodiscard]] constexpr bool has_body() const noexcept { return text.has_value(); }
    [[nodiscard]] constexpr ContentView body() const { return ContentView{std::string_view(*text)}; }
};

class MemoryView {
public:
    // An entry held: its header, its content unless erased, and moves its
    // lane's head.
    template <typename A>
    constexpr void put(const header::Header<A>& h, std::optional<std::string_view> content) {
        using header::Slot;
        Held e{copy(h, std::allocator<char>()), std::nullopt};
        if (content) {
            e.content = std::string(*content);
        }
        held_.push_back(std::move(e));
        const std::string_view scope = h.value(Slot::scope);
        const std::string_view lane = h.value(Slot::lane);
        const validator::Head now{validator::Id::of(h.value(Slot::id)), h.seq()};
        for (Lane& l : heads_) {
            if (l.scope == scope && l.lane == lane) {
                l.head = now;
                return;
            }
        }
        heads_.push_back(Lane{std::string(scope), std::string(lane), now});
    }
    // A forget's effect: the body erased and the id forgotten.
    constexpr void forget(std::string_view id) {
        if (Held* e = find(id)) {
            e->content.reset();
        }
        forgotten_.emplace_back(id);
    }
    // A live clearing of a content digest for a pattern in a scope.
    constexpr void clear(std::string_view scope, std::string_view content, std::string_view pattern) {
        clearings_.push_back(std::array<std::string, 3>{std::string(scope), std::string(content),
                                                        std::string(pattern)});
    }

    [[nodiscard]] constexpr std::optional<validator::Head> head(std::string_view scope, std::string_view lane) {
        ++reads_;
        for (const Lane& l : heads_) {
            if (l.scope == scope && l.lane == lane) {
                return l.head;
            }
        }
        return std::nullopt;
    }
    template <typename A>
    constexpr bool header(std::string_view id, header::Header<A>& out) {
        ++reads_;
        const Held* e = find(id);
        if (e == nullptr) {
            return false;
        }
        out = copy(e->h, out.get_allocator());
        return true;
    }
    template <typename A>
    constexpr bool body(std::string_view id, validator::Text<A>& out) {
        ++reads_;
        const Held* e = find(id);
        if (e == nullptr || !e->content) {
            return false;
        }
        out.assign(*e->content);
        return true;
    }
    [[nodiscard]] constexpr bool forgotten(std::string_view id) {
        ++reads_;
        for (const std::string& f : forgotten_) {
            if (f == id) {
                return true;
            }
        }
        return false;
    }
    // Whether a live clearing in scope clears content for pattern; this
    // view's clearings hold at every time.
    [[nodiscard]] constexpr bool cleared(std::string_view scope, std::string_view content, std::string_view pattern,
                                         std::string_view /*at*/) {
        ++reads_;
        for (const std::array<std::string, 3>& c : clearings_) {
            if (c[0] == scope && c[1] == content && c[2] == pattern) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] constexpr bool holds_content(std::string_view content) {
        ++reads_;
        for (const Held& e : held_) {
            if (e.h.text(header::Slot::content) == std::optional<std::string_view>(content)) {
                return true;
            }
        }
        return false;
    }

    // The size and count held, as a store's view gives them to stage; set
    // by volume, none read counted.
    constexpr void volume(std::uint64_t size, std::uint64_t count) noexcept {
        size_ = size;
        count_ = count;
    }
    [[nodiscard]] constexpr std::uint64_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::uint64_t count() const noexcept { return count_; }

    // The reads so far.
    [[nodiscard]] constexpr std::size_t reads() const noexcept { return reads_; }

private:
    struct Held {
        header::Header<> h;
        std::optional<std::string> content;
    };
    struct Lane {
        std::string scope;
        std::string lane;
        validator::Head head;
    };

    constexpr Held* find(std::string_view id) {
        for (Held& e : held_) {
            if (e.h.value(header::Slot::id) == id) {
                return &e;
            }
        }
        return nullptr;
    }

    std::vector<Held> held_;
    std::vector<Lane> heads_;
    std::vector<std::string> forgotten_;
    std::vector<std::array<std::string, 3>> clearings_;
    std::size_t reads_ = 0;
    std::uint64_t size_ = 0;
    std::uint64_t count_ = 0;
};

}  // namespace qiven3::test::fixtures
