---
name: cmake-third-party-install
description: >-
  配置或修改本仓库 CMake 时：统一三方库路径变量 THIRD_PARTY_LIBRARY_DIR、运行时
  DLL 拷贝、以及静态库/动态库的 install 与 *Config.cmake 以便 find_package。适用
  于编辑根或子目录 CMakeLists.txt、cmake/*.cmake、新增库目标、接入新三方依赖、
  或用户提到规范化工程/安装/打包时。约定原文见根目录 cmake-config-skill.txt。
---

# CMake 工程约定（camera_3d_stack）

## 与 `cmake-config-skill.txt` 对齐的三条主线

### 1. 三方库根目录 `THIRD_PARTY_LIBRARY_DIR`

- **必须**用缓存变量 **`THIRD_PARTY_LIBRARY_DIR`** 作为全仓库三方依赖的统一根；路径由团队自定义，支持 `-DTHIRD_PARTY_LIBRARY_DIR=...` 或环境变量（与根 `CMakeLists.txt` 现有写法一致）。
- 通过 `CMAKE_PREFIX_PATH` / `XXX_ROOT` 等把具体包指到该根下的子目录，**避免**在子项目中写死分散的绝对路径。

### 2. 运行时动态库拷贝

- **目标**：可执行文件/被测目标所在输出目录在构建后即可加载到**真实链接进来**的运行时 DLL，便于本地运行与联调。
- **本仓库推荐**：使用根 CMake 已接入的 **`camera3d_register_post_build_copy_dlls(<target>)`**（见 `cmake/CopyThirdPartyRuntime.cmake` + `CopyResolvedRuntimeDlls.cmake`），按 **`$<TARGET_RUNTIME_DLLS:...>`** 解析依赖再拷贝，避免把整个 `THIRD_PARTY_LIBRARY_DIR` 无差别拷进输出目录。
- 为新 **可执行目标** 或需独立运行的 **gtest/工具** 注册同一宏，保持行为一致。

### 3. 库目标的 install 与 `*Config.cmake`

- 对**生成物为静态库或共享库**且需给其它工程复用的目标：
  - 配置 **`install(TARGETS ... EXPORT ...)`**、**`install(DIRECTORY ...)`** 头文件等。
  - 提供 **`<Package>Config.cmake`**（及版本文件如需），使消费方可用 **`find_package(<Package> CONFIG)`** 导入目标。
- **默认安装前缀**：优先 **`CMAKE_INSTALL_PREFIX` 指向构建目录下的 `install`**（或项目已约定的等价路径），安装树布局保持清晰：**`include/`**、**`bin/`**、**`lib/`**、**`cmake/`**（项目内已有 `CameraSDKConfig.cmake.in` 等可作模板）。
- 避免在 Config 里硬编码开发者本机路径；使用 **`@PACKAGE_INIT@`** / 相对 **`CMAKE_CURRENT_LIST_DIR`** 等惯用法。

## 工程质量约束（来自 txt）

- **避免**逻辑重复、高耦合、过度设计；脚本与函数职责单一，便于复用与扩展。
- 新增 CMake 模块放入 **`cmake/`**，根 `CMakeLists.txt` 保持「策略与全局变量」，细节下沉子目录 `CMakeLists.txt`。

## 与本仓库已有文件的关系

- 根 **`CMakeLists.txt`**：已体现 `THIRD_PARTY_LIBRARY_DIR` 与 gRPC/Boost 等前缀；修改时保持同一模式。
- **`cmake/`**：DLL 拷贝、可选 `*Config.cmake.in` 模板；新增能力优先加在此目录并在根或子 CMake 中 `include`。
