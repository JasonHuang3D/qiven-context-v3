// sqlite_rig's unit for the analysis preset: the rig; raised over a call of the unit's own, as the cases give it
// theirs.

#include "support/stores/sqlite_rig.hpp"

namespace qiven3::test::sqlite_rig {

struct Call {
    void operator()() const {}
};

template std::string raised(const Call&);

}  // namespace qiven3::test::sqlite_rig
