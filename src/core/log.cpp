#include "core/log.hpp"

#include <raylib.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

constexpr std::size_t kMaxFormattedLength = 1024;
constexpr std::size_t kMaxDeferredQueue = 500;

struct TagCategory {
    std::string_view prefix;
    LogCategory category;
};

constexpr TagCategory kTagTable[] = {
    {"AUDIO", LogCategory::Audio},
    {"MAP", LogCategory::Map},
    {"GRAPH", LogCategory::Map},
    {"THING", LogCategory::Map},
    {"THINGDEFS", LogCategory::Map},
    {"MAPHANDLERS", LogCategory::Map},
    {"ASSET", LogCategory::Map},
    {"SCRIPT", LogCategory::Script},
    {"CLI", LogCategory::Script},
    {"FP", LogCategory::Script},
    {"RENDER", LogCategory::Render},
    {"HUD", LogCategory::Render},
    {"SKY", LogCategory::Render},
    {"RIG", LogCategory::Render},
    {"SCOPE-DEBUG", LogCategory::Render},
    {"SCREENSHOT", LogCategory::Render},
    {"TITLE", LogCategory::Ui},
    {"SETTINGS", LogCategory::Ui},
    {"FONT", LogCategory::Ui},
    {"ACTIONS", LogCategory::Ui},
    {"PHYSICS", LogCategory::Physics},
    {"Jolt", LogCategory::Physics},
    {"BSP", LogCategory::Compile},
    {"sloprad", LogCategory::Compile},
    {"slopbsp", LogCategory::Compile},
    {"slopfac", LogCategory::Compile},
    {"slopvis", LogCategory::Compile},
    {"slopmap", LogCategory::Compile},
};

struct LogState {
    std::mutex mutex;
    std::unordered_set<LogCategory> disabledCategories;
    std::vector<LogSink> immediateSinks;
    std::vector<LogSink> deferredSinks;
    std::deque<LogEntry> deferredQueue;
    std::size_t droppedSinceLastPump = 0;
};

LogState& state() {
    static LogState instance;
    return instance;
}

LogLevel fromRaylibLevel(int level) {
    switch (level) {
        case LOG_TRACE: return LogLevel::Trace;
        case LOG_DEBUG: return LogLevel::Debug;
        case LOG_INFO: return LogLevel::Info;
        case LOG_WARNING: return LogLevel::Warning;
        case LOG_ERROR: return LogLevel::Error;
        case LOG_FATAL: return LogLevel::Fatal;
        default: return LogLevel::Info;
    }
}

int toRaylibLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return LOG_TRACE;
        case LogLevel::Debug: return LOG_DEBUG;
        case LogLevel::Info: return LOG_INFO;
        case LogLevel::Warning: return LOG_WARNING;
        case LogLevel::Error: return LOG_ERROR;
        case LogLevel::Fatal: return LOG_FATAL;
    }
    return LOG_INFO;
}

LogCategory categoryFromPrefix(std::string_view text) {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) {
        return LogCategory::General;
    }
    const std::string_view tag = text.substr(0, colon);
    for (const TagCategory& entry : kTagTable) {
        if (tag == entry.prefix) {
            return entry.category;
        }
    }
    return LogCategory::General;
}

const char* levelAnsiColor(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
        case LogLevel::Debug: return "\x1b[90m";
        case LogLevel::Info: return "\x1b[0m";
        case LogLevel::Warning: return "\x1b[33m";
        case LogLevel::Error:
        case LogLevel::Fatal: return "\x1b[31m";
    }
    return "\x1b[0m";
}

void emit(LogLevel level, LogCategory category, std::string text) {
    LogState& s = state();
    LogEntry entry{level, category, std::move(text)};

    std::vector<LogSink> immediateCopy;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.disabledCategories.count(category) != 0) {
            return;
        }
        immediateCopy = s.immediateSinks;
        if (!s.deferredSinks.empty()) {
            if (s.deferredQueue.size() >= kMaxDeferredQueue) {
                s.deferredQueue.pop_front();
                ++s.droppedSinceLastPump;
            }
            s.deferredQueue.push_back(entry);
        }
    }

    for (const LogSink& sink : immediateCopy) {
        sink(entry);
    }
}

void vlog(LogLevel level, LogCategory category, const char* fmt, va_list args) {
    std::array<char, kMaxFormattedLength> buffer{};
    std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    emit(level, category, std::string(buffer.data()));
}

void onRaylibTraceLog(int logLevel, const char* text, va_list args) {
    if (text == nullptr) {
        return;
    }
    std::array<char, kMaxFormattedLength> buffer{};
    std::vsnprintf(buffer.data(), buffer.size(), text, args);
    emit(fromRaylibLevel(logLevel), categoryFromPrefix(buffer.data()), std::string(buffer.data()));
}

}

void Log::init(LogLevel minLevel) {
    SetTraceLogLevel(toRaylibLevel(minLevel));
    SetTraceLogCallback(&onRaylibTraceLog);
}

const char* Log::levelLabel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "?";
}

void Log::addDefaultConsoleSink() {
    addSink([](const LogEntry& entry) {
        std::fprintf(
            stderr,
            "%s[%s] %s\x1b[0m\n",
            levelAnsiColor(entry.level),
            Log::levelLabel(entry.level),
            entry.text.c_str());
    });
}

void Log::addSink(LogSink sink) {
    LogState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.immediateSinks.push_back(std::move(sink));
}

void Log::addDeferredSink(LogSink sink) {
    LogState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.deferredSinks.push_back(std::move(sink));
}

void Log::pump() {
    LogState& s = state();
    std::vector<LogEntry> drained;
    std::vector<LogSink> sinksCopy;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.droppedSinceLastPump > 0) {
            drained.push_back(LogEntry{
                LogLevel::Warning,
                LogCategory::General,
                "LOG: dropped " + std::to_string(s.droppedSinceLastPump) + " message(s), deferred queue was full"});
            s.droppedSinceLastPump = 0;
        }
        drained.insert(drained.end(), s.deferredQueue.begin(), s.deferredQueue.end());
        s.deferredQueue.clear();
        sinksCopy = s.deferredSinks;
    }
    for (const LogEntry& entry : drained) {
        for (const LogSink& sink : sinksCopy) {
            sink(entry);
        }
    }
}

void Log::setCategoryEnabled(LogCategory category, bool enabled) {
    LogState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (enabled) {
        s.disabledCategories.erase(category);
    } else {
        s.disabledCategories.insert(category);
    }
}

bool Log::categoryEnabled(LogCategory category) {
    LogState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.disabledCategories.count(category) == 0;
}

void Log::log(LogLevel level, LogCategory category, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(level, category, fmt, args);
    va_end(args);
}

void Log::trace(LogCategory category, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(LogLevel::Trace, category, fmt, args);
    va_end(args);
}

void Log::debug(LogCategory category, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(LogLevel::Debug, category, fmt, args);
    va_end(args);
}

void Log::info(LogCategory category, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(LogLevel::Info, category, fmt, args);
    va_end(args);
}

void Log::warning(LogCategory category, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(LogLevel::Warning, category, fmt, args);
    va_end(args);
}

void Log::error(LogCategory category, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(LogLevel::Error, category, fmt, args);
    va_end(args);
}

void Log::fatal(LogCategory category, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(LogLevel::Fatal, category, fmt, args);
    va_end(args);
}

}
