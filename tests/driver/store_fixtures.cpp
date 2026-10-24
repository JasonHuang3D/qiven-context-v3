// The store half's fixtures (driver/store_wire.hpp): entries.json's
// entries as the stage book stores them, and entries made from one with
// fields changed, for the store cases' setups. No check is made: a made
// entry is what the case asks for, valid or not, its id computed over its
// fields and its content's digest under the key named.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "driver/store_wire.hpp"
#include "entry/header.hpp"
#include "support/fixtures/fixtures.hpp"
#include "support/fixtures/stage_book.hpp"

namespace qiven3::drive::wire {
namespace {

namespace header = entry::header;
using Slot = header::Slot;

// The fixtures' names, as entries.json holds them; one not among them is
// the request's error, as the book would end the process.
void named(std::string_view name) {
    static const std::vector<std::string> names = [] {
        json::Basic<std::allocator<char>> value;
        test::fixtures::load_entries(value);
        std::vector<std::string> out;
        for (std::size_t i = 0; i < test::fixtures::entry_count(value); ++i) {
            out.emplace_back(test::fixtures::entry(value, i).name);
        }
        return out;
    }();
    if (std::find(names.begin(), names.end(), name) == names.end()) {
        throw RequestError("no fixture named " + std::string(name));
    }
}

Value entry_value(const test::stage_book::Stored& s) {
    Value out;
    out.set("line", to_hex(s.line));
    if (s.record) {
        out.set("body", to_hex(*s.record));
    }
    return out;
}

// The request's "fields", each a text but seq: the header's fields to set.
struct Changes {
    std::vector<std::pair<std::string, std::string>> texts;
    std::optional<std::int64_t> seq;
    std::vector<std::string> absent;

    explicit Changes(ArgView request) {
        if (const std::optional<ArgView> fields = request.find("fields")) {
            for (const auto& [key, value] : fields->as_object("fields")) {
                if (key == "seq") {
                    seq = value.as_integer("fields.seq");
                } else if (key == "scope" || key == "lane" || key == "parent" || key == "kind" || key == "writer" ||
                           key == "basis" || key == "at" || key == "target") {
                    texts.emplace_back(key, value.as_string("fields." + key));
                } else {
                    throw RequestError("no field " + key + " to set");
                }
            }
        }
        if (const std::optional<ArgView> list = request.find("absent")) {
            for (const ArgView item : list->as_array("absent")) {
                std::string key = item.as_string("absent");
                if (key != "parent" && key != "target" && key != "content") {
                    throw RequestError("absent names parent, target or content, not " + key);
                }
                absent.push_back(std::move(key));
            }
        }
    }
    [[nodiscard]] bool removes(std::string_view key) const {
        return std::find(absent.begin(), absent.end(), key) != absent.end();
    }
    void apply(header::FieldsView& f) const {
        for (const auto& [key, value] : texts) {
            const std::string_view v = value;
            if (key == "scope") {
                f.scope = v;
            } else if (key == "lane") {
                f.lane = v;
            } else if (key == "parent") {
                f.parent = v;
            } else if (key == "kind") {
                f.kind = v;
            } else if (key == "writer") {
                f.writer = v;
            } else if (key == "basis") {
                f.basis = v;
            } else if (key == "at") {
                f.at = v;
            } else {
                f.target = v;
            }
        }
        if (seq) {
            f.seq = *seq;
        }
        if (removes("parent")) {
            f.parent.reset();
        }
        if (removes("target")) {
            f.target.reset();
        }
    }
};

}  // namespace

Value fixture_entry(const test::stage_book::Book& book, ArgView request) {
    const std::string name = text(request, "name");
    named(name);
    return entry_value(book.stored(name));
}

Value fixture_made(const test::stage_book::Book& book, ArgView request) {
    const std::string like = text(request, "like");
    named(like);
    const Changes changes(request);
    const test::stage_book::Stored base = book.stored(like);
    if (changes.removes("content")) {
        // A header with no content and no body, as a forget's: its fields
        // read off the fixture's line, changed, the id computed.
        const header::Header<> h = header::parse_line(base.line);
        header::FieldsView f;
        f.scope = h.value(Slot::scope);
        f.lane = h.value(Slot::lane);
        f.seq = h.seq();
        f.parent = h.text(Slot::parent);
        f.kind = h.value(Slot::kind);
        f.writer = h.value(Slot::writer);
        f.basis = h.value(Slot::basis);
        f.at = h.value(Slot::at);
        f.target = h.text(Slot::target);
        changes.apply(f);
        test::stage_book::Stored s;
        header::make(f).line(s.line);
        return entry_value(s);
    }
    std::string key_of = like;
    if (const std::optional<ArgView> k = request.find("key_of")) {
        key_of = k->as_string("key_of");
        named(key_of);
    }
    if (!book.stored(key_of).record) {
        throw RequestError("the fixture " + key_of + " has no key");
    }
    std::string content;
    if (const std::optional<ArgView> c = request.find("content")) {
        content = c->as_string("content");
    } else if (base.content) {
        content = *base.content;
    } else {
        throw RequestError("the fixture " + like + " has no content: give one");
    }
    return entry_value(book.made(like, key_of, content, [&](header::FieldsView& f) { changes.apply(f); }));
}

}  // namespace qiven3::drive::wire
