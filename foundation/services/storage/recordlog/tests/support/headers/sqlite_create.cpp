// sqlite_create's unit for the analysis preset: made on the fake platform,
// as the SQLite store's create calls it.

#include "qiven/recordlog/sqlite_create.hpp"

#include "qiven/platform/fake/fake_platform.hpp"

namespace qiven::recordlog::sqlite {

template Connection made<qiven::platform::fake::FakePlatform>(const qiven::memory::String&, qiven::platform::fake::FakePlatform&,
                                                       const Identity&, const WhereView&, Opened&);

}  // namespace qiven::recordlog::sqlite
