#pragma once

// 日志与异步队列参数；各服务 main 在 InitLogging 前填充本结构。

#include <string>

namespace camera3d::diag {

// 诊断子系统统一配置；InitLogging 基于 spdlog（异步 + 滚动文件，见 logging.cpp）。
struct DiagConfig {
  std::string log_dir = "logs";
  std::string log_file_stem = "camera3d";
  std::string crash_dump_dir = "logs/crash";
  bool console_sink = true;
  // 异步队列满时的策略：true 丢弃最旧；false 阻塞（可能影响实时采集线程，慎用）
  bool async_discard_oldest = true;
  size_t async_queue_size = 8192;
  // 用户版可默认 warn；开发者版可设 debug
  std::string log_level = "info";
};

}  // namespace camera3d::diag
