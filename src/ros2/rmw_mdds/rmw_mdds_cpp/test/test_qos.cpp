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

#include "rmw/qos_profiles.h"

TEST(RmwMddsQos, ReportsIncompatibleReliability)
{
  rmw_qos_profile_t publisher_profile = rmw_qos_profile_default;
  publisher_profile.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  rmw_qos_profile_t subscription_profile = publisher_profile;
  subscription_profile.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;

  rmw_qos_compatibility_type_t compatibility = RMW_QOS_COMPATIBILITY_OK;
  char reason[256] = {};
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_qos_profile_check_compatible(
      publisher_profile, subscription_profile, &compatibility, reason, sizeof(reason)));
  EXPECT_EQ(RMW_QOS_COMPATIBILITY_ERROR, compatibility);
  EXPECT_NE(nullptr, std::strstr(reason, "Best effort publisher"));
}

TEST(RmwMddsQos, ReportsPotentialCompatibilityForUnknownReliability)
{
  rmw_qos_profile_t publisher_profile = rmw_qos_profile_default;
  publisher_profile.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  rmw_qos_profile_t subscription_profile = publisher_profile;
  subscription_profile.reliability = RMW_QOS_POLICY_RELIABILITY_UNKNOWN;

  rmw_qos_compatibility_type_t compatibility = RMW_QOS_COMPATIBILITY_OK;
  char reason[256] = {};
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_qos_profile_check_compatible(
      publisher_profile, subscription_profile, &compatibility, reason, sizeof(reason)));
  EXPECT_EQ(RMW_QOS_COMPATIBILITY_WARNING, compatibility);
  EXPECT_NE(nullptr, std::strstr(reason, "subscription is unknown"));
}
