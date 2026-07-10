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

#include <unistd.h>

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/detail/trigger__type_support.hpp>

#include "bridge_backend.hpp"
#include "ipc_broker.hpp"
#include "ipc_protocol.hpp"
#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/events_statuses/matched.h"
#include "rmw/get_network_flow_endpoints.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/network_flow_endpoint_array.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/subscription_options.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_interface/macros.h"

extern "C" {
void FakeMddsBridgeReset(void);
int FakeMddsBridgeInjectFor(
  const char * topicName, const char * typeName, const void * data, uint32_t len,
  uint64_t sequenceNumber);
}

namespace
{
class TempSocketPath
{
public:
  TempSocketPath()
  {
    char templ[] = "/tmp/rmw_mdds_broker_mode_XXXXXX";
    char * dir = mkdtemp(templ);
    if (dir != nullptr) {
      dir_ = dir;
      path_ = dir_ + "/broker.sock";
    }
  }

  ~TempSocketPath()
  {
    if (!path_.empty()) {
      unlink(path_.c_str());
    }
    if (!dir_.empty()) {
      rmdir(dir_.c_str());
    }
  }

  const std::string & path() const
  {
    return path_;
  }

private:
  std::string dir_;
  std::string path_;
};

class EnvVarGuard
{
public:
  explicit EnvVarGuard(const char * name) : name_(name)
  {
    const char * value = getenv(name_.c_str());
    if (value != nullptr) {
      had_value_ = true;
      value_ = value;
    }
  }

  ~EnvVarGuard()
  {
    if (had_value_) {
      setenv(name_.c_str(), value_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

  EnvVarGuard(const EnvVarGuard &) = delete;
  EnvVarGuard & operator=(const EnvVarGuard &) = delete;

private:
  std::string name_;
  bool had_value_ = false;
  std::string value_;
};

void SetEnclave(rmw_init_options_t * options, const char * enclave)
{
  options->allocator.deallocate(options->enclave, options->allocator.state);
  options->enclave = rcutils_strdup(enclave, options->allocator);
  ASSERT_NE(nullptr, options->enclave);
}

bool NodeGraphContains(
  const rmw_node_t * observer, const char * expected_name, const char * expected_namespace)
{
  rcutils_string_array_t node_names = rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t node_namespaces = rcutils_get_zero_initialized_string_array();
  if (rmw_get_node_names(observer, &node_names, &node_namespaces) != RMW_RET_OK) {
    return false;
  }
  bool found = false;
  for (size_t i = 0; i < node_names.size && i < node_namespaces.size; ++i) {
    if (
      node_names.data[i] != nullptr && node_namespaces.data[i] != nullptr &&
      std::strcmp(node_names.data[i], expected_name) == 0 &&
      std::strcmp(node_namespaces.data[i], expected_namespace) == 0) {
      found = true;
      break;
    }
  }
  EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&node_names));
  EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&node_namespaces));
  return found;
}

rmw_mdds_cpp::ipc::EndpointDescriptor MakeRemoteGraphServiceEndpoint(
  const char * service_name, const char * type_name)
{
  rmw_mdds_cpp::ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = 0x5152535455565758u;
  endpoint.kind = rmw_mdds_cpp::ipc::EndpointKind::kService;
  endpoint.node_name = "remote_graph_service_node";
  endpoint.node_namespace = "/remote_graph";
  endpoint.topic_name = service_name;
  endpoint.type_name = type_name;
  endpoint.mdds_type_name = type_name;
  endpoint.domain_id = 0u;
  endpoint.local_context_id = 0x12345678u;
  endpoint.qos = rmw_qos_profile_services_default;
  return endpoint;
}
}  // namespace

