---
name: hub-recon-grpc-shm
description: >-
  说明 hub / recon 服务、gRPC 与共享内存（SHM）之间的集成与调试要点。适用于修改
  hub_service、recon_service、hub_app_grpc、IPC/shm、proto 或阅读
  README_hub_recon_grpc_shm 相关文档时。
---

# Hub / Recon、gRPC 与 SHM

## 何时先读这个技能

- 用户提到 hub、recon、gRPC、共享内存、SHM、`hub_app_grpc`、相机流水线与 hub 的衔接。
- 正在编辑 `services/hub_service/`、`README_hub_recon_grpc_shm.md` 或相关 proto / CMake 目标。

## 建议动作

1. 若涉及整体架构或运行方式，先阅读仓库根目录的 `README_hub_recon_grpc_shm.md`。
2. 实现或排查时，结合 `services/hub_service/` 下 gRPC 入口（如 `hub_app_grpc.cpp`）与 `libs/ipc_shmem` 等现有抽象，保持与文档和现有错误处理风格一致。
3. 修改对外协议时，同步检查 proto 生成目标与调用方，避免只改一侧。

## 注意

- 以仓库内已有 CMake 目标与命名为准；不要引入与现有 `hub_service` 构建不一致的新依赖路径。
- Windows 下路径在文档与技能中统一使用正斜杠写法（如 `services/hub_service/`）。
