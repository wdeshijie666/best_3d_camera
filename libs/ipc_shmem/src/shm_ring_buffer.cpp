#include "ipc_shmem/shm_ring_buffer.h"
#include "ipc_shmem/shm_constants.h"

#include "platform_diag/logging.h"

#include <memory>

#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

#include <algorithm>
#include <cstring>

namespace camera3d::ipc {
namespace bip = boost::interprocess;

// --- ShmRingBuffer：Boost.Interprocess 映射区 + 头解析与轮询写槽 ---

struct ShmRingBuffer::Impl {
  std::string name;
  std::unique_ptr<bip::shared_memory_object> shm;
  std::unique_ptr<bip::mapped_region> region;
  std::uint8_t* bytes = nullptr;
  std::size_t mapped_size = 0;
};

// 实现 ShmRingBuffer::ShmRingBuffer：分配 Impl。
ShmRingBuffer::ShmRingBuffer() : impl_(std::make_unique<Impl>()) {}
// 实现 ShmRingBuffer::~ShmRingBuffer：默认释放映射（由 Impl 析构）。
ShmRingBuffer::~ShmRingBuffer() = default;

ShmRingBuffer::ShmRingBuffer(ShmRingBuffer&&) noexcept = default;
// 实现 ShmRingBuffer::operator=：移动 impl_。
ShmRingBuffer& ShmRingBuffer::operator=(ShmRingBuffer&& o) noexcept {
  if (this != &o) {
    impl_ = std::move(o.impl_);
  }
  return *this;
}

// 实现 ShmRingBuffer::RegionName：返回缓存区名。
const std::string& ShmRingBuffer::RegionName() const { return impl_->name; }

// 实现 ShmRingBuffer::CreateOrOpen：truncate、map、可选 memset 写全局头与槽步进。
bool ShmRingBuffer::CreateOrOpen(const std::string& region_name, std::size_t total_bytes,
                                 bool create_if_missing) {
  impl_->name = region_name;
  try {
    if (create_if_missing) {
      bip::shared_memory_object::remove(region_name.c_str());
      impl_->shm = std::make_unique<bip::shared_memory_object>(bip::create_only, region_name.c_str(),
                                                               bip::read_write);
      impl_->shm->truncate(static_cast<bip::offset_t>(total_bytes));
    } else {
      impl_->shm = std::make_unique<bip::shared_memory_object>(bip::open_only, region_name.c_str(),
                                                               bip::read_write);
    }
    impl_->region = std::make_unique<bip::mapped_region>(*impl_->shm, bip::read_write);
    impl_->bytes = static_cast<std::uint8_t*>(impl_->region->get_address());
    impl_->mapped_size = impl_->region->get_size();

    if (create_if_missing) {
      const std::uint32_t slot_count = kDefaultHubRingSlotCount;
      const std::size_t headers_end =
          sizeof(ShmGlobalHeader) + sizeof(ShmSlotHeader) * static_cast<std::size_t>(slot_count);
      if (impl_->mapped_size < headers_end + 1024) {
        CAMERA3D_LOGE("共享内存过小");
        return false;
      }
      std::memset(impl_->bytes, 0, impl_->mapped_size);
      auto* gh = reinterpret_cast<ShmGlobalHeader*>(impl_->bytes);
      gh->magic = 0xC3D3'0001u;
      gh->version = 1;
      gh->slot_count = slot_count;
      const auto per_slot_payload = (impl_->mapped_size - headers_end) / static_cast<std::size_t>(slot_count);
      gh->slot_stride_bytes =
          static_cast<std::uint32_t>(sizeof(ShmSlotHeader) + per_slot_payload);
    }
    CAMERA3D_LOGI("ShmRingBuffer 就绪 name={} size={}", region_name, impl_->mapped_size);
    return true;
  } catch (const std::exception& ex) {
    CAMERA3D_LOGE("共享内存创建/打开失败: {}", ex.what());
    return false;
  }
}

// 实现 ShmRingBuffer::SlotCount：读全局头 slot_count。
std::uint32_t ShmRingBuffer::SlotCount() const {
  if (!impl_->bytes) return 0;
  const auto* gh = reinterpret_cast<const ShmGlobalHeader*>(impl_->bytes);
  return gh->slot_count;
}

// 实现 ShmRingBuffer::MaxPublishedSeq：扫描各槽 seq_publish 取 max。
std::uint64_t ShmRingBuffer::MaxPublishedSeq() const {
  if (!impl_->bytes) return 0;
  const auto* gh = reinterpret_cast<const ShmGlobalHeader*>(impl_->bytes);
  if (gh->magic != 0xC3D3'0001u) return 0;
  const auto* slots = reinterpret_cast<const ShmSlotHeader*>(impl_->bytes + sizeof(ShmGlobalHeader));
  std::uint64_t m = 0;
  for (std::uint32_t i = 0; i < gh->slot_count; ++i) {
    m = (std::max)(m, slots[i].seq_publish);
  }
  return m;
}

// 实现 ShmRingBuffer::TryReadSlot：校验魔数与边界，返回 payload 指针。
bool ShmRingBuffer::TryReadSlot(std::uint32_t slot_index, ShmSlotHeader& out_meta,
                                const std::uint8_t*& out_payload, std::size_t& out_payload_len) const {
  out_payload = nullptr;
  out_payload_len = 0;
  if (!impl_->bytes) return false;
  const auto* gh = reinterpret_cast<const ShmGlobalHeader*>(impl_->bytes);
  if (gh->magic != 0xC3D3'0001u) return false;
  if (slot_index >= gh->slot_count) return false;
  const auto* slots = reinterpret_cast<const ShmSlotHeader*>(impl_->bytes + sizeof(ShmGlobalHeader));
  const ShmSlotHeader& slot = slots[slot_index];
  if (slot.seq_publish == 0 || slot.payload_size == 0) return false;
  const std::size_t end = static_cast<std::size_t>(slot.payload_offset) + slot.payload_size;
  if (end > impl_->mapped_size) return false;
  out_meta = slot;
  out_payload = impl_->bytes + slot.payload_offset;
  out_payload_len = static_cast<std::size_t>(slot.payload_size);
  return true;
}

// 实现 ShmRingBuffer::TryReadLatestSlot：选最大 seq 的槽再 TryReadSlot。
bool ShmRingBuffer::TryReadLatestSlot(ShmSlotHeader& out_meta, const std::uint8_t*& out_payload,
                                      std::size_t& out_payload_len, std::uint32_t* out_slot_index) const {
  if (!impl_->bytes) return false;
  const auto* gh = reinterpret_cast<const ShmGlobalHeader*>(impl_->bytes);
  if (gh->magic != 0xC3D3'0001u) return false;
  const auto* slots = reinterpret_cast<const ShmSlotHeader*>(impl_->bytes + sizeof(ShmGlobalHeader));
  std::uint64_t best_seq = 0;
  std::uint32_t best_idx = 0;
  for (std::uint32_t i = 0; i < gh->slot_count; ++i) {
    if (slots[i].seq_publish >= best_seq) {
      best_seq = slots[i].seq_publish;
      best_idx = i;
    }
  }
  if (best_seq == 0) return false;
  if (out_slot_index) *out_slot_index = best_idx;
  return TryReadSlot(best_idx, out_meta, out_payload, out_payload_len);
}

bool ShmRingBuffer::TryReadMappedRange(std::uint64_t offset_bytes, std::uint64_t size_bytes,
                                       const std::uint8_t*& out_payload, std::size_t& out_payload_len) const {
  out_payload = nullptr;
  out_payload_len = 0;
  if (!impl_->bytes || size_bytes == 0) return false;
  const std::uint64_t end = offset_bytes + size_bytes;
  if (end > impl_->mapped_size || end < offset_bytes) return false;
  out_payload = impl_->bytes + offset_bytes;
  out_payload_len = static_cast<std::size_t>(size_bytes);
  return true;
}

// 实现 ShmRingBuffer::TryWriteNextSlot：线程局部轮询槽索引，memcpy 负载并递增 seq。
bool ShmRingBuffer::TryWriteNextSlot(const void* payload, std::size_t payload_size, std::uint32_t width,
                                     std::uint32_t height, std::uint32_t pixel_format,
                                     std::uint64_t* out_seq, std::uint32_t* out_slot_index) {
  if (!impl_->bytes) return false;
  auto* gh = reinterpret_cast<ShmGlobalHeader*>(impl_->bytes);
  if (gh->magic != 0xC3D3'0001u) return false;

  static thread_local std::uint32_t s_next = 0;
  const std::uint32_t idx = s_next++ % gh->slot_count;
  if (out_slot_index) {
    *out_slot_index = idx;
  }
  auto* slots = reinterpret_cast<ShmSlotHeader*>(impl_->bytes + sizeof(ShmGlobalHeader));
  ShmSlotHeader& slot = slots[idx];

  const std::uint64_t next_seq = slot.seq_publish + 1;
  const std::size_t headers_end =
      sizeof(ShmGlobalHeader) + sizeof(ShmSlotHeader) * gh->slot_count;
  const std::size_t total_payload = impl_->mapped_size - headers_end;
  const std::size_t per_slot = total_payload / gh->slot_count;
  if (payload_size > per_slot) {
    CAMERA3D_LOGW("payload 大于单槽容量 per_slot={}", per_slot);
    return false;
  }
  const std::size_t payload_base = headers_end + static_cast<std::size_t>(idx) * per_slot;
  std::memcpy(impl_->bytes + payload_base, payload, payload_size);
  slot.width = width;
  slot.height = height;
  slot.pixel_format = pixel_format;
  slot.payload_offset = payload_base;
  slot.payload_size = payload_size;
  slot.seq_publish = next_seq;
  if (out_seq) *out_seq = next_seq;
  return true;
}

}  // namespace camera3d::ipc
