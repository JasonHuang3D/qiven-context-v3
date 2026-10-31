// location_bed's unit for the analysis preset: the header, its bed made and
// one step run on it, as location's cases run it.

#include "support/fixtures/location_bed.hpp"

namespace qiven3::test::location_bed {

int show_once() {
    Bed bed(Scratch{});
    bed.prepare(Step::show);
    return bed.run(Step::show);
}

}  // namespace qiven3::test::location_bed
