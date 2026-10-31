// connection's unit for the analysis preset: the header alone, its settings
// table read and its two wrappers move-only. sqlite.cpp defines its members.

#include "store/engine/connection.hpp"

#include <concepts>

namespace qiven3::store::sqlite {

static_assert(std::movable<Statement> && !std::copyable<Statement>);
static_assert(std::movable<Connection> && !std::copyable<Connection>);
static_assert(settings.front().when == When::read_write);

}  // namespace qiven3::store::sqlite
