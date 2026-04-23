#include "camera_driver/adapters.h"

#if defined(CAMERA3D_WITH_DAHENG_GALAXY)
namespace camera3d::camera {
std::shared_ptr<ICameraAdapter> CreateDaHengGalaxyCameraAdapter();
}
#endif

namespace camera3d::camera {

std::shared_ptr<ICameraAdapter> CreateDaHengCameraAdapter() {
#if defined(CAMERA3D_WITH_DAHENG_GALAXY)
  return CreateDaHengGalaxyCameraAdapter();
#else
  return CreateDaHengCameraAdapterStub();
#endif
}

}  // namespace camera3d::camera
