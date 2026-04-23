/**
 * @file hub_burst_tiff_save.cpp
 * @brief Hub 调试：将硬触发 burst 原始帧落盘为 TIFF（需 OpenCV imgcodecs）。
 */
#include "hub_burst_tiff_save.h"

#include "platform_diag/logging.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace camera3d::hub {
namespace {

namespace fs = std::filesystem;

std::string SanitizePathToken(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (unsigned char c : s) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
      o.push_back('_');
    } else {
      o.push_back(static_cast<char>(c));
    }
  }
  if (o.empty()) return "cam";
  return o;
}

bool FrameBufferToMat(const camera3d::camera::FrameBuffer& fb, cv::Mat& out) {
  if (fb.width == 0 || fb.height == 0 || fb.bytes.empty()) {
    return false;
  }
  const std::uint64_t area = static_cast<std::uint64_t>(fb.width) * fb.height;
  if (area == 0 || fb.bytes.size() % area != 0) {
    CAMERA3D_LOGW("[burst-tiff] 载荷与宽高不匹配 w={} h={} bytes={}", fb.width, fb.height, fb.bytes.size());
    return false;
  }
  const std::size_t bpp = fb.bytes.size() / static_cast<std::size_t>(area);
  int cv_type = -1;
  if (bpp == 1) {
    cv_type = CV_8UC1;
  } else if (bpp == 2) {
    cv_type = CV_16UC1;
  } else if (bpp == 3) {
    cv_type = CV_8UC3;
  } else if (bpp == 4) {
    cv_type = CV_8UC4;
  } else {
    CAMERA3D_LOGW("[burst-tiff] 不支持的每像素字节数 bpp={} w={} h={}", bpp, fb.width, fb.height);
    return false;
  }
  const int step = static_cast<int>(fb.width) * static_cast<int>(bpp);
  out = cv::Mat(static_cast<int>(fb.height), static_cast<int>(fb.width), cv_type,
                const_cast<std::uint8_t*>(fb.bytes.data()), static_cast<std::size_t>(step));
  return true;
}

std::string MakeTimestampDirName() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const std::time_t t = clock::to_time_t(now);
  std::tm lt{};
#if defined(_WIN32)
  localtime_s(&lt, &t);
#else
  localtime_r(&t, &lt);
#endif
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  char buf[80];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d_%03d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                lt.tm_hour, lt.tm_min, lt.tm_sec, static_cast<int>(ms.count()));
  return std::string(buf);
}

}  // namespace

void HubTrySaveBurstFramesTiff(
    const std::unordered_map<std::string, std::vector<camera3d::camera::FrameBuffer>>& bursts_by_id,
    const std::vector<std::string>& camera_order, std::uint64_t capture_id, std::uint32_t frames_per) {
  if (frames_per == 0 || camera_order.empty()) {
    return;
  }
  try {
    const fs::path root = fs::current_path() / "hub_img_save";
    fs::create_directories(root);
    const fs::path batch = root / MakeTimestampDirName();
    fs::create_directories(batch);

    int saved = 0;
    for (std::size_t cam_i = 0; cam_i < camera_order.size(); ++cam_i) {
      const std::string& mid = camera_order[cam_i];
      auto it = bursts_by_id.find(mid);
      if (it == bursts_by_id.end()) {
        CAMERA3D_LOGW("[burst-tiff] 缺少相机 burst 数据 manager_id={}", mid);
        continue;
      }
      const std::vector<camera3d::camera::FrameBuffer>& vec = it->second;
      if (vec.size() < frames_per) {
        CAMERA3D_LOGW("[burst-tiff] 帧数不足 manager_id={} got={} need={}", mid, vec.size(), frames_per);
        continue;
      }
      const std::string cam_token = SanitizePathToken(mid);
      for (std::uint32_t j = 0; j < frames_per; ++j) {
        cv::Mat m;
        if (!FrameBufferToMat(vec[j], m)) {
          CAMERA3D_LOGW("[burst-tiff] 跳过无法解析的帧 cam={} burst_idx={}", mid, j);
          continue;
        }
        std::ostringstream name;
        name << "cam" << cam_i << "_" << cam_token << "_f" << j << "_cid" << capture_id << ".tiff";
        const fs::path out_path = batch / name.str();
        if (!cv::imwrite(out_path.string(), m)) {
          CAMERA3D_LOGW("[burst-tiff] imwrite 失败 {}", out_path.string());
          continue;
        }
        ++saved;
      }
    }
    CAMERA3D_LOGI("[burst-tiff] 已保存 {} 张至 {}", saved, batch.string());
  } catch (const std::exception& e) {
    CAMERA3D_LOGW("[burst-tiff] 落盘异常: {}", e.what());
  } catch (...) {
    CAMERA3D_LOGW("[burst-tiff] 落盘未知异常");
  }
}

}  // namespace camera3d::hub
