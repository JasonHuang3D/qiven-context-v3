// The drives' common half (driver/drive.hpp): options, request helpers,
// the requests and replies on json, adapter_for and current, the HAL's
// table, the replies, the record of each request's arena and fault, and
// the loop's streams.
//
// The standard streams are binary on every system: each reply ends in LF
// alone, so the bytes a test compares do not depend on the system; on
// Windows _setmode takes the C runtime's text mode off both before the
// first request. A request's CR before its LF is dropped.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "driver/drive.hpp"
#include "memory/memory.hpp"
#include "platform/current.hpp"
#include "qiven3/utf.hpp"

#if defined(QIVEN3_DRIVE_HAL)
#include "hal/hal.hpp"
#endif

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif


namespace qiven3::drive {
namespace {

std::optional<std::string> config_of(ArgView request) {
    return request.find("config") ? std::optional<std::string>(text(request, "config")) : std::nullopt;
}

std::optional<std::string_view> view(const std::optional<std::string>& text) {
    return text ? std::optional<std::string_view>(*text) : std::nullopt;
}

// adapter_for and current as a command's main calls them: the made
// adapter's name.
std::optional<Value> adapter_for(Session&, ArgView request) {
    const std::optional<std::string> config = config_of(request);
#if defined(_WIN32)
    return scalar(platform::adapter_for(text(request, "system"), view(config)).name());
#else
    platform::adapter_for(text(request, "system"), view(config));
#endif
}

std::optional<Value> current(Session&, ArgView request) {
    const std::optional<std::string> config = config_of(request);
#if defined(_WIN32)
    return scalar(platform::current(view(config)).name());
#else
    platform::current(view(config));
#endif
}

}  // namespace

int refuse(std::string_view program, const std::string& why) {
    std::cerr << program << ": " << why << "\n";
    return 2;
}

std::optional<Options> read_options(int argc, char** argv, bool fake, std::string& why) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool valued =
            arg == "--config" || arg == "--hal" || (fake && (arg == "--root" || arg == "--seed" || arg == "--store"));
        if (!valued) {
            why = "an unknown argument: " + arg;
            return std::nullopt;
        }
        if (i + 1 >= argc) {
            why = arg + " takes a value";
            return std::nullopt;
        }
        const std::string value = argv[++i];
        if (arg == "--root") {
            options.root = value;
        } else if (arg == "--config") {
            options.config = value;
        } else if (arg == "--store") {
            if (value != "sqlite" && value != "memory") {
                why = "--store takes sqlite or memory: " + value;
                return std::nullopt;
            }
            options.store = value;
        } else if (arg == "--seed") {
            try {
                if (value.empty() || value[0] < '0' || value[0] > '9') {
                    throw std::invalid_argument(value);
                }
                std::size_t used = 0;
                options.seed = std::stoull(value, &used);
                if (used != value.size()) {
                    throw std::invalid_argument(value);
                }
            } catch (const std::exception&) {
                why = "--seed takes a decimal integer: " + value;
                return std::nullopt;
            }
        } else if (value == "fake") {
            options.hal_fake = true;
        } else {
            why = "--hal takes fake: " + value;
            return std::nullopt;
        }
    }
    if (fake && !options.root) {
        why = "the fake's drive takes --root DIR";
        return std::nullopt;
    }
#if !defined(QIVEN3_DRIVE_HAL)
    if (options.hal_fake) {
        why = "--hal fake: no HAL in this build (QIVEN3_HAL off)";
        return std::nullopt;
    }
#endif
    return options;
}

