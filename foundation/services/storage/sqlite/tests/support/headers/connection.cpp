// connection's unit for the analysis preset: the header alone, its two
// wrappers move-only. sqlite.cpp defines its members.

#include "qiven/storage_sqlite/connection.hpp"

#include <concepts>

namespace qiven::storage_sqlite {

static_assert(std::movable<Statement> && !std::copyable<Statement>);
static_assert(std::movable<Connection> && !std::copyable<Connection>);

}  // namespace qiven::storage_sqlite
