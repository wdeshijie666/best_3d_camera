// 基于 BestCamera3D 的 Hub 联通与相机通路烟测：
// 连接 → 曝光/增益读写（可选）→ Capture（可请求 Hub 内联回传测试图）→ 深度 SHM 校验。
#include "camera_sdk/best_camera_3d.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

void Usage() {
  std::cerr
      << "用法: hub_connectivity_test [选项]\n"
      << "  --hub <host[:port]>     默认 127.0.0.1:50051\n"
      << "  --device <addr>         默认 null:virtual0\n"
      << "  --com <n>               Hub 投影仪串口 COM 序号，默认 0（不打开）\n"
      << "  --async                 异步采集后等待再 GetDepth\n"
      << "  --wait-ms <n>           与 --async 配合，默认 500\n"
      << "  --loopback              透传 test_recon_shm_loopback（需 Hub 编译联调开关）\n"
      << "  --inline-image          透传 test_inline_image_reply，请求 Hub 在 CaptureReply 回传图片\n"
      << "  --inline-recon          便捷测试：同时打开 --inline-image + --loopback\n"
      << "  --save-dir <path>       保存回传图片目录，默认 logs/hub_connectivity_test_inline\n"
      << "  --exposure-us <v>       若设置则先 SetExposure 再采集\n";
}