TEST(RmwMddsBrokerMode, PubSubUsesBrokerWhenEnabled)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_broker_mode_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_broker_mode_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_broker_mode_string", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher) << rmw_get_error_string().str;

  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_broker_mode_string", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription) << rmw_get_error_string().str;

  std_msgs::msg::String msg;
  msg.data = "hello broker mode";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  void * subscription_handle = subscription->data;
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 1;
  subscriptions.subscribers = &subscription_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 200000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set, &timeout));
  EXPECT_NE(nullptr, subscriptions.subscribers[0]);

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ("hello broker mode", received.data);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsBrokerMode, DISABLED_FullParityBrokerModeNetworkFlowEndpointsReportMddsTransport)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_broker_network_flow_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_broker_network_flow_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_broker_network_flow", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher) << rmw_get_error_string().str;

  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_broker_network_flow", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription) << rmw_get_error_string().str;

  rmw_network_flow_endpoint_array_t publisher_endpoints =
    rmw_get_zero_initialized_network_flow_endpoint_array();
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_publisher_get_network_flow_endpoints(publisher, &allocator, &publisher_endpoints));
  ASSERT_EQ(1u, publisher_endpoints.size)
    << "broker/MDDS mode must expose real transport or broker network-flow metadata";
  ASSERT_NE(nullptr, publisher_endpoints.network_flow_endpoint);
  EXPECT_EQ(
    RMW_TRANSPORT_PROTOCOL_UNKNOWN,
    publisher_endpoints.network_flow_endpoint[0].transport_protocol);
  EXPECT_EQ(
    RMW_INTERNET_PROTOCOL_UNKNOWN,
    publisher_endpoints.network_flow_endpoint[0].internet_protocol);
  EXPECT_EQ(0u, publisher_endpoints.network_flow_endpoint[0].transport_port);
  EXPECT_STREQ("rmw_mdds_broker", publisher_endpoints.network_flow_endpoint[0].internet_address);

  rmw_network_flow_endpoint_array_t subscription_endpoints =
    rmw_get_zero_initialized_network_flow_endpoint_array();
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_network_flow_endpoints(
      subscription, &allocator, &subscription_endpoints));
  ASSERT_EQ(1u, subscription_endpoints.size)
    << "broker/MDDS mode must expose real transport or broker network-flow metadata";
  ASSERT_NE(nullptr, subscription_endpoints.network_flow_endpoint);
  EXPECT_EQ(
    RMW_TRANSPORT_PROTOCOL_UNKNOWN,
    subscription_endpoints.network_flow_endpoint[0].transport_protocol);
  EXPECT_EQ(
    RMW_INTERNET_PROTOCOL_UNKNOWN,
    subscription_endpoints.network_flow_endpoint[0].internet_protocol);
  EXPECT_EQ(0u, subscription_endpoints.network_flow_endpoint[0].transport_port);
  EXPECT_STREQ(
    "rmw_mdds_broker", subscription_endpoints.network_flow_endpoint[0].internet_address);

  if (subscription_endpoints.size > 0u) {
    EXPECT_EQ(RMW_RET_OK, rmw_network_flow_endpoint_array_fini(&subscription_endpoints));
  }
  if (publisher_endpoints.size > 0u) {
    EXPECT_EQ(RMW_RET_OK, rmw_network_flow_endpoint_array_fini(&publisher_endpoints));
  }
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsBrokerMode, DestroyedBrokerEndpointLeavesNoNodeGraphEntry)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_broker_destroy_graph_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_broker_destroy_graph_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_broker_destroy_graph_topic", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher) << rmw_get_error_string().str;

  ASSERT_TRUE(NodeGraphContains(node, "mdds_broker_destroy_graph_node", "/mdds"));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));

  rmw_node_t * observer =
    rmw_create_node(&context, "mdds_broker_destroy_graph_observer", "/mdds");
  ASSERT_NE(nullptr, observer);
  EXPECT_FALSE(NodeGraphContains(observer, "mdds_broker_destroy_graph_node", "/mdds"));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(observer));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsBrokerMode, EmptyBrokerGraphRefreshIsCached)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_broker_empty_graph_cache_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_broker_empty_graph_cache_node", "/mdds");
  ASSERT_NE(nullptr, node);

  auto query_node_graph = [node]() {
    const auto start = std::chrono::steady_clock::now();
    rcutils_string_array_t node_names = rcutils_get_zero_initialized_string_array();
    rcutils_string_array_t node_namespaces = rcutils_get_zero_initialized_string_array();
    EXPECT_EQ(RMW_RET_OK, rmw_get_node_names(node, &node_names, &node_namespaces));
    EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&node_names));
    EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&node_namespaces));
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  };

  const auto first_query = query_node_graph();
  const auto second_query = query_node_graph();
  EXPECT_LT(first_query, std::chrono::milliseconds(1000))
    << "empty broker graph refresh must settle without the full warmup timeout";
  EXPECT_LT(second_query, std::chrono::milliseconds(1000))
    << "recent empty broker graph refresh must be cached; first query took "
    << first_query.count() << " ms, second query took " << second_query.count() << " ms";

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsBrokerMode, RemoteGraphOnlyServiceDoesNotSatisfyAvailability)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  const std::vector<rmw_mdds_cpp::ipc::EndpointDescriptor> remote_endpoints{
    MakeRemoteGraphServiceEndpoint("/graph_only_trigger", "std_srvs/srv/Trigger")};
  const std::vector<uint8_t> graph_sync =
    rmw_mdds_cpp::ipc::EncodeGraphUpdate(0x6162636465666768u, 1u, remote_endpoints);
  ASSERT_EQ(
    1,
    FakeMddsBridgeInjectFor(
      "mdds_graph_sync", "mdds_graph_EndpointList", graph_sync.data(),
      static_cast<uint32_t>(graph_sync.size()), 1u));

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_broker_graph_only_service_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_broker_graph_only_service_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
    rosidl_typesupport_cpp, std_srvs, srv, Trigger)();
  rmw_client_t * client = rmw_create_client(
    node, type_support, "/graph_only_trigger", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client) << rmw_get_error_string().str;

  size_t service_count = 0u;
  ASSERT_EQ(RMW_RET_OK, rmw_count_services(node, "/graph_only_trigger", &service_count))
    << rmw_get_error_string().str;
  EXPECT_EQ(1u, service_count)
    << "remote graph-sync services should remain visible for graph introspection";

  bool available = true;
  ASSERT_EQ(RMW_RET_OK, rmw_service_server_is_available(node, client, &available))
    << rmw_get_error_string().str;
  EXPECT_FALSE(available)
    << "graph-only remote services must not make clients send before the local "
       "broker bridge request path has matched";

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  broker.Stop();
  FakeMddsBridgeReset();
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
}

