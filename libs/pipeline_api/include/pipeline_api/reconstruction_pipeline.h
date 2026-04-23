#pragma once

// 重建算法抽象：输入 ShmFrameView，输出点云/网格等 blob；recon_service 中可替换为真实现。

#include "pipeline_api/shm_frame_view.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace camera3d::pipeline {

enum class ReconPipelineError : int {
  kOk = 0,
  kInvalidInput = 1,
  kInternalError = 2,
};

class IReconstructionPipeline {
 public:
  virtual ~IReconstructionPipeline() = default;
  // 输入原始帧视图，输出点云/网格等 blob；失败时填写 out_message。
  virtual ReconPipelineError Process(const ShmFrameView& input, std::vector<std::uint8_t>& out_pointcloud_or_mesh_blob,
                                     std::string& out_message) = 0;
};

std::unique_ptr<IReconstructionPipeline> CreateNoOpReconstructionPipeline();
std::unique_ptr<IReconstructionPipeline> CreateLoggingReconstructionPipeline();

}  // namespace camera3d::pipeline
