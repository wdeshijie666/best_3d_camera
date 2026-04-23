# 示例：使用固定路径 `D:/libs/spdlog` 配置 CMake（日志 + `crash_handler`）

**仓库内已带可直接构建的示例目录**：`platform_diag/examples/minimal_app/`（含 `CMakeLists.txt` 与 `main.cpp`），在**该目录**下执行：

```text
cmake -S . -B build -DMY_SPDLOG_ROOT=D:/libs/spdlog
cmake --build build
```

生成可执行文件 **`minimal_app`**（Windows 在 `build/Release` 或 `build/Debug` 下，视生成器而定）。

下面补充**可粘贴**的说明性片段，便于他组在**自有**工程里接入。路径请按实机修改（正斜杠推荐）。

---

## 1. 两种 spdlog 布局（二选一）

| 情况 | 典型目录 | CMake 方式 |
|------|----------|------------|
| **A. 已 cmake install** | `D:/libs/spdlog/lib/cmake/spdlog/spdlogConfig.cmake` 等 | `find_package(spdlog CONFIG)` |
| **B. 仅头文件树** | 存在 `D:/libs/spdlog/include/spdlog/spdlog.h` | 自建 `INTERFACE` 目标并 **alias 成** `spdlog::spdlog_header_only`（与 `platform_diag/CMakeLists.txt` 要求一致） |

> `platform_diag` 的 `CMakeLists.txt` 会查找 **`spdlog::spdlog`** 或 **`spdlog::spdlog_header_only`**，且 **`add_library(platform_diag ...)` 之前** 必须已存在其一。

---

## 2. 顶层 `CMakeLists.txt` 完整示例

假设工程目录为：

```text
my_app/
  CMakeLists.txt
  main.cpp
  third_party/
    platform_diag/    # 拷贝自本仓 libs/platform_diag（整目录）
```

**`my_app/CMakeLists.txt`：**

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------- spdlog：优先 CONFIG，失败则用固定头路径 ----------
set(MY_SPDLOG_ROOT "D:/libs/spdlog")

# 将 install 前缀或 cmake 包目录加入搜索路径（A 类）
list(PREPEND CMAKE_PREFIX_PATH "${MY_SPDLOG_ROOT}")
list(PREPEND CMAKE_PREFIX_PATH "${MY_SPDLOG_ROOT}/install")
list(PREPEND CMAKE_PREFIX_PATH "${MY_SPDLOG_ROOT}/out/install")

find_package(spdlog CONFIG QUIET)

if(spdlog_FOUND)
  if(NOT (TARGET spdlog::spdlog OR TARGET spdlog::spdlog_header_only))
    find_package(spdlog CONFIG REQUIRED)
  endif()
else()
  # B 类：仅头文件
  if(EXISTS "${MY_SPDLOG_ROOT}/include/spdlog/spdlog.h")
    add_library(_my_spdlog_headers INTERFACE)
    target_include_directories(_my_spdlog_headers SYSTEM INTERFACE "${MY_SPDLOG_ROOT}/include")
    add_library(spdlog::spdlog_header_only ALIAS _my_spdlog_headers)
    message(STATUS "spdlog: 使用头文件路径 ${MY_SPDLOG_ROOT}/include")
  else()
    message(FATAL_ERROR
      "未找到 spdlog。请把 spdlog 放到 ${MY_SPDLOG_ROOT}，或提供带 spdlogConfig.cmake 的 install 树，并检查 CMAKE_PREFIX_PATH。")
  endif()
endif()

find_package(Threads REQUIRED)

# ---------- platform_diag：必须在 spdlog 目标已存在之后 ----------
add_subdirectory(third_party/platform_diag)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE platform_diag)
```

- **仅 Windows 上** `platform_diag` 会自动为静态库拉 **dbghelp**；你的 **exe** 若用 MSVC 链 **静态链接** `platform_diag`，一般已带上 MiniDump 所需依赖。
- 若把 `EXAMPLE_*.md` 里的 `MY_SPDLOG_ROOT` 改成环境变量，可用：  
  `set(MY_SPDLOG_ROOT "$ENV{MY_SPDLOG_ROOT}" CACHE PATH "spdlog root")`。

---

## 3. 最小 `main.cpp`（日志 + 崩溃转储顺序）

**`my_app/main.cpp`：**

```cpp
#include "platform_diag/diag_config.h"
#include "platform_diag/logging.h"
#include "platform_diag/crash_handler.h"

int main() {
  camera3d::diag::DiagConfig cfg;
  cfg.log_dir = "logs";
  cfg.log_file_stem = "my_app";
  cfg.crash_dump_dir = "logs/crash";
  cfg.console_sink = true;
  cfg.log_level = "info";

  // 1) 先初始化日志
  camera3d::diag::InitLogging(cfg, "my_app");
  // 2) 再装崩溃处理（Windows 下写 MiniDump 到 cfg.crash_dump_dir）
  camera3d::diag::InstallCrashHandlers(cfg.crash_dump_dir, "my_app");

  CAMERA3D_LOGI("hello, spdlog + crash_handler");

  return 0;
}
```

编译运行后，日志一般在 `logs/my_app.log`（或滚动文件，见 `logging.cpp` 实现），转储在 **`logs/crash/my_app_crash_<时间戳>.dmp`（仅 Windows）**。

---

## 4. 若 `platform_diag` 与 `my_app` 平级（不用 `third_party`）

把 `add_subdirectory(third_party/platform_diag)` 改成实际相对路径，例如：

```cmake
add_subdirectory(${CMAKE_SOURCE_DIR}/../platform_diag ${CMAKE_BINARY_DIR}/platform_diag_build)
```

保证在 **`add_subdirectory` 前** 已完成上文的 spdlog 目标创建。

---

## 5. 与当前仓库根 `CMakeLists.txt` 的对应关系

本仓库在根目录对 spdlog 做了**更完整**的候选路径搜索与 `spdlog_FOUND` 判断；上例是**最小**可运行版本。若与父工程同仓构建，直接 `add_subdirectory(libs/platform_diag)` 并继承父工程已配置好的 `spdlog` 即可。

---

## 6. 依赖小结（避免误解）

- **必须**：**spdlog**（`find_package` 或头文件 `INTERFACE`）、**Threads**（`platform_diag` 已 `PUBLIC` 链到可执行文件）。
- **Windows 崩溃转储**：**dbghelp** 由 `platform_diag` 静态库**私有**链接，一般无需在 `main` 里再写 `target_link_libraries(... dbghelp)`，除非你单独拆库。
- **不**需要：Google **Breakpad**（见 `README_CRASH_HANDLER.md`）。

---

**相关文档**：`README_LOG_MODULE.md`、`README_CRASH_HANDLER.md`
