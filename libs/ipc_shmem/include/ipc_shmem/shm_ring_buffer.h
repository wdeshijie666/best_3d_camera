#pragma once

// Boost.Interprocess 环形帧缓冲：Hub 写槽、算法进程读槽；头布局与 ShmSlotHeader 二进制兼容。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace camera3d::ipc {

// 单段共享内存上的环形槽位头（与 architecture 文档 §5.1 对齐的精简版）
#pragma pack(push, 1)
struct ShmGlobalHeader {
  std::uint32_t magic = 0xC3D3'0001u;       // 区段魔数，用于校验映射有效
  std::uint32_t version = 1;                 // 布局版本
  std::uint32_t slot_count = 0;             // ShmSlotHeader 槽个数
  std::uint32_t slot_stride_bytes = 0;     // 单槽头+负载步进（创建时写入）
};
struct ShmSlotHeader {
  std::uint64_t seq_publish = 0;           // 本槽已发布序号，0 表示从未写入
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t pixel_format = 0;
  std::uint32_t reserved0 = 0;             // 对齐保留
  std::uint64_t payload_offset = 0;        // 相对映射基址的字节偏移
  std::uint64_t payload_size = 0;          // 有效像素载荷长度
  std::int64_t timestamp_unix_ns = 0;      // 可选时间戳
};
#pragma pack(pop)

// Boost.Interprocess 共享内存段；Hub 写原始帧，重建等消费者读。区名/容量见 shm_constants.h。
class ShmRingBuffer {
 public:
  ShmRingBuffer();
  ~ShmRingBuffer();
  ShmRingBuffer(ShmRingBuffer&&) noexcept;
  ShmRingBuffer& operator=(ShmRingBuffer&&) noexcept;
  ShmRingBuffer(const ShmRingBuffer&) = delete;
  ShmRingBuffer& operator=(const ShmRingBuffer&) = delete;

  // create_if_missing=true 时 remove 后重建并初始化头；false 时仅 open_only。
  bool CreateOrOpen(const std::string& region_name, std::size_t total_bytes, bool create_if_missing);

  // 最近一次 CreateOrOpen 使用的区名。
  const std::string& RegionName() const;

  std::uint32_t SlotCount() const;

  // 当前各槽中最大 seq（0 表示尚无提交）
  std::uint64_t MaxPublishedSeq() const;

  // 读取指定槽：返回 false 若魔数无效、索引越界或尚未写入
  bool TryReadSlot(std::uint32_t slot_index, ShmSlotHeader& out_meta, const std::uint8_t*& out_payload,
                   std::size_t& out_payload_len) const;

  // 选取 seq 最大的槽（并列时取索引较大者）
  bool TryReadLatestSlot(ShmSlotHeader& out_meta, const std::uint8_t*& out_payload,
                         std::size_t& out_payload_len, std::uint32_t* out_slot_index) const;

  // 按映射基址 + 字节偏移读取（与 ShmFrameRef.offset_bytes 一致）。
  bool TryReadMappedRange(std::uint64_t offset_bytes, std::uint64_t size_bytes, const std::uint8_t*& out_payload,
                          std::size_t& out_payload_len) const;

  // out_slot_index：可选，返回本次写入的槽索引，便于 Hub 按槽构造 ShmFrameRef.offset_bytes
  bool TryWriteNextSlot(const void* payload, std::size_t payload_size, std::uint32_t width,
                        std::uint32_t height, std::uint32_t pixel_format, std::uint64_t* out_seq,
                        std::uint32_t* out_slot_index = nullptr);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace camera3d::ipc
