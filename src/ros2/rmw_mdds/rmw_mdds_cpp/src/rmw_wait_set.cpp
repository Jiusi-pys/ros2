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
rmw_wait_set_t *
rmw_create_wait_set(rmw_context_t * context, size_t max_conditions)
{
  if (rmw_mdds_cpp::CheckContextNotShutdown(context) != RMW_RET_OK) {
    return nullptr;
  }

  rmw_wait_set_t * wait_set = rmw_wait_set_allocate();
  if (wait_set == nullptr) {
    return nullptr;
  }
  wait_set->implementation_identifier = rmw_mdds_cpp_identifier;
  wait_set->guard_conditions = nullptr;
  wait_set->data = new (std::nothrow) rmw_mdds_cpp::WaitSetData{context, max_conditions};
  if (wait_set->data == nullptr) {
    rmw_wait_set_free(wait_set);
    return nullptr;
  }
  return wait_set;
}

rmw_ret_t
rmw_destroy_wait_set(rmw_wait_set_t * wait_set)
{
  if (wait_set == nullptr) {
    RMW_SET_ERROR_MSG("wait set is null");
    return RMW_RET_ERROR;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(wait_set->implementation_identifier)) {
    RMW_SET_ERROR_MSG("wait set implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }

  delete static_cast<rmw_mdds_cpp::WaitSetData *>(wait_set->data);
  rmw_wait_set_free(wait_set);
  return RMW_RET_OK;
}
}  // extern "C"
