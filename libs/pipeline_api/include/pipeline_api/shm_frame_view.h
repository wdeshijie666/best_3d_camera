#pragma once

// 算法输入：指向 SHM 或内存缓冲区的只读一帧描述（宽度/高度/像素格式/seq）。

#include <cstddef>
#include <cstdint>

namespace camera3d::pipeline {

// 只读视图：指向共享内存或其它缓冲区中的一帧
struct ShmFrameView {
  const std::uint8_t* data = nullptr;
  std::size_t size_bytes = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t pixel_format = 0;
  std::uint64_t seq = 0;
  // data 非空且 size_bytes>0 视为可送算法。
  bool IsValid() const { return data != nullptr && size_bytes > 0; }
};

}  // namespace camera3d::pipeline
