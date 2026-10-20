#pragma once

// The drive's wire: a request a line and a reply a line, each one JSON
// value in canonical form: keys sorted by their bytes, no space, strings
// raw in UTF-8 but for '"', '\\' and the control characters below U+0020,
// integers in decimal. As canonical JSON has, there is no null and no float.
// Bytes cross as lowercase hexadecimal strings.
//
// Test support: the core's strict parser and encoder come with their own
// package; this is the least the drive needs to read the tests' requests.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qiven3::drive {

// A request the wire cannot read, or a value of the wrong kind.
class WireError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Value {
public:
    enum class Kind { boolean, integer, string, array, object };

    Value() : kind_(Kind::object) {}  // an empty object
    Value(bool value) : kind_(Kind::boolean), boolean_(value) {}
    Value(int value) : kind_(Kind::integer), integer_(value) {}
    Value(std::int64_t value) : kind_(Kind::integer), integer_(value) {}
    Value(std::string value) : kind_(Kind::string), string_(std::move(value)) {}
    Value(const char* value) : kind_(Kind::string), string_(value) {}
    static Value array(std::vector<Value> items = {});

    Kind kind() const noexcept { return kind_; }
    // Each throws WireError naming what, for a value of another kind.
    bool as_boolean(std::string_view what) const;
    std::int64_t as_integer(std::string_view what) const;
    const std::string& as_string(std::string_view what) const;
    const std::vector<Value>& as_array(std::string_view what) const;

    // An array's item appended.
    Value& push(Value item);
    // An object's member, or null; set replaces a present key.
    const Value* find(std::string_view key) const;
    Value& set(std::string key, Value value);
    const std::vector<std::string>& keys() const noexcept { return keys_; }

private:
    Kind kind_;
    bool boolean_ = false;
    std::int64_t integer_ = 0;
    std::string string_;
    std::vector<Value> items_;      // an array's items, or an object's values
    std::vector<std::string> keys_;  // an object's keys, beside its values
};

// One value, the whole text: WireError for anything else, a repeated key
// among it.
Value parse(std::string_view text);

// The value's canonical bytes.
std::string encode(const Value& value);

// Bytes as lowercase hexadecimal, and back: WireError for an odd length or
// a character that is no hexadecimal digit.
std::string to_hex(std::string_view bytes);
std::string from_hex(std::string_view hex);

}  // namespace qiven3::drive
