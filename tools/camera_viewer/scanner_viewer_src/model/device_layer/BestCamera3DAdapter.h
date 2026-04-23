/**
 * @file BestCamera3DAdapter.h
 * @brief best_project camera_sdk（BestCamera3D + Hub gRPC）适配器，实现 IDeviceAdapter。
 */
#ifndef CAMERA_VIEWER_BEST_CAMERA3D_ADAPTER_H
#define CAMERA_VIEWER_BEST_CAMERA3D_ADAPTER_H

#include "IDeviceAdapter.h"
#include "DeviceInfo.h"

#include <ipc_shmem/shm_ring_buffer.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace camera3d::best {
class BestCamera3D;
}

namespace scanner_viewer {

class BestCamera3DAdapter final : public IDeviceAdapter {
 public:
  static constexpr const char* kDeviceModelTag = "Best-CameraHub";

  BestCamera3DAdapter();
  ~BestCamera3DAdapter() override;

  static std::vector<DeviceInfo> EnumerateDevices(int timeout_ms = 2500);

  bool Connect(const DeviceInfo* device_info, unsigned int timeout_ms) override;
  void Disconnect() override;
  bool IsConnected() const override;
  bool GetDeviceInfo(DeviceInfo& out) const override;

  bool GetParameterList(std::vector<ParamMeta>& out) const override;
  bool GetParameter(int id, std::vector<int>& values) const override;
  bool SetParameter(int id, const std::vector<int>& values) override;

  bool CaptureSync(UnifiedFrame& frame) override;
  bool StartAsyncCapture(FrameCallback callback) override;
  void StopAsyncCapture() override;
  bool IsAsyncCapturing() const override;

  void SetCaptureProjectorOp(std::uint32_t op) override;
  std::uint32_t CaptureProjectorOp() const override;

 private:
  void AsyncThreadMain();

  mutable std::mutex mutex_;
  std::uint32_t capture_projector_op_{0};
  std::unique_ptr<camera3d::best::BestCamera3D> camera_;
  DeviceInfo connected_{};
  bool have_connected_info_{false};

  std::unique_ptr<camera3d::ipc::ShmRingBuffer> shm_ring_;
  std::string shm_region_cached_;

  FrameCallback async_cb_;
  std::atomic<bool> async_stop_{true};
  std::thread async_thread_;
};

}  // namespace scanner_viewer

#endif
