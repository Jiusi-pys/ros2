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

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "loan_arena.hpp"

namespace
{
struct ArenaNestedValue
{
  using String = std::basic_string<
    char, std::char_traits<char>, rosidl_runtime_cpp::MessageAllocator<char>>;

  String value;
};
}  // namespace

TEST(MddsLoanArena, AllocatesAlignedDynamicSegmentsInsideBridgeLoan)
{
  alignas(std::max_align_t) std::array<uint8_t, 256> bridge_storage{};
  rmw_mdds_cpp::MddsLoanArena arena(bridge_storage.data(), bridge_storage.size());

  void * message = arena.Allocate(sizeof(uint64_t), alignof(uint64_t));
  ASSERT_NE(nullptr, message);
  EXPECT_TRUE(arena.Contains(message, sizeof(uint64_t)));
  EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(message) % alignof(uint64_t));

  char * string_bytes = static_cast<char *>(arena.Allocate(33u, alignof(char)));
  ASSERT_NE(nullptr, string_bytes);
  std::memset(string_bytes, 'm', 32u);
  string_bytes[32] = '\0';
  EXPECT_TRUE(arena.Contains(string_bytes, 33u));

  int32_t * sequence = static_cast<int32_t *>(arena.Allocate(16u * sizeof(int32_t),
    alignof(int32_t)));
  ASSERT_NE(nullptr, sequence);
  EXPECT_TRUE(arena.Contains(sequence, 16u * sizeof(int32_t)));
  EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(sequence) % alignof(int32_t));

  EXPECT_GT(arena.BytesUsed(), sizeof(uint64_t) + 33u + 16u * sizeof(int32_t));
  EXPECT_EQ(3u, arena.SegmentCount());
}

TEST(MddsLoanArena, RejectsOverflowAndResetsLifetime)
{
  alignas(std::max_align_t) std::array<uint8_t, 64> bridge_storage{};
  rmw_mdds_cpp::MddsLoanArena arena(bridge_storage.data(), bridge_storage.size());

  void * first = arena.Allocate(32u, alignof(std::max_align_t));
  ASSERT_NE(nullptr, first);
  EXPECT_TRUE(arena.Contains(first, 32u));

  EXPECT_EQ(nullptr, arena.Allocate(128u, alignof(std::max_align_t)));
  EXPECT_TRUE(arena.Contains(first, 32u));
  EXPECT_EQ(1u, arena.SegmentCount());

  arena.Reset();
  EXPECT_EQ(0u, arena.BytesUsed());
  EXPECT_EQ(0u, arena.SegmentCount());

  void * after_reset = arena.Allocate(16u, alignof(uint32_t));
  ASSERT_NE(nullptr, after_reset);
  EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(after_reset) % alignof(uint32_t));
}

TEST(MddsLoanArena, StatefulMessageAllocatorSurvivesConstructionScope)
{
  alignas(std::max_align_t) std::array<uint8_t, 512> bridge_storage{};
  rmw_mdds_cpp::MddsLoanMemoryResource resource(
    bridge_storage.data(), bridge_storage.size());
  using NestedAllocator = rosidl_runtime_cpp::MessageAllocator<ArenaNestedValue>;
  std::vector<ArenaNestedValue, NestedAllocator> values(
    NestedAllocator(resource.Resource()));

  values.resize(2u);
  values[0].value.assign(64u, 'm');

  EXPECT_TRUE(resource.Contains(
    values.data(), values.size() * sizeof(ArenaNestedValue)));
  EXPECT_TRUE(resource.Contains(values[0].value.data(), values[0].value.size()));
  EXPECT_GE(resource.SegmentCount(), 2u);
}
