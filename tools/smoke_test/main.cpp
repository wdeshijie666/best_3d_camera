#include "camera_sdk/best_camera_3d.h"

#include <cstdint>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  camera3d::best::BestDeviceInfo di;
  if (argc >= 3) {
    di = camera3d::best::BestDeviceInfo::DefaultSimulator();
    const std::string hub_arg = argv[1];
    if (hub_arg.find(':') != std::string::npos) {
      di.hub_host = hub_arg;
      di.hub_port = 0;
    } else {
      di.hub_host = hub_arg;
    }
    di.device_address = argv[2];
  } else if (argc >= 2) {
    di = camera3d::best::BestDeviceInfo::DefaultSimulator();
    const std::string hub_arg = argv[1];
    if (hub_arg.find(':') != std::string::npos) {
      di.hub_host = hub_arg;
      di.hub_port = 0;
    } else {
      di.hub_host = hub_arg;
    }
  } else {
    di = camera3d::best::BestDeviceInfo::DefaultSimulator();
  }

  camera3d::best::BestCamera3D cam;
  const camera3d::best::BestStatus st = cam.Connect(di);
  if (st != camera3d::best::BestStatus::kSuccess) {
    std::cerr << "Connect failed status=" << static_cast<int>(st) << " code=" << cam.LastErrorCode()
              << " msg=" << cam.LastErrorMessage() << "\n";
    return 11;
  }

  std::uint64_t job_id = 0;
  if (cam.CaptureSync(&job_id) != camera3d::best::BestStatus::kSuccess) {
    std::cerr << "CaptureSync failed code=" << cam.LastErrorCode() << " msg=" << cam.LastErrorMessage() << "\n";
    cam.Disconnect();
    return 12;
  }

  camera3d::best::BestShmFrameRef depth{};
  if (cam.QueryDepthFrame(depth) != camera3d::best::BestStatus::kSuccess) {
    std::cerr << "QueryDepthFrame failed code=" << cam.LastErrorCode() << " msg=" << cam.LastErrorMessage() << "\n";
    cam.Disconnect();
    return 13;
  }

  std::cout << "OK"
            << " peer=" << cam.RpcPeer() << " session=" << cam.SessionId() << " job_id=" << job_id
            << " region=" << depth.region_name << " seq=" << depth.seq << " offset=" << depth.offset_bytes
            << " size=" << depth.size_bytes << " w=" << depth.width << " h=" << depth.height
            << " fmt=" << depth.pixel_format << " ts_ns=" << depth.timestamp_unix_ns << "\n";

  cam.Disconnect();
  return 0;
}
