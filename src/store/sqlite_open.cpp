// sqlite: the open steps, run through the connection wrapper
// (engine/sqlite.hpp): the checkout walk and the file's presence, read
// through the standard library's file system reads (a file's size by
// sizes/); then identify, the settings, create's schema and its TRUNCATE
// checkpoint, the write probe and coherence; a create refused at any of
// them removes its files, through the wrapper's remove_file, and raises
// its error again naming each removal refused (reraise). This file
// names nothing of the engine's own: every call is the wrapper's.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "store/codes.hpp"
#include "store/engine/sqlite.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "store/errors.hpp"

namespace qiven3::store::sqlite {

namespace {

namespace fs = std::filesystem;

// The schema, a statement each, then the meta rows.
constexpr std::array<std::string_view, 7> schema = {
    "CREATE TABLE meta (name TEXT PRIMARY KEY, value INTEGER NOT NULL) STRICT",
    "CREATE TABLE entry (id TEXT PRIMARY KEY, scope TEXT NOT NULL, lane TEXT NOT NULL, seq INTEGER NOT NULL, "
    "parent TEXT REFERENCES entry (id), kind TEXT NOT NULL, target TEXT REFERENCES entry (id), "
    "line BLOB NOT NULL, body INTEGER NOT NULL, UNIQUE (scope, lane, seq)) STRICT",
    "CREATE INDEX entry_target ON entry (target) WHERE target IS NOT NULL",
    "CREATE INDEX entry_forget ON entry (scope) WHERE kind = 'forget'",
    "CREATE TABLE body (id TEXT PRIMARY KEY REFERENCES entry (id), record BLOB NOT NULL) STRICT",
    "CREATE TABLE head (scope TEXT NOT NULL, lane TEXT NOT NULL, id TEXT NOT NULL REFERENCES entry (id), "
    "seq INTEGER NOT NULL, PRIMARY KEY (scope, lane)) STRICT, WITHOUT ROWID",
    "INSERT INTO meta (name, value) VALUES ('size', 0), ('count', 0), ('forgets', 0), ('vacuumed', 0), "
    "('purged', 0)",
};

// Coherence's queries (step 7), each a count or a flag.
// A head row naming no entry of its lane at its seq, or not the greatest.
constexpr std::string_view bad_heads =
    "SELECT count(*) FROM head h LEFT JOIN entry e ON e.id = h.id WHERE e.id IS NULL OR e.scope <> h.scope "
    "OR e.lane <> h.lane OR e.seq <> h.seq OR EXISTS (SELECT 1 FROM entry n WHERE n.scope = h.scope "
    "AND n.lane = h.lane AND n.seq > h.seq)";
// An entry whose lane has no head row.
constexpr std::string_view headless =
    "SELECT EXISTS (SELECT 1 FROM entry e WHERE NOT EXISTS (SELECT 1 FROM head h WHERE h.scope = e.scope "
    "AND h.lane = e.lane))";
// A body row of a forget or of a forget's target.
constexpr std::string_view kept_bodies =
    "SELECT EXISTS (SELECT 1 FROM entry f WHERE f.kind = 'forget' AND (EXISTS (SELECT 1 FROM body b "
    "WHERE b.id = f.id) OR EXISTS (SELECT 1 FROM body b WHERE b.id = f.target)))";
constexpr std::string_view forget_rows = "SELECT count(*) FROM entry WHERE kind = 'forget'";
constexpr std::string_view entry_rows = "SELECT count(*) FROM entry";
constexpr std::string_view body_rows = "SELECT count(*) FROM body";
// The entries owed a body row: a body key no forget targets.
constexpr std::string_view owed_bodies =
    "SELECT count(*) FROM entry AS e WHERE e.body <> 0 AND NOT EXISTS (SELECT 1 FROM entry AS f "
    "WHERE f.target = e.id AND f.kind = 'forget')";
// A body row of an entry without the key.
constexpr std::string_view stray_bodies =
    "SELECT EXISTS (SELECT 1 FROM body AS b WHERE NOT EXISTS (SELECT 1 FROM entry AS e WHERE e.id = b.id "
    "AND e.body <> 0))";

// A short statement or detail composed in place: a pragma, a setting's
// name and value, a count; past its bytes the rest is dropped. A byte past
// a detail's, so that an Inline made of a full one cuts it at a code point.
class Text {
public:
    Text& add(std::string_view part) noexcept {
        for (const char c : part) {
            if (size_ == bytes_.size()) {
                break;
            }
            bytes_.at(size_++) = c;
        }
        return *this;
    }
    // In decimal.
    Text& number(std::int64_t value) noexcept {
        std::array<char, 24> digits{};
        std::size_t d = digits.size();
        // The magnitude unsigned, so that the most negative value has one too.
        std::uint64_t magnitude =
            value < 0 ? 0U - static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(value);
        do {
            digits.at(--d) = static_cast<char>('0' + (magnitude % 10U));
            magnitude /= 10U;
        } while (magnitude != 0U && d > 1);
        if (value < 0) {
            digits.at(--d) = '-';
        }
        return add(std::string_view(digits.data(), digits.size()).substr(d));
    }
    [[nodiscard]] std::string_view view() const noexcept { return {bytes_.data(), size_}; }

private:
    std::array<char, detail_bytes + 1> bytes_{};
    std::size_t size_ = 0;
};

// A path from its UTF-8 bytes, and a last component after them: each
// converted as UTF-8, never by the system's code page.
fs::path path_of(std::string_view utf8, std::u8string_view last = {}) {
    std::u8string text;
    for (const char c : utf8) {
        text.push_back(static_cast<char8_t>(c));
    }
    if (!last.empty()) {
        if (!utf8.empty() && utf8.back() != '/' && utf8.back() != '\\') {
            text.push_back(u8'/');
        }
        text.append(last);
    }
    return fs::path(text);
}

// A setting's value as the engine reads it back, copied into out.
std::string_view value_of(Connection& c, std::string_view name, Text& out, const WhereView& at) {
    Text sql;
    Statement s = c.prepare(sql.add("PRAGMA ").add(name).view(), at);
    if (s.step(at)) {
        out.add(s.text(0));
    }
    return out.view();
}

[[noreturn]] void incoherent(const WhereView& at, std::string_view detail) {
    throw StoreError::corrupt(at.operation, at.path, Corrupt::incoherent, detail);
}

// The meta rows into opened; a row missing, or one of another name,
// incoherent.
void read_meta(Connection& c, Opened& opened, const WhereView& at) {
    Statement s = c.prepare("SELECT name, value FROM meta", at);
    unsigned seen = 0;
    constexpr std::array<std::string_view, 5> names = {"size", "count", "forgets", "vacuumed", "purged"};
    while (s.step(at)) {
        const std::string_view name = s.text(0);
        const std::int64_t value = s.integer(1);
        std::size_t i = 0;
        while (i < names.size() && names.at(i) != name) {
            ++i;
        }
        if (i == names.size()) {
            incoherent(at, "meta: a row of another name");
        }
        seen |= 1U << i;
        if (i == 0) {
            opened.size = value;
        } else if (i == 1) {
            opened.count = value;
        } else if (i == 2) {
            opened.forgets = value;
        } else if (i == 3) {
            opened.vacuumed = value;
        } else {
            opened.purged = value;
        }
    }
    if (seen != 0x1FU) {
        incoherent(at, "meta: a row missing");
    }
}

}  // namespace

// Walked by directory_of, lexically, on the UTF-8 text: the standard
// library's path decomposition reaches names core does not call.
bool in_checkout(std::string_view path) {
    std::string_view at = path;
    for (;;) {
        std::error_code error;
        if (fs::exists(path_of(at, u8".git"), error)) {
            return true;
        }
        const std::string_view parent = directory_of(at);
        if (parent.empty() || parent == at) {
            return false;
        }
        at = parent;
    }
}

void presence(std::string_view path, bool create, const WhereView& at) {
    const fs::path p = path_of(path);
    std::error_code error;
    const fs::file_status status = fs::status(p, error);
    const bool there = fs::exists(status);
    if (create) {
        if (there) {
            throw StoreError(Code::exists, at.operation, at.path, "a file is there");
        }
        return;
    }
    if (!there) {
        throw StoreError(Code::missing, at.operation, at.path);
    }
    // The size read in sizes/ (file_bytes): none for a file not regular.
    if (file_bytes(path) == std::optional<std::uint64_t>(0)) {
        throw StoreError::corrupt(at.operation, at.path, Corrupt::corrupt, "0 bytes: a truncation");
    }
}

std::string_view directory_of(std::string_view path) noexcept {
    const std::size_t cut = path.find_last_of("/\\");
    if (cut == std::string_view::npos) {
        return {};
    }
    // A root keeps its separator: "/", "D:\".
    if (cut == 0 || path.at(cut - 1) == ':') {
        return path.substr(0, cut + 1);
    }
    return path.substr(0, cut);
}

bool is_directory(std::string_view path) {
    std::error_code error;
    return fs::is_directory(path_of(path), error);
}

void identify(Connection& c, const WhereView& at) {
    Text detail;
    const std::int64_t id = c.integer("PRAGMA application_id", at);
    if (id != application_id) {
        throw StoreError::corrupt(at.operation, at.path, Corrupt::foreign, detail.add("application_id ").number(id).view());
    }
    const std::int64_t version = c.integer("PRAGMA user_version", at);
    if (version != user_version) {
        throw StoreError::corrupt(at.operation, at.path, Corrupt::version, detail.add("user_version ").number(version).view());
    }
}

void apply(Connection& c, bool read_only, const WhereView& at) {
    for (const Setting& s : settings) {
        if (s.when == When::create || (read_only && s.when == When::read_write)) {
            continue;
        }
        Text sql;
        c.exec(sql.add("PRAGMA ").add(s.name.view()).add(" = ").add(s.value.view()).view(), at);
    }
    if (const std::optional<std::size_t> i = differing(c, at)) {
        const Setting& s = settings.at(*i);
        Text value;
        Text detail;
        const std::string_view read = value_of(c, s.name.view(), value, at);
        throw StoreError(Code::settings, at.operation, at.path, detail.add(s.name.view()).add(" ").add(read).view());
    }
}

std::optional<std::size_t> differing(Connection& c, const WhereView& at) {
    for (std::size_t i = 0; i < settings.size(); ++i) {
        const Setting& s = settings.at(i);
        if (s.when == When::create) {
            continue;
        }
        Text value;
        if (value_of(c, s.name.view(), value, at) != s.value.view()) {
            return i;
        }
    }
    return std::nullopt;
}

void create_schema(Connection& c, const WhereView& at) {
    c.exec("BEGIN IMMEDIATE", at);
    try {
        for (const std::string_view sql : schema) {
            c.exec(sql, at);
        }
        for (const Setting& s : settings) {
            if (s.when == When::create) {
                Text sql;
                c.exec(sql.add("PRAGMA ").add(s.name.view()).add(" = ").add(s.value.view()).view(), at);
            }
        }
        c.exec("COMMIT", at);
    } catch (...) {
        c.rollback();
        throw;
    }
    c.exec("PRAGMA wal_checkpoint(TRUNCATE)", at);
}

void probe(Connection& c, const WhereView& at) {
    c.exec("BEGIN IMMEDIATE", at);
    try {
        c.exec("UPDATE meta SET value = value WHERE name = 'size'", at);
    } catch (...) {
        c.rollback();
        throw;
    }
    c.exec("ROLLBACK", at);
}

Opened coherence(Connection& c, const WhereView& at) {
    Opened opened;
    c.exec("BEGIN", at);
    try {
        read_meta(c, opened, at);
        if (c.integer(bad_heads, at) != 0) {
            incoherent(at, "head: a row not naming its lane's last entry");
        }
        if (c.integer(headless, at) != 0) {
            incoherent(at, "head: a lane with entries and no row");
        }
        if (c.integer(kept_bodies, at) != 0) {
            throw StoreError::corrupt(at.operation, at.path, Corrupt::forgotten_body,
                                      "a body of a forget or of its target");
        }
        const std::int64_t forgets = c.integer(forget_rows, at);
        if (forgets != opened.forgets) {
            incoherent(at, "meta: forgets not the forget rows");
        }
        if (opened.purged < 0 || opened.purged > opened.vacuumed || opened.vacuumed > opened.forgets) {
            incoherent(at, "meta: purged, vacuumed and forgets out of order");
        }
        opened.entries = c.integer(entry_rows, at);
        opened.bodies = c.integer(body_rows, at);
        if (opened.bodies != c.integer(owed_bodies, at) || c.integer(stray_bodies, at) != 0) {
            incoherent(at, "body: the rows not the entries with a body key no forget targets");
        }
        c.exec("COMMIT", at);
    } catch (...) {
        c.rollback();
        throw;
    }
    return opened;
}

Refused unmake(std::string_view path) noexcept {
    Refused refused = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        if (!remove_file(path, removal_names.at(i))) {
            refused = static_cast<Refused>(refused | (1U << i));
        }
    }
    if (refused == 0 && !remove_file(path, {})) {
        refused = 1U << 3U;
    }
    return refused;
}