namespace {

int nibble(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

Value open_and_closed(json::Kind kind) {
    Value value;
    json::Writer<std::allocator<char>> writer(value);
    writer.open(kind);
    writer.close();
    return value;
}

}  // namespace

void ArgView::expect(json::Kind kind, std::string_view what, const char* name) const {
    if (this->kind() != kind) {
        throw RequestError(std::string(what) + ": not " + name);
    }
}

std::optional<ArgView> ArgView::find(std::string_view key) const {
    if (kind() != json::Kind::object) {
        return std::nullopt;
    }
    const std::optional<json::Index> found = value_->find(at_, key);
    return found ? std::optional<ArgView>(ArgView(*value_, *found)) : std::nullopt;
}

bool ArgView::as_boolean(std::string_view what) const {
    expect(json::Kind::boolean, what, "true or false");
    return value_->boolean(at_);
}

std::int64_t ArgView::as_integer(std::string_view what) const {
    expect(json::Kind::integer, what, "an integer");
    return value_->integer(at_);
}

std::string ArgView::as_string(std::string_view what) const {
    expect(json::Kind::string, what, "a string");
    return std::string(value_->text(at_));
}

std::vector<ArgView> ArgView::as_array(std::string_view what) const {
    expect(json::Kind::array, what, "an array");
    std::vector<ArgView> items;
    items.reserve(value_->count(at_));
    for (json::Index k = at_ + 1; k < value_->next(at_); k = value_->next(k)) {
        items.emplace_back(*value_, k);
    }
    return items;
}

std::vector<std::pair<std::string, ArgView>> ArgView::as_object(std::string_view what) const {
    expect(json::Kind::object, what, "an object");
    std::vector<std::pair<std::string, ArgView>> members;
    members.reserve(value_->count(at_));
    for (json::Index k = at_ + 1; k < value_->next(at_); k = value_->next(k + 1)) {
        members.emplace_back(std::string(value_->text(k)), ArgView(*value_, k + 1));
    }
    return members;
}

Value parse(std::string_view line) {
    Value request;
    try {
        if (const std::optional<ParseCode> fault = utf::check(line)) {
            throw ParseError{*fault};
        }
        json::scan(line, json::max_depth, request);
    } catch (const ParseError& error) {
        throw RequestError("the request: " + std::string(qiven3::name(error.code)));
    }
    // What json reads and canonical JSON has not: canon's walk refuses
    // them in the strict parser.
    for (json::Index k = 0; k < request.size(); ++k) {
        if (request.kind(k) == json::Kind::null) {
            throw RequestError("the request: null");
        }
        if (request.kind(k) == json::Kind::string && utf::check(request.text(k))) {
            throw RequestError("the request: a lone surrogate escape");
        }
    }
    return request;
}

std::string encode(const Value& reply) {
    std::string out;
    json::encode(reply, out);
    return out;
}

Value scalar(std::string_view text) {
    Value value;
    json::Writer<std::allocator<char>> writer(value);
    writer.string(text);
    return value;
}

Value scalar(std::int64_t integer) {
    Value value;
    json::Writer<std::allocator<char>> writer(value);
    writer.integer(integer);
    return value;
}

Value array() { return open_and_closed(json::Kind::array); }

Value object() { return open_and_closed(json::Kind::object); }

std::string to_hex(std::string_view bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const char c : bytes) {
        const auto u = static_cast<unsigned char>(c);
        out += digits[u >> 4U];
        out += digits[u & 0xFU];
    }
    return out;
}

std::string from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        throw RequestError("hexadecimal of an odd length");
    }
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int high = nibble(hex[i]);
        const int low = nibble(hex[i + 1]);
        if (high < 0 || low < 0) {
            throw RequestError("a character that is no hexadecimal digit");
        }
        out += static_cast<char>((high << 4) | low);
    }
    return out;
}

ArgView argument(ArgView request, std::string_view key) {
    const std::optional<ArgView> found = request.find(key);
    if (!found) {
        throw RequestError("the argument '" + std::string(key) + "' is missing");
    }
    return *found;
}

std::string text(ArgView request, std::string_view key) { return argument(request, key).as_string(key); }

std::int64_t integer(ArgView request, std::string_view key, std::optional<std::int64_t> fallback) {
    const std::optional<ArgView> found = request.find(key);
    if (!found && fallback) {
        return *fallback;
    }
    return argument(request, key).as_integer(key);
}

bool flag(ArgView request, std::string_view key, bool fallback) {
    const std::optional<ArgView> found = request.find(key);
    return found ? found->as_boolean(key) : fallback;
}

platform::Seconds deadline(ArgView request, std::string_view key) {
    return platform::Seconds(static_cast<double>(integer(request, key, 5000)) / 1000.0);
}

const Operations& operations() {
    static const Operations table = [] {
        Operations all;
        all.emplace("adapter_for", adapter_for);
        all.emplace("current", current);
#if defined(QIVEN3_DRIVE_HAL)
        add_hal_operations(all);
#endif
        return all;
    }();
    return table;
}

std::optional<Value> common(Session& session, const std::string& name, ArgView request) {
    const auto found = operations().find(name);
    if (found == operations().end()) {
        throw RequestError(name.starts_with("hal_") && !session.options().hal_fake
                               ? "no HAL here: " + name + " needs --hal fake in a build with it"
                               : "no operation '" + name + "'");
    }
    return found->second(session, request);
}

