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

#include "gtest/gtest.h"

#include "rmw/error_handling.h"
#include "rmw/rmw.h"

TEST(RmwMddsLogging, AcceptsDefinedLogSeverities)
{
  const rmw_log_severity_t severities[] = {
    RMW_LOG_SEVERITY_DEBUG,
    RMW_LOG_SEVERITY_INFO,
    RMW_LOG_SEVERITY_WARN,
    RMW_LOG_SEVERITY_ERROR,
    RMW_LOG_SEVERITY_FATAL,
  };

  for (rmw_log_severity_t severity : severities) {
    EXPECT_EQ(RMW_RET_OK, rmw_set_log_severity(severity)) << static_cast<int>(severity);
  }
}

TEST(RmwMddsLogging, RejectsUnknownLogSeverity)
{
  rmw_reset_error();
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_set_log_severity(static_cast<rmw_log_severity_t>(RMW_LOG_SEVERITY_FATAL + 1)));
  EXPECT_TRUE(rmw_error_is_set());
  rmw_reset_error();
}
