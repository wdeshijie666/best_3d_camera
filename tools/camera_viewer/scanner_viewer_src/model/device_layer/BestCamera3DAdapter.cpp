#include "BestCamera3DAdapter.h"

#include "DeviceModelTags.h"

#include "../data_center/UnifiedFrame.h"

#include "common/log/Logger.h"
#include "common/unified_image_qt.h"

#include "camera_sdk/best_camera_3d.h"
#include "camera_sdk/best_types.h"

#include <ipc_shmem/shm_constants.h>

#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QStandardPaths>
#include <QString>

#include <chrono>
#include <cmath>
#include <cinttypes>

namespace scanner_viewer {

namespace {

constexpr int kParamExposure2d = static_cast<int>(camera3d::best::ParameterType::kExposure2d);
constexpr int kParamGain2d = static_cast<int>(camera3d::best::ParameterType::kGain2d);
constexpr int kParamGamma2d = static_cast<int>(camera3d::best::ParameterType::kGamma2d);

// 与 Hub Capture 真机路径一致：检测标志 Hub 侧忽略；开重建占位以便 GetDepth 走 final/兼容 frame；
// projector_op=0 时 Hub 使用 ProductionCommand::kWhiteScreenToEnd（见 hub_app_grpc）；联调宏保持关闭。
constexpr bool kCaptureWithDetectionPipeline = false;
constexpr bool kCaptureWithReconstructionPipeline = true;
constexpr bool kCaptureTestReconShmLoopback = false;
constexpr bool kCaptureTestInlineImageReply = false;

void LogBestCaptureFailure(const char* stage, camera3d::best::BestCamera3D* cam) {
  if (!cam) {
    return;
  }
  LOG_WARN("[viewer.capture] %s 失败 hub_status=%d sdk_err=%d msg=%s", stage, cam->LastHubStatusCode(),
           cam->LastErrorCode(), cam->LastErrorMessage().c_str());
}

bool ReadShmToUnifiedImage(camera3d::ipc::ShmRingBuffer* ring, const camera3d::best::BestShmFrameRef& ref,
                           UnifiedImage& out_img) {
  const std::uint8_t* payload = nullptr;
  std::size_t len = 0;
  if (!ring || !ring->TryReadMappedRange(ref.offset_bytes, ref.size_bytes, payload, len) || !payload) {
    return false;
  }
  out_img.width = static_cast<int>(ref.width);
  out_img.height = static_cast<int>(ref.height);
  out_img.channels = 1;
  const std::uint64_t wh = static_cast<std::uint64_t>(ref.width) * ref.height;
  out_img.is_16bit = ref.size_bytes >= wh * 2;
  out_img.data.assign(payload, payload + len);
  return true;
}

void SaveRawBurstVectorToDisk(const std::vector<UnifiedImage>& images, std::uint64_t capture_id,
                              const char* log_tag) {
  if (images.empty()) {
    return;
  }
  const QDateTime now = QDateTime::currentDateTime();
  const int zz = (now.time().msec() / 10) % 100;
  const QString stamp =
      now.toString(QStringLiteral("yyyyMMddHHmmss")) + QStringLiteral("%1").arg(zz, 2, 10, QLatin1Char('0'));
  const QString root =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/camera_viewer_burst");
  QDir().mkpath(root);
  const QString folder = root + QLatin1Char('/') + stamp;
  if (!QDir().mkpath(folder)) {
    LOG_WARN("[viewer.capture] 无法创建导出目录 %s", folder.toUtf8().constData());
    return;
  }
  int wrote = 0;
  for (int i = 0; i < static_cast<int>(images.size()); ++i) {
    const UnifiedImage& im = images[static_cast<std::size_t>(i)];
    const QImage qi = UnifiedImageToQImage(im);
    if (qi.isNull()) {
      LOG_WARN("[viewer.capture] 无法将图像转为 QImage idx=%d wh=%dx%d is16=%d ch=%d bytes=%zu", i, im.width, im.height,
               im.is_16bit ? 1 : 0, im.channels, im.data.size());
      continue;
    }
    const QString path = QStringLiteral("%1/raw_%2_%3x%4_cid%5.tiff")
                             .arg(folder)
                             .arg(i, 3, 10, QLatin1Char('0'))
                             .arg(qi.width())
                             .arg(qi.height())
                             .arg(static_cast<qulonglong>(capture_id));
    if (!qi.save(path, "TIFF")) {
      LOG_WARN("[viewer.capture] 保存 TIFF 失败，请确认 Qt 可加载 imageformats 下的 TIFF 插件: %s",
               path.toUtf8().constData());
      continue;
    }
    ++wrote;
  }
  LOG_INFO("[viewer.capture] %s 已保存 %d/%zu 张 TIFF 到 %s", log_tag, wrote, images.size(), folder.toUtf8().constData());
}

bool PopulateUnifiedFrameAfterCaptureLocked(camera3d::best::BestCamera3D* camera,
                                             std::unique_ptr<camera3d::ipc::ShmRingBuffer>& shm_ring,
                                             std::string& shm_region_cached, UnifiedFrame& frame,
                                             std::uint64_t client_capture_id) {
  frame.hardware_raw_frames.clear();
  frame.depth = UnifiedImage{};
  std::vector<camera3d::best::BestCameraRawFrameItem> raws;
  const auto raw_list_st = camera->QueryDepthCameraRawFrames(raws, client_capture_id);
  if (raw_list_st == camera3d::best::BestStatus::kSuccess && raws.empty()) {
    LOG_WARN("[viewer.capture] camera_raw_frames 为空，将用单帧 GetDepth；若 Hub 为多帧 burst，请确认 SHM 槽数足够且 GetDepth 带齐 repeated");
  }
  if (raw_list_st == camera3d::best::BestStatus::kSuccess && !raws.empty()) {
    for (const auto& it : raws) {
      if (it.region_name.empty() || it.size_bytes == 0 || it.width == 0 || it.height == 0) {
        LOG_WARN("[viewer.capture] burst 项元数据无效 burst_index=%u", it.burst_frame_index);
        return false;
      }
      if (shm_region_cached != it.region_name || !shm_ring) {
        shm_ring = std::make_unique<camera3d::ipc::ShmRingBuffer>();
        if (!shm_ring->CreateOrOpen(it.region_name, camera3d::ipc::kDefaultHubRingTotalBytes, false)) {
          LOG_WARN("[viewer.capture] SHM CreateOrOpen 失败 region=%s", it.region_name.c_str());
          shm_ring.reset();
          shm_region_cached.clear();
          return false;
        }
        shm_region_cached = it.region_name;
      }
      UnifiedImage ui;
      if (!ReadShmToUnifiedImage(shm_ring.get(), static_cast<const camera3d::best::BestShmFrameRef&>(it), ui)) {
        LOG_WARN("[viewer.capture] burst_index=%u 读 SHM 失败", it.burst_frame_index);
        return false;
      }
      frame.hardware_raw_frames.push_back(std::move(ui));
    }
    if (frame.hardware_raw_frames.empty()) {
      return false;
    }
    frame.depth = frame.hardware_raw_frames[0];
    frame.timestamp = std::chrono::steady_clock::now();
    SaveRawBurstVectorToDisk(frame.hardware_raw_frames, client_capture_id, "burst(GetDepth.camera_raw_frames)");
    LOG_INFO("[viewer.capture] burst 共 %zu 张，2D 使用第 0 张 capture_id=%" PRIu64, frame.hardware_raw_frames.size(),
             client_capture_id);
    return true;
  }

  camera3d::best::BestShmFrameRef ref;
  if (camera->QueryDepthFrame(ref, client_capture_id) != camera3d::best::BestStatus::kSuccess) {
    LogBestCaptureFailure("QueryDepthFrame(GetDepth)", camera);
    return false;
  }
  if (ref.region_name.empty() || ref.size_bytes == 0 || ref.width == 0 || ref.height == 0) {
    LOG_WARN("[viewer.capture] GetDepth 元数据无效 region=%s size=%" PRIu64 " wh=%ux%u", ref.region_name.c_str(),
             static_cast<std::uint64_t>(ref.size_bytes), static_cast<unsigned>(ref.width),
             static_cast<unsigned>(ref.height));
    return false;
  }
  if (shm_region_cached != ref.region_name || !shm_ring) {
    shm_ring = std::make_unique<camera3d::ipc::ShmRingBuffer>();
    if (!shm_ring->CreateOrOpen(ref.region_name, camera3d::ipc::kDefaultHubRingTotalBytes, false)) {
      LOG_WARN("[viewer.capture] SHM CreateOrOpen 失败 region=%s", ref.region_name.c_str());
      shm_ring.reset();
      shm_region_cached.clear();
      return false;
    }
    shm_region_cached = ref.region_name;
  }
  if (!ReadShmToUnifiedImage(shm_ring.get(), ref, frame.depth)) {
    LOG_WARN("[viewer.capture] SHM TryReadMappedRange 失败 offset=%" PRIu64 " size=%" PRIu64,
             static_cast<std::uint64_t>(ref.offset_bytes), static_cast<std::uint64_t>(ref.size_bytes));
    return false;
  }
  frame.timestamp = std::chrono::steady_clock::now();
  {
    std::vector<UnifiedImage> one;
    one.push_back(frame.depth);
    SaveRawBurstVectorToDisk(one, client_capture_id, "单帧(GetDepth.frame/raw 兼容路径)");
  }
  LOG_INFO("[viewer.capture] 单帧深度已就绪 client_capture_id=%" PRIu64 " bytes=%zu wh=%dx%d", client_capture_id,
           frame.depth.data.size(), frame.depth.width, frame.depth.height);
  return true;
}

}  // namespace

BestCamera3DAdapter::BestCamera3DAdapter() { async_stop_.store(true); }

BestCamera3DAdapter::~BestCamera3DAdapter() {
  async_stop_.store(true);
  if (async_thread_.joinable()) async_thread_.join();
  std::lock_guard<std::mutex> lk(mutex_);
  if (camera_) {
    camera_->Disconnect();
    camera_.reset();
  }
  shm_ring_.reset();
  shm_region_cached_.clear();
}

std::vector<DeviceInfo> BestCamera3DAdapter::EnumerateDevices(int timeout_ms) {
  std::vector<camera3d::best::BestDeviceInfo> found;
  if (camera3d::best::BestCamera3D::DiscoverDevices(found, 0, timeout_ms) != camera3d::best::BestStatus::kSuccess) {
    return {};
  }
  std::vector<DeviceInfo> out;
  out.reserve(found.size());
  for (const auto& b : found) {
    DeviceInfo d;
    d.name = b.display_name;
    d.ip = b.hub_host;
    d.port = b.hub_port ? b.hub_port : static_cast<std::uint16_t>(50051);
    d.serial_number = b.serial_number;
    d.firmware_version = b.firmware_version;
    d.model_name = kDeviceModelBestCameraHub;
    d.model_type = 0;
    d.hub_device_address = b.device_address;
    out.push_back(std::move(d));
  }
  return out;
}

void BestCamera3DAdapter::Disconnect() {
  async_stop_.store(true);
  if (async_thread_.joinable()) async_thread_.join();

  std::lock_guard<std::mutex> lk(mutex_);
  async_cb_ = nullptr;
  if (camera_) {
    camera_->Disconnect();
    camera_.reset();
  }
  shm_ring_.reset();
  shm_region_cached_.clear();
  have_connected_info_ = false;
}

bool BestCamera3DAdapter::Connect(const DeviceInfo* device_info, unsigned int timeout_ms) {
  async_stop_.store(true);
  if (async_thread_.joinable()) async_thread_.join();

  std::lock_guard<std::mutex> lk(mutex_);
  async_cb_ = nullptr;
  if (camera_) {
    camera_->Disconnect();
    camera_.reset();
  }
  shm_ring_.reset();
  shm_region_cached_.clear();

  camera_ = std::make_unique<camera3d::best::BestCamera3D>();
  camera3d::best::BestDeviceInfo bd;
  if (device_info) {
    bd.hub_host = device_info->ip;
    bd.hub_port = device_info->port ? device_info->port : static_cast<std::uint16_t>(50051);
    bd.device_address = device_info->hub_device_address.empty()
                            ? camera3d::best::BestDeviceInfo::DefaultSimulator().device_address
                            : device_info->hub_device_address;
    bd.serial_number = device_info->serial_number;
    bd.model = device_info->model_name;
    bd.display_name = device_info->name;
    connected_ = *device_info;
    have_connected_info_ = true;
  } else {
    bd = camera3d::best::BestDeviceInfo::DefaultSimulator();
    connected_ = DeviceInfo{};
    connected_.name = bd.display_name;
    connected_.ip = bd.hub_host;
    connected_.port = bd.hub_port ? bd.hub_port : static_cast<std::uint16_t>(50051);
    connected_.hub_device_address = bd.device_address;
    connected_.model_name = kDeviceModelBestCameraHub;
    have_connected_info_ = true;
  }
  const auto st = camera_->Connect(bd, timeout_ms);
  if (st != camera3d::best::BestStatus::kSuccess) {
    camera_.reset();
    have_connected_info_ = false;
    return false;
  }
  return true;
}

bool BestCamera3DAdapter::IsConnected() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return camera_ && camera_->IsConnected();
}

