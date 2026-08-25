#pragma once

#include <functional>
#include <string>

#if defined(__GNUC__) || defined(__clang__)
#define SLOPENGINE_LOG_PRINTF(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#else
#define SLOPENGINE_LOG_PRINTF(fmt_index, first_arg)
#endif

namespace slopengine {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};

enum class LogCategory {
    General,
    Audio,
    Map,
    Script,
    Render,
    Ui,
    Physics,
    Compile,
};

struct LogEntry {
    LogLevel level;
    LogCategory category;
    std::string text;
};

using LogSink = std::function<void(const LogEntry&)>;

namespace Log {

void init(LogLevel minLevel);

const char* levelLabel(LogLevel level);

void addDefaultConsoleSink();
void addSink(LogSink sink);
void addDeferredSink(LogSink sink);
void pump();

void setCategoryEnabled(LogCategory category, bool enabled);
bool categoryEnabled(LogCategory category);

void log(LogLevel level, LogCategory category, const char* fmt, ...) SLOPENGINE_LOG_PRINTF(3, 4);
void trace(LogCategory category, const char* fmt, ...) SLOPENGINE_LOG_PRINTF(2, 3);
void debug(LogCategory category, const char* fmt, ...) SLOPENGINE_LOG_PRINTF(2, 3);
void info(LogCategory category, const char* fmt, ...) SLOPENGINE_LOG_PRINTF(2, 3);
void warning(LogCategory category, const char* fmt, ...) SLOPENGINE_LOG_PRINTF(2, 3);
void error(LogCategory category, const char* fmt, ...) SLOPENGINE_LOG_PRINTF(2, 3);
void fatal(LogCategory category, const char* fmt, ...) SLOPENGINE_LOG_PRINTF(2, 3);

}

}

#undef SLOPENGINE_LOG_PRINTF
