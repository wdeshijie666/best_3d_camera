// 检测/重建管线的占位实现：NoOp 与 Logging，供 recon_service 等链接验证通路。

#include "pipeline_api/detection_pipeline.h"
#include "pipeline_api/reconstruction_pipeline.h"

#include "platform_diag/logging.h"

namespace camera3d::pipeline {
namespace {

// 空检测：恒 kOk，无输出。
class NoOpDetection final : public IDetectionPipeline {
 public:
  PipelineError Process(const ShmFrameView& input, std::vector<std::uint8_t>& out_result_blob,
                        std::string& out_message) override {
    (void)input;
    out_result_blob.clear();
    out_message.clear();
    return PipelineError::kOk;
  }
};

// 日志检测：校验视图后打一条 info。
class LoggingDetection final : public IDetectionPipeline {
 public:
  PipelineError Process(const ShmFrameView& input, std::vector<std::uint8_t>& out_result_blob,
                        std::string& out_message) override {
    out_result_blob.clear();
    if (!input.IsValid()) {
      out_message = "invalid frame view";
      return PipelineError::kInvalidInput;
    }
    CAMERA3D_LOGI("DetectionPipeline: seq={} size={} {}x{} fmt={}", input.seq, input.size_bytes,
                  input.width, input.height, input.pixel_format);
    out_message = "noop";
    return PipelineError::kOk;
  }
};

// 空重建：恒 kOk。
class NoOpRecon final : public IReconstructionPipeline {
 public:
  ReconPipelineError Process(const ShmFrameView& input, std::vector<std::uint8_t>& out_blob,
                             std::string& out_message) override {
    (void)input;
    out_blob.clear();
    out_message.clear();
    return ReconPipelineError::kOk;
  }
};

// 日志重建：校验视图后打一条 info。
class LoggingRecon final : public IReconstructionPipeline {
 public:
  ReconPipelineError Process(const ShmFrameView& input, std::vector<std::uint8_t>& out_blob,
                             std::string& out_message) override {
    out_blob.clear();
    if (!input.IsValid()) {
      out_message = "invalid frame view";
      return ReconPipelineError::kInvalidInput;
    }
    CAMERA3D_LOGI("ReconstructionPipeline: seq={} size={} {}x{}", input.seq, input.size_bytes, input.width,
                  input.height);
    out_message = "noop";
    return ReconPipelineError::kOk;
  }
};

}  // namespace

// 实现 CreateNoOpDetectionPipeline。
std::unique_ptr<IDetectionPipeline> CreateNoOpDetectionPipeline() {
  return std::make_unique<NoOpDetection>();
}

// 实现 CreateLoggingDetectionPipeline。
std::unique_ptr<IDetectionPipeline> CreateLoggingDetectionPipeline() {
  return std::make_unique<LoggingDetection>();
}

// 实现 CreateNoOpReconstructionPipeline。
std::unique_ptr<IReconstructionPipeline> CreateNoOpReconstructionPipeline() {
  return std::make_unique<NoOpRecon>();
}

// 实现 CreateLoggingReconstructionPipeline。
std::unique_ptr<IReconstructionPipeline> CreateLoggingReconstructionPipeline() {
  return std::make_unique<LoggingRecon>();
}

}  // namespace camera3d::pipeline
