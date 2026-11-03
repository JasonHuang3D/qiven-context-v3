// sqlite_create's unit for the analysis preset: made on the fake platform,
// as the SQLite store's create calls it.

#include "store/sqlite_create.hpp"

#include "qiven/platform/fake/fake_platform.hpp"

namespace qiven3::store::sqlite {

template Connection made<qiven::platform::fake::FakePlatform>(const qiven::memory::String&, qiven::platform::fake::FakePlatform&,
                                                       const WhereView&, Opened&);

}  // namespace qiven3::store::sqlite
