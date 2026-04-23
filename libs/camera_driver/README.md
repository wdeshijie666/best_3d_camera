# camera_driver：多适配器与 CMake 开关

本库提供 `CameraManager`、统一类型 `DeviceInfo` / `FrameBuffer`，以及按品牌划分的 `ICameraAdapter` 实现。Hub 等服务通过 `RegisterAdapter` 注册原型，再用 `EnumerateAll` / `CreateAndOpenDevice` 管理会话。

## DeviceInfo 与连接流程

- **`EnumerateAll()`**  
  返回 `std::vector<DeviceInfo>`：由各已注册适配器原型的 `EnumerateDevices()` 汇总。每个元素至少包含：
  - `backend_id`：逻辑品牌，如 `null`、`daheng`
  - `device_id`：传给该 backend `Open()` 的硬件键（如大恒序列号、`virtual0`）
  - 其余字段（型号、序列号、最大分辨率等）由适配器在枚举或打开后尽力填充

- **`CreateAndOpenDevice(const DeviceInfo& device, const std::string& manager_device_id = {})`**  
  使用 `device.backend_id` 查找已注册原型，`CloneForSession()` 后调用 `Open(device.device_id)`。  
  `manager_device_id` 为空时自动生成唯一 id（如 `cam-000001`）；非空则必须全局唯一，否则返回空串。

- **字符串地址（gRPC / 配置）**  
  仍可使用 `backend:device_key` 形式（如 `null:virtual0`、`daheng:SN123`），通过头文件 `camera_driver/device_info_io.h` 中的：
  - `ParseDeviceAddress` → `DeviceInfo`
  - `FormatDeviceAddress` ← `DeviceInfo`

## 适配器编译开关（CMake）

### `CAMERA3D_ENABLE_ADAPTER_DAHENG`（默认 `ON`）

- **`ON`**：编译大恒相关源码（`daheng_camera_select.cpp` + 真实 Galaxy 或桩），并在头文件 `adapters.h` 中导出 `CreateDaHengCameraAdapter()`；`hub_service` 会注册该适配器。
- **`OFF`**：**完全不编译**任何 `daheng_*.cpp`，不链接 Galaxy，不注册大恒适配器；适用于无大恒依赖的交付或交叉编译场景。

配置示例：

```bash
cmake -B build -DCAMERA3D_ENABLE_ADAPTER_DAHENG=OFF
```

### `CAMERA3D_WITH_DAHENG_GALAXY`（默认 `OFF`，仅当大恒开关为 `ON` 时由 `cmake/DaHengGalaxy.cmake` 探测）

- **`ON` 且探测成功**：编译 `daheng_camera_galaxy.cpp`，链接 `GxIAPICPPEx` 等，并定义宏 `CAMERA3D_WITH_DAHENG_GALAXY=1`。
- **否则**：编译 `daheng_camera_adapter_stub.cpp`（占位实现，`Open` 失败、`EnumerateDevices` 为空）。

详见 `cmake/DaHengGalaxy.cmake` 中的 `DAHENG_GALAXY_SDK_ROOT`、`DAHENG_GALAXY_LIB_DIR` 等变量说明。

### 大恒/Galaxy 运行时 DLL 拷贝策略

根目录与 `camera_driver` 在配置阶段写入缓存变量 **`CAMERA3D_SKIP_DAHENG_RUNTIME_DLLS`**（`0`/`1`）：

- 当 **未同时满足**：`CAMERA3D_ENABLE_ADAPTER_DAHENG` + `CAMERA3D_WITH_DAHENG_GALAXY` + `DAHENG_GALAXY_FOUND` 时，值为 **`1`**：`cmake/CopyThirdPartyRuntimeScript.cmake` 在从 `THIRD_PARTY_LIBRARY_DIR` 递归拷贝 DLL 时**跳过**常见大恒运行时文件名（如 `GxIAPI.dll`、`GxIAPICPP.dll` 等，可在脚本内按项目实际扩展）。
- 否则为 **`0`**：与其它第三方 DLL 一样正常拷贝。

这样在未启用真实大恒链路时，可避免把 Galaxy 运行时铺进输出目录，减少误依赖与体积。

## 后续新增其它品牌适配器（建议步骤）

1. 在本目录增加 `xxx_camera_adapter.cpp` / 头文件，实现 `ICameraAdapter`（`EnumerateDevices()` 返回 `std::vector<DeviceInfo>`，`BackendId()` 固定品牌前缀）。
2. 在 `CMakeLists.txt` 增加 `option(CAMERA3D_ENABLE_ADAPTER_XXX ...)`，用 `if()` 控制是否加入 `_camera_driver_sources`。
3. 在 `adapters.h` 中用 `#if CAMERA3D_ENABLE_ADAPTER_XXX` 包裹工厂函数声明。
4. 若该品牌有独立运行时 DLL，仿照 `CAMERA3D_SKIP_DAHENG_RUNTIME_DLLS` 增加缓存变量，并在 `CopyThirdPartyRuntimeScript.cmake` 中增加对应过滤规则。
5. 在 `hub_service/main.cpp`（或其它组装点）用 `#if CAMERA3D_ENABLE_ADAPTER_XXX` 调用 `RegisterAdapter`。

保持「**一个品牌一个 CMake 开关 + 可选运行时 DLL 过滤**」的模式，便于裁剪与 CI 矩阵。