bool ParseUInt(const char* s, unsigned& out) {
  char* end = nullptr;
  const unsigned long v = std::strtoul(s, &end, 10);
  if (!s || !*s || !end || *end != '\0' || v > 0xFFFFFFFFu) return false;
  out = static_cast<unsigned>(v);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string hub_arg;
  std::string device_arg;
  unsigned com_index = 0;
  bool async = false;
  unsigned wait_ms = 500;
  bool loopback = false;
  bool inline_image = false;
  bool have_exposure = false;
  double exposure_us = 0;
  std::filesystem::path save_dir = "logs/hub_connectivity_test_inline";

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      Usage();
      return 0;
    }
    if (a == "--hub" && i + 1 < argc) {
      hub_arg = argv[++i];
    } else if (a == "--device" && i + 1 < argc) {
      device_arg = argv[++i];
    } else if (a == "--com" && i + 1 < argc) {
      unsigned v = 0;
      if (!ParseUInt(argv[++i], v)) {
        std::cerr << "无效 --com\n";
        return 2;
      }
      com_index = v;
    } else if (a == "--async") {
      async = true;
    } else if (a == "--wait-ms" && i + 1 < argc) {
      unsigned v = 0;
      if (!ParseUInt(argv[++i], v)) {
        std::cerr << "无效 --wait-ms\n";
        return 2;
      }
      wait_ms = v;
    } else if (a == "--loopback") {
      loopback = true;
    } else if (a == "--inline-image") {
      inline_image = true;
    } else if (a == "--inline-recon") {
      inline_image = true;
      loopback = true;
    } else if (a == "--save-dir" && i + 1 < argc) {
      save_dir = argv[++i];
    } else if (a == "--exposure-us" && i + 1 < argc) {
      have_exposure = true;
      exposure_us = std::strtod(argv[++i], nullptr);
    } else {
      std::cerr << "未知参数: " << a << "\n";
      Usage();
      return 2;
    }
  }

  camera3d::best::BestDeviceInfo di = camera3d::best::BestDeviceInfo::DefaultSimulator();
  if (!hub_arg.empty()) {
    if (hub_arg.find(':') != std::string::npos) {
      di.hub_host = hub_arg;
      di.hub_port = 0;
    } else {
      di.hub_host = hub_arg;
    }
  }
  if (!device_arg.empty()) di.device_address = device_arg;
  di.projector_com_index = static_cast<std::uint32_t>(com_index);

  camera3d::best::BestCamera3D cam;
  std::cout << "sdk_version=" << camera3d::best::BestCamera3D::SdkVersion() << "\n";

  if (cam.Connect(di) != camera3d::best::BestStatus::kSuccess) {
    std::cerr << "[FAIL] Connect code=" << cam.LastErrorCode() << " msg=" << cam.LastErrorMessage() << "\n";
    return 11;
  }
  std::cout << "[OK] Connect peer=" << cam.RpcPeer() << " session=" << cam.SessionId() << "\n";

  {
    using camera3d::best::ParameterType;
    using camera3d::best::ParameterValue;
    std::vector<ParameterValue> pv_out;
    if (cam.GetParameters({ParameterType::kExposure2d, ParameterType::kGain2d, ParameterType::kGamma2d}, &pv_out) ==
        camera3d::best::BestStatus::kSuccess) {
      for (const ParameterValue& p : pv_out) {
        std::cout << "[OK] GetParameters type=" << static_cast<int>(p.type) << " value=" << p.value << "\n";
      }
    } else {
      std::cout << "[SKIP] GetParameters code=" << cam.LastErrorCode() << " msg=" << cam.LastErrorMessage() << "\n";
    }
  }

  if (have_exposure) {
    if (cam.SetParameters({{camera3d::best::ParameterType::kExposure2d, exposure_us}}) !=
        camera3d::best::BestStatus::kSuccess) {
      std::cerr << "[FAIL] SetParameters(exposure_2d) code=" << cam.LastErrorCode()
                << " msg=" << cam.LastErrorMessage() << "\n";
      cam.Disconnect();
      return 12;
    }
    std::cout << "[OK] SetParameters exposure_2d us=" << exposure_us << "\n";
  }

  std::uint64_t job_id = 0;
  const camera3d::best::BestStatus cap_st =
      async ? cam.CaptureAsync(&job_id, false, false, 0, loopback, inline_image)
            : cam.CaptureSync(&job_id, false, false, 0, loopback, inline_image);
  if (cap_st != camera3d::best::BestStatus::kSuccess) {
    std::cerr << "[FAIL] Capture code=" << cam.LastErrorCode() << " msg=" << cam.LastErrorMessage() << "\n";
    cam.Disconnect();
    return 13;
  }
  std::cout << "[OK] Capture " << (async ? "async" : "sync") << " job_id=" << job_id << "\n";

  if (inline_image) {
    camera3d::best::BestInlineImage inl{};
    if (cam.GetLastCaptureInlineImage(inl) != camera3d::best::BestStatus::kSuccess || inl.payload.empty()) {
      std::cerr << "[FAIL] inline image missing after Capture\n";
      cam.Disconnect();
      return 15;
    }
    std::error_code ec;
    std::filesystem::create_directories(save_dir, ec);
    if (ec) {
      std::cerr << "[FAIL] create save dir failed: " << save_dir.string() << " ec=" << ec.message() << "\n";
      cam.Disconnect();
      return 16;
    }
    std::string base = inl.name.empty() ? ("capture_" + std::to_string(job_id) + ".bin") : inl.name;
    std::filesystem::path out = save_dir / base;
    if (std::filesystem::exists(out, ec) && !ec) {
      out = save_dir / ("capture_" + std::to_string(job_id) + "_" + base);
    }
    std::ofstream ofs(out, std::ios::binary | std::ios::trunc);
    if (!ofs) {
      std::cerr << "[FAIL] open output failed: " << out.string() << "\n";
      cam.Disconnect();
      return 17;
    }
    ofs.write(reinterpret_cast<const char*>(inl.payload.data()),
              static_cast<std::streamsize>(inl.payload.size()));
    if (!ofs) {
      std::cerr << "[FAIL] write output failed: " << out.string() << "\n";
      cam.Disconnect();
      return 18;
    }
    std::cout << "[OK] Inline image saved: " << out.string() << " bytes=" << inl.payload.size() << "\n";
  }

  if (async && wait_ms > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(wait_ms)));
  }

  camera3d::best::BestShmFrameRef depth{};
  const std::uint64_t qid = async ? job_id : 0;
  if (cam.QueryDepthFrame(depth, qid) != camera3d::best::BestStatus::kSuccess) {
    std::cerr << "[FAIL] QueryDepthFrame code=" << cam.LastErrorCode() << " msg=" << cam.LastErrorMessage() << "\n";
    cam.Disconnect();
    return 14;
  }

  std::cout << "[OK] Depth SHM region=" << depth.region_name << " seq=" << depth.seq << " off=" << depth.offset_bytes
            << " size=" << depth.size_bytes << " w=" << depth.width << " h=" << depth.height
            << " fmt=" << depth.pixel_format << " ts_ns=" << depth.timestamp_unix_ns << "\n";

  cam.Disconnect();
  std::cout << "[OK] Disconnect\n";
  return 0;
}
