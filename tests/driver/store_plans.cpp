// The SQLite store's own requests (driver/store_wire.hpp): the plans, each
// of the view's reads and the export's statements, its EXPLAIN QUERY PLAN
// lines as the store's own connection plans them; and the seam's statement
// run through that connection (S18's incoherent rows); and the settings,
// each read back on that connection (S19).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "qiven3/entry/cognition.hpp"
#include "driver/store_wire.hpp"
#include "qiven/recordlog/sqlite_steps.hpp"
#include "qiven/recordlog/exchange.hpp"

namespace qiven3::drive::wire {

namespace store = qiven::recordlog;

namespace {

struct PlannedView {
    std::string_view name;
    std::string_view sql;
};

constexpr std::array<PlannedView, 9> statements = {{
    {"keyed_roots", store::sqlite::keyed_roots},
    {"record_members", store::sqlite::record_members},
    {"target_members", store::sqlite::target_members},
    {"held_line", store::sqlite::held_line},
    {"held_scope", store::sqlite::held_scope},
    {"content_scan", store::sqlite::content_scan},
    {"forget_counts", store::sqlite::forget_counts},
    {"head_rows", store::sqlite::head_rows},
    {"lane_rows", store::sqlite::lane_rows},
}};

}  // namespace

Value plans(store::sqlite::Connection& c, ArgView) {
    const store::WhereView at = store::sqlite::site("plan", "");
    Value out = array();
    for (const PlannedView& p : statements) {
        Value lines = array();
        store::sqlite::Statement s = c.prepare(std::string("EXPLAIN QUERY PLAN ") + std::string(p.sql), at);
        while (s.step(at)) {
            lines.push(scalar(s.text(3)));
        }
        Value item;
        item.set("name", p.name);
        item.set("lines", lines);
        out.push(item);
    }
    return out;
}

// The seam's write (support/seams' write): "sql" run through the store's
// connection, its engine's code given back, 0 where it ran.
Value seam_write(store::sqlite::Connection& c, ArgView request) {
    Value out;
    out.set("code", std::int64_t{c.run(text(request, "sql"))});
    return out;
}

// Each setting the store applies, with the value the connection reads
// back: busy_timeout by its pragma, as the wrapper sets it; then the file
// identity cognition's create writes, when "create".
Value settings(store::sqlite::Connection& c, ArgView) {
    const store::WhereView at = store::sqlite::site("settings", "");
    constexpr std::array<std::string_view, 3> when = {"every", "read_write", "create"};
    Value out = array();
    const auto row = [&](std::string_view name, std::string_view value, std::string_view written) {
        store::sqlite::Statement read = c.prepare("PRAGMA " + std::string(name), at);
        Value item;
        item.set("name", name);
        item.set("value", value);
        item.set("when", written);
        if (read.step(at)) {
            item.set("read", read.text(0));
        }
        out.push(item);
    };
    for (const store::sqlite::Setting& s : store::sqlite::settings) {
        row(s.name.view(), s.value.view(), when.at(static_cast<std::size_t>(s.when)));
    }
    row("application_id", std::to_string(qiven3::entry::cognition_identity.application_id), when.at(2));
    row("user_version", std::to_string(store::schema_version), when.at(2));
    return out;
}

}  // namespace qiven3::drive::wire
