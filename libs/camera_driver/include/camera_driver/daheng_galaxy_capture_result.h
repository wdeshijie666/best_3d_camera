#pragma once

#include "camera_driver/camera_capture_result.h"

#if defined(CAMERA3D_WITH_DAHENG_GALAXY)
#include <GalaxyIncludes.h>
#endif

namespace camera3d::camera {

#if defined(CAMERA3D_WITH_DAHENG_GALAXY)

// 大恒 GxIAPICPP 原生帧：仅持有 CImageDataPointer（引用计数），不在适配器内做像素级 memcpy。
// SetResultCallback 中可 static_cast 为本类型后直接用 IImageData 接口；若需跨线程保存请在回调内 clone() 或自行拷贝缓冲区。
// 注意：回调返回后适配器会对同一帧调用 QBuf，请勿在回调外长期保存 image_data 指针而不拷贝。
struct DahengGalaxyCameraResult final : CameraCaptureResultBase {
  CImageDataPointer image_data;

  DahengGalaxyCameraResult() = default;
  explicit DahengGalaxyCameraResult(CImageDataPointer& p) : image_data(p) {
    success = !image_data.IsNull() && image_data->GetStatus() == GX_FRAME_STATUS_SUCCESS;
  }

  CameraResultKind kind() const noexcept override { return CameraResultKind::kDahengGalaxyNative; }
  CameraCaptureResultBase* clone() const override {
    auto* p = new DahengGalaxyCameraResult();
    p->success = success;
    p->error_message = error_message;
    p->image_data = image_data;
    return p;
  }
};

#else

struct DahengGalaxyCameraResult;

#endif

}  // namespace camera3d::camera
