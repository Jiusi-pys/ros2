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

#include "loan_arena.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <new>

namespace rmw_mdds_cpp {
namespace {
bool IsPowerOfTwo(size_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

bool AddOverflows(uintptr_t lhs, size_t rhs) {
  return rhs > std::numeric_limits<uintptr_t>::max() - lhs;
}

struct LoanAllocationRecord {
  void *ptr = nullptr;
};

std::mutex &LoanAllocationMutex() {
  static std::mutex mutex;
  return mutex;
}

std::array<LoanAllocationRecord, 1024> &LoanAllocations() {
  static std::array<LoanAllocationRecord, 1024> allocations{};
  return allocations;
}

thread_local MddsLoanArena *g_active_loan_arena = nullptr;
thread_local size_t g_active_loan_allocation_count = 0u;
thread_local size_t g_active_loan_alignment = alignof(std::max_align_t);

void RegisterLoanAllocation(void *ptr) {
  if (ptr == nullptr) {
    return;
  }
  MddsLoanArena *saved_arena = g_active_loan_arena;
  const size_t saved_count = g_active_loan_allocation_count;
  const size_t saved_alignment = g_active_loan_alignment;
  g_active_loan_arena = nullptr;
  g_active_loan_allocation_count = 0u;
  g_active_loan_alignment = alignof(std::max_align_t);
  bool registered = false;
  {
    std::lock_guard<std::mutex> lock(LoanAllocationMutex());
    for (LoanAllocationRecord &record : LoanAllocations()) {
      if (record.ptr == nullptr) {
        record.ptr = ptr;
        registered = true;
        break;
      }
    }
  }
  g_active_loan_arena = saved_arena;
  g_active_loan_allocation_count = saved_count;
  g_active_loan_alignment = saved_alignment;
  if (!registered) {
    throw std::bad_alloc();
  }
}

bool ReleaseLoanAllocation(void *ptr) {
  if (ptr == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(LoanAllocationMutex());
  auto &allocations = LoanAllocations();
  const auto it = std::find_if(
      allocations.begin(), allocations.end(),
      [ptr](const LoanAllocationRecord &record) { return record.ptr == ptr; });
  if (it == allocations.end()) {
    return false;
  }
  it->ptr = nullptr;
  return true;
}

void *TryAllocateFromActiveLoanArena(size_t size) {
  if (g_active_loan_arena == nullptr || g_active_loan_allocation_count == 0u) {
    return nullptr;
  }
  MddsLoanArena *arena = g_active_loan_arena;
  --g_active_loan_allocation_count;
  if (g_active_loan_allocation_count == 0u) {
    g_active_loan_arena = nullptr;
  }
  void *ptr = arena->Allocate(size, g_active_loan_alignment);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  RegisterLoanAllocation(ptr);
  return ptr;
}
} // namespace

MddsLoanArena::MddsLoanArena(void *storage, size_t capacity) {
  Reset(storage, capacity);
}

void MddsLoanArena::Reset(void *storage, size_t capacity) {
  begin_ = static_cast<uint8_t *>(storage);
  capacity_ = begin_ == nullptr ? 0u : capacity;
  offset_ = 0u;
  segment_count_ = 0u;
}

void MddsLoanArena::Reset() {
  offset_ = 0u;
  segment_count_ = 0u;
}

void *MddsLoanArena::Allocate(size_t size, size_t alignment) {
  if (begin_ == nullptr || size == 0u || !IsPowerOfTwo(alignment)) {
    return nullptr;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(begin_);
  if (AddOverflows(base, offset_)) {
    return nullptr;
  }
  const uintptr_t current = base + offset_;
  if (AddOverflows(current, alignment - 1u)) {
    return nullptr;
  }
  const uintptr_t aligned =
      (current + alignment - 1u) & ~(static_cast<uintptr_t>(alignment) - 1u);
  if (aligned < base || AddOverflows(aligned, size)) {
    return nullptr;
  }
  const uintptr_t end = aligned + size;
  if (AddOverflows(base, capacity_) || end > base + capacity_) {
    return nullptr;
  }
  offset_ = static_cast<size_t>(end - base);
  ++segment_count_;
  return reinterpret_cast<void *>(aligned);
}

bool MddsLoanArena::Contains(const void *data, size_t size) const {
  if (begin_ == nullptr || data == nullptr) {
    return false;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(begin_);
  const uintptr_t ptr = reinterpret_cast<uintptr_t>(data);
  if (ptr < base || AddOverflows(ptr, size) || AddOverflows(base, capacity_)) {
    return false;
  }
  return ptr + size <= base + capacity_;
}

size_t MddsLoanArena::BytesUsed() const { return offset_; }

size_t MddsLoanArena::SegmentCount() const { return segment_count_; }

void *MddsLoanArena::Data() const { return begin_; }

size_t MddsLoanArena::Capacity() const { return capacity_; }

void ArmLoanArenaForNextAllocation(MddsLoanArena *arena,
                                   size_t allocation_count, size_t alignment) {
  g_active_loan_arena = allocation_count == 0u ? nullptr : arena;
  g_active_loan_allocation_count =
      g_active_loan_arena == nullptr ? 0u : allocation_count;
  g_active_loan_alignment =
      alignment == 0u ? alignof(std::max_align_t) : alignment;
}

void DisarmLoanArenaAllocation(const MddsLoanArena *arena) {
  if (g_active_loan_arena == arena) {
    g_active_loan_arena = nullptr;
    g_active_loan_allocation_count = 0u;
    g_active_loan_alignment = alignof(std::max_align_t);
  }
}

} // namespace rmw_mdds_cpp

void *operator new(std::size_t size) {
  if (void *ptr = rmw_mdds_cpp::TryAllocateFromActiveLoanArena(size)) {
    return ptr;
  }
  void *ptr = std::malloc(size == 0u ? 1u : size);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void *operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void *ptr) noexcept {
  if (rmw_mdds_cpp::ReleaseLoanAllocation(ptr)) {
    return;
  }
  std::free(ptr);
}

void operator delete[](void *ptr) noexcept { ::operator delete(ptr); }

void operator delete(void *ptr, std::size_t) noexcept {
  ::operator delete(ptr);
}

void operator delete[](void *ptr, std::size_t) noexcept {
  ::operator delete(ptr);
}