bool BestCamera3DAdapter::GetDeviceInfo(DeviceInfo& out) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!have_connected_info_) return false;
  out = connected_;
  return true;
}

bool BestCamera3DAdapter::GetParameterList(std::vector<ParamMeta>& out) const {
  out.clear();
  ParamMeta exp{};
  exp.id = kParamExposure2d;
  exp.name = "2D曝光 (exposure_2d)";
  exp.description = u8"调节 2D 相机曝光时间（微秒），Hub SetParameters / ParameterType::kExposure2d";
  exp.unit = "us";
  exp.min_val = 1;
  exp.max_val = 500000;
  exp.kind = ParamKind::Int;
  out.push_back(exp);

  ParamMeta gain{};
  gain.id = kParamGain2d;
  gain.name = "2D增益 (gain_2d)";
  gain.description = u8"调节 2D 相机增益；界面值为 dB×1000（毫分贝）";
  gain.unit = "mdB";
  gain.min_val = 0;
  gain.max_val = 48000;
  gain.kind = ParamKind::Int;
  out.push_back(gain);

  ParamMeta gamma{};
  gamma.id = kParamGamma2d;
  gamma.name = "2D伽马 (gamma_2d)";
  gamma.description = u8"调节 2D 相机伽马；界面值为线性值×1000（毫伽马）";
  gamma.unit = "mgamma";
  gamma.min_val = 100;
  gamma.max_val = 10000;
  gamma.kind = ParamKind::Int;
  out.push_back(gamma);
  return true;
}

