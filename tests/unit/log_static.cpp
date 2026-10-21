// log's static half: the fields' types an event takes, the descriptors the
// concept refuses, the catalogue's names, levels and bounds, and the line's
// encoder, Count's saturation, pick and the levels' names in constant
// evaluation, on std::allocator, so that a wrong result, or undefined
// behaviour on the way, fails the build. Then the widest event, for
// unit/log_test.cpp's budget.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "log/catalogue.hpp"
#include "log/line.hpp"
#include "log/log.hpp"
#include "memory/memory.hpp"
#include "support/log/cases.hpp"

namespace {

namespace qlog = qiven3::log;
namespace events = qiven3::log::events;

// An event of op.begin with a field of type F compiles.
template <typename F>
concept Emits = requires(qlog::Log& lg, const F& f) { qlog::event(lg, events::op_begin, f); };

static_assert(Emits<qlog::Name>);
static_assert(!Emits<std::string_view>);
static_assert(!Emits<qiven3::memory::String>);
static_assert(!Emits<std::string>);
static_assert(!Emits<const char*>);
static_assert(!Emits<qlog::Count>);
static_assert(qlog::FieldsOf<events::OpEnd, qlog::Name, qlog::Name, qlog::Millis, qlog::Count>);
static_assert(!qlog::FieldsOf<events::OpEnd, qlog::Name, qlog::Name, qlog::Count, qlog::Count>);
static_assert(!qlog::FieldsOf<events::OpEnd, qlog::Name, qlog::Name, qlog::Millis>);

// A descriptor with a view's field, or more keys than types, is none.
struct ViewField {
    static constexpr qlog::Level level = qlog::Level::info;
    static constexpr std::string_view name = "test.view";
    static constexpr std::array<std::string_view, 1> keys{"text"};
    using fields = qlog::Types<std::string_view>;
};
struct ExtraKey {
    static constexpr qlog::Level level = qlog::Level::info;
    static constexpr std::string_view name = "test.extra";
    static constexpr std::array<std::string_view, 2> keys{"a", "b"};
    using fields = qlog::Types<qlog::Count>;
};
static_assert(!qlog::Descriptor<ViewField>);
static_assert(!qlog::Descriptor<ExtraKey>);

// The catalogue: each name, level and bound as the table states them.
template <typename D>
constexpr bool row(std::string_view name, qlog::Level level) {
    return D::name == name && D::level == level && qlog::well_formed<D>() &&
           qlog::max_line<D>() <= qlog::Log::max_line_bytes;
}
static_assert(row<events::LogStart>("log.start", qlog::Level::info));
static_assert(row<events::LogDropped>("log.dropped", qlog::Level::warn));
static_assert(row<events::OpBegin>("op.begin", qlog::Level::debug));
static_assert(row<events::OpEnd>("op.end", qlog::Level::info));
static_assert(row<events::ErrorTyped>("error.typed", qlog::Level::error));
static_assert(row<events::DriveRequest>("drive.request", qlog::Level::debug));
static_assert(row<events::DriveReply>("drive.reply", qlog::Level::debug));
static_assert(row<events::CaseEnd>("case.end", qlog::Level::info));
static_assert(row<events::FaultInjected>("fault.injected", qlog::Level::debug));

// The encoder in constant evaluation: keys in code point order.
constexpr bool line_is(const qlog::HeadView& head, std::string_view want) {
    const std::array<qlog::FieldView, 3> fields{qlog::field("name", qlog::Name("read")),
                                               qlog::field("peak", qlog::Count(4096U)),
                                               qlog::field("ms", qlog::Millis(12U))};
    std::string out;
    qlog::encode_line(head, fields, out);
    return out == want;
}
static_assert(line_is(qlog::HeadView{"info", "op.end", 3, 4, 5},
                      R"({"event":"op.end","level":"info","log":3,"ms":12,"name":"read","op":4,"peak":4096,"t":5})"));
static_assert(line_is(qlog::HeadView{"error", "e", qlog::max_number, 0, 0},
                      R"({"event":"e","level":"error","log":9007199254740991,"ms":12,"name":"read","op":0,)"
                      R"("peak":4096,"t":0})"));

// Count and Millis saturate at JSON's range.
static_assert(qlog::Count(std::uint64_t{1} << 60U).value() == qlog::max_number);
static_assert(qlog::Count(qlog::max_number).value() == qlog::max_number);
static_assert(qlog::Millis(~std::uint64_t{0}).value() == qlog::max_number);
static_assert(qlog::Count(std::uint8_t{7}).value() == 7);

// pick: run-time text chooses a catalogued name, else the fallback.
constexpr std::array<qlog::Name, 2> table{"read", "write"};
static_assert(qlog::pick("write", table, "unknown") == qlog::Name("write"));
static_assert(qlog::pick("writer", table, "unknown") == qlog::Name("unknown"));
static_assert(qlog::pick("", table, "unknown") == qlog::Name("unknown"));

static_assert(qlog::level_name(qlog::Level::trace).text() == "trace");
static_assert(qlog::level_name(qlog::Level::error).text() == "error");
static_assert(qlog::Level::trace < qlog::Level::debug && qlog::Level::warn < qlog::Level::error);

// The widest event: eight keys of 8 bytes, each a 64-byte name.
struct Widest {
    static constexpr qlog::Level level = qlog::Level::info;
    static constexpr std::string_view name = "test.widest";
    static constexpr std::array<std::string_view, 8> keys{"field_01", "field_02", "field_03", "field_04",
                                                          "field_05", "field_06", "field_07", "field_08"};
    using fields = qlog::Types<qlog::Name, qlog::Name, qlog::Name, qlog::Name, qlog::Name, qlog::Name, qlog::Name, qlog::Name>;
};
constexpr qlog::Name wide = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static_assert(wide.text().size() == qlog::Name::max_bytes);
static_assert(qlog::max_line<Widest>() <= qlog::Log::max_line_bytes);

}  // namespace

namespace qiven3::test {

void widest(qlog::Log& lg) noexcept { qlog::event(lg, Widest{}, wide, wide, wide, wide, wide, wide, wide, wide); }

std::size_t widest_max() noexcept { return qlog::max_line<Widest>(); }

std::size_t widest_peak(memory::Arena& arena) {
    std::array<qlog::FieldView, 8> fields{};
    for (std::size_t i = 0; i < fields.size(); ++i) {
        fields.at(i) = qlog::field(Widest::keys.at(i), wide);
    }
    {
        memory::String out{memory::Allocator<char>(arena)};
        qlog::encode_line(qlog::HeadView{"info", Widest::name, qlog::max_number, 0, 0}, fields, out);
    }
    return arena.peak();
}

bool constant_held() noexcept { return true; }

}  // namespace qiven3::test
