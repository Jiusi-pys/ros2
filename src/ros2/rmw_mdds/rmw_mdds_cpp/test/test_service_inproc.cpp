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
#include <std_srvs/srv/detail/trigger__functions.h>
#include <std_srvs/srv/detail/trigger__type_support.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <example_interfaces/srv/detail/add_two_ints__type_support.hpp>
#include <std_srvs/srv/detail/trigger__type_support.hpp>
#include <std_msgs/msg/string.hpp>
#include <test_msgs/srv/arrays.hpp>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/subscription_options.h"
#include "rosidl_runtime_c/string_functions.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_interface/macros.h"

namespace
{
class LocalOnlyTransportEnvironment : public testing::Environment
{
public:
  void SetUp() override
  {
    setenv("RMW_MDDS_BROKER", "0", 1);
    unsetenv("RMW_MDDS_BROKER_SOCKET");
    unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  }
};

testing::Environment * const g_local_only_transport_environment =
  testing::AddGlobalTestEnvironment(new LocalOnlyTransportEnvironment);

struct AddTwoIntsRequest
{
  int64_t a;
  int64_t b;
};

struct AddTwoIntsResponse
{
  int64_t sum;
};

void SetEnclave(rmw_init_options_t * options, const char * enclave)
{
  options->allocator.deallocate(options->enclave, options->allocator.state);
  options->enclave = rcutils_strdup(enclave, options->allocator);
  ASSERT_NE(nullptr, options->enclave);
}

void CountEventCallback(const void * user_data, size_t number_of_events)
{
  auto * callback_count = static_cast<size_t *>(const_cast<void *>(user_data));
  if (callback_count != nullptr) {
    *callback_count += number_of_events;
  }
}

const rosidl_service_type_support_t * UnsupportedServiceTypeSupportHandle(
  const rosidl_service_type_support_t *, const char *)
{
  return nullptr;
}
}  // namespace

