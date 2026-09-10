#pragma once

#include <chrono>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>

namespace go_plugin::log {

/**
 * Level is the severity a host reads off a line. These are the only five
 * severities go-plugin understands; anything else leaves it unable to tell the
 * level and it files the line at its own.
 */
enum class Level { Trace, Debug, Info, Warn, Error };

/**
 * Field is one key/value pair carried by a line. Values are converted at the
 * call site, so a field costs a small string and nothing else.
 */
class Field {
public:
    Field(std::string_view key, std::string_view value);
    Field(std::string_view key, const char* value);
    Field(std::string_view key, const std::string& value);
    Field(std::string_view key, bool value);
    Field(std::string_view key, int value);
    Field(std::string_view key, long long value);
    Field(std::string_view key, unsigned long long value);
    Field(std::string_view key, double value);

    const std::string& key() const { return key_; }
    const std::string& value() const { return value_; }

    /** True when the value is to be written as a JSON literal rather than a string. */
    bool literal() const { return literal_; }

private:
    Field(std::string_view key, std::string value, bool literal);

    std::string key_;
    std::string value_;
    bool literal_ = false;
};

/**
 * Record is one line's worth of content, handed to a Sink already assembled.
 * A backend that wants to encode differently — a file, a test buffer, a second
 * transport — reads the record rather than parsing the encoded line back.
 */
struct Record {
    Level level = Level::Info;
    std::string_view message;
    const Field* fields = nullptr;
    std::size_t field_count = 0;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * Sink receives every record that passes the level filter. The default sink
 * encodes the record for the host and writes it to standard error in a single
 * write, so lines from different threads cannot interleave.
 */
using Sink = std::function<void(const Record&)>;

/** Replaces the sink. Pass nullptr to restore the default. */
void SetSink(Sink sink);

/** Records below this level are dropped before they are encoded. Info by default. */
void SetLevel(Level min);
Level GetLevel();
bool Enabled(Level level);

/** Writes one record. Cheap and thread-safe; a dropped level costs a comparison. */
void Write(Level level, std::string_view message, std::initializer_list<Field> fields = {});

/**
 * Submit hands an already-assembled record to the sink, level filter included.
 * This is the seam a backend adapter sits on: a logging library that already
 * knows the time, severity and origin of a line reports it through here rather
 * than losing them to a second timestamp.
 */
void Submit(const Record& record);

inline void Trace(std::string_view message, std::initializer_list<Field> fields = {}) {
    Write(Level::Trace, message, fields);
}
inline void Debug(std::string_view message, std::initializer_list<Field> fields = {}) {
    Write(Level::Debug, message, fields);
}
inline void Info(std::string_view message, std::initializer_list<Field> fields = {}) {
    Write(Level::Info, message, fields);
}
inline void Warn(std::string_view message, std::initializer_list<Field> fields = {}) {
    Write(Level::Warn, message, fields);
}
inline void Error(std::string_view message, std::initializer_list<Field> fields = {}) {
    Write(Level::Error, message, fields);
}

/**
 * Encode renders a record in the format the host parses, without the trailing
 * newline. Public so a backend can reuse the encoding, and so it can be tested
 * directly.
 */
std::string Encode(const Record& record);

/**
 * FormatTimestamp renders a time the way the host's parser demands: exactly six
 * fractional digits, and an offset written either as "Z" or with a colon. A
 * timestamp in any other shape makes the host reject the whole line and fall
 * back to reporting it as unparsed text at its own level, so this is a contract
 * and not a preference.
 */
std::string FormatTimestamp(std::chrono::system_clock::time_point tp);

/** The level's name as the host spells it. */
std::string_view LevelName(Level level);

}  // namespace go_plugin::log
