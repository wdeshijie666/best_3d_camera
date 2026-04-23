# Hub 与 Recon 服务交互说明（gRPC + 共享内存）

本文基于当前仓库实现做只读分析，目标是解释：

- `hub_service` 与 `recon_service` 如何交互
- 为什么采用 “gRPC 传控制面 + 共享内存传大数据面”
- 关键协议语法（Proto/gRPC）与关键共享内存语法（ring buffer 读写）

---

## 1. 总体架构：控制面与数据面分离

当前实现采用经典双通道：

- **控制面（Control Plane）**：走 gRPC，传小对象与流程控制信息  
  例如：region 名、slot index、seq、宽高、像素格式、状态码。
- **数据面（Data Plane）**：走共享内存（SHM），传图像 payload 大块数据  
  避免把大图像字节直接塞进 gRPC 消息，降低拷贝与序列化成本。

对应代码位置：

- Hub 主服务：`services/hub_service/hub_app_grpc.cpp`
- Hub -> Recon 调用桥：`services/hub_service/hub_shm_loopback_test.cpp`
- Recon 回环服务：`services/recon_service/recon_shm_loopback_grpc.cpp`
- Recon 轮询拉取 burst 落盘：`services/recon_service/recon_hub_burst_receiver.cpp`（非 stub 默认编译）
- Hub 协议：`proto/camera_hub.proto`
- Recon 测试协议：`proto/camera_recon_test.proto`
- SHM ring：`libs/ipc_shmem/include/ipc_shmem/shm_ring_buffer.h`、`libs/ipc_shmem/src/shm_ring_buffer.cpp`
- SHM 常量：`libs/ipc_shmem/include/ipc_shmem/shm_constants.h`

---

## 2. 角色与职责

## 2.1 Hub 服务（服务端 + 客户端双角色）

在 `services/hub_service/hub_app_grpc.cpp` 中，`CameraHubServiceImpl` 作为 gRPC 服务端，主要负责：

- `Connect`：建立会话、创建/打开 Hub 共享内存 ring（默认 `camera3d_hub_ring`）
- `Capture`：采集帧并写入 SHM；可选触发 recon 回环联调
- `GetDepth`：返回 `ShmFrameRef`（共享内存引用，不是直接返回大 payload）
- `TestSaveReconEcho`：供 recon 回调，用于验证 echo SHM 中的数据

在 `services/hub_service/hub_shm_loopback_test.cpp` 中，Hub 还扮演 gRPC 客户端：

- `TryInvokeReconShmLoopbackAfterPublish(...)`  
  调 recon 的 `ProcessHubRaw`，把 Hub ring 的槽位元数据发过去。

## 2.2 Recon 服务（服务端 + 客户端双角色）

在 `services/recon_service/recon_shm_loopback_grpc.cpp` 中：

- 作为 gRPC 服务端，实现 `ReconShmLoopbackTest.ProcessHubRaw`
- 在 `ProcessHubRaw` 内部又作为 gRPC 客户端，回调 Hub 的 `TestSaveReconEcho`
- `ProcessHubRawRequest` 含 **`repeated HubRawFrameRef hub_raw_frames`** 时：按每项 `hub_slot_index` / `hub_slot_seq` 从 Hub ring 读槽并落盘到同一批次目录；**echo 回环仍只回传第 0 项**载荷。未填 repeated 时走旧单槽字段（`hub_slot_index` 等）。

也就是说，当前链路是 “Hub 调 Recon + Recon 回调 Hub” 的 **双向 unary RPC**；burst 联调时 Hub 在 `hub_shm_loopback_test.cpp` 中一次性填入与 `CapturePublication.camera_raw_slots` 等长的 repeated。

与联调宏无关的默认路径：`recon_hub_burst_receiver.cpp` 在独立线程中对 Hub 执行 **`Connect` → 周期性 `GetDepth(client_capture_id=0)`**；当 `DepthReply` 出现新的 `client_capture_id` 且 **`camera_raw_frames` 非空**时，在本进程 **attach 同一 Hub ring**，按各 `ShmFrameRef` 将 payload 写入 **`recon_img_save/<时间戳>/`**（环境变量见仓库根 `README.md` §3 重建侧说明）。

---

## 3. 一次完整交互时序（开启 loopback）

以 `CaptureRequest.test_recon_shm_loopback=true` 为例：

