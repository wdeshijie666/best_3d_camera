# 崩溃与未处理异常处理（`crash_handler`）说明

本文件说明 **`platform_diag`** 中 **`crash_handler.h` / `crash_handler.cpp`** 的职责、调用顺序、平台差异与转储位置，供其他组与日志模块一并接入时参考。

---

## 1. 功能概览

| 项目 | 说明 |
|------|------|
| **头文件** | `include/platform_diag/crash_handler.h` |
| **实现** | `src/crash_handler.cpp` |
| **入口** | `void InstallCrashHandlers(const std::string& dump_dir_utf8, std::string_view process_name);` |
| **依赖** | 需**先**完成 `InitLogging`（见 `README_LOG_MODULE.md`），因 SEH/terminate 路径会调用 `DefaultLogger()` 与 `CAMERA3D_LOGE` 等。 |
| **平台** | **Windows**：未处理 SEH → 写 **MiniDump（.dmp）**；**非 Windows**：仅安装 `std::set_terminate` 的析构路径，无 MiniDump 与 `SetUnhandledExceptionFilter`（见源码 `#ifdef _WIN32`）。 |

---

## 2. 调用顺序（必须遵守）

1. 配置并调用 **`InitLogging(DiagConfig, process_tag)`**（保证 `cfg.crash_dump_dir` 与传入 `InstallCrashHandlers` 的目录一致或为其子目录，由调用方约定）。  
2. 调用 **`InstallCrashHandlers(dump_dir_utf8, process_name)`**。  
   - `dump_dir_utf8`：转储目录，UTF-8 字符串；实现内会 `create_directories`。  
   - `process_name`：用于生成转储文件名前缀。  
3. 正常运行业务逻辑。

若**未**初始化日志，`DefaultLogger()` 为 `nullptr` 时，SEH 分支中写日志会跳过，但 **MiniDump 仍会尝试写入**（在 Windows 上）。

---

## 3. Windows 行为细节

- 使用 **`SetUnhandledExceptionFilter`** 安装 **`UnhandledExceptionFilterImpl`**。  
- 发生未处理 SEH 时：  
  - 向默认 logger 打 **error** 一条（含异常 code 的十六进制）。  
  - 在 `dump_dir` 下生成：  
    `{process_name}_crash_{时间戳}.dmp`  
  - 转储类型包含：`MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules`。  
- 链接：目标需链接 **dbghelp**；本仓 `platform_diag` 的 `CMakeLists.txt` 在 `WIN32` 上已 `target_link_libraries(... PRIVATE dbghelp)`。

---

## 4. 未处理 C++ 异常与 `std::terminate`

- 通过 **`std::set_terminate(OnTerminate)`** 安装处理器。  
- 在 `OnTerminate` 中尝试 `std::current_exception()` 并打日志后 **`std::abort()`**。  
- 该路径**不**走 Windows SEH MiniDump（除非异常再触发 SEH）；与纯 `throw` 未捕获语义一致。

---

## 5. 与 `DiagConfig` 的关系

- `diag_config.h` 中 `crash_dump_dir` 为**配置字段**，用于各进程 `main` 统一填参。  
- **`InstallCrashHandlers` 不会自动读 `DiagConfig`**，需应用层传入与配置一致的目录，例如：  

  `InstallCrashHandlers(cfg.crash_dump_dir, "my_service");`

---

## 6. 与 Breakpad 的关系

- 本实现**不依赖** Google Breakpad；根工程若定义 `CAMERA3D_HAVE_BREAKPAD` 等，为**其他目标/宏**使用，**`crash_handler.cpp` 内无 Breakpad 代码**。  
- 若他组已集成 Breakpad，需自行避免与 `SetUnhandledExceptionFilter` 冲突（通常只选一种完整转储方案）。

---

## 7. 集成清单中的文件

与 **`LOG_MODULE_FILE_LIST.txt`** 中以下条目对应：

- `include/platform_diag/crash_handler.h`  
- `src/crash_handler.cpp`  

与 **`README_LOG_MODULE.md`** 中日志、线程、**dbghelp** 等依赖说明一并阅读即可。

---

## 8. 排障简表

| 现象 | 可能原因 |
|------|----------|
| 无 .dmp 生成 | 非 Windows；或目录无写权限；`CreateFileW` 失败（路径非法）。 |
| 有 dmp 但日志无 “未处理 SEH” | `InitLogging` 未成功，`DefaultLogger()` 为空，仅影响日志行。 |
| 链接错误 dbghelp | 未对目标链接 `dbghelp.lib` / 未在 CMake 中传递 Windows 系统库。 |

---

**相关文档**：`README_LOG_MODULE.md`（含整体 `platform_diag` 与打包路径）。
