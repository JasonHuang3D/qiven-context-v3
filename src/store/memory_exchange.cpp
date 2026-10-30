#include "store/memory_exchange.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "entry/header.hpp"
#include "memory/memory.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/keys.hpp"
#include "store/memory_state.hpp"
#include "store/stage.hpp"

namespace qiven3::store::state {

namespace {

using header::Slot;

[[noreturn]] void incoherent(Operation op, std::string_view detail) {
    throw StoreError::corrupt(name(op), {}, Corrupt::incoherent, detail);
}

// The bodies forgets dropped while an export was open, freed.
void release_kept(Data& d) noexcept {
    for (Held& e : d.held) {
        if (!e.has_body && !e.body.empty()) {
            Bytes gone(e.body.get_allocator());
            gone.swap(e.body);
        }
    }
}

}  // namespace

void guard(const Data& d, std::span<const GuardView> guard) {
    const std::string_view op = name(Operation::forget);
    for (const Lane& l : d.lanes) {
        const std::string_view scope(l.scope);
        const std::string_view lane(l.name);
        const auto g = std::find_if(guard.begin(), guard.end(),
                                    [&](const GuardView& v) { return v.scope == scope && v.lane == lane; });
        const bool absent = g == guard.end();
        if (absent || g->id != l.head.id.view()) {
            throw StoreError::head_conflict(op, {}, scope, lane, absent ? std::string_view() : g->id, l.head.id.view());
        }
    }
    for (const GuardView& v : guard) {
        if (lane_of(d, v.scope, v.lane) == none) {
            throw StoreError::head_conflict(op, {}, v.scope, v.lane, v.id, {});
        }
    }
}

std::uint64_t forgetting(const Data& d, std::span<const std::uint32_t> targets, std::string_view scope,
                         Vector<Id>& erased) {
    erased.clear();
    erased.reserve(targets.size());
    std::uint64_t bytes = 0;
    for (std::size_t j = 0; j < targets.size(); ++j) {
        const std::uint32_t t = targets[j];
        const std::span<const std::uint32_t> before = targets.first(j);
        if (t == none || !d.held[t].has_body || std::string_view(d.lanes[d.held[t].lane].scope) != scope ||
            std::find(before.begin(), before.end(), t) != before.end()) {
            continue;
        }
        erased.push_back(d.held[t].id);
        bytes += d.held[t].body.size() + 1U;
    }
    return bytes;
}

void close_exports(Data& d, std::uint64_t owner) noexcept {
    std::erase_if(d.exports, [owner](const Open& o) { return o.owner == owner; });
    if (d.exports.empty()) {
        release_kept(d);
    }
}

void import_one(Data& d, const EntryView& line, Rows<Link>& links) {
    const std::size_t at = d.held.size();
    if (at >= max_entries) {
        throw memory::MemoryError::budget("store/memory", max_entries, at + 1U, at);
    }
    const auto i = static_cast<std::uint32_t>(at);
    std::uint64_t bytes = stage::detail::sum(line.line.size(), 1);
    if (line.body) {
        bytes = stage::detail::sum(bytes, stage::detail::sum(line.body->size(), 1));
    }
    const KeysView& k = line.keys;
    if (k.target) {
        links.push_back(Link{i, Id::of(*k.target)});
    }
    Held e = make_held(d, line.line, line.body, k);
    e.lane = lane_of(d, k.scope, k.lane);
    if (e.lane == none) {
        Lane l(d.heap);
        l.scope.assign(k.scope);
        l.name.assign(k.lane);
        d.lanes.push_back(std::move(l));
        e.lane = static_cast<std::uint32_t>(d.lanes.size() - 1U);
    }
    Lane& lane = d.lanes[e.lane];
    lane.ids.push_back(i);
    d.by_id.push_back(i);
    lane.head = Head{e.id, e.seq};
    d.forgets += e.is_forget ? 1U : 0U;
    d.held.push_back(std::move(e));
    d.size = stage::detail::sum(d.size, bytes);
    d.count += 1U;
}

void sort_ids(Data& d) noexcept {
    std::sort(d.by_id.begin(), d.by_id.end(),
              [&d](std::uint32_t a, std::uint32_t b) { return d.held[a].id.view() < d.held[b].id.view(); });
}

void link(Data& d, std::uint32_t i, std::string_view target) noexcept {
    const std::uint32_t t = find(d, target);
    d.held[i].target = t;
    if (d.held[i].is_forget && t != none && d.held[t].forget == none) {
        d.held[t].forget = i;
    }
}

KeysView held_keys(const Data& d, std::uint32_t i, std::optional<std::string_view> target) noexcept {
    const Held& e = d.held[i];
    const Lane& l = d.lanes[e.lane];
    const auto k = static_cast<std::size_t>(e.seq - 1);
    const bool first = e.seq <= 1 || k >= l.ids.size();
    const std::optional<std::string_view> parent =
        first ? std::nullopt : std::optional<std::string_view>(d.held[l.ids[k - 1]].id.view());
    return KeysView{e.id.view(), std::string_view(l.scope), std::string_view(l.name), std::string_view(e.kind),
                    e.seq,       parent,                    target,                   e.content};
}

void reset(Data& d) noexcept {
    Rows<Held>(d.held.get_allocator()).swap(d.held);
    Rows<std::uint32_t>(d.by_id.get_allocator()).swap(d.by_id);
    Rows<Lane>(d.lanes.get_allocator()).swap(d.lanes);
    d.size = 0;
    d.count = 0;
    d.forgets = 0;
}

void verify_entry(const Data& d, std::uint32_t l, std::size_t k, const Header& h) {
    constexpr Operation op = Operation::verify;
    const Lane& lane = d.lanes[l];
    const std::uint32_t i = lane.ids[k];
    const Held& e = d.held[i];
    const std::string_view id = e.id.view();
    const std::optional<std::string_view> parent = h.text(Slot::parent);
    const bool chained = k == 0 ? !parent : parent == d.held[lane.ids[k - 1]].id.view();
    if (!chained || h.seq() != static_cast<std::int64_t>(k) + 1 || e.seq != h.seq() || e.lane != l ||
        h.value(Slot::scope) != std::string_view(lane.scope) || h.value(Slot::lane) != std::string_view(lane.name)) {
        incoherent(op, id);
    }
    const std::optional<std::string_view> target = h.text(Slot::target);
    if (target ? e.target == none || d.held[e.target].id.view() != *target : e.target != none) {
        incoherent(op, id);
    }
    if (e.is_forget != (h.value(Slot::kind) == "forget") ||
        (e.is_forget && (e.target == none || d.held[e.target].forget > i)) ||
        (e.forget != none && (!d.held[e.forget].is_forget || d.held[e.forget].target != i))) {
        incoherent(op, id);
    }
    if (e.forget != none && e.has_body) {
        throw StoreError::corrupt(name(op), {}, Corrupt::forgotten_body, id);
    }
    const std::optional<std::string_view> content = h.text(Slot::content);
    if (content.has_value() != e.content || (!content && e.has_body) ||
        (content && !e.has_body && e.forget == none)) {
        incoherent(op, id);
    }
}

Counts verify_totals(const Data& d) {
    constexpr Operation op = Operation::verify;
    Counts c;
    c.entries = d.held.size();
    c.lanes = d.lanes.size();
    std::size_t in_lanes = 0;
    for (const Lane& l : d.lanes) {
        if (l.ids.empty() || l.head.id != d.held[l.ids.back()].id || l.head.seq != d.held[l.ids.back()].seq) {
            incoherent(op, "a head not its lane's last entry");
        }
        in_lanes += l.ids.size();
    }
    std::uint64_t size = 0;
    for (const Held& e : d.held) {
        c.bodies += e.has_body ? 1U : 0U;
        c.forgets += e.is_forget ? 1U : 0U;
        size += e.line.size() + 1U + (e.has_body ? e.body.size() + 1U : 0U);
    }
    if (in_lanes != d.held.size() || d.by_id.size() != d.held.size() || d.count != c.entries ||
        d.forgets != c.forgets || d.size != size) {
        incoherent(op, "the counters not the entries'");
    }
    for (std::size_t k = 1; k < d.by_id.size(); ++k) {
        if (!(d.held[d.by_id[k - 1]].id.view() < d.held[d.by_id[k]].id.view())) {
            incoherent(op, "the ids out of order");
        }
    }
    return c;
}

MemoryExport::MemoryExport(State& s, Data& d, std::uint64_t& owner, const Scopes& scopes,
                           std::span<const AfterView> after)
    : state_(&s), lanes_(Allocator<Pos>(d.heap)), gate_(false), held_(static_cast<std::uint32_t>(d.held.size())) {
    lanes_.reserve(d.lanes.size());
    for (std::size_t l = 0; l < d.lanes.size(); ++l) {
        const Lane& lane = d.lanes[l];
        if (!in_scopes(scopes, std::string_view(lane.scope))) {
            continue;
        }
        Pos p{static_cast<std::uint32_t>(l), 0, static_cast<std::uint32_t>(lane.ids.size())};
        for (const AfterView& a : after) {
            if (a.scope == std::string_view(lane.scope) && a.lane == std::string_view(lane.name) && a.seq > 0) {
                p.start = a.seq < static_cast<std::int64_t>(p.end) ? static_cast<std::uint32_t>(a.seq) : p.end;
            }
        }
        lanes_.push_back(p);
    }
    std::sort(lanes_.begin(), lanes_.end(), [&d](const Pos& a, const Pos& b) {
        const Lane& x = d.lanes[a.lane];
        const Lane& y = d.lanes[b.lane];
        return x.scope != y.scope ? x.scope < y.scope : x.name < y.name;
    });
    snapshot_ = Snapshot{forgets_in(d, scopes), measure(d.size, d.count, s.limits())};
    at_ = lanes_.empty() ? 0 : lanes_.front().start;
    if (owner == 0) {
        owner = ++d.serial;
    }
    d.exports.push_back(Open{owner, d.serial + 1U});
    id_ = ++d.serial;
}

MemoryExport::MemoryExport(MemoryExport&& other) noexcept
    : state_(std::exchange(other.state_, nullptr)), lanes_(std::move(other.lanes_)), gate_(other.gate_),
      snapshot_(other.snapshot_), id_(other.id_), held_(other.held_), lane_(other.lane_), at_(other.at_) {
    other.gate_.close();
}

MemoryExport& MemoryExport::operator=(MemoryExport&& other) noexcept {
    if (this != &other) {
        release();
        state_ = std::exchange(other.state_, nullptr);
        lanes_ = std::move(other.lanes_);
        gate_ = other.gate_;
        snapshot_ = other.snapshot_;
        id_ = other.id_;
        held_ = other.held_;
        lane_ = other.lane_;
        at_ = other.at_;
        other.gate_.close();
    }
    return *this;
}

void MemoryExport::open(const Data& d) const {
    if (std::none_of(d.exports.begin(), d.exports.end(), [this](const Open& o) { return o.id == id_; })) {
        throw StoreError(Code::closed, name(Operation::export_), {});
    }
}

Snapshot MemoryExport::heads(Vector<LaneHead>& rows) {
    gate_.enter(Operation::export_, {});
    const Locked lock(*state_, Operation::export_, {});
    const Data& d = lock.data();
    open(d);
    rows.clear();
    rows.reserve(lanes_.size());
    for (const Pos& p : lanes_) {
        const Lane& lane = d.lanes[p.lane];
        const Held& e = d.held[lane.ids[p.end - 1U]];
        rows.push_back(LaneHead{Head{e.id, e.seq}, memory::Inline<scope_bytes>(std::string_view(lane.scope)),
                                memory::Inline<lane_bytes>(std::string_view(lane.name))});
    }
    return snapshot_;
}

bool MemoryExport::next(Line& line) {
    constexpr Operation op = Operation::export_;
    gate_.enter(op, {});
    const Locked lock(*state_, op, {});
    const Data& d = lock.data();
    open(d);
    while (lane_ < lanes_.size()) {
        const Pos& p = lanes_[lane_];
        if (at_ < p.end) {
            const std::uint32_t i = d.lanes[p.lane].ids[at_];
            const Held& e = d.held[i];
            const bool forgotten = e.forget != none && e.forget < held_;
            const bool body = !forgotten && !e.body.empty();
            if (!body && !forgotten && e.content) {
                throw StoreError::corrupt(name(op), {}, Corrupt::incoherent, e.id.view());
            }
            const KeysView keys = held_keys(
                d, i, e.target != none ? std::optional<std::string_view>(d.held[e.target].id.view()) : std::nullopt);
            if (!line.keys.assign(keys)) {
                incoherent(op, e.id.view());
            }
            line.header.assign(std::string_view(e.line));
            line.body.assign(body ? std::string_view(e.body) : std::string_view());
            line.has_body = body;
            line.forgotten = forgotten;
            ++at_;
            return true;
        }
        if (++lane_ < lanes_.size()) {
            at_ = lanes_[lane_].start;
        }
    }
    return false;
}

void MemoryExport::release() noexcept {
    gate_.close();
    if (state_ == nullptr) {
        return;
    }
    {
        const Locked lock(*state_, Unbounded{});
        Data& d = lock.data();
        std::erase_if(d.exports, [this](const Open& o) { return o.id == id_; });
        if (d.exports.empty()) {
            release_kept(d);
        }
    }
    Rows<Pos>(lanes_.get_allocator()).swap(lanes_);
    state_ = nullptr;
}

}  // namespace qiven3::store::state
