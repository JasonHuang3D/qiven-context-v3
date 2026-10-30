#include "store/memory_state.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "entry/body.hpp"
#include "entry/header.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/keys.hpp"

namespace qiven3::store::state {

namespace {

using header::Slot;

[[noreturn]] void incoherent(Operation op, std::string_view path, std::string_view detail) {
    throw StoreError::corrupt(name(op), path, Corrupt::incoherent, detail);
}

// The entries a call adds to lane l, from the batch's row `from` on.
std::size_t run_of(std::span<const Held> rows, std::size_t from, std::uint32_t l) noexcept {
    std::size_t n = 0;
    for (std::size_t j = from; j < rows.size(); ++j) {
        n += rows[j].lane == l ? 1U : 0U;
    }
    return n;
}

}  // namespace

void exhausted(std::size_t request, const memory::Heap& heap) {
    throw memory::MemoryError::budget("store/memory", heap.budget(), request, heap.used());
}

void busy(Operation op, std::string_view path) {
    throw StoreError::busy(name(op), path, static_cast<double>(busy_wait.count()));
}

State::State(std::span<std::byte> pages, const Limits& limits) : data_(pages), limits_(limits) {
    if (!valid(limits)) {
        throw StoreError(Code::settings, "open", {}, "limits not in order");
    }
}

#if defined(QIVEN3_FAULTS)
void attach(State& state, memory::Countdown* countdown) noexcept {
    if (!state.mutex_.try_lock_for(busy_wait)) {
        memory::fail_fast("store/memory: the state's lock held past its wait at attach");
    }
    state.data_.heap.attach(countdown);
    state.mutex_.unlock();
}
#endif

std::uint32_t find(const Data& d, std::string_view id) noexcept {
    const auto at = std::lower_bound(d.by_id.begin(), d.by_id.end(), id, [&d](std::uint32_t i, std::string_view v) {
        return d.held[i].id.view() < v;
    });
    return at != d.by_id.end() && d.held[*at].id.view() == id ? *at : none;
}

std::uint32_t lane_of(const Data& d, std::string_view scope, std::string_view lane) noexcept {
    for (std::size_t l = 0; l < d.lanes.size(); ++l) {
        if (std::string_view(d.lanes[l].scope) == scope && std::string_view(d.lanes[l].name) == lane) {
            return static_cast<std::uint32_t>(l);
        }
    }
    return none;
}

std::uint64_t forgets_in(const Data& d, const Scopes& scopes) noexcept {
    if (!scopes) {
        return d.forgets;
    }
    std::uint64_t n = 0;
    for (const Held& e : d.held) {
        n += e.is_forget && in_scopes(scopes, std::string_view(d.lanes[e.lane].scope)) ? 1U : 0U;
    }
    return n;
}

void heads_of(const Data& d, const Scopes& scopes, Vector<LaneHead>& rows) {
    rows.clear();
    for (const Lane& l : d.lanes) {
        if (in_scopes(scopes, std::string_view(l.scope))) {
            rows.push_back(LaneHead{l.head, memory::Inline<scope_bytes>(std::string_view(l.scope)),
                                    memory::Inline<lane_bytes>(std::string_view(l.name))});
        }
    }
    std::sort(rows.begin(), rows.end(), [](const LaneHead& a, const LaneHead& b) {
        return a.scope.view() != b.scope.view() ? a.scope.view() < b.scope.view() : a.lane.view() < b.lane.view();
    });
}

std::optional<Head> expect(const Data& d, Operation op, std::string_view path, std::string_view scope,
                           std::string_view lane, Expected expected) {
    const std::uint32_t l = lane_of(d, scope, lane);
    const std::optional<Head> now = l == none ? std::nullopt : std::optional<Head>(d.lanes[l].head);
    const std::string_view actual = now ? now->id.view() : std::string_view();
    if (expected.has_value() != now.has_value() || expected.value_or(std::string_view()) != actual) {
        throw StoreError::head_conflict(name(op), path, scope, lane, expected.value_or(std::string_view()), actual);
    }
    return now;
}

Header read_header(const Held& e, Operation op, std::string_view path, const Chars& allocator) {
    Header h(allocator);
    try {
        h = header::parse_line(std::string_view(e.line), allocator);
    } catch (const ParseError&) {
        incoherent(op, path, "a header line the parser refuses");
    } catch (const header::HeaderError&) {
        incoherent(op, path, "a header line that is no object");
    }
    if (const std::optional<header::Code> fault = header::try_check(h)) {
        if (*fault == header::Code::id_mismatch) {
            throw StoreError::corrupt(name(op), path, Corrupt::id_mismatch, e.id.view());
        }
        incoherent(op, path, header::name(*fault));
    }
    if (h.value(Slot::id) != e.id.view()) {
        throw StoreError::corrupt(name(op), path, Corrupt::id_mismatch, e.id.view());
    }
    return h;
}

void record_of(const Data& d, std::uint32_t i, Record& out) {
    const Held& e = d.held[i];
    const Lane& l = d.lanes[e.lane];
    out.kind.assign(std::string_view(e.kind));
    out.line.assign(std::string_view(e.line));
    out.id = e.id;
    out.scope = memory::Inline<scope_bytes>(std::string_view(l.scope));
    out.lane = memory::Inline<lane_bytes>(std::string_view(l.name));
    out.seq = e.seq;
    out.parent = e.seq > 1 ? std::optional<Id>(d.held[l.ids[static_cast<std::size_t>(e.seq - 2)]].id) : std::nullopt;
    out.target = e.target != none ? std::optional<Id>(d.held[e.target].id) : std::nullopt;
    out.body = e.content;
}

Batch::Batch(Data& d) noexcept : rows_(Allocator<Held>(d.heap)), lanes_(Allocator<Lane>(d.heap)) {}

Held make_held(Data& d, std::string_view line, std::optional<std::string_view> record, const KeysView& k) {
    Held e(d.heap);
    e.line.assign(line);
    if (record) {
        e.body.assign(*record);
        e.has_body = true;
    }
    e.content = k.body;
    e.kind.assign(k.kind);
    e.id = Id::of(k.id);
    e.seq = k.seq;
    e.is_forget = k.kind == "forget";
    return e;
}

void Batch::add(Data& d, std::string_view line, std::optional<std::string_view> record, const KeysView& k) {
    const std::size_t at = d.held.size() + rows_.size();
    if (at >= max_entries) {
        throw memory::MemoryError::budget("store/memory", max_entries, at + 1U, d.held.size());
    }
    Held e = make_held(d, line, record, k);
    if (const std::optional<std::string_view> t = k.target) {
        e.target = find(d, *t);
        for (std::size_t j = rows_.size(); e.target == none && j > 0; --j) {
            if (rows_[j - 1].id.view() == *t) {
                e.target = static_cast<std::uint32_t>(d.held.size() + j - 1);
            }
        }
    }
    const std::string_view scope = k.scope;
    const std::string_view name = k.lane;
    e.lane = lane_of(d, scope, name);
    for (std::size_t j = 0; e.lane == none && j < lanes_.size(); ++j) {
        if (std::string_view(lanes_[j].scope) == scope && std::string_view(lanes_[j].name) == name) {
            e.lane = static_cast<std::uint32_t>(d.lanes.size() + j);
        }
    }
    if (e.lane == none) {
        Lane l(d.heap);
        l.scope.assign(scope);
        l.name.assign(name);
        lanes_.push_back(std::move(l));
        e.lane = static_cast<std::uint32_t>(d.lanes.size() + lanes_.size() - 1U);
    }
    rows_.push_back(std::move(e));
    forgets_ += rows_.back().is_forget ? 1U : 0U;
}

void Batch::reserve(Data& d) {
    const std::size_t total = d.held.size() + rows_.size();
    d.held.reserve(total);
    d.by_id.reserve(total);
    d.lanes.reserve(d.lanes.size() + lanes_.size());
    const std::span<const Held> rows(rows_);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::uint32_t l = rows[i].lane;
        if (i > 0 && rows[i - 1].lane == l) {
            continue;
        }
        Rows<std::uint32_t>& ids = l < d.lanes.size() ? d.lanes[l].ids : lanes_[l - d.lanes.size()].ids;
        ids.reserve(ids.size() + run_of(rows, i, l));
    }
}