void reraise(Refused refused) {
    if (refused == 0) {
        throw;
    }
    // The error's detail, then each refused removal's name.
    const auto named = [refused](std::string_view detail) {
        Text text;
        text.add(detail).add(detail.empty() ? "" : "; ").add("removal refused: ");
        std::string_view comma;
        for (std::size_t i = 0; i < removal_names.size(); ++i) {
            if ((refused & (1U << i)) != 0U) {
                text.add(comma).add(removal_names.at(i));
                comma = ", ";
            }
        }
        return text;
    };
    try {
        throw;
    } catch (StoreError& e) {
        e.set_detail(named(e.detail()).view());
        throw;
    } catch (memory::MemoryError& e) {
        e.detail = decltype(e.detail)(named(e.detail.view()).view());
        throw;
    } catch (platform::PlatformError& e) {
        e.detail = decltype(e.detail)(named(e.detail.view()).view());
        throw;
    }
}

namespace {

// Steps 2 (the connection) to 7.
Connection connected(const memory::String& path, bool create, bool read_only, const WhereView& at, Opened& opened) {
    Connection c = Connection::open(path, read_only, at);
    if (!create) {
        identify(c, at);
    }
    apply(c, read_only, at);
    if (create) {
        create_schema(c, at);
    }
    if (!read_only) {
        probe(c, at);
    }
    opened = coherence(c, at);
    return c;
}

}  // namespace

Connection connect(const memory::String& path, bool create, bool read_only, const WhereView& at, Opened& opened) {
    return connected(path, create, read_only, at, opened);
}

}  // namespace qiven3::store::sqlite
