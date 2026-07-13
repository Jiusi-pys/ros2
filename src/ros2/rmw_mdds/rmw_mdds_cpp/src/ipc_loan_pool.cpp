// Copyright 2026 Kaihong Digital Industry Development Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ipc_loan_pool.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

namespace rmw_mdds_cpp
{
namespace ipc
{
namespace
{

constexpr uint32_t kFixedLoanPoolMagic = 0x314c504du;  // "MPL1"
constexpr uint32_t kDynamicLoanPoolMagic = 0x324c504du;  // "MPL2"
constexpr size_t kSlotAlignment = 64u;

struct FixedLoanPoolHeader
{
  uint32_t magic;
  uint32_t version;
  uint64_t generation;
  uint32_t slot_size;
  uint32_t slot_count;
  uint32_t payload_offset;
  uint32_t slot_stride;
};

struct DynamicLoanPoolHeader
{
  uint32_t magic;
  uint32_t version;
  uint64_t generation;
  uint32_t flags;
  uint32_t payload_capacity;
  uint32_t arena_capacity;
  uint32_t slot_count;
  uint32_t payload_offset;
  uint32_t arena_offset;
  uint32_t slot_stride;
};

struct ComputedLayout
{
  size_t payload_offset = 0u;
  size_t arena_offset = 0u;
  size_t slot_stride = 0u;
  size_t mapping_size = 0u;
};

void SetError(std::string * error, const std::string & message)
{
  if (error != nullptr) {
    *error = message;
  }
}

std::string ErrnoMessage(const char * action)
{
  return std::string(action) + ": " + std::strerror(errno);
}

bool AlignUp(size_t value, size_t alignment, size_t * aligned)
{
  if (
    aligned == nullptr || alignment == 0u ||
    value > std::numeric_limits<size_t>::max() - (alignment - 1u))
  {
    return false;
  }
  *aligned = (value + alignment - 1u) & ~(alignment - 1u);
  return true;
}

size_t SystemPageSize()
{
  const long page_size = sysconf(_SC_PAGESIZE);
  return page_size > 0 ? static_cast<size_t>(page_size) : 4096u;
}

bool ComputeFixedLayout(
  uint32_t slot_size, uint32_t slot_count, ComputedLayout * layout)
{
  if (
    slot_size == 0u || slot_count == 0u || slot_count > kMaxLoanPoolSlotCount ||
    layout == nullptr)
  {
    return false;
  }
  if (!AlignUp(sizeof(FixedLoanPoolHeader), SystemPageSize(), &layout->payload_offset) ||
    !AlignUp(slot_size, kSlotAlignment, &layout->slot_stride))
  {
    return false;
  }
  if (
    layout->slot_stride >
    (std::numeric_limits<size_t>::max() - layout->payload_offset) / slot_count)
  {
    return false;
  }
  layout->mapping_size = layout->payload_offset + layout->slot_stride * slot_count;
  return true;
}

bool ComputeDynamicLayout(
  uint32_t payload_capacity, uint32_t arena_capacity, uint32_t slot_count,
  ComputedLayout * layout)
{
  if (
    payload_capacity == 0u || payload_capacity > kDefaultDynamicLoanPayloadCapacity ||
    arena_capacity == 0u || arena_capacity > kDefaultDynamicLoanArenaCapacity ||
    slot_count == 0u || slot_count > kMaxDynamicLoanPoolSlotCount || layout == nullptr)
  {
    return false;
  }
  const size_t page = SystemPageSize();
  size_t payload_stride = 0u;
  size_t arena_stride = 0u;
  if (
    !AlignUp(sizeof(DynamicLoanPoolHeader), page, &layout->payload_offset) ||
    !AlignUp(payload_capacity, page, &payload_stride) ||
    !AlignUp(arena_capacity, page, &arena_stride) ||
    payload_stride > std::numeric_limits<size_t>::max() - layout->payload_offset)
  {
    return false;
  }
  layout->arena_offset = layout->payload_offset + payload_stride;
  if (payload_stride > std::numeric_limits<size_t>::max() - arena_stride) {
    return false;
  }
  layout->slot_stride = payload_stride + arena_stride;
  if (
    layout->slot_stride >
    (std::numeric_limits<size_t>::max() - layout->payload_offset) / slot_count)
  {
    return false;
  }
  layout->mapping_size = layout->payload_offset + layout->slot_stride * slot_count;
  return true;
}

bool ComputeLayout(const LoanPoolDescriptor & descriptor, ComputedLayout * layout)
{
  if (layout == nullptr) {
    return false;
  }
  *layout = ComputedLayout{};
  if (descriptor.version == kFixedLoanPoolVersion) {
    return ComputeFixedLayout(descriptor.slot_size, descriptor.slot_count, layout);
  }
  if (descriptor.version == kDynamicLoanPoolVersion) {
    return ComputeDynamicLayout(
      descriptor.slot_size, descriptor.arena_size, descriptor.slot_count, layout);
  }
  return false;
}

std::string ParentDirectory(const std::string & socket_path)
{
  const size_t slash = socket_path.find_last_of('/');
  if (slash == std::string::npos) {
    return "/tmp";
  }
  return slash == 0u ? "/" : socket_path.substr(0u, slash);
}

uint64_t MakeGeneration(uint64_t broker_id, uint64_t entity_id)
{
  uint64_t generation = broker_id ^ (entity_id + 0x9e3779b97f4a7c15ull);
  generation ^= static_cast<uint64_t>(getpid()) << 32u;
  generation ^= static_cast<uint64_t>(
    std::chrono::steady_clock::now().time_since_epoch().count());
  return generation == 0u ? 1u : generation;
}

std::string MakePoolPath(
  const std::string & socket_path, uint64_t broker_id, uint64_t entity_id,
  uint64_t generation)
{
  char name[160];
  const int written = std::snprintf(
    name, sizeof(name), "rmw_mdds_loan_%016llx_%016llx_%016llx.pool",
    static_cast<unsigned long long>(broker_id),
    static_cast<unsigned long long>(entity_id),
    static_cast<unsigned long long>(generation));
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(name)) {
    return std::string();
  }
  const std::string directory = ParentDirectory(socket_path);
  return directory == "/" ? directory + name : directory + "/" + name;
}

bool ValidateDescriptor(const LoanPoolDescriptor & descriptor, std::string * error)
{
  if (descriptor.path.empty() || descriptor.path.front() != '/') {
    SetError(error, "loan pool path must be absolute");
    return false;
  }
  if (descriptor.generation == 0u) {
    SetError(error, "loan pool descriptor has an invalid generation");
    return false;
  }
  if (descriptor.version == kFixedLoanPoolVersion) {
    if (
      descriptor.flags != 0u || descriptor.arena_size != 0u ||
      descriptor.slot_size == 0u || descriptor.slot_count == 0u ||
      descriptor.slot_count > kMaxLoanPoolSlotCount)
    {
      SetError(error, "fixed loan pool descriptor has invalid dimensions or flags");
      return false;
    }
    return true;
  }
  if (descriptor.version == kDynamicLoanPoolVersion) {
    if (
      descriptor.flags != kLoanPoolFlagTypedArena || descriptor.slot_size == 0u ||
      descriptor.slot_size > kDefaultDynamicLoanPayloadCapacity ||
      descriptor.arena_size == 0u ||
      descriptor.arena_size > kDefaultDynamicLoanArenaCapacity ||
      descriptor.slot_count == 0u ||
      descriptor.slot_count > kMaxDynamicLoanPoolSlotCount)
    {
      SetError(error, "dynamic loan pool descriptor has invalid dimensions or flags");
      return false;
    }
    return true;
  }
  SetError(error, "unsupported loan pool descriptor version");
  return false;
}

bool FitsU32(size_t value)
{
  return value <= std::numeric_limits<uint32_t>::max();
}

bool HeaderMatchesDescriptor(
  const void * mapping, size_t mapping_size, const LoanPoolDescriptor & descriptor,
  const ComputedLayout & layout)
{
  if (mapping == nullptr || mapping_size != layout.mapping_size) {
    return false;
  }
  if (descriptor.version == kFixedLoanPoolVersion) {
    if (mapping_size < sizeof(FixedLoanPoolHeader)) {
      return false;
    }
    const auto * header = static_cast<const FixedLoanPoolHeader *>(mapping);
    return header->magic == kFixedLoanPoolMagic &&
           header->version == kFixedLoanPoolVersion &&
           header->generation == descriptor.generation &&
           header->slot_size == descriptor.slot_size &&
           header->slot_count == descriptor.slot_count &&
           header->payload_offset == layout.payload_offset &&
           header->slot_stride == layout.slot_stride;
  }
  if (descriptor.version == kDynamicLoanPoolVersion) {
    if (mapping_size < sizeof(DynamicLoanPoolHeader)) {
      return false;
    }
    const auto * header = static_cast<const DynamicLoanPoolHeader *>(mapping);
    return header->magic == kDynamicLoanPoolMagic &&
           header->version == kDynamicLoanPoolVersion &&
           header->generation == descriptor.generation &&
           header->flags == descriptor.flags &&
           header->payload_capacity == descriptor.slot_size &&
           header->arena_capacity == descriptor.arena_size &&
           header->slot_count == descriptor.slot_count &&
           header->payload_offset == layout.payload_offset &&
           header->arena_offset == layout.arena_offset &&
           header->slot_stride == layout.slot_stride;
  }
  return false;
}

}  // namespace

LoanPoolOwner::~LoanPoolOwner()
{
  Reset();
}

bool LoanPoolOwner::Create(
  const std::string & socket_path, uint64_t broker_id, uint64_t entity_id,
  uint32_t slot_size, uint32_t slot_count, std::string * error)
{
  LoanPoolDescriptor descriptor;
  descriptor.version = kFixedLoanPoolVersion;
  descriptor.slot_size = slot_size;
  descriptor.slot_count = slot_count;
  return CreateInternal(socket_path, broker_id, entity_id, std::move(descriptor), error);
}

bool LoanPoolOwner::CreateDynamic(
  const std::string & socket_path, uint64_t broker_id, uint64_t entity_id,
  uint32_t payload_capacity, uint32_t arena_capacity, uint32_t slot_count,
  std::string * error)
{
  LoanPoolDescriptor descriptor;
  descriptor.version = kDynamicLoanPoolVersion;
  descriptor.flags = kLoanPoolFlagTypedArena;
  descriptor.slot_size = payload_capacity;
  descriptor.arena_size = arena_capacity;
  descriptor.slot_count = slot_count;
  return CreateInternal(socket_path, broker_id, entity_id, std::move(descriptor), error);
}

bool LoanPoolOwner::CreateInternal(
  const std::string & socket_path, uint64_t broker_id, uint64_t entity_id,
  LoanPoolDescriptor descriptor, std::string * error)
{
  Reset();
  descriptor.generation = MakeGeneration(broker_id, entity_id);
  descriptor.path = MakePoolPath(socket_path, broker_id, entity_id, descriptor.generation);
  if (!ValidateDescriptor(descriptor, error)) {
    return false;
  }
  ComputedLayout layout;
  if (!ComputeLayout(descriptor, &layout) ||
    !FitsU32(layout.payload_offset) || !FitsU32(layout.arena_offset) ||
    !FitsU32(layout.slot_stride) ||
    layout.mapping_size > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
  {
    SetError(error, "loan pool dimensions overflow");
    return false;
  }

  int flags = O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  const int fd = open(descriptor.path.c_str(), flags, 0600);
  if (fd < 0) {
    SetError(error, ErrnoMessage("create loan pool failed"));
    return false;
  }
  if (ftruncate(fd, static_cast<off_t>(layout.mapping_size)) != 0) {
    SetError(error, ErrnoMessage("resize loan pool failed"));
    close(fd);
    unlink(descriptor.path.c_str());
    return false;
  }
  void * mapping = mmap(
    nullptr, layout.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) {
    SetError(error, ErrnoMessage("map loan pool failed"));
    close(fd);
    unlink(descriptor.path.c_str());
    return false;
  }

  std::memset(mapping, 0, layout.payload_offset);
  if (descriptor.version == kFixedLoanPoolVersion) {
    auto * header = static_cast<FixedLoanPoolHeader *>(mapping);
    header->magic = kFixedLoanPoolMagic;
    header->version = kFixedLoanPoolVersion;
    header->generation = descriptor.generation;
    header->slot_size = descriptor.slot_size;
    header->slot_count = descriptor.slot_count;
    header->payload_offset = static_cast<uint32_t>(layout.payload_offset);
    header->slot_stride = static_cast<uint32_t>(layout.slot_stride);
  } else {
    auto * header = static_cast<DynamicLoanPoolHeader *>(mapping);
    header->magic = kDynamicLoanPoolMagic;
    header->version = kDynamicLoanPoolVersion;
    header->generation = descriptor.generation;
    header->flags = descriptor.flags;
    header->payload_capacity = descriptor.slot_size;
    header->arena_capacity = descriptor.arena_size;
    header->slot_count = descriptor.slot_count;
    header->payload_offset = static_cast<uint32_t>(layout.payload_offset);
    header->arena_offset = static_cast<uint32_t>(layout.arena_offset);
    header->slot_stride = static_cast<uint32_t>(layout.slot_stride);
  }
  if (msync(mapping, layout.payload_offset, MS_SYNC) != 0) {
    SetError(error, ErrnoMessage("publish loan pool header failed"));
    munmap(mapping, layout.mapping_size);
    close(fd);
    unlink(descriptor.path.c_str());
    return false;
  }

  fd_ = fd;
  mapping_ = mapping;
  mapping_size_ = layout.mapping_size;
  payload_offset_ = layout.payload_offset;
  arena_offset_ = layout.arena_offset;
  slot_stride_ = layout.slot_stride;
  descriptor_ = std::move(descriptor);
  slots_.assign(descriptor_.slot_count, Slot{});
  next_loan_id_ = 1u;
  return true;
}

bool LoanPoolOwner::Store(
  const uint8_t * payload, size_t payload_size, uint64_t * loan_id,
  uint32_t * slot_index, std::string * error, bool * pool_full)
{
  if (pool_full != nullptr) {
    *pool_full = false;
  }
  if (
    loan_id == nullptr || slot_index == nullptr || mapping_ == nullptr ||
    (payload == nullptr && payload_size != 0u) || payload_size > descriptor_.slot_size ||
    (descriptor_.version == kFixedLoanPoolVersion && payload_size == 0u))
  {
    SetError(error, "loan pool store arguments are invalid");
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (uint32_t i = 0u; i < slots_.size(); ++i) {
    Slot & slot = slots_[i];
    if (slot.in_use) {
      continue;
    }
    uint64_t id = next_loan_id_++;
    if (id == 0u) {
      id = next_loan_id_++;
    }
    auto * destination = static_cast<uint8_t *>(mapping_) + payload_offset_ + slot_stride_ * i;
    if (payload_size != 0u) {
      std::memcpy(destination, payload, payload_size);
    }
    std::atomic_thread_fence(std::memory_order_release);
    slot.loan_id = id;
    slot.in_use = true;
    *loan_id = id;
    *slot_index = i;
    return true;
  }
  if (pool_full != nullptr) {
    *pool_full = true;
  }
  SetError(error, "loan pool has no free slots");
  return false;
}

bool LoanPoolOwner::Release(uint64_t loan_id)
{
  if (loan_id == 0u) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (Slot & slot : slots_) {
    if (slot.in_use && slot.loan_id == loan_id) {
      slot.in_use = false;
      slot.loan_id = 0u;
      return true;
    }
  }
  return false;
}

void LoanPoolOwner::Reset()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (mapping_ != nullptr) {
    munmap(mapping_, mapping_size_);
  }
  if (fd_ >= 0) {
    close(fd_);
  }
  if (!descriptor_.path.empty()) {
    unlink(descriptor_.path.c_str());
  }
  fd_ = -1;
  mapping_ = nullptr;
  mapping_size_ = 0u;
  payload_offset_ = 0u;
  arena_offset_ = 0u;
  slot_stride_ = 0u;
  descriptor_ = LoanPoolDescriptor{};
  slots_.clear();
  next_loan_id_ = 1u;
}

const LoanPoolDescriptor & LoanPoolOwner::descriptor() const
{
  return descriptor_;
}

LoanPoolMapping::~LoanPoolMapping()
{
  Reset();
}

bool LoanPoolMapping::Open(const LoanPoolDescriptor & descriptor, std::string * error)
{
  Reset();
  if (!ValidateDescriptor(descriptor, error)) {
    return false;
  }
  int flags = (descriptor.version == kDynamicLoanPoolVersion ? O_RDWR : O_RDONLY) | O_CLOEXEC;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  const int fd = open(descriptor.path.c_str(), flags);
  if (fd < 0) {
    SetError(error, ErrnoMessage("open loan pool failed"));
    return false;
  }
  struct stat st {};
  if (
    fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (st.st_mode & 0077) != 0 ||
    st.st_size < static_cast<off_t>(sizeof(FixedLoanPoolHeader)))
  {
    SetError(error, "loan pool backing object is invalid");
    close(fd);
    return false;
  }
  ComputedLayout layout;
  if (!ComputeLayout(descriptor, &layout) ||
    static_cast<uint64_t>(st.st_size) != static_cast<uint64_t>(layout.mapping_size))
  {
    SetError(error, "loan pool backing size does not match registration descriptor");
    close(fd);
    return false;
  }
  const size_t mapping_size = layout.mapping_size;
  void * mapping = mmap(nullptr, mapping_size, PROT_READ, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) {
    SetError(error, ErrnoMessage("map loan pool read-only failed"));
    close(fd);
    return false;
  }
  if (!HeaderMatchesDescriptor(mapping, mapping_size, descriptor, layout)) {
    SetError(error, "loan pool header does not match registration descriptor");
    munmap(mapping, mapping_size);
    close(fd);
    return false;
  }
  if (descriptor.version == kDynamicLoanPoolVersion) {
    for (uint32_t slot = 0u; slot < descriptor.slot_count; ++slot) {
      auto * arena = static_cast<uint8_t *>(mapping) + layout.arena_offset +
        layout.slot_stride * slot;
      if (mprotect(arena, descriptor.arena_size, PROT_READ | PROT_WRITE) != 0) {
        SetError(error, ErrnoMessage("make loan pool typed arena writable failed"));
        munmap(mapping, mapping_size);
        close(fd);
        return false;
      }
    }
  }

  fd_ = fd;
  mapping_ = mapping;
  mapping_size_ = mapping_size;
  payload_offset_ = layout.payload_offset;
  arena_offset_ = layout.arena_offset;
  slot_stride_ = layout.slot_stride;
  descriptor_ = descriptor;
  return true;
}

const void * LoanPoolMapping::Resolve(
  uint64_t generation, uint32_t slot_index, uint32_t payload_size,
  std::string * error) const
{
  if (
    mapping_ == nullptr || generation != descriptor_.generation ||
    slot_index >= descriptor_.slot_count || payload_size > descriptor_.slot_size ||
    (descriptor_.version == kFixedLoanPoolVersion && payload_size == 0u))
  {
    SetError(error, "loaned sample descriptor is outside the registered pool");
    return nullptr;
  }
  std::atomic_thread_fence(std::memory_order_acquire);
  return static_cast<const uint8_t *>(mapping_) + payload_offset_ + slot_stride_ * slot_index;
}

void * LoanPoolMapping::ResolveArena(
  uint64_t generation, uint32_t slot_index, std::string * error) const
{
  if (
    mapping_ == nullptr || descriptor_.version != kDynamicLoanPoolVersion ||
    generation != descriptor_.generation || slot_index >= descriptor_.slot_count)
  {
    SetError(error, "typed arena descriptor is outside the registered pool");
    return nullptr;
  }
  std::atomic_thread_fence(std::memory_order_acquire);
  return static_cast<uint8_t *>(mapping_) + arena_offset_ + slot_stride_ * slot_index;
}

bool LoanPoolMapping::Contains(const void * data, size_t size) const
{
  if (mapping_ == nullptr || data == nullptr) {
    return false;
  }
  const uintptr_t begin = reinterpret_cast<uintptr_t>(mapping_);
  if (mapping_size_ > std::numeric_limits<uintptr_t>::max() - begin) {
    return false;
  }
  const uintptr_t end = begin + mapping_size_;
  const uintptr_t value = reinterpret_cast<uintptr_t>(data);
  return end >= begin && value >= begin && value <= end && size <= end - value;
}

bool LoanPoolMapping::ContainsArena(uint32_t slot_index, const void * data, size_t size) const
{
  if (
    mapping_ == nullptr || descriptor_.version != kDynamicLoanPoolVersion ||
    slot_index >= descriptor_.slot_count || data == nullptr)
  {
    return false;
  }
  const uintptr_t begin = reinterpret_cast<uintptr_t>(mapping_) + arena_offset_ +
    slot_stride_ * slot_index;
  if (descriptor_.arena_size > std::numeric_limits<uintptr_t>::max() - begin) {
    return false;
  }
  const uintptr_t end = begin + descriptor_.arena_size;
  const uintptr_t value = reinterpret_cast<uintptr_t>(data);
  return value >= begin && value <= end && size <= end - value;
}

void LoanPoolMapping::Reset()
{
  if (mapping_ != nullptr) {
    munmap(mapping_, mapping_size_);
  }
  if (fd_ >= 0) {
    close(fd_);
  }
  fd_ = -1;
  mapping_ = nullptr;
  mapping_size_ = 0u;
  payload_offset_ = 0u;
  arena_offset_ = 0u;
  slot_stride_ = 0u;
  descriptor_ = LoanPoolDescriptor{};
}

bool LoanPoolMapping::valid() const
{
  return mapping_ != nullptr;
}

bool LoanPoolMapping::dynamic() const
{
  return valid() && descriptor_.version == kDynamicLoanPoolVersion;
}

uint32_t LoanPoolMapping::arena_capacity() const
{
  return dynamic() ? descriptor_.arena_size : 0u;
}

}  // namespace ipc
}  // namespace rmw_mdds_cpp
