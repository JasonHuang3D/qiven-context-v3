// The store half's wire forms (driver/store_wire.hpp).

#include "driver/store_wire.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"

namespace qiven3::drive::wire {
namespace {

std::int64_t n(std::uint64_t value) { return static_cast<std::int64_t>(value); }

std::uint64_t positive(ArgView request, std::string_view key, std::uint64_t fallback) {
    const std::int64_t got = integer(request, key, static_cast<std::int64_t>(fallback));
    if (got <= 0) {
        throw RequestError(std::string(key) + ": a threshold, above 0");
    }
    return static_cast<std::uint64_t>(got);
}

std::optional<std::string> body_of(ArgView item) {
    return item.find("body") ? std::optional<std::string>(from_hex(text(item, "body"))) : std::nullopt;
}

std::size_t half(ArgView item, std::string_view key) {
    const std::optional<ArgView> found = item.find(key);
    return found ? found->as_string(key).size() / 2 : 0;
}

}  // namespace

Value head(const store::Head& h) {
    Value out;
    out.set("id", h.id.view());
    out.set("seq", h.seq);
    return out;
}

Value volume(const store::Volume& v) {
    Value out;
    out.set("size", n(v.size));
    out.set("count", n(v.count));
    out.set("over_target", v.over_target);
    out.set("over_warning", v.over_warning);
    return out;
}

Value purge(const store::Purge& p) {
    Value out;
    out.set("done", p.done);
    if (!p.done) {
        out.set("reason", p.reason.view());
    }
    return out;
}

Value written(const store::Written& w) {
    Value out;
    out.set("head", head(w.head));
    out.set("volume", volume(w.volume));
    out.set("purge", purge(w.purge));
    return out;
}

Value rows(std::span<const store::LaneHead> list) {
    Value out = array();
    for (const store::LaneHead& row : list) {
        Value item;
        item.set("scope", row.scope.view());
        item.set("lane", row.lane.view());
        item.set("id", row.head.id.view());
        item.set("seq", row.head.seq);
        out.push(item);
    }
    return out;
}

Value counts(const store::Counts& c) {
    Value out;
    out.set("entries", n(c.entries));
    out.set("bodies", n(c.bodies));
    out.set("forgets", n(c.forgets));
    out.set("lanes", n(c.lanes));
    return out;
}

Value limits(const store::Limits& l) {
    Value out;
    out.set("target_size", n(l.target_size));
    out.set("warning_size", n(l.warning_size));
    out.set("max_size", n(l.max_size));
    out.set("target_count", n(l.target_count));
    out.set("warning_count", n(l.warning_count));
    out.set("max_count", n(l.max_count));
    return out;
}

Value sizes(const store::Sizes& s) {
    Value detail = object();
    for (const store::Figure& figure : s.view()) {
        detail.set(figure.name.view(), n(figure.value));
    }
    Value out;
    out.set("volume", volume(s.volume));
    out.set("detail", detail);
    return out;
}

std::string header_line(const store::Header& header, memory::Arena& arena) {
    memory::String out{memory::Allocator<char>(arena)};
    header.line(out);
    return to_hex(std::string_view(out.data(), out.size()));
}

Value line(const store::Line& l) {
    Value out;
    out.set("line", to_hex(std::string_view(l.header.data(), l.header.size())));
    if (l.has_body) {
        out.set("body", to_hex(std::string_view(l.body.data(), l.body.size())));
    }
    out.set("forgotten", l.forgotten);
    return out;
}

Value error(const store::StoreError& e) {
    using store::Kind;
    Value out;
    out.set("kind", "store");
    out.set("class", store::name(e.kind()));
    out.set("operation", e.operation());
    out.set("path", e.path());
    out.set("detail", e.detail());
    switch (e.kind()) {
    case Kind::store_error:
        out.set("code", store::name(e.code().value_or(store::Code::internal)));
        break;
    case Kind::head_conflict:
        out.set("scope", e.scope());
        out.set("lane", e.lane());
        out.set("expected", e.expected());
        out.set("actual", e.actual());
        break;
    case Kind::entry_invalid:
        out.set("code", e.entry_code());
        out.set("index", n(e.index()));
        out.set("id", e.id());
        out.set("field", e.field());
        out.set("pattern", e.pattern());
        break;
    case Kind::not_found:
        out.set("id", e.id());
        break;
    case Kind::busy:
        // The seconds waited, in milliseconds: canonical JSON has no float.
        out.set("ms", static_cast<std::int64_t>(std::llround(e.seconds() * 1000.0)));
        break;
    case Kind::locked:
        out.set("code", store::name(e.cause().value_or(store::Cause::io)));
        break;
    case Kind::corrupt:
        out.set("code", store::name(e.corrupt().value_or(store::Corrupt::corrupt)));
        break;
    case Kind::full:
        out.set("code", store::name(e.full().value_or(store::Full::store_full)));
        out.set("size", n(e.size()));
        out.set("count", n(e.count()));
        out.set("max_size", n(e.max_size()));
        out.set("max_count", n(e.max_count()));
        break;
    case Kind::not_empty:
        out.set("count", n(e.count()));
        break;
    }
    return out;
}

store::Limits limits_of(ArgView request) {
    store::Limits l;
    const std::optional<ArgView> given = request.find("limits");
    if (!given) {
        return l;
    }
    l.target_size = positive(*given, "target_size", l.target_size);
    l.warning_size = positive(*given, "warning_size", l.warning_size);
    l.max_size = positive(*given, "max_size", l.max_size);
    l.target_count = positive(*given, "target_count", l.target_count);
    l.warning_count = positive(*given, "warning_count", l.warning_count);
    l.max_count = positive(*given, "max_count", l.max_count);
    return l;
}

Entries::Entries(ArgView request, std::string_view key) {
    const std::vector<ArgView> items = argument(request, key).as_array(key);
    lines_.reserve(items.size());
    bodies_.reserve(items.size());
    for (const ArgView item : items) {
        lines_.push_back(from_hex(text(item, "line")));
        bodies_.push_back(body_of(item));
    }
    views_.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        views_.push_back(store::EntryView{lines_[i], bodies_[i] ? std::optional<std::string_view>(*bodies_[i])
                                                                : std::nullopt});
    }
}

