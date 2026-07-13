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

#ifndef RMW_MDDS_CPP_SRC__LOAN_ARENA_HPP_
#define RMW_MDDS_CPP_SRC__LOAN_ARENA_HPP_

#include <cstddef>
#include <cstdint>

#include "rosidl_runtime_cpp/message_allocator.hpp"

namespace rmw_mdds_cpp
{

class MddsLoanArena {
public:
  MddsLoanArena() = default;
  MddsLoanArena(void *storage, size_t capacity);

  void Reset(void *storage, size_t capacity);
  void Reset();

  void * Allocate(size_t size, size_t alignment);
  bool Contains(const void *data, size_t size) const;

  size_t BytesUsed() const;
  size_t SegmentCount() const;
  void * Data() const;
  size_t Capacity() const;

private:
  uint8_t *begin_ = nullptr;
  size_t capacity_ = 0;
  size_t offset_ = 0;
  size_t segment_count_ = 0;
};

class MddsLoanMemoryResource {
public:
  MddsLoanMemoryResource(void *storage, size_t capacity);

  void * Allocate(size_t size, size_t alignment);
  bool Contains(const void *data, size_t size) const;
  size_t BytesUsed() const;
  size_t SegmentCount() const;
  const rosidl_runtime_cpp::MessageMemoryResource * Resource() const;

private:
  static void * AllocateCallback(void *state, size_t size, size_t alignment);
  static void DeallocateCallback(
    void *state, void *pointer, size_t size,
    size_t alignment);

  MddsLoanArena arena_;
  rosidl_runtime_cpp::MessageMemoryResource resource_{};
};

} // namespace rmw_mdds_cpp

#endif // RMW_MDDS_CPP_SRC__LOAN_ARENA_HPP_