bool BestCamera3DAdapter::GetParameter(int id, std::vector<int>& values) const {
  std::lock_guard<std::mutex> lk(mutex_);
  values.clear();
  if (!camera_ || !camera_->IsConnected()) return false;
  if (id == kParamExposure2d) {
    std::vector<camera3d::best::ParameterValue> pv;
    if (camera_->GetParameters({camera3d::best::ParameterType::kExposure2d}, &pv) !=
            camera3d::best::BestStatus::kSuccess ||
        pv.empty())
      return false;
    values.push_back(static_cast<int>(std::lround(pv[0].value)));
    return true;
  }
  if (id == kParamGain2d) {
    std::vector<camera3d::best::ParameterValue> pv;
    if (camera_->GetParameters({camera3d::best::ParameterType::kGain2d}, &pv) !=
            camera3d::best::BestStatus::kSuccess ||
        pv.empty())
      return false;
    values.push_back(static_cast<int>(std::lround(pv[0].value * 1000.0)));
    return true;
  }
  if (id == kParamGamma2d) {
    std::vector<camera3d::best::ParameterValue> pv;
    if (camera_->GetParameters({camera3d::best::ParameterType::kGamma2d}, &pv) !=
            camera3d::best::BestStatus::kSuccess ||
        pv.empty())
      return false;
    values.push_back(static_cast<int>(std::lround(pv[0].value * 1000.0)));
    return true;
  }
  return false;
}

