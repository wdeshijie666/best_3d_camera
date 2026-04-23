/**
 * @file Logger.h
 * @brief 日志模块对外接口：基于 spdlog，支持级别控制，低耦合可复用
 * 使用方仅依赖本头文件，不直接依赖 spdlog
 */
#ifndef SCANNER_VIEWER_COMMON_LOG_LOGGER_H
#define SCANNER_VIEWER_COMMON_LOG_LOGGER_H

#include <string>

namespace scanner_viewer {
namespace log {

/** 日志级别：级别越高越重要，设置某级别后仅该级别及以上会输出 */
enum class Level {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Err   = 4,
    Off   = 5   ///< 关闭日志输出
};

/**
 * 初始化默认 logger（控制台 + 可选文件）。
 * 若已初始化则仅更新 sink，可多次调用。
 * @param log_file 若非空则同时写入该文件；空则仅控制台
 */
void Init(const std::string& log_file = std::string());

/** 设置全局日志级别，低于此级别的日志将不输出 */
void SetLevel(Level level);

/** 获取当前全局日志级别 */
Level GetLevel();

/** 是否应输出该级别（用于先判断再格式化，避免无效开销） */
bool ShouldLog(Level level);

// --- 格式化写日志（支持 fmt 风格，如 "hello {}"）---

void Trace(const char* fmt, ...);
void Debug(const char* fmt, ...);
void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);

}  // namespace log
}  // namespace scanner_viewer

// 便捷宏：仅当级别开启时才求值（避免无用的格式化）
#define LOG_TRACE(...)  do { if (scanner_viewer::log::ShouldLog(scanner_viewer::log::Level::Trace)) scanner_viewer::log::Trace(__VA_ARGS__); } while(0)
#define LOG_DEBUG(...)  do { if (scanner_viewer::log::ShouldLog(scanner_viewer::log::Level::Debug)) scanner_viewer::log::Debug(__VA_ARGS__); } while(0)
#define LOG_INFO(...)   do { if (scanner_viewer::log::ShouldLog(scanner_viewer::log::Level::Info))  scanner_viewer::log::Info(__VA_ARGS__); } while(0)
#define LOG_WARN(...)   do { if (scanner_viewer::log::ShouldLog(scanner_viewer::log::Level::Warn))  scanner_viewer::log::Warn(__VA_ARGS__); } while(0)
#define LOG_ERROR(...)  do { if (scanner_viewer::log::ShouldLog(scanner_viewer::log::Level::Err))   scanner_viewer::log::Error(__VA_ARGS__); } while(0)

#endif  // SCANNER_VIEWER_COMMON_LOG_LOGGER_H
