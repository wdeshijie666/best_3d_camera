# Scanner Viewer（3D 相机客户端）

Qt5 + VTK 8.2 的 MVC 客户端：设备抽象、数据中心统一帧、2D/3D 视图与海康 MV3D / AIever-A 系列（可选）采集。

## 构建要求

- CMake ≥ 3.16，C++17  
- **Qt5**：Widgets、Core、Gui、Concurrent（`CMAKE_PREFIX_PATH` 或环境变量 `QT_DIR`）

## CMake 选项与路径

- **相机（至少开启一项）**  
  - `SCANNER_VIEWER_ENABLE_HIK_MV3D`：海康 HiViewer MV3D（默认 **ON**）  
  - `SCANNER_VIEWER_ENABLE_A_SERIES`：AIR Scanner（默认 **OFF**）  
- **海康异步（仅海康开启时）**：`HIK_MV3D_ASYNC_USE_FETCH_LOOP`（默认 OFF，为 ON 时使用 `FetchFrame` 轮询线程）

**第三方根目录** `THIRD_PARTY_LIBRARY_DIR`（默认示例路径可在 CMake 中修改）下约定布局，也可对各子路径单独 `-D` 覆盖：

| 变量 | 默认相对路径 | 说明 |
|------|----------------|------|
| `VTK_ROOT` | `…/VTK` | 可自动推导 `VTK_DIR`（如 `lib/cmake/vtk-8.2`） |
| `SPDLOG_ROOT` | `…/spdlog` | 头文件 `include` |
| `ELA_WIDGET_TOOLS_ROOT` | `…/ElaWidgetTools` | **预编译包**，需含 `lib/cmake/ElaWidgetToolsConfig.cmake` |
| `HIK_MV3D_ROOT` | `…/HiViewer/Development` | 海康 SDK（`Includes`、`Libraries/win64`） |
| `AIR_SDK_DIR` | `…/AIeveR-A SDK` | 仅 A 系列开启时需要（含 `include/AIRScanner.hpp`） |

## 构建示例

```bash
cd scanner_viewer
mkdir build && cd build
cmake .. -DTHIRD_PARTY_LIBRARY_DIR=/path/to/3rdParty
cmake --build . --config Release
```

仅海康、关闭 A 系列时可省略 AIR；仅 A 系列时需 `-DSCANNER_VIEWER_ENABLE_A_SERIES=ON -DSCANNER_VIEWER_ENABLE_HIK_MV3D=OFF` 并正确设置 `AIR_SDK_DIR`。

## Windows 运行依赖

配置成功时，构建后脚本会将 Qt（windeployqt）、VTK、ElaWidgetTools（`bin` 下对应 Debug/Release DLL）、以及已启用相机相关的 SDK DLL 拷贝到可执行文件目录；若某 DLL 不存在则对应步骤可能跳过，需自行保证 PATH 或同目录部署。

## 源码结构

- **`app/`**：`MainWindow` 主界面与 Tab 逻辑  
- **`model/device_layer/`**：`IDeviceAdapter`、设备信息；按选项编译 `ASeriesAdapter` / `HikvisionAdapter`  
- **`model/data_center/`**：`UnifiedFrame`、`DataCenter`（采集结果汇聚）  
- **`view/viewer_core/`**：`ImageView2D`、`PointCloudView3D`、`RangeSlider`  
- **`view/param_ui/`**：`CameraParamPanel`  
- **`controller/`**：`DeviceController`（枚举/连接）、`AcquisitionController`（采集）  
- **`common/log/`**：基于 spdlog 的日志  

数据流：适配器 → `DataCenter` → UI 通过数据中心回调/查询更新 2D 与 3D 视图。
