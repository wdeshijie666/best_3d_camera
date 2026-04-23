#pragma once

// 未捕获异常与可选 MiniDump；依赖 diag 日志目录存在。

#include <string>

namespace camera3d::diag {

// 安装未捕获异常与（Windows）结构化异常转储；应在日志初始化之后尽早调用
void InstallCrashHandlers(const std::string& dump_dir_utf8, std::string_view process_name);

}  // namespace camera3d::diag
