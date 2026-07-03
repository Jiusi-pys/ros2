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

#include <cstdlib>

#include <gtest/gtest.h>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/detail/trigger__type_support.hpp>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/subscription_options.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_interface/macros.h"

namespace
{
void SetEnclave(rmw_init_options_t * options, const char * enclave)
{
  options->allocator.deallocate(options->enclave, options->allocator.state);
  options->enclave = rcutils_strdup(enclave, options->allocator);
  ASSERT_NE(nullptr, options->enclave);
}
}  // namespace

TEST(RmwMddsBridgeRequired, ExplicitBridgeLibraryFailureRejectsEndpoints)
{
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_required_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_bridge_required_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * parameter_events = rmw_create_publisher(
    node, type_support, "/parameter_events", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, parameter_events);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, parameter_events));

  const rosidl_service_type_support_t * service_type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
    rosidl_typesupport_cpp, std_srvs, srv, Trigger)();
  rmw_service_t * type_description_service = rmw_create_service(
    node, service_type_support, "/mdds/mdds_bridge_required_node/get_type_description",
    &rmw_qos_profile_services_default);
  EXPECT_NE(nullptr, type_description_service);
  if (type_description_service != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, type_description_service));
  }
  rmw_reset_error();

  rmw_service_t * unrelated_type_description_service = rmw_create_service(
    node, service_type_support, "/other_node/get_type_description",
    &rmw_qos_profile_services_default);
  EXPECT_EQ(nullptr, unrelated_type_description_service);
  if (unrelated_type_description_service != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, unrelated_type_description_service));
  }
  rmw_reset_error();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_bridge_required", &rmw_qos_profile_default,
    &publisher_options);
  EXPECT_EQ(nullptr, publisher);
  if (publisher != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  }
  rmw_reset_error();

  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_bridge_required", &rmw_qos_profile_default,
    &subscription_options);
  EXPECT_EQ(nullptr, subscription);
  if (subscription != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  }
  rmw_reset_error();

  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}
