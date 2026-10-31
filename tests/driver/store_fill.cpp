// A fill's claims (driver/store_threads.hpp's store_fill): claims like the
// fixture A on one lane after its head, each its content with the mark
// replaced by its seq, made by the stage book a batch at a time. No check
// is made: the store's append makes the contract's. And a racer's claim
// (driver/store_race.hpp).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "driver/store_race.hpp"
#include "driver/store_threads.hpp"
#include "entry/header.hpp"
#include "support/fixtures/stage_book.hpp"

namespace qiven3::drive::wire {
namespace {

namespace header = entry::header;

// A time minute minutes after 2026-10-17T08:00, as the header has it.
std::string at_of(std::int64_t minute) {
    const std::int64_t hour = 8 + (minute / 60);
    if (minute < 0 || hour > 23) {
        throw RequestError("store_fill: the claims' times pass the day");
    }
    const auto two = [](std::int64_t n) { return std::string(n < 10 ? "0" : "") + std::to_string(n); };
    return "2026-10-17T" + two(hour) + ":" + two(minute % 60) + ":00.000Z";
}

}  // namespace

Fill::Fill(const test::stage_book::Book& book, ArgView request, const std::optional<store::Head>& head)
    : book_(&book), scope_(text(request, "scope")), lane_(text(request, "lane")), content_(text(request, "content")),
      mark_(text(request, "mark")), writer_(text(request, "writer")), count_(integer(request, "count")),
      minute_(integer(request, "minute", 0)) {
    if (mark_.empty() || content_.find(mark_) == std::string::npos) {
        throw RequestError("store_fill: the content holds no mark");
    }
    if (count_ < 0) {
        throw RequestError("store_fill: a count below 0");
    }
    if (head) {
        first_ = head->seq + 1;
        last_ = std::string(head->id.view());
    }
}

std::span<const store::EntryView> Fill::next() {
    lines_.clear();
    bodies_.clear();
    views_.clear();
    expected_ = last_;
    const std::int64_t n = std::min(fill_batch, count_ - made_);
    for (std::int64_t i = 0; i < n; ++i, ++made_) {
        const std::int64_t seq = first_ + made_;
        std::string content = content_;
        content.replace(content.find(mark_), mark_.size(), std::to_string(seq));
        const std::string at = at_of(minute_ + (made_ / 60));
        test::stage_book::Stored s = book_->made("A", "A", content, [&](header::FieldsView& f) {
            f.scope = scope_;
            f.lane = lane_;
            f.seq = seq;
            f.writer = writer_;
            f.at = at;
            f.parent = last_ ? std::optional<std::string_view>(*last_) : std::nullopt;
        });
        last_ = std::string(header::parse_line(s.line).value(header::Slot::id));
        lines_.push_back(std::move(s.line));
        bodies_.push_back(std::move(s.record));
    }
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        views_.push_back({lines_[i], bodies_[i] ? std::optional<std::string_view>(*bodies_[i]) : std::nullopt});
    }
    return views_;
}

test::stage_book::Stored claim(const test::stage_book::Book& book, const std::string& scope,
                               const std::string& lane, const std::string& writer, const std::string& content,
                               std::int64_t seq, std::optional<std::string_view> parent) {
    const std::string at = at_of(seq / 60);
    return book.made("A", "A", content, [&](header::FieldsView& f) {
        f.scope = scope;
        f.lane = lane;
        f.seq = seq;
        f.writer = writer;
        f.at = at;
        f.parent = parent;
    });
}

store::Expected Fill::expected() const noexcept {
    return expected_ ? store::Expected(*expected_) : store::Expected(std::nullopt);
}

}  // namespace qiven3::drive::wire
