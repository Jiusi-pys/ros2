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

#include "context.hpp"

#include <new>

#include "rmw/allocators.h"
#include "rmw/error_handling.h"
#include "rmw/rmw.h"

#include "rmw_mdds_cpp/identifier.hpp"

extern "C"
{
rmw_guard_condition_t *
rmw_create_guard_condition(rmw_context_t * context)
{
  if (rmw_mdds_cpp::CheckContextNotShutdown(context) != RMW_RET_OK) {
    return nullptr;
  }

  rmw_guard_condition_t * guard_condition = rmw_guard_condition_allocate();
  if (guard_condition == nullptr) {
    return nullptr;
  }
  guard_condition->implementation_identifier = rmw_mdds_cpp_identifier;
  guard_condition->data = new (std::nothrow) rmw_mdds_cpp::GuardConditionData{false};
  guard_condition->context = context;
  if (guard_condition->data == nullptr) {
    rmw_guard_condition_free(guard_condition);
    return nullptr;
  }
  return guard_condition;
}

rmw_ret_t
rmw_destroy_guard_condition(rmw_guard_condition_t * guard_condition)
{
  if (guard_condition == nullptr) {
    RMW_SET_ERROR_MSG("guard condition is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(guard_condition->implementation_identifier)) {
    RMW_SET_ERROR_MSG("guard condition implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }

  delete static_cast<rmw_mdds_cpp::GuardConditionData *>(guard_condition->data);
  rmw_guard_condition_free(guard_condition);
  return RMW_RET_OK;
}

rmw_ret_t
rmw_trigger_guard_condition(const rmw_guard_condition_t * guard_condition)
{
  if (guard_condition == nullptr) {
    RMW_SET_ERROR_MSG("guard condition is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(guard_condition->implementation_identifier)) {
    RMW_SET_ERROR_MSG("guard condition implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  auto * data = static_cast<rmw_mdds_cpp::GuardConditionData *>(guard_condition->data);
  if (data == nullptr) {
    RMW_SET_ERROR_MSG("guard condition data is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  data->triggered = true;
  return RMW_RET_OK;
}
}  // extern "C"
