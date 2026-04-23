#pragma once

// 可选编译：重建侧 gRPC 服务，与 Hub SHM 回环联调配套（见 README_hub_recon_grpc_shm.md）。

#include <string>

namespace camera3d::recon {

// 阻塞运行，直到进程退出（供独立线程调用）
void RunReconShmLoopbackGrpcServer(const std::string& listen_address);

}  // namespace camera3d::recon
