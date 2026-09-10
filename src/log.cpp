#include "go_plugin/log.hpp"

#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>
#include <mutex>
#include <unistd.h>

namespace go_plugin::log {
namespace {

std::atomic<int> g_level{static_cast<int>(Level::Info)};

std::mutex& SinkMutex() {
    static std::mutex m;
    return m;
}

std::mutex& WriteMutex() {
    static std::mutex m;
    return m;
}

Sink& CurrentSink() {
    static Sink sink;
    return sink;
}

void AppendEscaped(std::string& out, std::string_view s) {
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
}

void WriteToStderr(const Record& record) {
    std::string line = Encode(record);
    line += '\n';

    // One write of the whole line: a plugin logs from its gRPC threads and from
    // whatever library it drives, and the host reads a line at a time.
    std::lock_guard<std::mutex> lock(WriteMutex());
    ssize_t written = 0;
    while (written < static_cast<ssize_t>(line.size())) {
        ssize_t n = ::write(STDERR_FILENO, line.data() + written, line.size() - written);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return;
        }
        written += n;
    }
}

}  // namespace

Field::Field(std::string_view key, std::string value, bool literal)
    : key_(key), value_(std::move(value)), literal_(literal) {}

Field::Field(std::string_view key, std::string_view value) : Field(key, std::string(value), false) {}
Field::Field(std::string_view key, const char* value)
    : Field(key, std::string(value ? value : ""), false) {}
Field::Field(std::string_view key, const std::string& value) : Field(key, value, false) {}
Field::Field(std::string_view key, bool value) : Field(key, value ? "true" : "false", true) {}
Field::Field(std::string_view key, int value) : Field(key, std::to_string(value), true) {}
Field::Field(std::string_view key, long long value) : Field(key, std::to_string(value), true) {}
Field::Field(std::string_view key, unsigned long long value)
    : Field(key, std::to_string(value), true) {}

Field::Field(std::string_view key, double value) : Field(key, std::string(), true) {
    // printf would write "0,5" under a comma-decimal locale, and "nan" for a
    // NaN — either makes the host reject the line.
    if (!std::isfinite(value)) {
        value_ = std::isnan(value) ? "\"NaN\"" : (value > 0 ? "\"+Inf\"" : "\"-Inf\"");
        return;
    }

    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << value;
    value_ = out.str();
}

std::string_view LevelName(Level level) {
    switch (level) {
    case Level::Trace: return "trace";
    case Level::Debug: return "debug";
    case Level::Info: return "info";
    case Level::Warn: return "warn";
    case Level::Error: return "error";
    }
    return "info";
}

std::string FormatTimestamp(std::chrono::system_clock::time_point tp) {
    using namespace std::chrono;

    // floor, not a cast: a cast truncates towards zero, handing back a negative
    // remainder for a clock still set before the epoch.
    const auto secs = floor<seconds>(tp);
    const auto micros = duration_cast<microseconds>(tp - secs).count();

    const std::time_t t = system_clock::to_time_t(secs);
    std::tm tm{};
    if (localtime_r(&t, &tm) == nullptr) {
        gmtime_r(&t, &tm);
    }

    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", &tm);

    char frac[16];
    std::snprintf(frac, sizeof frac, ".%06lld", static_cast<long long>(micros));

    std::string out = stamp;
    out += frac;

    // "Z" or "+hh:mm" — strftime's %z writes "+0200", which the host rejects.
    const long offset = tm.tm_gmtoff;
    if (offset == 0) {
        out += 'Z';
    } else {
        const long abs_offset = offset < 0 ? -offset : offset;
        const int hours = static_cast<int>((abs_offset / 3600) % 100);
        const int minutes = static_cast<int>((abs_offset % 3600) / 60);
        char zone[8];
        std::snprintf(zone, sizeof zone, "%c%02d:%02d", offset < 0 ? '-' : '+', hours, minutes);
        out += zone;
    }
    return out;
}

std::string Encode(const Record& record) {
    std::string out;
    out.reserve(128 + record.message.size());

    out += "{\"@level\":\"";
    out += LevelName(record.level);
    out += "\",\"@message\":\"";
    AppendEscaped(out, record.message);
    out += "\",\"@timestamp\":\"";
    out += FormatTimestamp(record.timestamp);
    out += '"';

    for (std::size_t i = 0; i < record.field_count; ++i) {
        const Field& field = record.fields[i];
        out += ",\"";
        AppendEscaped(out, field.key());
        out += "\":";
        if (field.literal()) {
            out += field.value();
        } else {
            out += '"';
            AppendEscaped(out, field.value());
            out += '"';
        }
    }

    out += '}';
    return out;
}

void SetSink(Sink sink) {
    std::lock_guard<std::mutex> lock(SinkMutex());
    CurrentSink() = std::move(sink);
}

void SetLevel(Level min) { g_level.store(static_cast<int>(min), std::memory_order_relaxed); }

Level GetLevel() { return static_cast<Level>(g_level.load(std::memory_order_relaxed)); }

bool Enabled(Level level) { return static_cast<int>(level) >= g_level.load(std::memory_order_relaxed); }

void Submit(const Record& record) {
    Sink sink;
    {
        std::lock_guard<std::mutex> lock(SinkMutex());
        sink = CurrentSink();
    }
    if (sink) {
        sink(record);
    } else {
        WriteToStderr(record);
    }
}

void Write(Level level, std::string_view message, std::initializer_list<Field> fields) {
    if (!Enabled(level)) return;

    Record record;
    record.level = level;
    record.message = message;
    record.fields = fields.begin();
    record.field_count = fields.size();
    record.timestamp = std::chrono::system_clock::now();
    Submit(record);
}

}  // namespace go_plugin::log
