// stage_book's unit for the analysis preset: the header, and a refusal
// read from an operation of the kind the cases give it.

#include "support/fixtures/stage_book.hpp"

#include <optional>

namespace qiven3::test::stage_book {

std::optional<Refusal> refused_nothing() {
    return refused([] {});
}

}  // namespace qiven3::test::stage_book