TEST(RmwMddsService, ServiceServerIsAvailableUsesUpstreamBadArgumentReturnCodes)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_service_available_arguments_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_service_available_argument_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_GET_SRV_TYPE_SUPPORT(std_srvs, srv, Trigger);
  rmw_client_t * client = rmw_create_client(
    node, type_support, "/mdds_service_available_argument", &rmw_qos_profile_default);
  ASSERT_NE(nullptr, client) << rmw_get_error_string().str;

  bool available = false;
  EXPECT_EQ(RMW_RET_ERROR, rmw_service_server_is_available(nullptr, client, &available))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_ERROR, rmw_service_server_is_available(node, nullptr, &available))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_ERROR, rmw_service_server_is_available(node, client, nullptr))
    << rmw_get_error_string().str;
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, CreateServiceAndClientRejectUnsupportedTypeSupport)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_invalid_service_type_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_invalid_service_type_node", "/mdds");
  ASSERT_NE(nullptr, node);

  rosidl_service_type_support_t unsupported_type_support{};
  unsupported_type_support.typesupport_identifier = "rmw_mdds_invalid_service_type_support";
  unsupported_type_support.func = UnsupportedServiceTypeSupportHandle;

  rmw_service_t * service = rmw_create_service(
    node, &unsupported_type_support, "/mdds_invalid_service_type",
    &rmw_qos_profile_services_default);
  EXPECT_EQ(nullptr, service);
  if (service != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  }
  rmw_reset_error();

  rmw_client_t * client = rmw_create_client(
    node, &unsupported_type_support, "/mdds_invalid_service_type",
    &rmw_qos_profile_services_default);
  EXPECT_EQ(nullptr, client);
  if (client != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  }
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, CreateServiceAndClientRejectInvalidServiceNames)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_invalid_service_name_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_invalid_service_name_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  const char * invalid_service_names[] = {"", "relative_service", "/foo bar"};

  for (const char * service_name : invalid_service_names) {
    rmw_service_t * service =
      rmw_create_service(node, type_support, service_name, &rmw_qos_profile_services_default);
    EXPECT_EQ(nullptr, service) << service_name;
    if (service != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
    }
    rmw_reset_error();

    rmw_client_t * client =
      rmw_create_client(node, type_support, service_name, &rmw_qos_profile_services_default);
    EXPECT_EQ(nullptr, client) << service_name;
    if (client != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
    }
    rmw_reset_error();
  }

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, CreateServiceAndClientRejectInvalidQosProfiles)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_invalid_service_qos_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_invalid_service_qos_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  const rmw_qos_profile_t * invalid_qos_profiles[] = {nullptr, &rmw_qos_profile_unknown};

  for (const rmw_qos_profile_t * qos_profile : invalid_qos_profiles) {
    const char * qos_label = qos_profile == nullptr ? "null" : "unknown";

    rmw_service_t * service =
      rmw_create_service(node, type_support, "/mdds_invalid_service_qos", qos_profile);
    EXPECT_EQ(nullptr, service) << qos_label;
    if (service != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
    }
    rmw_reset_error();

    rmw_client_t * client =
      rmw_create_client(node, type_support, "/mdds_invalid_service_qos", qos_profile);
    EXPECT_EQ(nullptr, client) << qos_label;
    if (client != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
    }
    rmw_reset_error();
  }

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, InProcessAddTwoIntsRoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_service_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_service_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_service_t * service =
    rmw_create_service(node, type_support, "/mdds_add_two_ints", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t * client =
    rmw_create_client(node, type_support, "/mdds_add_two_ints", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  rmw_gid_t client_gid{};
  ASSERT_EQ(RMW_RET_OK, rmw_get_gid_for_client(client, &client_gid));

  AddTwoIntsRequest request{7, 35};
  int64_t sequence_id = -1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &request, &sequence_id));
  EXPECT_GT(sequence_id, 0);

  void * service_handle = service->data;
  rmw_services_t services;
  services.service_count = 1;
  services.services = &service_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(nullptr, nullptr, &services, nullptr, nullptr, wait_set, &timeout));
  EXPECT_NE(nullptr, services.services[0]);

  AddTwoIntsRequest received_request{0, 0};
  rmw_service_info_t request_header{};
  bool request_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_request(service, &request_header, &received_request, &request_taken));
  ASSERT_TRUE(request_taken);
  EXPECT_EQ(sequence_id, request_header.request_id.sequence_number);
  EXPECT_GT(request_header.source_timestamp, 0);
  EXPECT_GE(request_header.received_timestamp, request_header.source_timestamp);
  EXPECT_EQ(
    0, std::memcmp(client_gid.data, request_header.request_id.writer_guid, RMW_GID_STORAGE_SIZE));
  EXPECT_EQ(7, received_request.a);
  EXPECT_EQ(35, received_request.b);

  AddTwoIntsResponse response{received_request.a + received_request.b};
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &request_header.request_id, &response));

  void * client_handle = client->data;
  rmw_clients_t clients;
  clients.client_count = 1;
  clients.clients = &client_handle;
  ASSERT_EQ(RMW_RET_OK, rmw_wait(nullptr, nullptr, nullptr, &clients, nullptr, wait_set, &timeout));
  EXPECT_NE(nullptr, clients.clients[0]);

  AddTwoIntsResponse received_response{0};
  rmw_service_info_t response_header{};
  bool response_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_response(client, &response_header, &received_response, &response_taken));
  ASSERT_TRUE(response_taken);
  EXPECT_EQ(sequence_id, response_header.request_id.sequence_number);
  EXPECT_GT(response_header.source_timestamp, 0);
  EXPECT_GE(response_header.received_timestamp, response_header.source_timestamp);
  EXPECT_EQ(42, received_response.sum);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, ServiceAvailabilityMatchesCClientToCppService)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_mixed_service_type_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_mixed_service_type_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * cpp_type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, std_srvs, srv, Trigger)();
  const rosidl_service_type_support_t * c_type_support =
    ROSIDL_GET_SRV_TYPE_SUPPORT(std_srvs, srv, Trigger);

  rmw_service_t * service = rmw_create_service(
    node, cpp_type_support, "/mdds_mixed_trigger", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service) << rmw_get_error_string().str;
  rmw_client_t * client = rmw_create_client(
    node, c_type_support, "/mdds_mixed_trigger", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client) << rmw_get_error_string().str;

  bool is_available = false;
  ASSERT_EQ(RMW_RET_OK, rmw_service_server_is_available(node, client, &is_available))
    << rmw_get_error_string().str;
  EXPECT_TRUE(is_available);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, WaitClearsUnreadySubscriptionsWhenServiceIsReady)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_wait_mixed_service_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_wait_mixed_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * service_type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_service_t * service = rmw_create_service(
    node, service_type_support, "/mdds_wait_service", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t * client = rmw_create_client(
    node, service_type_support, "/mdds_wait_service", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  const rosidl_message_type_support_t * message_type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, message_type_support, "/mdds_wait_empty_topic", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  AddTwoIntsRequest request{1, 2};
  int64_t sequence_id = -1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &request, &sequence_id));

  void * subscription_handle = subscription->data;
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 1;
  subscriptions.subscribers = &subscription_handle;

  void * service_handle = service->data;
  rmw_services_t services;
  services.service_count = 1;
  services.services = &service_handle;

  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 2);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, &services, nullptr, nullptr, wait_set, &timeout));
  EXPECT_EQ(nullptr, subscriptions.subscribers[0]);
  EXPECT_NE(nullptr, services.services[0]);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, InProcessArraysServiceRoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_arrays_service_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_arrays_service_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, test_msgs, srv, Arrays)();
  rmw_service_t * service =
    rmw_create_service(node, type_support, "/mdds_arrays", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t * client =
    rmw_create_client(node, type_support, "/mdds_arrays", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  test_msgs::srv::Arrays::Request request;
  request.bool_values = {true, false, true};
  request.int32_values = {7, -35, 3588};
  request.string_values = {"request", "array", "payload"};
  request.basic_types_values[0].int32_value = 42;

  int64_t sequence_id = -1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &request, &sequence_id));

  void * service_handle = service->data;
  rmw_services_t services;
  services.service_count = 1;
  services.services = &service_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(nullptr, nullptr, &services, nullptr, nullptr, wait_set, &timeout));

  test_msgs::srv::Arrays::Request received_request;
  rmw_service_info_t request_header{};
  bool request_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_request(service, &request_header, &received_request, &request_taken));
  ASSERT_TRUE(request_taken);
  EXPECT_EQ(sequence_id, request_header.request_id.sequence_number);
  EXPECT_EQ(request.bool_values, received_request.bool_values);
  EXPECT_EQ(request.int32_values, received_request.int32_values);
  EXPECT_EQ(request.string_values, received_request.string_values);
  EXPECT_EQ(42, received_request.basic_types_values[0].int32_value);

  test_msgs::srv::Arrays::Response response;
  response.bool_values = {false, true, false};
  response.int32_values = {-1, 0, 1};
  response.string_values = {"response", "array", "payload"};
  response.basic_types_values[1].int32_value = -3588;
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &request_header.request_id, &response));

  void * client_handle = client->data;
  rmw_clients_t clients;
  clients.client_count = 1;
  clients.clients = &client_handle;
  ASSERT_EQ(RMW_RET_OK, rmw_wait(nullptr, nullptr, nullptr, &clients, nullptr, wait_set, &timeout));

  test_msgs::srv::Arrays::Response received_response;
  rmw_service_info_t response_header{};
  bool response_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_response(client, &response_header, &received_response, &response_taken));
  ASSERT_TRUE(response_taken);
  EXPECT_EQ(sequence_id, response_header.request_id.sequence_number);
  EXPECT_EQ(response.bool_values, received_response.bool_values);
  EXPECT_EQ(response.int32_values, received_response.int32_values);
  EXPECT_EQ(response.string_values, received_response.string_values);
  EXPECT_EQ(-3588, received_response.basic_types_values[1].int32_value);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, InProcessTriggerResponseCopiesCStringPayload)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_trigger_service_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_trigger_service_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_c, std_srvs, srv, Trigger)();
  rmw_service_t * service =
    rmw_create_service(node, type_support, "/mdds_trigger", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t * client =
    rmw_create_client(node, type_support, "/mdds_trigger", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  std_srvs__srv__Trigger_Request request;
  ASSERT_TRUE(std_srvs__srv__Trigger_Request__init(&request));
  int64_t sequence_id = -1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &request, &sequence_id));

  void * service_handle = service->data;
  rmw_services_t services;
  services.service_count = 1;
  services.services = &service_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(nullptr, nullptr, &services, nullptr, nullptr, wait_set, &timeout));

  std_srvs__srv__Trigger_Request received_request;
  ASSERT_TRUE(std_srvs__srv__Trigger_Request__init(&received_request));
  rmw_service_info_t request_header{};
  bool request_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_request(service, &request_header, &received_request, &request_taken));
  ASSERT_TRUE(request_taken);

  std_srvs__srv__Trigger_Response response;
  ASSERT_TRUE(std_srvs__srv__Trigger_Response__init(&response));
  response.success = true;
  const char * original_message = "trigger response with owned string payload";
  ASSERT_TRUE(rosidl_runtime_c__String__assign(&response.message, original_message));
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &request_header.request_id, &response));
  response.message.data[0] = 'X';

  void * client_handle = client->data;
  rmw_clients_t clients;
  clients.client_count = 1;
  clients.clients = &client_handle;
  ASSERT_EQ(RMW_RET_OK, rmw_wait(nullptr, nullptr, nullptr, &clients, nullptr, wait_set, &timeout));

  std_srvs__srv__Trigger_Response received_response;
  ASSERT_TRUE(std_srvs__srv__Trigger_Response__init(&received_response));
  rmw_service_info_t response_header{};
  bool response_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_response(client, &response_header, &received_response, &response_taken));
  ASSERT_TRUE(response_taken);
  EXPECT_EQ(sequence_id, response_header.request_id.sequence_number);
  EXPECT_TRUE(received_response.success);
  EXPECT_FALSE(received_response.message.data == response.message.data);
  EXPECT_STREQ(original_message, received_response.message.data);

  if (received_response.message.data == response.message.data) {
    received_response.message.data = nullptr;
    received_response.message.size = 0;
    received_response.message.capacity = 0;
  }
  std_srvs__srv__Trigger_Response__fini(&received_response);
  std_srvs__srv__Trigger_Response__fini(&response);
  std_srvs__srv__Trigger_Request__fini(&received_request);
  std_srvs__srv__Trigger_Request__fini(&request);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, RequestAndResponseCallbacksReportUnreadSamples)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_service_callback_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_service_callback_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_service_t * service =
    rmw_create_service(node, type_support, "/mdds_callback_add_two_ints", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t * client =
    rmw_create_client(node, type_support, "/mdds_callback_add_two_ints", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  AddTwoIntsRequest first_request{1, 2};
  AddTwoIntsRequest second_request{3, 4};
  AddTwoIntsRequest third_request{5, 6};
  int64_t first_sequence_id = -1;
  int64_t second_sequence_id = -1;
  int64_t third_sequence_id = -1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &first_request, &first_sequence_id));
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &second_request, &second_sequence_id));

  size_t request_callback_count = 0;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_service_set_on_new_request_callback(service, CountEventCallback, &request_callback_count));
  EXPECT_EQ(2u, request_callback_count);

  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &third_request, &third_sequence_id));
  EXPECT_EQ(3u, request_callback_count);

  AddTwoIntsRequest received_first_request{0, 0};
  AddTwoIntsRequest received_second_request{0, 0};
  AddTwoIntsRequest received_third_request{0, 0};
  rmw_service_info_t first_request_header{};
  rmw_service_info_t second_request_header{};
  rmw_service_info_t third_request_header{};
  bool request_taken = false;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_request(
      service, &first_request_header, &received_first_request, &request_taken));
  ASSERT_TRUE(request_taken);
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_request(
      service, &second_request_header, &received_second_request, &request_taken));
  ASSERT_TRUE(request_taken);
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_request(
      service, &third_request_header, &received_third_request, &request_taken));
  ASSERT_TRUE(request_taken);
  EXPECT_EQ(first_sequence_id, first_request_header.request_id.sequence_number);
  EXPECT_EQ(second_sequence_id, second_request_header.request_id.sequence_number);
  EXPECT_EQ(third_sequence_id, third_request_header.request_id.sequence_number);

  AddTwoIntsResponse first_response{received_first_request.a + received_first_request.b};
  AddTwoIntsResponse second_response{received_second_request.a + received_second_request.b};
  AddTwoIntsResponse third_response{received_third_request.a + received_third_request.b};
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &first_request_header.request_id, &first_response));
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &second_request_header.request_id, &second_response));

  size_t response_callback_count = 0;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_client_set_on_new_response_callback(client, CountEventCallback, &response_callback_count));
  EXPECT_EQ(2u, response_callback_count);

  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &third_request_header.request_id, &third_response));
  EXPECT_EQ(3u, response_callback_count);

  ASSERT_EQ(RMW_RET_OK, rmw_service_set_on_new_request_callback(service, nullptr, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_client_set_on_new_response_callback(client, nullptr, nullptr));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}
