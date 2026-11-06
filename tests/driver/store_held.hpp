#pragma once

// The store half's objects held by id (driver/store_ops.hpp): each state,
// store object, scan, export and appender in a table of its kind, on the
// heap, with the id of what it was made over; a request names one by its
// key. The drive D gives the ids, one count for all, by next_id().
//
// thread: one

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "driver/drive.hpp"

namespace qiven3::drive::held {

// An object held by its id, and the id of what it was made over.
template <class T>
struct Kept {
    std::unique_ptr<T> object;
    std::int64_t parent = 0;
};
template <class T>
using Table = std::map<std::int64_t, Kept<T>>;

// The object a request names by key, or RequestError.
template <class T>
Kept<T>& named(Table<T>& table, ArgView request, std::string_view key) {
    const auto found = table.find(integer(request, key));
    if (found == table.end()) {
        throw RequestError("no " + std::string(key) + " held by that id");
    }
    return found->second;
}

// The id a new object is held by.
template <class D, class T>
std::int64_t keep(D& d, Table<T>& table, std::unique_ptr<T> made, std::int64_t parent) {
    const std::int64_t id = d.next_id();
    table.emplace(id, Kept<T>{std::move(made), parent});
    return id;
}

// Whether any object of the table was made over parent.
template <class T>
bool over(const Table<T>& table, std::int64_t parent) {
    for (const auto& [id, kept] : table) {
        if (kept.parent == parent) {
            return true;
        }
    }
    return false;
}

inline Value id_value(std::string_view key, std::int64_t id) {
    Value out;
    out.set(key, id);
    return out;
}

}  // namespace qiven3::drive::held
