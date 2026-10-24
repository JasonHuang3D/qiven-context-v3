// seams: the naive store's files (seams.hpp).

#include "support/seams/seams.hpp"

#include <fstream>
#include <ios>
#include <iterator>
#include <utility>

#include "entry/header.hpp"
#include "store/errors.hpp"

namespace qiven3::test::seams {

namespace {

// A name's bytes as a file name: letters, digits and - as they are, every
// other byte %XX, so that "project:qiven" names a file on each system.
std::string escaped(std::string_view name) {
    static constexpr std::string_view hex = "0123456789abcdef";
    std::string out;
    for (const char c : name) {
        const auto b = static_cast<unsigned char>(c);
        if ((b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '-') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(hex[b >> 4U]);
            out.push_back(hex[b & 15U]);
        }
    }
    return out;
}

std::string id_of(std::string_view line) {
    return std::string(entry::header::parse_line(line).value(entry::header::Slot::id));
}

}  // namespace

NaiveStore::NaiveStore(std::filesystem::path directory) : directory_(std::move(directory)) {
    std::filesystem::create_directories(directory_);
}

std::filesystem::path NaiveStore::file(std::string_view scope, std::string_view lane, std::string_view suffix) const {
    return directory_ / (escaped(scope) + "~" + escaped(lane) + std::string(suffix));
}

std::optional<std::string> NaiveStore::head(std::string_view scope, std::string_view lane) const {
    std::ifstream in(file(scope, lane, ".head"), std::ios::binary);
    std::string id((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (id.empty()) {
        return std::nullopt;
    }
    return id;
}

std::string NaiveStore::append(std::string_view scope, std::string_view lane, store::Expected expected,
                               std::span<const store::EntryView> entries) {
    const std::optional<std::string> now = head(scope, lane);
    const std::string_view held = now ? std::string_view(*now) : std::string_view();
    if (held != expected.value_or(std::string_view())) {
        throw store::StoreError::head_conflict("append", directory_.generic_string(), scope, lane,
                                               expected.value_or(std::string_view()), held);
    }
    if (entries.empty()) {
        return std::string(held);
    }
    {
        std::ofstream out(file(scope, lane, ".jsonl"), std::ios::binary | std::ios::app);
        for (const store::EntryView& e : entries) {
            out << e.line;
            if (e.body) {
                out << '\t' << *e.body;
            }
            out << '\n';
        }
    }
    std::string last = id_of(entries.back().line);
    std::ofstream(file(scope, lane, ".head"), std::ios::binary | std::ios::trunc) << last;
    return last;
}

std::vector<std::string> NaiveStore::lines(std::string_view scope, std::string_view lane) const {
    std::ifstream in(file(scope, lane, ".jsonl"), std::ios::binary);
    std::vector<std::string> out;
    for (std::string line; std::getline(in, line);) {
        out.push_back(std::move(line));
    }
    return out;
}

}  // namespace qiven3::test::seams