TEST(RmwMddsBrokerMode, MissingServiceAvailabilityReturnsPromptly)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_missing_service_prompt_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_missing_service_prompt_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
    rosidl_typesupport_cpp, std_srvs, srv, Trigger)();
  rmw_client_t * client = rmw_create_client(
    node, type_support, "/missing_service_prompt", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client) << rmw_get_error_string().str;

  bool available = true;
  const auto start = std::chrono::steady_clock::now();
  ASSERT_EQ(RMW_RET_OK, rmw_service_server_is_available(node, client, &available))
    << rmw_get_error_string().str;
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_FALSE(available);
  EXPECT_LT(elapsed, std::chrono::milliseconds(500))
    << "service availability is queried inside rcl/rclpy wait_for_service loops "
       "and must not consume the caller's whole timeout budget per poll";

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  broker.Stop();
}

TEST(RmwMddsBrokerMode, CountMatchedEndpointsExcludesIncompatibleQos)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_broker_qos_count_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_broker_qos_count_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_qos_profile_t offered_qos = rmw_qos_profile_default;
  offered_qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  rmw_qos_profile_t requested_qos = rmw_qos_profile_default;
  requested_qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_broker_qos_count", &offered_qos, &publisher_options);
  ASSERT_NE(nullptr, publisher) << rmw_get_error_string().str;
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_broker_qos_count", &requested_qos, &subscription_options);
  ASSERT_NE(nullptr, subscription) << rmw_get_error_string().str;

  size_t subscription_count = 1u;
  EXPECT_EQ(
    RMW_RET_OK, rmw_publisher_count_matched_subscriptions(publisher, &subscription_count));
  EXPECT_EQ(0u, subscription_count);

  size_t publisher_count = 1u;
  EXPECT_EQ(RMW_RET_OK, rmw_subscription_count_matched_publishers(subscription, &publisher_count));
  EXPECT_EQ(0u, publisher_count);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsBrokerMode, ContextFiniClearsStaleGraphCacheBeforeNextContext)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rcutils_allocator_t allocator = rcutils_get_default_allocator();

  rmw_init_options_t first_options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&first_options, allocator));
  SetEnclave(&first_options, "/rmw_mdds_broker_graph_cache_first");

  rmw_context_t first_context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&first_options, &first_context));
  rmw_node_t * first_node =
    rmw_create_node(&first_context, "mdds_broker_graph_cache_first_node", "/mdds");
  ASSERT_NE(nullptr, first_node);

  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  rmw_publisher_t * publisher = rmw_create_publisher(
    first_node, type_support, "/mdds_broker_graph_cache", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher) << rmw_get_error_string().str;
  rmw_subscription_t * subscription = rmw_create_subscription(
    first_node, type_support, "/mdds_broker_graph_cache", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription) << rmw_get_error_string().str;

  size_t matched_subscriptions = 0u;
  for (int attempt = 0; attempt < 20; ++attempt) {
    ASSERT_EQ(
      RMW_RET_OK, rmw_publisher_count_matched_subscriptions(publisher, &matched_subscriptions));
    if (matched_subscriptions == 1u) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_EQ(1u, matched_subscriptions);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(first_node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(first_node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(first_node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&first_context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&first_context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&first_options));

  rmw_init_options_t second_options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&second_options, allocator));
  SetEnclave(&second_options, "/rmw_mdds_broker_graph_cache_second");

  rmw_context_t second_context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&second_options, &second_context));
  rmw_node_t * second_node =
    rmw_create_node(&second_context, "mdds_broker_graph_cache_second_node", "/mdds");
  ASSERT_NE(nullptr, second_node);
  rmw_subscription_t * fresh_subscription = rmw_create_subscription(
    second_node, type_support, "/mdds_broker_graph_cache", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, fresh_subscription) << rmw_get_error_string().str;

  rmw_event_t matched_event = rmw_get_zero_initialized_event();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_event_init(&matched_event, fresh_subscription, RMW_EVENT_SUBSCRIPTION_MATCHED));

  rmw_matched_status_t matched_status{};
  bool taken = false;
  EXPECT_EQ(RMW_RET_OK, rmw_take_event(&matched_event, &matched_status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(0u, matched_status.total_count);
  EXPECT_EQ(0u, matched_status.total_count_change);
  EXPECT_EQ(0u, matched_status.current_count);
  EXPECT_EQ(0, matched_status.current_count_change);

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&matched_event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(second_node, fresh_subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(second_node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&second_context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&second_context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&second_options));
}

TEST(RmwMddsBrokerMode, PublisherMatchedEventTotalsUseBrokerGraphOnlyOnce)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_broker_publisher_event_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_broker_publisher_event_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_broker_publisher_event", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher) << rmw_get_error_string().str;

  rmw_event_t matched_event = rmw_get_zero_initialized_event();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_publisher_event_init(&matched_event, publisher, RMW_EVENT_PUBLICATION_MATCHED));

  rmw_subscription_t * first_subscription = rmw_create_subscription(
    node, type_support, "/mdds_broker_publisher_event", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, first_subscription) << rmw_get_error_string().str;
  rmw_subscription_t * second_subscription = rmw_create_subscription(
    node, type_support, "/mdds_broker_publisher_event", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, second_subscription) << rmw_get_error_string().str;

  size_t matched_subscriptions = 0u;
  for (int attempt = 0; attempt < 20; ++attempt) {
    ASSERT_EQ(
      RMW_RET_OK, rmw_publisher_count_matched_subscriptions(publisher, &matched_subscriptions));
    if (matched_subscriptions == 2u) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_EQ(2u, matched_subscriptions);

  rmw_matched_status_t matched_status{};
  bool taken = false;
  EXPECT_EQ(RMW_RET_OK, rmw_take_event(&matched_event, &matched_status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(2u, matched_status.total_count);
  EXPECT_EQ(2u, matched_status.total_count_change);
  EXPECT_EQ(2u, matched_status.current_count);
  EXPECT_EQ(2, matched_status.current_count_change);

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&matched_event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, second_subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, first_subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsBrokerMode, IgnoreLocalPublicationsSkipsOnlyLocalIgnoredSubscription)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_broker_ignore_local_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_broker_ignore_local_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t ignored_options = rmw_get_default_subscription_options();
  ignored_options.ignore_local_publications = true;
  rmw_subscription_options_t normal_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_broker_ignore_local", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher) << rmw_get_error_string().str;
  rmw_subscription_t * ignored_subscription = rmw_create_subscription(
    node, type_support, "/mdds_broker_ignore_local", &rmw_qos_profile_default,
    &ignored_options);
  ASSERT_NE(nullptr, ignored_subscription) << rmw_get_error_string().str;
  rmw_subscription_t * normal_subscription = rmw_create_subscription(
    node, type_support, "/mdds_broker_ignore_local", &rmw_qos_profile_default,
    &normal_options);
  ASSERT_NE(nullptr, normal_subscription) << rmw_get_error_string().str;

  std_msgs::msg::String msg;
  msg.data = "local broker sample";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  void * subscription_handles[2] = {ignored_subscription->data, normal_subscription->data};
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 2;
  subscriptions.subscribers = subscription_handles;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 200000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 2);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set, &timeout));

  std_msgs::msg::String ignored_received;
  bool ignored_taken = true;
  EXPECT_EQ(
    RMW_RET_OK, rmw_take(ignored_subscription, &ignored_received, &ignored_taken, nullptr));
  EXPECT_FALSE(ignored_taken);

  std_msgs::msg::String normal_received;
  bool normal_taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(normal_subscription, &normal_received, &normal_taken, nullptr));
  EXPECT_TRUE(normal_taken);
  EXPECT_EQ("local broker sample", normal_received.data);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, normal_subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, ignored_subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}
