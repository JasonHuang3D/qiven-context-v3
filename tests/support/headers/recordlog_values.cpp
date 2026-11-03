// values' unit for the analysis preset: the header alone, its constants read.

#include "qiven/recordlog/values.hpp"

namespace qiven::recordlog {

static_assert(valid(Limits{1, 2, 3, 1, 2, 3}) && name(Operation::export_) == "export" && raises(Operation::import_, Kind::not_empty));

}  // namespace qiven::recordlog
