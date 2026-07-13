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

#ifndef RMW_MDDS_CPP_SRC__IPC_LOAN_POOL_HPP_
#define RMW_MDDS_CPP_SRC__IPC_LOAN_POOL_HPP_

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rmw_mdds_cpp
{
namespace ipc
{

constexpr uint32_t kDefaultLoanPoolSlotCount = 32u;
constexpr uint32_t kMaxLoanPoolSlotCount = 1024u;
constexpr uint32_t kFixedLoanPoolVersion = 1u;
constexpr uint32_t kDynamicLoanPoolVersion = 2u;
constexpr uint32_t kLoanPoolFlagTypedArena = 1u << 0u;
constexpr uint32_t kDefaultDynamicLoanPoolSlotCount = 2u;
constexpr uint32_t kMaxDynamicLoanPoolSlotCount = 8u;
constexpr uint32_t kDefaultDynamicLoanPayloadCapacity = 16u * 1024u * 1024u;
constexpr uint32_t kDefaultDynamicLoanArenaCapacity = 32u * 1024u * 1024u;

struct LoanPoolDescriptor
{
  std::string path;
  uint64_t generation = 0u;
  uint32_t version = kFixedLoanPoolVersion;
  uint32_t flags = 0u;
  uint32_t slot_size = 0u;
  uint32_t arena_size = 0u;
  uint32_t slot_count = 0u;
};

class LoanPoolOwner
{
public:
  LoanPoolOwner() = default;
  ~LoanPoolOwner();

  LoanPoolOwner(const LoanPoolOwner &) = delete;
  LoanPoolOwner & operator=(const LoanPoolOwner &) = delete;

  bool Create(
    const std::string & socket_path, uint64_t broker_id, uint64_t entity_id,
    uint32_t slot_size, uint32_t slot_count, std::string * error);
  bool CreateDynamic(
    const std::string & socket_path, uint64_t broker_id, uint64_t entity_id,
    uint32_t payload_capacity, uint32_t arena_capacity, uint32_t slot_count,
    std::string * error);
  bool Store(
    const uint8_t * payload, size_t payload_size, uint64_t * loan_id,
    uint32_t * slot_index, std::string * error, bool * pool_full = nullptr);
  bool Release(uint64_t loan_id);
  void Reset();

  const LoanPoolDescriptor & descriptor() const;

private:
  bool CreateInternal(
    const std::string & socket_path, uint64_t broker_id, uint64_t entity_id,
    LoanPoolDescriptor descriptor, std::string * error);

  struct Slot
  {
    uint64_t loan_id = 0u;
    bool in_use = false;
  };

  int fd_ = -1;
  void * mapping_ = nullptr;
  size_t mapping_size_ = 0u;
  size_t payload_offset_ = 0u;
  size_t arena_offset_ = 0u;
  size_t slot_stride_ = 0u;
  LoanPoolDescriptor descriptor_;
  std::vector<Slot> slots_;
  uint64_t next_loan_id_ = 1u;
  std::mutex mutex_;
};

class LoanPoolMapping
{
public:
  LoanPoolMapping() = default;
  ~LoanPoolMapping();

  LoanPoolMapping(const LoanPoolMapping &) = delete;
  LoanPoolMapping & operator=(const LoanPoolMapping &) = delete;

  bool Open(const LoanPoolDescriptor & descriptor, std::string * error);
  const void * Resolve(
    uint64_t generation, uint32_t slot_index, uint32_t payload_size,
    std::string * error) const;
  void * ResolveArena(
    uint64_t generation, uint32_t slot_index, std::string * error) const;
  bool Contains(const void * data, size_t size) const;
  bool ContainsArena(uint32_t slot_index, const void * data, size_t size) const;
  void Reset();

  bool valid() const;
  bool dynamic() const;
  uint32_t arena_capacity() const;

private:
  int fd_ = -1;
  void * mapping_ = nullptr;
  size_t mapping_size_ = 0u;
  size_t payload_offset_ = 0u;
  size_t arena_offset_ = 0u;
  size_t slot_stride_ = 0u;
  LoanPoolDescriptor descriptor_;
};

}  // namespace ipc
}  // namespace rmw_mdds_cpp

#endif  // RMW_MDDS_CPP_SRC__IPC_LOAN_POOL_HPP_
