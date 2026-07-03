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

#include <cstdlib>
#include <cstring>

#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/detail/trigger__type_support.hpp>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rcutils/types/string_array.h"
#include "rmw/error_handling.h"
#include "rmw/get_node_info_and_types.h"
#include "rmw/get_service_names_and_types.h"
#include "rmw/get_topic_endpoint_info.h"
#include "rmw/get_topic_names_and_types.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/names_and_types.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/sanity_checks.h"
#include "rmw/subscription_options.h"
#include "rmw/topic_endpoint_info_array.h"
#include "rosidl_runtime_c/type_hash.h"
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

void SetEnclave(rmw_init_options_t * options, const char * enclave)
{
  options->allocator.deallocate(options->enclave, options->allocator.state);
  options->enclave = rcutils_strdup(enclave, options->allocator);
  ASSERT_NE(nullptr, options->enclave);
}

void ExpectSingleTopicType(
  const rmw_names_and_types_t & names_and_types, const char * topic, const char * type)
{
  ASSERT_EQ(1u, names_and_types.names.size);
  ASSERT_NE(nullptr, names_and_types.names.data);
  EXPECT_STREQ(topic, names_and_types.names.data[0]);

  ASSERT_NE(nullptr, names_and_types.types);
  ASSERT_EQ(1u, names_and_types.types[0].size);
  ASSERT_NE(nullptr, names_and_types.types[0].data);
  EXPECT_STREQ(type, names_and_types.types[0].data[0]);
}

void ExpectTypeHashEquals(const rosidl_type_hash_t & actual, const rosidl_type_hash_t & expected)
{
  EXPECT_EQ(expected.version, actual.version);
  EXPECT_EQ(0, std::memcmp(expected.value, actual.value, ROSIDL_TYPE_HASH_SIZE));
}

using TopicNamesByNodeQuery = rmw_ret_t (*)(
  const rmw_node_t *, rcutils_allocator_t *, const char *, const char *, bool,
  rmw_names_and_types_t *);
using ServiceNamesByNodeQuery = rmw_ret_t (*)(
  const rmw_node_t *, rcutils_allocator_t *, const char *, const char *,
  rmw_names_and_types_t *);

void ExpectTopicNamesByNodeQueryResult(
  TopicNamesByNodeQuery query, const rmw_node_t * node, rcutils_allocator_t * allocator,
  const char * node_name, const char * node_namespace, rmw_ret_t expected_ret)
{
  rmw_names_and_types_t names_and_types = rmw_get_zero_initialized_names_and_types();
  const rmw_ret_t ret =
    query(node, allocator, node_name, node_namespace, false, &names_and_types);
  EXPECT_EQ(expected_ret, ret) << rmw_get_error_string().str;
  if (ret == RMW_RET_OK) {
    EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_fini(&names_and_types));
    return;
  }
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_check_zero(&names_and_types));
  rmw_reset_error();
}

void ExpectServiceNamesByNodeQueryResult(
  ServiceNamesByNodeQuery query, const rmw_node_t * node, rcutils_allocator_t * allocator,
  const char * node_name, const char * node_namespace, rmw_ret_t expected_ret)
{
  rmw_names_and_types_t names_and_types = rmw_get_zero_initialized_names_and_types();
  const rmw_ret_t ret = query(node, allocator, node_name, node_namespace, &names_and_types);
  EXPECT_EQ(expected_ret, ret) << rmw_get_error_string().str;
  if (ret == RMW_RET_OK) {
    EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_fini(&names_and_types));
    return;
  }
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_check_zero(&names_and_types));
  rmw_reset_error();
}
}  // namespace