bool BestCamera3DAdapter::SetParameter(int id, const std::vector<int>& values) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!camera_ || !camera_->IsConnected() || values.empty()) return false;
  if (id == kParamExposure2d) {
    const double us = static_cast<double>(values[0]);
    return camera_->SetParameters({{camera3d::best::ParameterType::kExposure2d, us}}) ==
           camera3d::best::BestStatus::kSuccess;
  }
  if (id == kParamGain2d) {
    const double db = static_cast<double>(values[0]) / 1000.0;
    return camera_->SetParameters({{camera3d::best::ParameterType::kGain2d, db}}) ==
           camera3d::best::BestStatus::kSuccess;
  }
  if (id == kParamGamma2d) {
    const double g = static_cast<double>(values[0]) / 1000.0;
    return camera_->SetParameters({{camera3d::best::ParameterType::kGamma2d, g}}) ==
           camera3d::best::BestStatus::kSuccess;
  }
  return false;
}

void BestCamera3DAdapter::SetCaptureProjectorOp(std::uint32_t op) {
  std::lock_guard<std::mutex> lk(mutex_);
  capture_projector_op_ = op;
}

std::uint32_t BestCamera3DAdapter::CaptureProjectorOp() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return capture_projector_op_;
}

bool BestCamera3DAdapter::CaptureSync(UnifiedFrame& frame) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!camera_ || !camera_->IsConnected()) {
    LOG_WARN("[viewer.capture] CaptureSync 跳过：未连接 Hub");
    return false;
  }
  frame = UnifiedFrame{};
  std::uint64_t client_capture_id = 0;
  const std::uint32_t pop = capture_projector_op_;
  LOG_INFO("[viewer.capture] CaptureSync 调用 Hub（with_rec=%d projector_op=%u）",
           kCaptureWithReconstructionPipeline ? 1 : 0, static_cast<unsigned>(pop));
  if (camera_->CaptureSync(&client_capture_id, kCaptureWithDetectionPipeline, kCaptureWithReconstructionPipeline, pop,
                            kCaptureTestReconShmLoopback,
                            kCaptureTestInlineImageReply) != camera3d::best::BestStatus::kSuccess) {
    LogBestCaptureFailure("CaptureSync(gRPC)", camera_.get());
    return false;
  }
  LOG_INFO("[viewer.capture] Capture RPC 成功 client_capture_id=%" PRIu64, client_capture_id);
  return PopulateUnifiedFrameAfterCaptureLocked(camera_.get(), shm_ring_, shm_region_cached_, frame,
                                                client_capture_id);
}

