/**
 * @file Logger.cpp
 * @brief 日志模块实现：基于 spdlog，仅本文件依赖 spdlog
 */
#include "Logger.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#endif

namespace scanner_viewer {
namespace log {

namespace {

constexpr size_t kFormatBufferSize = 4096;

static spdlog::level::level_enum ToSpdlogLevel(Level lvl) {
    switch (lvl) {
        case Level::Trace: return spdlog::level::trace;
        case Level::Debug: return spdlog::level::debug;
        case Level::Info:  return spdlog::level::info;
        case Level::Warn:  return spdlog::level::warn;
        case Level::Err:   return spdlog::level::err;
        case Level::Off:   return spdlog::level::off;
        default:           return spdlog::level::info;
    }
}

static Level FromSpdlogLevel(spdlog::level::level_enum lvl) {
    switch (lvl) {
        case spdlog::level::trace: return Level::Trace;
        case spdlog::level::debug: return Level::Debug;
        case spdlog::level::info:  return Level::Info;
        case spdlog::level::warn:  return Level::Warn;
        case spdlog::level::err:   return Level::Err;
        case spdlog::level::off:   return Level::Off;
        default:                    return Level::Info;
    }
}

static void FormatV(char* buf, size_t buf_size, const char* fmt, std::va_list ap) {
    if (buf_size == 0) return;
    int n = std::vsnprintf(buf, buf_size, fmt, ap);
    if (n < 0 || static_cast<size_t>(n) >= buf_size)
        buf[buf_size - 1] = '\0';
}

}  // namespace

void Init(const std::string& log_file) {
#ifdef _WIN32
    // 源码为 UTF-8（与工程 /utf-8 一致）；默认控制台为系统 ANSI 会导致中文乱码。
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    if (!log_file.empty()) {
        try {
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true));
        } catch (...) {
            // 文件打开失败时仅控制台
        }
    }
    auto logger = std::make_shared<spdlog::logger>("scanner_viewer", sinks.begin(), sinks.end());
    logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
    logger->flush_on(spdlog::level::err);
    spdlog::set_default_logger(logger);
}

void SetLevel(Level level) {
    spdlog::set_level(ToSpdlogLevel(level));
}

Level GetLevel() {
    return FromSpdlogLevel(spdlog::get_level());
}

bool ShouldLog(Level level) {
    return spdlog::should_log(ToSpdlogLevel(level));
}

void Trace(const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    char buf[kFormatBufferSize];
    FormatV(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    spdlog::trace("{}", buf);
}

void Debug(const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    char buf[kFormatBufferSize];
    FormatV(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    spdlog::debug("{}", buf);
}

void Info(const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    char buf[kFormatBufferSize];
    FormatV(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    spdlog::info("{}", buf);
}

void Warn(const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    char buf[kFormatBufferSize];
    FormatV(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    spdlog::warn("{}", buf);
}

void Error(const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    char buf[kFormatBufferSize];
    FormatV(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    spdlog::error("{}", buf);
}

}  // namespace log
}  // namespace scanner_viewer
