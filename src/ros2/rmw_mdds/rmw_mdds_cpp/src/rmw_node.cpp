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

#include "broker.hpp"
#include "rcutils/strdup.h"

#include "rmw/allocators.h"
#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw/validate_namespace.h"
#include "rmw/validate_node_name.h"

#include "rmw_mdds_cpp/identifier.hpp"

namespace
{
bool ValidateNodeName(const char * name)
{
  int validation_result = RMW_NODE_NAME_VALID;
  size_t invalid_index = 0;
  const rmw_ret_t ret = rmw_validate_node_name(name, &validation_result, &invalid_index);
  if (ret == RMW_RET_OK && validation_result == RMW_NODE_NAME_VALID) {
    return true;
  }
  RMW_SET_ERROR_MSG("node name is invalid");
  return false;
}

bool ValidateNodeNamespace(const char * namespace_)
{
  int validation_result = RMW_NAMESPACE_VALID;
  size_t invalid_index = 0;
  const rmw_ret_t ret = rmw_validate_namespace(namespace_, &validation_result, &invalid_index);
  if (ret == RMW_RET_OK && validation_result == RMW_NAMESPACE_VALID) {
    return true;
  }
  RMW_SET_ERROR_MSG("node namespace is invalid");
  return false;
}
}  // namespace

extern "C"
{
rmw_node_t *
rmw_create_node(rmw_context_t * context, const char * name, const char * namespace_)
{
  if (name == nullptr || namespace_ == nullptr) {
    RMW_SET_ERROR_MSG("node name or namespace is null");
    return nullptr;
  }
  if (rmw_mdds_cpp::CheckContextNotShutdown(context) != RMW_RET_OK) {
    return nullptr;
  }
  if (!ValidateNodeName(name) || !ValidateNodeNamespace(namespace_)) {
    return nullptr;
  }

  rmw_node_t * node = rmw_node_allocate();
  if (node == nullptr) {
    return nullptr;
  }

  rcutils_allocator_t allocator = context->options.allocator;
  node->implementation_identifier = rmw_mdds_cpp_identifier;
  const char * enclave = context->options.enclave == nullptr ? "" : context->options.enclave;
  node->data = new (std::nothrow) rmw_mdds_cpp::NodeData{
    context, nullptr, name, namespace_, enclave};
  node->name = rcutils_strdup(name, allocator);
  node->namespace_ = rcutils_strdup(namespace_, allocator);
  node->context = context;
  auto * data = static_cast<rmw_mdds_cpp::NodeData *>(node->data);
  if (data != nullptr) {
    data->graph_guard_condition = rmw_create_guard_condition(context);
  }
  if (data == nullptr || node->name == nullptr || node->namespace_ == nullptr ||
    data->graph_guard_condition == nullptr)
  {
    if (data != nullptr && data->graph_guard_condition != nullptr) {
      rmw_ret_t guard_ret = rmw_destroy_guard_condition(data->graph_guard_condition);
      (void)guard_ret;
    }
    delete data;
    allocator.deallocate(const_cast<char *>(node->name), allocator.state);
    allocator.deallocate(const_cast<char *>(node->namespace_), allocator.state);
    rmw_node_free(node);
    return nullptr;
  }

  context->impl->node_count++;
  rmw_mdds_cpp::RegisterNode(data);
  return node;
}

rmw_ret_t
rmw_destroy_node(rmw_node_t * node)
{
  if (node == nullptr) {
    RMW_SET_ERROR_MSG("node is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(node->implementation_identifier)) {
    RMW_SET_ERROR_MSG("node implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  auto * data = static_cast<rmw_mdds_cpp::NodeData *>(node->data);
  if (data == nullptr || data->context == nullptr || data->context->impl == nullptr) {
    RMW_SET_ERROR_MSG("node data is invalid");
    return RMW_RET_INVALID_ARGUMENT;
  }

  rcutils_allocator_t allocator = data->context->options.allocator;
  if (data->context->impl->node_count > 0) {
    data->context->impl->node_count--;
  }
  rmw_mdds_cpp::UnregisterNode(data);
  if (data->graph_guard_condition != nullptr) {
    rmw_ret_t guard_ret = rmw_destroy_guard_condition(data->graph_guard_condition);
    if (guard_ret != RMW_RET_OK) {
      return guard_ret;
    }
  }
  allocator.deallocate(const_cast<char *>(node->name), allocator.state);
  allocator.deallocate(const_cast<char *>(node->namespace_), allocator.state);
  delete data;
  rmw_node_free(node);
  return RMW_RET_OK;
}

rmw_ret_t
rmw_node_assert_liveliness(const rmw_node_t * node)
{
  if (node == nullptr) {
    RMW_SET_ERROR_MSG("node is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(node->implementation_identifier)) {
    RMW_SET_ERROR_MSG("node implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  const auto * data = static_cast<const rmw_mdds_cpp::NodeData *>(node->data);
  if (data == nullptr) {
    RMW_SET_ERROR_MSG("node data is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_mdds_cpp::NoteNodePublishersLivelinessAsserted(
    data->node_name.c_str(), data->node_namespace.c_str(), rmw_mdds_cpp::MddsNowNanoseconds());
  return RMW_RET_OK;
}

const rmw_guard_condition_t *
rmw_node_get_graph_guard_condition(const rmw_node_t * node)
{
  if (node == nullptr || !rmw_mdds_cpp::IsMddsIdentifier(node->implementation_identifier)) {
    RMW_SET_ERROR_MSG("node is invalid");
    return nullptr;
  }
  const auto * data = static_cast<const rmw_mdds_cpp::NodeData *>(node->data);
  if (data == nullptr || data->graph_guard_condition == nullptr) {
    RMW_SET_ERROR_MSG("node graph guard condition is invalid");
    return nullptr;
  }
  return data->graph_guard_condition;
}
}  // extern "C"
