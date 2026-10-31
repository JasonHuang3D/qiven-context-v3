// values' unit for the analysis preset: the header alone, its constants read.

#include "store/values.hpp"

namespace qiven3::store {

static_assert(valid(Limits{}) && name(Operation::export_) == "export" && raises(Operation::import_, Kind::not_empty));

}  // namespace qiven3::store
