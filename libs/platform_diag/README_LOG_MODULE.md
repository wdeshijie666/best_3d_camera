# 日志与诊断子系统（`platform_diag`）使用说明

本文档供**其他组**将本仓中的日志模块**单独接入**到自有工程时参考。对应源码位于仓库 **`libs/platform_diag/`**。

> **说明**：本说明与清单文件为后续移交补充；不修改原业务代码。集成时以本文档与 `LOG_MODULE_FILE_LIST.txt` 为准即可。

**崩溃与 MiniDump（`crash_handler`）**：见同目录 **[`README_CRASH_HANDLER.md`](README_CRASH_HANDLER.md)**。

**固定路径 spdlog 的 CMake + `main.cpp` 示例**（如 `D:/libs/spdlog`）：**[`EXAMPLE_CMAKE_SPDLOG.md`](EXAMPLE_CMAKE_SPDLOG.md)**。

---

## 1. 功能概览

| 能力 | 说明 |
|------|------|
| 日志 | 基于 [spdlog](https://github.com/gabime/spdlog)，异步队列 + 滚动文件 + 可选彩色控制台。 |
| 配置 | `DiagConfig` 统一控制日志目录、文件名前缀、等级、异步队列等。 |
| 崩溃处理（可选） | `InstallCrashHandlers`：未捕获 C++ 异常、Windows 下 SEH 与 MiniDump（`dbghelp`），可与 Breakpad 宏配合（见下）。 |
| 版本展示（可选） | `build_info.h` 中 `kCamera3dStackName` / `kCamera3dStackVersion` 字符串。 |

**对外主要入口**

- `camera3d::diag::InitLogging(const DiagConfig&, std::string_view process_tag);`  
  应在进程内**尽早**调用，且**早于** `InstallCrashHandlers`（若使用崩溃处理）。
- `camera3d::diag::DefaultLogger();` 返回 `std::shared_ptr<spdlog::logger>`；未初始化时为 `nullptr`。
- 宏（需已 `InitLogging` 成功）：`CAMERA3D_LOGI(...)`、`CAMERA3D_LOGW(...)`、`CAMERA3D_LOGE(...)`。用法与 `spdlog::info/warn/error` 一致（支持 `fmt` 风格占位符，视 spdlog 编译方式而定）。

---

## 2. 需打包/拷贝的文件

见同目录下 **`LOG_MODULE_FILE_LIST.txt`** 逐文件列表。

**最简方式**：将 **`libs/platform_diag/`** 整个目录原样复制到目标工程（或 Git 子树/子模块仅包含该目录），并保留同目录的 **`PlatformDiagConfig.cmake.in`** 所在路径在目标工程 `cmake/` 中可引用（若使用与当前仓一致的 `install(EXPORT ...)` 流程）。

若**仅需要日志、不需要崩溃转储**：

- 可只链接 `logging.cpp` + `diag_config.cpp` 与对应头文件，并在自有 CMake 中不编译 `crash_handler.cpp`、不调用 `InstallCrashHandlers`。  
- 但当前本仓 **`CMakeLists.txt` 为静态库整体编进上述三个 .cpp**；若要裁剪，需他组在自有 CMake 中**自行拆分目标**（本说明不代改现仓工程）。

---

## 3. 依赖

| 依赖 | 用途 |
|------|------|
| **spdlog** | 必需。建议 `find_package(spdlog CONFIG)`，目标 `spdlog::spdlog` 或 `spdlog::spdlog_header_only` 二选一。 |
| **C++17** 及以上 | 与当前实现一致（`std::filesystem` 等），若降低标准需自改。 |
| **Threads** | CMake `Threads::Threads`（`logging` 中异步池等）。 |
| **Windows** | 崩溃转储与部分路径：`dbghelp`；`crash_handler.cpp` 内。 |
| **（可选）Breakpad** | 当定义 `CAMERA3D_HAVE_BREAKPAD=1` 并配置 `BREAKPAD_CLIENT_INCLUDE_DIR` 等时，走 Breakpad 相关（与根工程选项一致，他组可不自开）。 |

本仓库根 **`CMakeLists.txt`** 在找不到 spdlog 的 CONFIG 时，会尝试在 `THIRD_PARTY_LIBRARY_DIR` 下用**仅头文件**的 INTERFACE 目标替代；**他组可不受此约束**，直接安装官方 spdlog 并 `find_package` 即可。

---

## 4. 集成方式建议

### 4.1 在 CMake 中

与当前仓相同思路：

```cmake
add_subdirectory(third_party/platform_diag)  # 路径按实际放置
target_link_libraries(你的可执行或库 PRIVATE platform_diag)  # 若保留目标名
```

- 库目标名在源文件中为 `platform_diag`（**静态库**），`PUBLIC` 会传递 **spdlog** 与 **Threads**。
- 若使用安装导出，可参考 **`cmake/PlatformDiagConfig.cmake.in`** 生成 `PlatformDiagConfig.cmake`（`find_dependency` 与 `camera3d::` 命名空间见该模板）。

### 4.2 在代码中（最小顺序）

```cpp
#include "platform_diag/diag_config.h"
#include "platform_diag/logging.h"
#include "platform_diag/crash_handler.h"  // 若需崩溃转储

int main() {
  camera3d::diag::DiagConfig cfg;
  cfg.log_dir = "logs";
  cfg.log_file_stem = "myapp";
  cfg.log_level = "info";

  camera3d::diag::InitLogging(cfg, "myapp");  // 第二个参数为 tag，会出现在每行 pattern 中

  camera3d::diag::InstallCrashHandlers(cfg.crash_dump_dir, "myapp");

  CAMERA3D_LOGI("start");
  // ...
  return 0;
}
```

`DiagConfig` 各字段说明见头文件 `diag_config.h`（`async_discard_oldest`、`async_queue_size` 等）。

---

## 5. 与 SDK/版本字符串

若需展示与仓库一致的栈名称/版本，可包含 **`include/platform_diag/build_info.h`**。若他组有自有版本，可**仅引用接口而替换常量**于自有头文件，无需改本模块。

---

## 6. 行为与现仓一致时注意事项

- 日志**不会**在 `InitLogging` 中自动 flush 到磁盘以外的特殊逻辑；关进程前如需落盘，可按 spdlog 习惯 `spdlog::shutdown()`（若他组在全局析构中显式注册）。
- `InitLogging` 内若已创建 `g_logger`，**重复调用会直接返回**（不重置配置）。
- Windows 控制台输出中文时，实现中会对 stdout 做 UTF-8 尝试（见 `logging.cpp`）。

---

## 7. 打 zip 的实用命令（仅供参考）

在仓库根目录执行，按清单打包（PowerShell 示例需根据路径调整）：

```powershell
# 仅作示例：将 platform_diag 目录打成 zip
Compress-Archive -Path "libs\platform_diag" -DestinationPath "platform_diag_log_module.zip"
```

若只按清单中文件打精简包，可用 `LOG_MODULE_FILE_LIST.txt` 结合脚本逐文件加入压缩包；**更推荐**直接打包整个 **`libs/platform_diag`** 目录，以免遗漏头文件子目录结构。

---

## 8. 联系与变更

- 本模块随 `camera_3d_stack` 演进而更新；`build_info.h` 中版本号与仓库发布策略以工程为准。
- 若他组**不能**使用 `camera3d` 命名空间，需在**自有工程**中做薄封装或 fork 时批量改名（本说明不要求修改本仓库现网代码）。

---

**文件版本**：与 `LOG_MODULE_FILE_LIST.txt` 同目录维护。