Ranges::Ranges(ArgView request) {
    const std::optional<ArgView> scopes = request.find("scopes");
    const std::optional<ArgView> after = request.find("after");
    const std::optional<ArgView> guard = request.find("guard");
    const std::vector<ArgView> s = scopes ? scopes->as_array("scopes") : std::vector<ArgView>{};
    const std::vector<ArgView> a = after ? after->as_array("after") : std::vector<ArgView>{};
    const std::vector<ArgView> g = guard ? guard->as_array("guard") : std::vector<ArgView>{};
    whole_ = !scopes;
    // Reserved whole first, so no text moves once viewed.
    texts_.reserve(s.size() + (2 * a.size()) + (3 * g.size()));
    const auto keep = [this](std::string value) -> std::string_view { return texts_.emplace_back(std::move(value)); };
    for (const ArgView item : s) {
        scopes_.push_back(keep(item.as_string("scopes")));
    }
    for (const ArgView item : a) {
        const std::string_view scope = keep(text(item, "scope"));
        const std::string_view lane = keep(text(item, "lane"));
        after_.push_back(store::AfterView{scope, lane, integer(item, "seq")});
    }
    for (const ArgView item : g) {
        const std::string_view scope = keep(text(item, "scope"));
        const std::string_view lane = keep(text(item, "lane"));
        guard_.push_back(store::GuardView{scope, lane, keep(text(item, "id"))});
    }
}

store::Scopes Ranges::scopes() const noexcept {
    if (whole_) {
        return std::nullopt;
    }
    return std::span<const std::string_view>(scopes_);
}

Given::Given(ArgView request) {
    for (const ArgView item : argument(request, "heads").as_array("heads")) {
        store::LaneHead row;
        row.head = store::Head{store::Id::of(text(item, "id")), integer(item, "seq")};
        row.scope = memory::Inline<store::scope_bytes>(text(item, "scope"));
        row.lane = memory::Inline<store::lane_bytes>(text(item, "lane"));
        heads_.push_back(row);
    }
    for (const ArgView item : argument(request, "lines").as_array("lines")) {
        lines_.push_back(Held{from_hex(text(item, "line")), body_of(item), flag(item, "forgotten", false)});
    }
}

void Given::heads(store::Vector<store::LaneHead>& rows) const {
    for (const store::LaneHead& row : heads_) {
        rows.push_back(row);
    }
}

bool Given::next(store::Line& line) {
    if (at_ == lines_.size()) {
        return false;
    }
    const Held& held = lines_[at_++];
    line.header.assign(std::string_view(held.line));
    line.has_body = held.body.has_value();
    line.body.assign(held.body ? std::string_view(*held.body) : std::string_view());
    line.forgotten = held.forgotten;
    return true;
}

std::size_t lines_budget(ArgView request, std::string_view key) {
    std::size_t bytes = 0;
    if (const std::optional<ArgView> list = request.find(key)) {
        for (const ArgView item : list->as_array(key)) {
            bytes = memory::add(bytes, memory::add(half(item, "line"), half(item, "body")).value_or(SIZE_MAX))
                        .value_or(SIZE_MAX);
        }
    }
    return store::operation_budget(bytes).value_or(SIZE_MAX);
}

}  // namespace qiven3::drive::wire
