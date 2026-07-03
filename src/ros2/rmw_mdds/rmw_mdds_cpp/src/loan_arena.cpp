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
#include <limits>

namespace rmw_mdds_cpp
{
namespace
{
bool IsPowerOfTwo(size_t value)
{
  return value != 0u && (value & (value - 1u)) == 0u;
}

bool AddOverflows(uintptr_t lhs, size_t rhs)
{
  return rhs > std::numeric_limits<uintptr_t>::max() - lhs;
}
}  // namespace

MddsLoanArena::MddsLoanArena(void * storage, size_t capacity)
{
  Reset(storage, capacity);
}

void MddsLoanArena::Reset(void * storage, size_t capacity)
{
  begin_ = static_cast<uint8_t *>(storage);
  capacity_ = begin_ == nullptr ? 0u : capacity;
  offset_ = 0u;
  segment_count_ = 0u;
}

void MddsLoanArena::Reset()
{
  offset_ = 0u;
  segment_count_ = 0u;
}

void * MddsLoanArena::Allocate(size_t size, size_t alignment)
{
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
  const uintptr_t aligned = (current + alignment - 1u) & ~(static_cast<uintptr_t>(alignment) - 1u);
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

bool MddsLoanArena::Contains(const void * data, size_t size) const
{
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

size_t MddsLoanArena::BytesUsed() const
{
  return offset_;
}

size_t MddsLoanArena::SegmentCount() const
{
  return segment_count_;
}

void * MddsLoanArena::Data() const
{
  return begin_;
}

size_t MddsLoanArena::Capacity() const
{
  return capacity_;
}

}  // namespace rmw_mdds_cpp