Value ok_reply(std::optional<Value> result) {
    Value out;
    out.set("ok", true);
    if (result) {
        out.set("result", *result);
    }
    return out;
}

Value error_reply(Value error) {
    Value reply;
    reply.set("ok", false);
    reply.set("error", error);
    return reply;
}

Value typed(std::string_view kind, std::string_view code, std::string_view operation, std::string_view detail) {
    Value error;
    error.set("kind", kind);
    error.set("code", code);
    error.set("operation", operation);
    error.set("detail", detail);
    return error;
}

Value plain(std::string_view kind, std::string_view detail) {
    Value error;
    error.set("kind", kind);
    error.set("detail", detail);
    return error;
}

Value caught(std::string_view name) {
    try {
        throw;
    } catch (const platform::PlatformError& error) {
        return error_reply(
            typed("platform", platform::name(error.code), error.operation.view(), error.detail.view()));
    } catch (const memory::MemoryError& error) {
        return error_reply(typed("memory", memory::name(error.code), error.operation.view(), error.detail.view()));
#if defined(QIVEN3_DRIVE_HAL)
    } catch (const hal::HalError& error) {
        return error_reply(typed("hal", hal::name(error.code), error.operation.view(), error.detail.view()));
#endif
    } catch (const RequestError& error) {
        return error_reply(plain("request", error.what()));
    } catch (const std::exception& error) {
        return error_reply(plain("exception", std::string(name) + ": " + error.what()));
    }
}

std::size_t budget(ArgView request, std::size_t stated) {
    const std::optional<ArgView> given = request.find("budget");
    if (!given) {
        return stated;
    }
    const std::int64_t bytes = given->as_integer("budget");
    if (bytes < 0) {
        throw RequestError("budget: a count of bytes, not negative");
    }
    return static_cast<std::size_t>(bytes);
}

void begin(Session& session, ArgView request) {
    session.metered() = Metered{};
    const std::optional<ArgView> fault = request.find("fault");
    if (!fault) {
        return;
    }
#if defined(QIVEN3_FAULTS)
    const std::int64_t k = integer(*fault, "k");
    const std::string of = text(*fault, "of");
    Faults& faults = session.faults();
    if (k < 0 || (of != "allocation" && of != "platform")) {
        throw RequestError("fault: {\"k\": K, not negative, \"of\": \"allocation\" or \"platform\"}");
    }
    faults.armed = of == "allocation" ? &faults.allocation : &faults.platform;
    faults.k = static_cast<std::size_t>(k);
    faults.armed->arm(faults.k);
#else
    (void)session;
    throw RequestError("not applicable: built without QIVEN3_FAULTS, so no fault");
#endif
}

Value finish(Session& session, Value reply, std::size_t held) {
    const Metered& metered = session.metered();
    if (metered.counted) {
        Value memory;
        memory.set("budget", static_cast<std::int64_t>(metered.budget));
        memory.set("peak", static_cast<std::int64_t>(metered.peak));
        memory.set("left", static_cast<std::int64_t>(metered.left));
        reply.set("memory", memory);
    }
    reply.set("held", static_cast<std::int64_t>(held));
#if defined(QIVEN3_FAULTS)
    Faults& faults = session.faults();
    if (faults.armed != nullptr) {
        Value fault;
        fault.set("k", static_cast<std::int64_t>(faults.k));
        fault.set("of", faults.armed == &faults.allocation ? "allocation" : "platform");
        fault.set("calls", static_cast<std::int64_t>(faults.armed->calls()));
        fault.set("fired", faults.armed->fired());
        reply.set("fault", fault);
        faults.armed = nullptr;
    }
    faults.allocation.disarm();
    faults.platform.disarm();
#endif
    return reply;
}

void ready_streams() {
    // A caller's mistake ends the drive through fail_fast, which the
    // diagnostic entry reports.
#if defined(_WIN32)
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1) {
        memory::fail_fast("the drive's standard streams did not take binary mode");
    }
#endif
}

