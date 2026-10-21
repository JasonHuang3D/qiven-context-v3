// K13's planted field: an event whose descriptor's field is a
// std::string_view. Built with QIVEN3_PLANT_FIELD (the target
// qiven3_log_plant, which the build never builds) it must not compile, at
// the line marked; built without it (qiven3_log_plant_control, in every
// build), the same event with a Name compiles. tests/drive/test_log.py
// builds both.

#include <array>
#include <string_view>

#include "log/log.hpp"

namespace {

namespace qlog = qiven3::log;

struct Planted {
    static constexpr qlog::Level level = qlog::Level::info;
    static constexpr std::string_view name = "k13.planted";
    static constexpr std::array<std::string_view, 1> keys{"text"};
#if defined(QIVEN3_PLANT_FIELD)
    using fields = qlog::Types<std::string_view>;
#else
    using fields = qlog::Types<qlog::Name>;
#endif
};

}  // namespace

void qiven3_log_plant(qlog::Log& lg, std::string_view text) noexcept;

void qiven3_log_plant(qlog::Log& lg, [[maybe_unused]] std::string_view text) noexcept {
#if defined(QIVEN3_PLANT_FIELD)
    qlog::event(lg, Planted{}, text);  // K13: the plant
#else
    qlog::event(lg, Planted{}, qlog::Name("control"));  // K13: the control
#endif
}
