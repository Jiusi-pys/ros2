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

#include "rmw/features.h"
#include "rmw/rmw.h"

TEST(RmwMddsIdentity, ReportsIdentifierAndFormat)
{
  EXPECT_STREQ("rmw_mdds_cpp", rmw_get_implementation_identifier());
  EXPECT_STREQ("cdr", rmw_get_serialization_format());
  EXPECT_TRUE(rmw_feature_supported(RMW_FEATURE_MESSAGE_INFO_PUBLICATION_SEQUENCE_NUMBER));
  EXPECT_TRUE(rmw_feature_supported(RMW_FEATURE_MESSAGE_INFO_RECEPTION_SEQUENCE_NUMBER));
  EXPECT_FALSE(rmw_feature_supported(RMW_MIDDLEWARE_SUPPORTS_TYPE_DISCOVERY));
  EXPECT_TRUE(rmw_feature_supported(RMW_MIDDLEWARE_CAN_TAKE_DYNAMIC_MESSAGE));
}