void Batch::commit(Data& d, std::uint64_t size) {
    for (Lane& l : lanes_) {
        d.lanes.push_back(std::move(l));
    }
    for (Held& e : rows_) {
        const auto i = static_cast<std::uint32_t>(d.held.size());
        Lane& lane = d.lanes[e.lane];
        lane.ids.push_back(i);
        lane.head = Head{e.id, e.seq};
        if (e.is_forget && e.target != none) {
            Held& t = d.held[e.target];
            t.forget = t.forget == none ? i : t.forget;
            if (t.has_body && d.exports.empty()) {
                Bytes gone(t.body.get_allocator());
                gone.swap(t.body);
            }
            t.has_body = false;
        }
        const std::string_view id = e.id.view();
        const auto at = std::lower_bound(d.by_id.begin(), d.by_id.end(), id, [&d](std::uint32_t k, std::string_view v) {
            return d.held[k].id.view() < v;
        });
        d.by_id.insert(at, i);
        d.held.push_back(std::move(e));
    }
    d.size += size;
    d.count += rows_.size();
    d.forgets += forgets_;
    rows_.clear();
    lanes_.clear();
}

void Batch::apply(Data& d, std::uint64_t size) {
    reserve(d);
    commit(d, size);
}

std::optional<Head> StateView::head(std::string_view scope, std::string_view lane) {
    const std::uint32_t l = lane_of(*data_, scope, lane);
    return l == none ? std::nullopt : std::optional<Head>(data_->lanes[l].head);
}

