// records' unit for the analysis preset: the store's plain records, their
// views and their makers.

#include "support/stores/records.hpp"

#include <concepts>

namespace qiven3::test::records {

static_assert(std::copy_constructible<Rec>);

}  // namespace qiven3::test::records
