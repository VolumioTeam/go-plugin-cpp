#pragma once

#include <chrono>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>

namespace go_plugin::log {

/**
 * The only five severities go-plugin understands. Anything else leaves the host
 * unable to tell the level and it files the line at its own.
 */
enum class Level { Trace, Debug, Info, Warn, Error };

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

    /** Whether the value is written as a JSON literal rather than a string. */
    bool literal() const { return literal_; }

private:
    Field(std::string_view key, std::string value, bool literal);

    std::string key_;
    std::string value_;
    bool literal_ = false;
};

struct Record {
    Level level = Level::Info;
    std::string_view message;
    const Field* fields = nullptr;
    std::size_t field_count = 0;
    std::chrono::system_clock::time_point timestamp;
};

/** Receives every record. The default encodes it and writes it to stderr. */
using Sink = std::function<void(const Record&)>;

/** Pass nullptr to restore the default. */
void SetSink(Sink sink);

/** Governs Write only, not Submit. Info by default. */
void SetLevel(Level min);
Level GetLevel();
bool Enabled(Level level);

void Write(Level level, std::string_view message, std::initializer_list<Field> fields = {});

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
 * The seam a backend adapter sits on, so a library that already knows a line's
 * time, severity and origin does not lose them to a second timestamp.
 *
 * SetLevel is deliberately not applied: the record comes from a library that
 * has already decided to emit it, and dropping it again here would lose what a
 * plugin meant to say.
 */
void Submit(const Record& record);

/** Renders a record in the host's format, without the trailing newline. */
std::string Encode(const Record& record);

/**
 * Exactly six fractional digits, and an offset written either as "Z" or with a
 * colon. A timestamp in any other shape makes the host reject the whole line
 * and report it as unparsed text at its own level.
 */
std::string FormatTimestamp(std::chrono::system_clock::time_point tp);

std::string_view LevelName(Level level);

}  // namespace go_plugin::log