TEST(RmwMddsGraph, ReportsLocalPubSubTopicsAndTypes)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_graph_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_graph_node", "/mdds");
  ASSERT_NE(nullptr, node);

  rcutils_string_array_t node_names = rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t node_namespaces = rcutils_get_zero_initialized_string_array();
  ASSERT_EQ(RMW_RET_OK, rmw_get_node_names(node, &node_names, &node_namespaces));
  ASSERT_EQ(1u, node_names.size);
  ASSERT_EQ(1u, node_namespaces.size);
  EXPECT_STREQ("mdds_graph_node", node_names.data[0]);
  EXPECT_STREQ("/mdds", node_namespaces.data[0]);
  EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&node_names));
  EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&node_namespaces));

  rcutils_string_array_t node_names_with_enclaves = rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t node_namespaces_with_enclaves =
    rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t enclaves = rcutils_get_zero_initialized_string_array();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_get_node_names_with_enclaves(
      node, &node_names_with_enclaves, &node_namespaces_with_enclaves, &enclaves));
  ASSERT_EQ(1u, node_names_with_enclaves.size);
  ASSERT_EQ(1u, node_namespaces_with_enclaves.size);
  ASSERT_EQ(1u, enclaves.size);
  EXPECT_STREQ("mdds_graph_node", node_names_with_enclaves.data[0]);
  EXPECT_STREQ("/mdds", node_namespaces_with_enclaves.data[0]);
  EXPECT_STREQ("/rmw_mdds_graph_test", enclaves.data[0]);
  EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&node_names_with_enclaves));
  EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&node_namespaces_with_enclaves));
  EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&enclaves));

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  ASSERT_NE(nullptr, type_support);
  ASSERT_NE(nullptr, type_support->get_type_hash_func);
  const rosidl_type_hash_t * expected_type_hash = type_support->get_type_hash_func(type_support);
  ASSERT_NE(nullptr, expected_type_hash);
  ASSERT_NE(ROSIDL_TYPE_HASH_VERSION_UNSET, expected_type_hash->version);
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  rmw_qos_profile_t endpoint_qos = rmw_qos_profile_default;
  endpoint_qos.depth = 7;
  endpoint_qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  endpoint_qos.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_graph_string", &endpoint_qos, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_graph_string", &endpoint_qos, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_names_and_types_t topic_names = rmw_get_zero_initialized_names_and_types();
  ASSERT_EQ(RMW_RET_OK, rmw_get_topic_names_and_types(node, &allocator, false, &topic_names));
  ExpectSingleTopicType(topic_names, "/mdds_graph_string", "std_msgs/msg/String");
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_fini(&topic_names));

  rmw_names_and_types_t publisher_names = rmw_get_zero_initialized_names_and_types();
  ASSERT_EQ(
    RMW_RET_OK, rmw_get_publisher_names_and_types_by_node(
                  node, &allocator, "mdds_graph_node", "/mdds", false, &publisher_names));
  ExpectSingleTopicType(publisher_names, "/mdds_graph_string", "std_msgs/msg/String");
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_fini(&publisher_names));

  rmw_names_and_types_t subscriber_names = rmw_get_zero_initialized_names_and_types();
  ASSERT_EQ(
    RMW_RET_OK, rmw_get_subscriber_names_and_types_by_node(
                  node, &allocator, "mdds_graph_node", "/mdds", false, &subscriber_names));
  ExpectSingleTopicType(subscriber_names, "/mdds_graph_string", "std_msgs/msg/String");
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_fini(&subscriber_names));

  rmw_gid_t publisher_gid{};
  ASSERT_EQ(RMW_RET_OK, rmw_get_gid_for_publisher(publisher, &publisher_gid));

  rmw_topic_endpoint_info_array_t publishers_info =
    rmw_get_zero_initialized_topic_endpoint_info_array();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_get_publishers_info_by_topic(
      node, &allocator, "/mdds_graph_string", false, &publishers_info));
  ASSERT_EQ(1u, publishers_info.size);
  const rmw_topic_endpoint_info_t & publisher_info = publishers_info.info_array[0];
  EXPECT_STREQ("mdds_graph_node", publisher_info.node_name);
  EXPECT_STREQ("/mdds", publisher_info.node_namespace);
  EXPECT_STREQ("std_msgs/msg/String", publisher_info.topic_type);
  ExpectTypeHashEquals(publisher_info.topic_type_hash, *expected_type_hash);
  EXPECT_EQ(RMW_ENDPOINT_PUBLISHER, publisher_info.endpoint_type);
  EXPECT_EQ(
    0, std::memcmp(publisher_gid.data, publisher_info.endpoint_gid, RMW_GID_STORAGE_SIZE));
  EXPECT_EQ(7u, publisher_info.qos_profile.depth);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_RELIABLE, publisher_info.qos_profile.reliability);
  EXPECT_EQ(RMW_QOS_POLICY_DURABILITY_VOLATILE, publisher_info.qos_profile.durability);

  rmw_topic_endpoint_info_array_t subscriptions_info =
    rmw_get_zero_initialized_topic_endpoint_info_array();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_get_subscriptions_info_by_topic(
      node, &allocator, "/mdds_graph_string", false, &subscriptions_info));
  ASSERT_EQ(1u, subscriptions_info.size);
  const rmw_topic_endpoint_info_t & subscription_info = subscriptions_info.info_array[0];
  EXPECT_STREQ("mdds_graph_node", subscription_info.node_name);
  EXPECT_STREQ("/mdds", subscription_info.node_namespace);
  EXPECT_STREQ("std_msgs/msg/String", subscription_info.topic_type);
  ExpectTypeHashEquals(subscription_info.topic_type_hash, *expected_type_hash);
  EXPECT_EQ(RMW_ENDPOINT_SUBSCRIPTION, subscription_info.endpoint_type);
  EXPECT_EQ(7u, subscription_info.qos_profile.depth);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_RELIABLE, subscription_info.qos_profile.reliability);
  EXPECT_EQ(RMW_QOS_POLICY_DURABILITY_VOLATILE, subscription_info.qos_profile.durability);

  EXPECT_EQ(RMW_RET_OK, rmw_topic_endpoint_info_array_fini(&subscriptions_info, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_topic_endpoint_info_array_fini(&publishers_info, &allocator));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsGraph, ReportsLocalServiceClientGraph)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_service_graph_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_service_graph_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, std_srvs, srv, Trigger)();

  rmw_service_t * service = rmw_create_service(
    node, type_support, "/mdds_graph_trigger", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t * client =
    rmw_create_client(node, type_support, "/mdds_graph_trigger", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  size_t service_count = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_count_services(node, "/mdds_graph_trigger", &service_count));
  EXPECT_EQ(1u, service_count);
  size_t client_count = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_count_clients(node, "/mdds_graph_trigger", &client_count));
  EXPECT_EQ(1u, client_count);

  bool is_available = false;
  ASSERT_EQ(RMW_RET_OK, rmw_service_server_is_available(node, client, &is_available));
  EXPECT_TRUE(is_available);

  rmw_names_and_types_t service_names = rmw_get_zero_initialized_names_and_types();
  ASSERT_EQ(RMW_RET_OK, rmw_get_service_names_and_types(node, &allocator, &service_names));
  ExpectSingleTopicType(service_names, "/mdds_graph_trigger", "std_srvs/srv/Trigger");
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_fini(&service_names));

  rmw_names_and_types_t service_names_by_node = rmw_get_zero_initialized_names_and_types();
  ASSERT_EQ(
    RMW_RET_OK, rmw_get_service_names_and_types_by_node(
                  node, &allocator, "mdds_service_graph_node", "/mdds", &service_names_by_node));
  ExpectSingleTopicType(service_names_by_node, "/mdds_graph_trigger", "std_srvs/srv/Trigger");
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_fini(&service_names_by_node));

  rmw_names_and_types_t client_names_by_node = rmw_get_zero_initialized_names_and_types();
  ASSERT_EQ(
    RMW_RET_OK, rmw_get_client_names_and_types_by_node(
                  node, &allocator, "mdds_service_graph_node", "/mdds", &client_names_by_node));
  ExpectSingleTopicType(client_names_by_node, "/mdds_graph_trigger", "std_srvs/srv/Trigger");
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_fini(&client_names_by_node));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsGraph, CountApisRejectInvalidNames)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_graph_count_argument_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_graph_count_argument_node", "/mdds");
  ASSERT_NE(nullptr, node);

  size_t count = 0;
  const char * invalid_topic_name = "not a valid topic name !";
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_count_publishers(node, invalid_topic_name, &count))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_count_subscribers(node, invalid_topic_name, &count))
    << rmw_get_error_string().str;
  rmw_reset_error();

  const char * invalid_service_name = "not a valid service name !";
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_count_clients(node, invalid_service_name, &count))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_count_services(node, invalid_service_name, &count))
    << rmw_get_error_string().str;
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsGraph, EndpointInfoRejectsInvalidAllocatorWithoutEndpoints)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_graph_empty_endpoint_argument_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_graph_empty_endpoint_argument_node", "/mdds");
  ASSERT_NE(nullptr, node);

  rcutils_allocator_t invalid_allocator = rcutils_get_zero_initialized_allocator();
  rmw_topic_endpoint_info_array_t publishers_info =
    rmw_get_zero_initialized_topic_endpoint_info_array();
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_get_publishers_info_by_topic(
      node, &invalid_allocator, "/mdds_graph_empty_endpoint", false, &publishers_info))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_topic_endpoint_info_array_check_zero(&publishers_info));

  rmw_topic_endpoint_info_array_t subscriptions_info =
    rmw_get_zero_initialized_topic_endpoint_info_array();
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_get_subscriptions_info_by_topic(
      node, &invalid_allocator, "/mdds_graph_empty_endpoint", false, &subscriptions_info))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_topic_endpoint_info_array_check_zero(&subscriptions_info));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsGraph, EndpointInfoRejectsInvalidTopicNames)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_graph_endpoint_name_argument_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_graph_endpoint_name_argument_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const char * invalid_topic_name = "not a valid topic name !";
  rmw_topic_endpoint_info_array_t publishers_info =
    rmw_get_zero_initialized_topic_endpoint_info_array();
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_get_publishers_info_by_topic(
      node, &allocator, invalid_topic_name, false, &publishers_info))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_topic_endpoint_info_array_check_zero(&publishers_info));

  rmw_topic_endpoint_info_array_t subscriptions_info =
    rmw_get_zero_initialized_topic_endpoint_info_array();
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_get_subscriptions_info_by_topic(
      node, &allocator, invalid_topic_name, false, &subscriptions_info))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_topic_endpoint_info_array_check_zero(&subscriptions_info));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsGraph, NodeNameQueriesRejectNonZeroOutputArrays)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_graph_node_name_argument_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_graph_node_name_argument_node", "/mdds");
  ASSERT_NE(nullptr, node);

  rcutils_string_array_t node_names = rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t node_namespaces = rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t enclaves = rcutils_get_zero_initialized_string_array();

  ASSERT_EQ(RCUTILS_RET_OK, rcutils_string_array_init(&node_names, 1u, &allocator));
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_get_node_names(node, &node_names, &node_namespaces))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_check_zero_rmw_string_array(&node_namespaces));
  EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&node_names));

  ASSERT_EQ(RCUTILS_RET_OK, rcutils_string_array_init(&node_namespaces, 1u, &allocator));
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_get_node_names(node, &node_names, &node_namespaces))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_check_zero_rmw_string_array(&node_names));
  EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&node_namespaces));

  ASSERT_EQ(RCUTILS_RET_OK, rcutils_string_array_init(&enclaves, 1u, &allocator));
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_get_node_names_with_enclaves(node, &node_names, &node_namespaces, &enclaves))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_check_zero_rmw_string_array(&node_names));
  EXPECT_EQ(RMW_RET_OK, rmw_check_zero_rmw_string_array(&node_namespaces));
  EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&enclaves));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsGraph, NamesAndTypesQueriesRejectNonZeroOutputArrays)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_graph_names_and_types_argument_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_graph_names_and_types_argument_node", "/mdds");
  ASSERT_NE(nullptr, node);

  rmw_names_and_types_t topic_names_and_types = rmw_get_zero_initialized_names_and_types();
  ASSERT_EQ(RMW_RET_OK, rmw_names_and_types_init(&topic_names_and_types, 1u, &allocator));
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_get_topic_names_and_types(node, &allocator, false, &topic_names_and_types))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_fini(&topic_names_and_types));

  rmw_names_and_types_t service_names_and_types = rmw_get_zero_initialized_names_and_types();
  ASSERT_EQ(RMW_RET_OK, rmw_names_and_types_init(&service_names_and_types, 1u, &allocator));
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_get_service_names_and_types(node, &allocator, &service_names_and_types))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_fini(&service_names_and_types));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsGraph, NamesAndTypesByNodeRejectsInvalidQueriedNodes)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_graph_argument_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_graph_argument_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const char * invalid_node_name = "not a valid node name !";
  const char * invalid_node_namespace = "not a valid node namespace !";
  const char * missing_node_name = "missing_mdds_graph_argument_node";
  const char * node_namespace = "/mdds";

  for (TopicNamesByNodeQuery query :
    {rmw_get_publisher_names_and_types_by_node, rmw_get_subscriber_names_and_types_by_node})
  {
    ExpectTopicNamesByNodeQueryResult(
      query, node, &allocator, nullptr, node_namespace, RMW_RET_INVALID_ARGUMENT);
    ExpectTopicNamesByNodeQueryResult(
      query, node, &allocator, invalid_node_name, node_namespace, RMW_RET_INVALID_ARGUMENT);
    ExpectTopicNamesByNodeQueryResult(
      query, node, &allocator, "mdds_graph_argument_node", nullptr, RMW_RET_INVALID_ARGUMENT);
    ExpectTopicNamesByNodeQueryResult(
      query, node, &allocator, "mdds_graph_argument_node", invalid_node_namespace,
      RMW_RET_INVALID_ARGUMENT);
    ExpectTopicNamesByNodeQueryResult(
      query, node, &allocator, missing_node_name, node_namespace,
      RMW_RET_NODE_NAME_NON_EXISTENT);
  }

  for (ServiceNamesByNodeQuery query :
    {rmw_get_service_names_and_types_by_node, rmw_get_client_names_and_types_by_node})
  {
    ExpectServiceNamesByNodeQueryResult(
      query, node, &allocator, nullptr, node_namespace, RMW_RET_INVALID_ARGUMENT);
    ExpectServiceNamesByNodeQueryResult(
      query, node, &allocator, invalid_node_name, node_namespace, RMW_RET_INVALID_ARGUMENT);
    ExpectServiceNamesByNodeQueryResult(
      query, node, &allocator, "mdds_graph_argument_node", nullptr, RMW_RET_INVALID_ARGUMENT);
    ExpectServiceNamesByNodeQueryResult(
      query, node, &allocator, "mdds_graph_argument_node", invalid_node_namespace,
      RMW_RET_INVALID_ARGUMENT);
    ExpectServiceNamesByNodeQueryResult(
      query, node, &allocator, missing_node_name, node_namespace,
      RMW_RET_NODE_NAME_NON_EXISTENT);
  }

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}
