// The SQLite store's own requests (driver/store_wire.hpp): the plans, each
// of the view's reads and the export's statements, its EXPLAIN QUERY PLAN
// lines as the store's own connection plans them; and the seam's statement
// run through that connection (S18's incoherent rows).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "driver/store_wire.hpp"
#include "store/engine/sqlite.hpp"
#include "store/exchange.hpp"

namespace qiven3::drive::wire {
namespace {

struct PlannedView {
    std::string_view name;
    std::string_view sql;
};

constexpr std::array<PlannedView, 7> statements = {{
    {"direction_roots", store::sqlite::direction_roots},
    {"record_members", store::sqlite::record_members},
    {"member_rows", store::sqlite::member_rows},
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

}  // namespace qiven3::drive::wire