void BestCamera3DAdapter::AsyncThreadMain() {
  while (!async_stop_.load(std::memory_order_relaxed)) {
    UnifiedFrame frame;
    bool ok = false;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      if (camera_ && camera_->IsConnected()) {
        frame = UnifiedFrame{};
        std::uint64_t client_capture_id = 0;
        const std::uint32_t pop = capture_projector_op_;
        if (camera_->CaptureSync(&client_capture_id, kCaptureWithDetectionPipeline,
                                 kCaptureWithReconstructionPipeline, pop,
                                 kCaptureTestReconShmLoopback, kCaptureTestInlineImageReply) !=
            camera3d::best::BestStatus::kSuccess) {
          LogBestCaptureFailure("连续采集 CaptureSync", camera_.get());
        } else {
          ok = PopulateUnifiedFrameAfterCaptureLocked(camera_.get(), shm_ring_, shm_region_cached_, frame,
                                                      client_capture_id);
        }
      }
    }
    FrameCallback cb_copy;
    if (ok) {
      std::lock_guard<std::mutex> lk(mutex_);
      cb_copy = async_cb_;
    }
    if (ok && cb_copy) cb_copy(frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }
}

bool BestCamera3DAdapter::StartAsyncCapture(FrameCallback callback) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!camera_ || !camera_->IsConnected()) return false;
  if (async_thread_.joinable()) return false;
  async_cb_ = std::move(callback);
  async_stop_.store(false);
  async_thread_ = std::thread([this] { AsyncThreadMain(); });
  return true;
}

void BestCamera3DAdapter::StopAsyncCapture() {
  async_stop_.store(true);
  if (async_thread_.joinable()) async_thread_.join();
  std::lock_guard<std::mutex> lk(mutex_);
  async_cb_ = nullptr;
}

bool BestCamera3DAdapter::IsAsyncCapturing() const { return async_thread_.joinable(); }

}  // namespace scanner_viewer