bool next_line(std::string& line) {
    if (!std::getline(std::cin, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return true;
}

std::size_t send(const Value& reply) {
    const std::string bytes = encode(reply);
    std::cout << bytes << '\n' << std::flush;
    return bytes.size();
}

namespace {

// The process's Journal's pages, of static storage.
test::Journal::Pages pages;

// The names a request's op, an error's kind, its code and its operation
// are picked from: the drives' operations and those the typed errors name.
constexpr std::array<log::Name, 90> operation_names{
    "adapter_for", "current", "name", "config_dir", "resolve", "normcase", "facts", "synced",
    "replace", "create", "remove_directory", "read", "program", "run", "code_page", "stdout", "residual",
    "random_bytes", "wipe", "now", "pages", "pages_check", "pages_free", "set_clock", "set_read_error",
    "refuse_pages", "calls", "written", "lent", "hal_fake", "hal_facts", "hal_device",
    "hal_allocate", "hal_upload", "hal_download", "hal_free", "hal_run", "hal_counts",
    "allocate", "read_line", "wait",
    // The store half's requests (driver/store_ops.hpp), then the store's
    // operations as its errors name them.
    "factory_make", "factory_again", "factory_read_only", "factory_reopen", "factory_create", "factory_drop",
    "fixture_entry", "fixture_made", "store_capabilities", "store_append", "store_head", "store_heads",
    "store_header", "store_body", "store_forgotten", "store_scan", "store_forget", "store_purge", "store_export",
    "store_import", "store_size", "store_verify", "store_plans", "store_seam_write", "store_close",
    "store_read_only", "store_limits", "store_drop",
    "scan_next", "scan_close", "export_heads", "export_next", "export_close", "export_drop",
    "open", "append", "head", "heads", "header", "body", "forgotten", "scan", "forget", "purge",
    "export", "import", "size", "verify", "close"};
constexpr std::array<log::Name, 6> kinds{"platform", "memory", "hal", "store", "request", "exception"};
constexpr std::array<log::Name, 38> codes{"unsupported", "config", "path", "exists", "busy",
                                          "io", "start", "deadline", "output", "memory",
                                          "budget", "size", "kernel", "launch",
                                          // The store's codes, causes and kinds' codes.
                                          "closed", "location", "missing", "read_only", "thread",
                                          "settings", "engine_version", "guard", "internal", "forget_kind",
                                          "whole_record", "held", "not_writable", "permission",
                                          "not_a_store", "corrupt", "foreign", "version", "incoherent",
                                          "id_mismatch", "body_mismatch", "forgotten_body", "store_full",
                                          "disk_full"};

static_assert(log::pick("hal_run", operation_names, "unknown") == log::Name("hal_run"));
static_assert(log::pick("sk-not-an-operation", operation_names, "unknown") == log::Name("unknown"));
static_assert(log::pick("exception", kinds, "unknown") == log::Name("exception"));
static_assert(log::pick("launch", codes, "unknown") == log::Name("launch"));

// The kinds of the typed errors.
bool typed_kind(std::string_view kind) noexcept {
    return kind == "platform" || kind == "memory" || kind == "hal" || kind == "store";
}

}  // namespace

test::Journal::Pages& journal_pages() noexcept { return pages; }

log::Name named(const std::optional<Value>& request) noexcept {
    if (!request) {
        return "unknown";
    }
    try {
        const std::optional<ArgView> op = ArgView(*request).find("op");
        if (op && op->kind() == json::Kind::string) {
            return log::pick(op->as_string("op"), operation_names, "unknown");
        }
    } catch (...) {
        // A request whose op cannot be read is unknown.
    }
    return "unknown";
}

void noted(log::Op& op, const Value& reply, std::size_t bytes) noexcept {
    log::Name status = "unknown";
    log::Name outcome = "unknown";
    std::uint64_t peak = 0;
    try {
        const ArgView r(reply);
        if (flag(r, "ok", false)) {
            status = "ok";
            outcome = "ok";
        } else {
            const ArgView error = argument(r, "error");
            const std::string kind = text(error, "kind");
            status = log::pick(kind, kinds, "unknown");
            outcome = status;
            if (typed_kind(kind)) {
                // A store error of a kind with no code ends as its kind.
                const std::optional<ArgView> code = error.find("code");
                outcome = code ? log::pick(code->as_string("code"), codes, "unknown") : status;
                log::event(op, log::events::error_typed, status, outcome,
                           log::pick(text(error, "operation"), operation_names, "unknown"));
            }
        }
        if (const std::optional<ArgView> memory = r.find("memory")) {
            const std::int64_t got = integer(*memory, "peak");
            peak = got > 0 ? static_cast<std::uint64_t>(got) : 0;
        }
    } catch (...) {
        // A reply the drive made is always readable; were it not, its events
        // say unknown.
    }
    log::event(op, log::events::drive_reply, status, log::Count(bytes));
    op.end(outcome, log::Count(peak));
}

}  // namespace qiven3::drive