bool StateView::header(std::string_view id, Header& out) {
    const std::uint32_t i = find(*data_, id);
    if (i == none) {
        return false;
    }
    out = read_header(data_->held[i], op_, path_, out.get_allocator());
    return true;
}

bool StateView::body(std::string_view id, validator::Text<Chars>& out) {
    const std::uint32_t i = find(*data_, id);
    if (i == none || !data_->held[i].has_body) {
        return false;
    }
    out.assign(entry::body::detail::parts(std::string_view(data_->held[i].body)).content);
    return true;
}

bool StateView::forgotten(std::string_view id) {
    const std::uint32_t i = find(*data_, id);
    return i != none && data_->held[i].forget != none;
}

std::optional<Id> StateView::forget_of(std::string_view id) {
    const std::uint32_t i = find(*data_, id);
    if (i == none || data_->held[i].forget == none) {
        return std::nullopt;
    }
    return data_->held[data_->held[i].forget].id;
}

bool StateView::line(std::string_view id, String& out) {
    const std::uint32_t i = find(*data_, id);
    if (i == none) {
        return false;
    }
    out.assign(data_->held[i].line);
    return true;
}

bool StateView::held_in(std::string_view id, std::string_view scope) {
    const std::uint32_t i = find(*data_, id);
    return i != none && std::string_view(data_->lanes[data_->held[i].lane].scope) == scope;
}

Vector<std::uint32_t> StateView::lanes_of(std::string_view scope) {
    const Data& d = *data_;
    Vector<std::uint32_t> out(allocator_);
    for (std::uint32_t l = 0; l < d.lanes.size(); ++l) {
        if (std::string_view(d.lanes[l].scope) == scope) {
            out.push_back(l);
        }
    }
    std::sort(out.begin(), out.end(), [&d](std::uint32_t a, std::uint32_t b) {
        return std::string_view(d.lanes[a].name) < std::string_view(d.lanes[b].name);
    });
    return out;
}

void StateView::targets(std::string_view id, std::string_view scope, Vector<Id>& out) {
    out.clear();
    const Data& d = *data_;
    const std::uint32_t t = find(d, id);
    if (t == none) {
        return;
    }
    for (const Held& e : d.held) {
        if (e.target == t && std::string_view(d.lanes[e.lane].scope) == scope) {
            out.push_back(e.id);
        }
    }
}

void StateView::targets(std::string_view id, Vector<Id>& out) {
    out.clear();
    const Data& d = *data_;
    const std::uint32_t t = find(d, id);
    if (t == none) {
        return;
    }
    for (const Held& e : d.held) {
        if (e.target == t) {
            out.push_back(e.id);
        }
    }
}

void StateView::keyed(std::string_view scope, std::string_view kind, Vector<Id>& out) {
    out.clear();
    const Data& d = *data_;
    for (const std::uint32_t l : lanes_of(scope)) {
        for (const std::uint32_t r : d.lanes[l].ids) {
            if (d.held[r].target == none && std::string_view(d.held[r].kind) == kind) {
                out.push_back(d.held[r].id);
            }
        }
    }
}

bool StateView::contains(std::string_view bytes) {
    return std::any_of(data_->held.begin(), data_->held.end(), [bytes](const Held& e) {
        return std::string_view(e.line).find(bytes) != std::string_view::npos;
    });
}

}  // namespace qiven3::store::state
