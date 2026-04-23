#pragma once

// 检测算法进程内抽象；独立 detect_service 已移除，接口可保留给重建进程内嵌或后续扩展。

#include "pipeline_api/shm_frame_view.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace camera3d::pipeline {

enum class PipelineError : int {
  kOk = 0,
  kInvalidInput = 1,
  kInternalError = 2,
};

// 检测算法抽象：输入原始帧视图，输出序列化结果（如 JSON/protobuf bytes）
class IDetectionPipeline {
 public:
  virtual ~IDetectionPipeline() = default;
  // 处理一帧视图，输出算法 blob（如 JSON/自定义二进制）；out_message 供日志或 UI。
  virtual PipelineError Process(const ShmFrameView& input, std::vector<std::uint8_t>& out_result_blob,
                                std::string& out_message) = 0;
};

std::unique_ptr<IDetectionPipeline> CreateNoOpDetectionPipeline();
std::unique_ptr<IDetectionPipeline> CreateLoggingDetectionPipeline();

}  // namespace camera3d::pipeline
