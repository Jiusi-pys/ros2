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

#include <cstring>

#include "bridge_backend.hpp"
#include "rmw/qos_profiles.h"

extern "C" void FakeMddsBridgeReset(void);
extern "C" int FakeMddsBridgeBorrowLoanedCount(void);
extern "C" int FakeMddsBridgePublishLoanedCount(void);
extern "C" int FakeMddsBridgeReturnLoanedCount(void);

TEST(RmwMddsBridgeBackendLoaned, LoadsLoanedSampleBridgeSymbols)
{
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  auto & backend = rmw_mdds_cpp::BridgeBackend::Instance();
  backend.ResetForTesting();
  ASSERT_TRUE(backend.Available());

  void * publisher =
    backend.CreatePublisher("mdds_bridge_loaned", "std_msgs/msg/String", &rmw_qos_profile_default);
  ASSERT_NE(nullptr, publisher);

  void * loan = nullptr;
  void * data = nullptr;
  ASSERT_TRUE(backend.BorrowLoanedSample(publisher, 32u, &loan, &data));
  ASSERT_NE(nullptr, loan);
  ASSERT_NE(nullptr, data);
  EXPECT_EQ(1, FakeMddsBridgeBorrowLoanedCount());

  const char payload[] = "loaned bridge";
  std::memcpy(data, payload, sizeof(payload));
  EXPECT_TRUE(backend.PublishLoaned(publisher, loan, static_cast<uint32_t>(sizeof(payload))));
  EXPECT_EQ(1, FakeMddsBridgePublishLoanedCount());
  EXPECT_EQ(0, FakeMddsBridgeReturnLoanedCount());

  ASSERT_TRUE(backend.BorrowLoanedSample(publisher, 16u, &loan, &data));
  EXPECT_TRUE(backend.ReturnLoanedSample(publisher, loan));
  EXPECT_EQ(1, FakeMddsBridgeReturnLoanedCount());

  backend.DestroyPublisher(publisher);
  backend.ResetForTesting();
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
}
