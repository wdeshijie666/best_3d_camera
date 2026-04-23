#include "platform_diag/logging.h"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <cctype>
#include <filesystem>
#include <mutex>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace camera3d::diag {
namespace {

std::mutex g_mu;
std::shared_ptr<spdlog::logger> g_logger;

#if defined(_WIN32)
// 工程以 UTF-8 源文件编译（/utf-8）；控制台默认常为 GBK，需设为 UTF-8 才能正确显示中文。
// 仅在 stdout 连接到控制台时设置；重定向到文件/管道时不改，避免无关副作用。
void TryEnableUtf8ConsoleForStdoutSink() {
  const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out == nullptr || out == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD mode = 0;
  if (!GetConsoleMode(out, &mode)) {
    return;
  }
  SetConsoleOutputCP(65001);
  SetConsoleCP(65001);
}
#endif

std::string LevelFromString(const std::string& s) {
  std::string out = s;
  for (auto& c : out) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  return out;
}

}  // namespace

// 实现 InitLogging：注册异步 spdlog、滚动文件与可选控制台（Windows 下尝试 UTF-8 控制台）。
void InitLogging(const DiagConfig& cfg, std::string_view process_tag) {
  std::scoped_lock lock(g_mu);
  if (g_logger) {
    return;
  }

  std::error_code ec;
  std::filesystem::create_directories(cfg.log_dir, ec);

  spdlog::init_thread_pool(cfg.async_queue_size, 1U);
  spdlog::set_level(spdlog::level::info);

  const std::string pattern = std::string("[%Y-%m-%d %H:%M:%S.%e] [") + std::string(process_tag) +
                              std::string("] [%^%l%$] [%t] %v");

  std::vector<spdlog::sink_ptr> sinks;
  const auto file_path =
      (std::filesystem::path(cfg.log_dir) / (cfg.log_file_stem + std::string(".log"))).string();
  constexpr std::size_t kMaxFileBytes = 16 * 1024 * 1024;
  constexpr std::size_t kMaxFiles = 8;
  sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(file_path, kMaxFileBytes, kMaxFiles));

  if (cfg.console_sink) {
#if defined(_WIN32)
    TryEnableUtf8ConsoleForStdoutSink();
#endif
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
  }

  g_logger = std::make_shared<spdlog::async_logger>(
      std::string(process_tag), sinks.begin(), sinks.end(), spdlog::thread_pool(),
      cfg.async_discard_oldest ? spdlog::async_overflow_policy::overrun_oldest
                               : spdlog::async_overflow_policy::block);
  g_logger->set_pattern(pattern);

  const auto lvl = LevelFromString(cfg.log_level);
  if (lvl == "trace")
    g_logger->set_level(spdlog::level::trace);
  else if (lvl == "debug")
    g_logger->set_level(spdlog::level::debug);
  else if (lvl == "warn" || lvl == "warning")
    g_logger->set_level(spdlog::level::warn);
  else if (lvl == "error")
    g_logger->set_level(spdlog::level::err);
  else if (lvl == "critical")
    g_logger->set_level(spdlog::level::critical);
  else
    g_logger->set_level(spdlog::level::info);

  spdlog::register_logger(g_logger);
  spdlog::set_default_logger(g_logger);
}

// 实现 DefaultLogger：返回全局 g_logger（可能为 nullptr）。
std::shared_ptr<spdlog::logger> DefaultLogger() {
  std::scoped_lock lock(g_mu);
  return g_logger;
}

}  // namespace camera3d::diag
