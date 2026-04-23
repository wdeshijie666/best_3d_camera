#pragma once

// spdlog 封装：InitLogging 注册默认 logger；CAMERA3D_LOG* 写入文件/控制台（见 diag_config）。

#include "platform_diag/diag_config.h"

#include <memory>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

namespace camera3d::diag {

// 初始化 spdlog：滚动文件 + 可选控制台；应在 crash handler 之前调用
void InitLogging(const DiagConfig& cfg, std::string_view process_tag);

// 获取已注册 logger；未初始化时返回 nullptr
std::shared_ptr<spdlog::logger> DefaultLogger();

}  // namespace camera3d::diag

#define CAMERA3D_LOGI(...)                                 \
  do {                                                     \
    if (auto _lg = ::camera3d::diag::DefaultLogger())      \
      _lg->info(__VA_ARGS__);                              \
  } while (0)

#define CAMERA3D_LOGW(...)                                 \
  do {                                                     \
    if (auto _lg = ::camera3d::diag::DefaultLogger())      \
      _lg->warn(__VA_ARGS__);                             \
  } while (0)

#define CAMERA3D_LOGE(...)                                 \
  do {                                                     \
    if (auto _lg = ::camera3d::diag::DefaultLogger())      \
      _lg->error(__VA_ARGS__);                           \
  } while (0)