1. 客户端调用 Hub：`CameraHub.Capture`
2. Hub 把 burst 各帧 payload 写入 `camera3d_hub_ring`，`CapturePublication.camera_raw_slots` 记录每槽 `slot + seq` 等
3. Hub 调 Recon：`ReconShmLoopbackTest.ProcessHubRaw`（请求中带 **`hub_raw_frames`** 全量描述）
4. Recon 打开 Hub ring，按 `hub_slot_index` 读取，校验 `seq_publish == hub_slot_seq`
5. Recon 将 payload 写入 echo ring：`camera3d_recon_loopback_echo`
6. Recon 回调 Hub：`CameraHub.TestSaveReconEcho`，携带 echo 的 `ShmFrameRef`
7. Hub 读取 echo ring 对应槽位，校验后保存/确认
8. Recon 向 Hub 返回 `ProcessHubRawReply.code=0`
9. Hub 向客户端返回 `CaptureReply` 成功

---

## 4. gRPC / Proto 语法讲解（结合本项目）

## 4.1 service 与 rpc

在 `.proto` 中：

- `service`：定义一组远程方法（类似服务接口）
- `rpc`：定义单个远程方法签名

本项目有两个服务：

- `camera_hub.proto`：`service CameraHub`
- `camera_recon_test.proto`：`service ReconShmLoopbackTest`

## 4.2 message

- `message`：请求/响应的数据结构
- 字段使用 `type name = tag;` 语法（tag 是 wire 编号，变更时要谨慎）

例如 Hub 的共享内存引用对象（简化理解）：

- `region_name`：共享内存区名
- `seq`：发布序号（用于确认同一槽位是否被新数据覆盖）
- `offset_bytes/size_bytes`：payload 在 SHM 内的位置与长度
- `width/height/pixel_format`：图像元信息
- `timestamp_unix_ns`：纳秒时间戳

## 4.3 unary / stream / repeated / oneof

当前仓库里这两份协议主要是 **unary RPC**（一问一答）：

- 没有发现 stream RPC（客户端流/服务端流/双向流）
- 多路原始图：`DepthReply.camera_raw_frames`（`repeated CameraRawFrameRef`）按配置文件 `cameras` 从左到右列出每路 `ShmFrameRef` 与 `camera_index` / `serial_number` / `ip` / `manager_device_id`，以及便于构造 `cv::Mat` 的 `channels` 与 `row_step_bytes`（紧密打包时的启发式值）。

所以核心模式是：**多个 unary 组合成时序链路**，而不是单个流式会话。

### 4.4 Hub 配置文件与多相机

- 默认读取 `config/hub_service.json`，可通过环境变量 `CAMERA3D_HUB_CONFIG` 覆盖路径。
- 示例见仓库 `config/hub_service.example.json`：`hub.listen`、`projector.com_index`（**必须 > 0**）、`cameras[].camera`（`ip`、`serial_number`、`backend`、`device_id`）。数组顺序即**从左到右**的 `camera_index`。
- **启动时**若配置文件缺失、解析失败或 `UnifiedStartupFromConfig` 任一步失败：**进程不退出**，`CAMERA3D_LOGE` 打日志（随 `platform_diag` 落盘并可在控制台查看），同时更新内部 `hub_runtime_`，SDK 通过 **`ConnectReply.status.code` / `message`** 与业务 RPC 的 `status` 获知状态。状态码数值与含义与 SDK 共用头文件 **`libs/camera_hub_api/include/camera3d/hub/hub_service_state_codes.h`**（含 `HubServiceStateDescribeZh` 简短中文说明）。成功路径下仍在监听 gRPC **之前**完成相机/串口/SHM/硬触发编排。
- `Disconnect` 仅停止编排并清空会话侧状态，**不销毁** SHM、串口与物理相机；后台线程约每 2 秒检测串口并尝试恢复。
- **仅 gRPC 监听端口绑定失败时进程返回非 0**；配置与初始化失败时 Hub 仍启动，由状态码表达不可用原因。

### 4.5 算法侧：从 `CameraRawFrameRef` 构造 `cv::Mat`

