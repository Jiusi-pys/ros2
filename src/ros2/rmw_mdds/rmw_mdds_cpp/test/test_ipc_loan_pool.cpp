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

#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include "ipc_loan_pool.hpp"

namespace
{

class TempLoanDirectory
{
public:
  TempLoanDirectory()
  {
    char path[] = "/tmp/rmw_mdds_loan_pool_XXXXXX";
    char * created = mkdtemp(path);
    if (created != nullptr) {
      directory_ = created;
      socket_path_ = directory_ + "/broker.sock";
    }
  }

  ~TempLoanDirectory()
  {
    if (!directory_.empty()) {
      rmdir(directory_.c_str());
    }
  }

  const std::string & socket_path() const
  {
    return socket_path_;
  }

private:
  std::string directory_;
  std::string socket_path_;
};

TEST(RmwMddsIpcLoanPool, KeepsSlotsPinnedUntilExactLoanReturn)
{
  TempLoanDirectory temp;
  ASSERT_FALSE(temp.socket_path().empty());

  rmw_mdds_cpp::ipc::LoanPoolOwner owner;
  std::string error;
  ASSERT_TRUE(owner.Create(temp.socket_path(), 0x1111u, 0x2222u, 4u, 2u, &error)) << error;
  const rmw_mdds_cpp::ipc::LoanPoolDescriptor descriptor = owner.descriptor();
  struct stat st {};
  ASSERT_EQ(0, stat(descriptor.path.c_str(), &st));
  EXPECT_EQ(0600, st.st_mode & 0777);

  rmw_mdds_cpp::ipc::LoanPoolMapping mapping;
  ASSERT_TRUE(mapping.Open(descriptor, &error)) << error;

  const std::array<uint8_t, 4> first{{1u, 2u, 3u, 4u}};
  const std::array<uint8_t, 4> second{{5u, 6u, 7u, 8u}};
  uint64_t first_loan = 0u;
  uint32_t first_slot = 0u;
  ASSERT_TRUE(owner.Store(first.data(), first.size(), &first_loan, &first_slot, &error)) << error;
  const void * first_data = mapping.Resolve(
    descriptor.generation, first_slot, first.size(), &error);
  ASSERT_NE(nullptr, first_data) << error;
  EXPECT_EQ(0, std::memcmp(first.data(), first_data, first.size()));
  EXPECT_TRUE(mapping.Contains(first_data, first.size()));

  uint64_t second_loan = 0u;
  uint32_t second_slot = 0u;
  ASSERT_TRUE(owner.Store(second.data(), second.size(), &second_loan, &second_slot, &error)) << error;
  EXPECT_NE(first_slot, second_slot);

  uint64_t exhausted_loan = 0u;
  uint32_t exhausted_slot = 0u;
  EXPECT_FALSE(owner.Store(
    first.data(), first.size(), &exhausted_loan, &exhausted_slot, &error));
  EXPECT_TRUE(owner.Release(first_loan));
  EXPECT_FALSE(owner.Release(first_loan));
  EXPECT_TRUE(owner.Store(
    first.data(), first.size(), &exhausted_loan, &exhausted_slot, &error)) << error;
  EXPECT_EQ(first_slot, exhausted_slot);

  EXPECT_EQ(
    nullptr,
    mapping.Resolve(descriptor.generation + 1u, exhausted_slot, first.size(), &error));
  EXPECT_EQ(
    nullptr,
    mapping.Resolve(descriptor.generation, descriptor.slot_count, first.size(), &error));

  EXPECT_TRUE(owner.Release(second_loan));
  EXPECT_TRUE(owner.Release(exhausted_loan));
  owner.Reset();
  EXPECT_NE(0, access(descriptor.path.c_str(), F_OK));
  EXPECT_EQ(0, std::memcmp(first.data(), first_data, first.size()));
}

TEST(RmwMddsIpcLoanPool, RejectsDescriptorGenerationMismatch)
{
  TempLoanDirectory temp;
  ASSERT_FALSE(temp.socket_path().empty());

  rmw_mdds_cpp::ipc::LoanPoolOwner owner;
  std::string error;
  ASSERT_TRUE(owner.Create(temp.socket_path(), 0x3333u, 0x4444u, 8u, 1u, &error)) << error;
  rmw_mdds_cpp::ipc::LoanPoolDescriptor stale = owner.descriptor();
  ++stale.generation;

  rmw_mdds_cpp::ipc::LoanPoolMapping mapping;
  EXPECT_FALSE(mapping.Open(stale, &error));
  EXPECT_FALSE(mapping.valid());
}

TEST(RmwMddsIpcLoanPool, DynamicPoolSeparatesPayloadAndWritableTypedArena)
{
  TempLoanDirectory temp;
  ASSERT_FALSE(temp.socket_path().empty());

  rmw_mdds_cpp::ipc::LoanPoolOwner owner;
  std::string error;
  ASSERT_TRUE(owner.CreateDynamic(
    temp.socket_path(), 0x5555u, 0x6666u, 128u, 8192u, 2u, &error)) << error;
  const rmw_mdds_cpp::ipc::LoanPoolDescriptor descriptor = owner.descriptor();
  EXPECT_EQ(rmw_mdds_cpp::ipc::kDynamicLoanPoolVersion, descriptor.version);
  EXPECT_EQ(rmw_mdds_cpp::ipc::kLoanPoolFlagTypedArena, descriptor.flags);
  EXPECT_EQ(128u, descriptor.slot_size);
  EXPECT_EQ(8192u, descriptor.arena_size);
  EXPECT_EQ(2u, descriptor.slot_count);

  rmw_mdds_cpp::ipc::LoanPoolMapping mapping;
  ASSERT_TRUE(mapping.Open(descriptor, &error)) << error;
  ASSERT_TRUE(mapping.dynamic());

  const std::array<uint8_t, 7> payload{{1u, 3u, 5u, 7u, 9u, 11u, 13u}};
  uint64_t loan_id = 0u;
  uint32_t slot_index = 0u;
  ASSERT_TRUE(owner.Store(
    payload.data(), payload.size(), &loan_id, &slot_index, &error)) << error;
  const void * mapped_payload = mapping.Resolve(
    descriptor.generation, slot_index, payload.size(), &error);
  ASSERT_NE(nullptr, mapped_payload) << error;
  EXPECT_EQ(0, std::memcmp(payload.data(), mapped_payload, payload.size()));

  void * arena = mapping.ResolveArena(descriptor.generation, slot_index, &error);
  ASSERT_NE(nullptr, arena) << error;
  EXPECT_NE(mapped_payload, arena);
  std::memset(arena, 0x5a, descriptor.arena_size);
  EXPECT_TRUE(mapping.ContainsArena(slot_index, arena, descriptor.arena_size));
  EXPECT_FALSE(mapping.ContainsArena(slot_index, mapped_payload, payload.size()));
  EXPECT_EQ(
    nullptr,
    mapping.ResolveArena(descriptor.generation + 1u, slot_index, &error));
  EXPECT_EQ(
    nullptr,
    mapping.ResolveArena(descriptor.generation, descriptor.slot_count, &error));
  EXPECT_TRUE(owner.Release(loan_id));
}

}  // namespace