1. 使用 `frame.region_name` 等与单路 `ShmFrameRef` 相同的方式 attach Hub ring，按 `offset_bytes` / `size_bytes` 得到指向 payload 的指针（与现有单路读法一致）。
2. 令 `W = frame.width`，`H = frame.height`，`PF = frame.pixel_format`（与适配器约定一致）。
3. 若 `channels != 0` 且 `row_step_bytes != 0`，可将数据视为 `H × W`、元素类型为 `CV_8U`、通道数为 `channels` 的紧密或定步长布局：`cv::Mat(H, W, CV_8UC(channels), ptr, row_step_bytes)`（**不拷贝**，生命周期需保证在 SHM 映射有效期间内使用）。
4. 若 `channels == 0`，则应根据 `pixel_format` 与厂商文档自行解析像素布局；`size_bytes` 应等于 `height * step`，其中 `step` 不小于 `width * elemSize`。
5. 检测/重建占位管线仍只针对**最左相机（`camera_index == 0`）**的 raw 写 `intermediate_frame` / `final_frame`；`DepthReply.raw_frame` 与 `frame` 的兼容语义与单相机时一致（以第一路为准）。多路 recon 回环联调仍为 TODO。

---

## 5. 共享内存语法与实现语义（ring buffer）

## 5.1 命名与容量

在 `shm_constants.h`：

- Hub ring 名：`camera3d_hub_ring`（默认总容量与槽数见 `kDefaultHubRingTotalBytes` / `kDefaultHubRingSlotCount`，需覆盖「多路×burst + 可选 final」连续写入，避免槽轮转覆盖）
- Recon echo ring 名：`camera3d_recon_loopback_echo`（默认 `kReconLoopbackEchoRingTotalBytes`）

## 5.2 RingBuffer 结构

`ShmRingBuffer` 大体包含：

- `ShmGlobalHeader`：全局信息（magic/version/slot_count/stride）
- 多个 slot（Hub 中枢区创建时写入 `slot_count`，与 `kDefaultHubRingSlotCount` 一致）
- 每个 slot 前有 `ShmSlotHeader`，后面是 payload 区

`ShmSlotHeader` 关键字段：

- `seq_publish`：发布序号
- `payload_offset` / `payload_size`
- `width` / `height` / `pixel_format`
- `timestamp_unix_ns`

## 5.3 写入语法（生产者）

典型调用语义（Hub/Recon 都在用）：

- `TryWriteNextSlot(payload_ptr, payload_size, width, height, pixel_format, out_slot, out_seq)`

含义：

- 找到可写槽位
- 写 header + payload
- 返回本次发布的 `slot` 和 `seq`

## 5.4 读取语法（消费者）

典型调用语义：

- `TryReadSlot(slot_index, out_payload_ptr, out_payload_size, out_header)`

读取后会校验：

- `out_header.seq_publish` 是否等于 gRPC 带来的 seq  
  （防止读到“同槽位旧帧/新帧混淆”）

---

## 6. 为什么要 “gRPC + SHM” 混合

如果只用 gRPC 传图像：

- 大 payload 会有序列化/反序列化开销
- 进程间可能增加拷贝次数

如果只用 SHM：

- 缺少跨进程流程控制、状态码、超时、错误语义

混合方案优点：

- gRPC：流程编排、错误语义、版本化协议
- SHM：高吞吐大数据搬运

这就是当前仓库采用的控制面/数据面分离策略。

---

## 7. 关键失败处理与超时

已实现的保护点（代表性）：

- Hub -> Recon 调用设置 deadline（30s）
- Recon -> Hub 回调设置 deadline（30s）
- SHM 读取后校验 seq，避免读到错误代际数据
- `ProcessHubRaw` 使用业务 code 区分失败阶段（打开 SHM、读槽、写槽、回调失败等）

当前实现特征：

- 以“失败即返回”为主，未实现复杂重试/退避

---

## 8. 当前实现状态（很重要）

从实现看，Hub 不再单独对接检测服务：`with_detection_pipeline` 请求标志会被忽略（不占检测阶段 SHM）；`with_reconstruction_pipeline` 仍用于在 Hub SHM 上预留“最终帧”槽位占位。检测与重建合并到重建侧后，再由重建服务消费原始帧并回写结果。

因此你现在看到的 “Hub <-> Recon + SHM” 更多是：

- 交互与数据通路验证（loopback）
- 协议与共享内存机制验证

不是最终完整算法处理链路的终态实现。

---

## 9. 建议阅读顺序

1. `proto/camera_hub.proto`
2. `proto/camera_recon_test.proto`
3. `libs/ipc_shmem/include/ipc_shmem/shm_ring_buffer.h`
4. `services/hub_service/hub_app_grpc.cpp`
5. `services/hub_service/hub_shm_loopback_test.cpp`
6. `services/recon_service/recon_shm_loopback_grpc.cpp`
7. `tools/hub_connectivity_test/main.cpp`

这样能先理解协议，再理解数据结构，最后看时序落地。

