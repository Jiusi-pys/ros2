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
#include <example_interfaces/srv/detail/add_two_ints__type_support.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <std_msgs/msg/string.hpp>
#include <vector>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/serialized_message.h"
#include "rmw/subscription_options.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_interface/macros.h"

extern "C" void FakeMddsBridgeReset(void);
extern "C" int FakeMddsBridgeInitCount(void);
extern "C" int FakeMddsBridgePublishCount(void);
extern "C" const char *FakeMddsBridgeLastPublisherTopic(void);
extern "C" const char *FakeMddsBridgeLastSubscriberTopic(void);
extern "C" const char *FakeMddsBridgeLastPublisherType(void);
extern "C" const char *FakeMddsBridgeLastSubscriberType(void);
extern "C" const uint8_t *FakeMddsBridgeLastPayloadData(void);
extern "C" uint32_t FakeMddsBridgeLastPayloadLen(void);
extern "C" void FakeMddsBridgeSetPublisherUnackedCount(
  const char *topicName, const char *typeName, uint32_t count);
extern "C" void FakeMddsBridgeInject(const void *data, uint32_t len);
extern "C" void FakeMddsBridgeInjectWithMetadata(const void *data, uint32_t len,
                                                 uint64_t sequenceNumber,
                                                 const uint8_t *senderGuid);

namespace {
struct AddTwoIntsRequest {
  int64_t a;
  int64_t b;
};

struct AddTwoIntsResponse {
  int64_t sum;
};

void SetEnclave(rmw_init_options_t *options, const char *enclave) {
  options->allocator.deallocate(options->enclave, options->allocator.state);
  options->enclave = rcutils_strdup(enclave, options->allocator);
  ASSERT_NE(nullptr, options->enclave);
}

void AppendU32(std::vector<uint8_t> *payload, uint32_t value) {
  payload->push_back(static_cast<uint8_t>(value & 0xffu));
  payload->push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  payload->push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  payload->push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

void AppendI32(std::vector<uint8_t> *payload, int32_t value) {
  AppendU32(payload, static_cast<uint32_t>(value));
}

void AppendF64(std::vector<uint8_t> *payload, double value) {
  const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
  payload->insert(payload->end(), bytes, bytes + sizeof(value));
}

std::vector<uint8_t>
EncodeGatewayPoseStamped(const geometry_msgs::msg::PoseStamped &msg) {
  std::vector<uint8_t> payload;
  payload.reserve(4u + msg.header.frame_id.size() + 8u + 7u * sizeof(double));
  AppendU32(&payload, static_cast<uint32_t>(msg.header.frame_id.size()));
  payload.insert(payload.end(), msg.header.frame_id.begin(),
                 msg.header.frame_id.end());
  AppendI32(&payload, msg.header.stamp.sec);
  AppendU32(&payload, msg.header.stamp.nanosec);
  AppendF64(&payload, msg.pose.position.x);
  AppendF64(&payload, msg.pose.position.y);
  AppendF64(&payload, msg.pose.position.z);
  AppendF64(&payload, msg.pose.orientation.x);
  AppendF64(&payload, msg.pose.orientation.y);
  AppendF64(&payload, msg.pose.orientation.z);
  AppendF64(&payload, msg.pose.orientation.w);
  return payload;
}

geometry_msgs::msg::PoseStamped
MakePoseStamped(const char *frame_id, int32_t sec, uint32_t nanosec, double x) {
  geometry_msgs::msg::PoseStamped msg;
  msg.header.frame_id = frame_id;
  msg.header.stamp.sec = sec;
  msg.header.stamp.nanosec = nanosec;
  msg.pose.position.x = x;
  msg.pose.position.y = -3.5;
  msg.pose.position.z = 0.125;
  msg.pose.orientation.x = 0.0;
  msg.pose.orientation.y = 0.0;
  msg.pose.orientation.z = 0.7071067811865476;
  msg.pose.orientation.w = 0.7071067811865476;
  return msg;
}

std::vector<uint8_t> SerializedBytes(const rmw_serialized_message_t &message) {
  return std::vector<uint8_t>(message.buffer,
                              message.buffer + message.buffer_length);
}
} // namespace

TEST(RmwMddsBridgeBackend, UsesConfiguredBridgeLibrary) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  EXPECT_EQ(1, FakeMddsBridgeInitCount());
  rmw_node_t *node = rmw_create_node(&context, "mdds_bridge_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_bridge_string",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t *subscription =
      rmw_create_subscription(node, type_support, "/mdds_bridge_string",
                              &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);
  EXPECT_STREQ("mdds_bridge_string", FakeMddsBridgeLastPublisherTopic());
  EXPECT_STREQ("mdds_bridge_string", FakeMddsBridgeLastSubscriberTopic());
  EXPECT_STREQ("std_msgs/msg/String", FakeMddsBridgeLastPublisherType());
  EXPECT_STREQ("std_msgs/msg/String", FakeMddsBridgeLastSubscriberType());

  std_msgs::msg::String msg;
  msg.data = "bridge backed";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));
  EXPECT_EQ(1, FakeMddsBridgePublishCount());
  ASSERT_EQ(msg.data.size(), FakeMddsBridgeLastPayloadLen());
  ASSERT_EQ(0, std::memcmp(msg.data.data(), FakeMddsBridgeLastPayloadData(),
                           msg.data.size()));

  std_msgs::msg::String received;
  bool taken = false;
  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(RMW_RET_OK, rmw_take_with_info(subscription, &received, &taken,
                                           &message_info, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ("bridge backed", received.data);
  EXPECT_EQ(1u, message_info.publication_sequence_number);
  EXPECT_EQ(1u, message_info.reception_sequence_number);

  const char injected[] = "from gateway";
  uint8_t sender_guid[16];
  for (size_t i = 0; i < sizeof(sender_guid); ++i) {
    sender_guid[i] = static_cast<uint8_t>(0xa0u + i);
  }
  FakeMddsBridgeInjectWithMetadata(
      injected, static_cast<uint32_t>(std::strlen(injected)), 77u, sender_guid);
  received.data.clear();
  taken = false;
  message_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(RMW_RET_OK, rmw_take_with_info(subscription, &received, &taken,
                                           &message_info, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ("from gateway", received.data);
  EXPECT_EQ(77u, message_info.publication_sequence_number);
  EXPECT_EQ(2u, message_info.reception_sequence_number);
  EXPECT_STREQ("rmw_mdds_cpp",
               message_info.publisher_gid.implementation_identifier);
  EXPECT_EQ(0, std::memcmp(sender_guid, message_info.publisher_gid.data,
                           sizeof(sender_guid)));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
}

TEST(RmwMddsBridgeBackend, UsesGatewayPoseStampedRawPayload) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_pose_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_pose_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          geometry_msgs::msg::PoseStamped>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_bridge_pose",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t *subscription =
      rmw_create_subscription(node, type_support, "/mdds_bridge_pose",
                              &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);
  EXPECT_STREQ("mdds_bridge_pose", FakeMddsBridgeLastPublisherTopic());
  EXPECT_STREQ("mdds_bridge_pose", FakeMddsBridgeLastSubscriberTopic());
  EXPECT_STREQ("geometry_msgs/msg/PoseStamped",
               FakeMddsBridgeLastPublisherType());
  EXPECT_STREQ("geometry_msgs/msg/PoseStamped",
               FakeMddsBridgeLastSubscriberType());

  geometry_msgs::msg::PoseStamped msg =
      MakePoseStamped("mdds_frame", 1718000000, 123456789u, 7.0);
  const std::vector<uint8_t> expected_payload = EncodeGatewayPoseStamped(msg);
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));
  ASSERT_EQ(expected_payload.size(), FakeMddsBridgeLastPayloadLen());
  EXPECT_EQ(expected_payload,
            std::vector<uint8_t>(FakeMddsBridgeLastPayloadData(),
                                 FakeMddsBridgeLastPayloadData() +
                                     FakeMddsBridgeLastPayloadLen()));

  geometry_msgs::msg::PoseStamped received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("mdds_frame", received.header.frame_id);

  geometry_msgs::msg::PoseStamped injected =
      MakePoseStamped("dds_frame", -7, 42u, 42.5);
  const std::vector<uint8_t> injected_payload =
      EncodeGatewayPoseStamped(injected);
  FakeMddsBridgeInject(injected_payload.data(),
                       static_cast<uint32_t>(injected_payload.size()));
  received = geometry_msgs::msg::PoseStamped();
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ("dds_frame", received.header.frame_id);
  EXPECT_EQ(-7, received.header.stamp.sec);
  EXPECT_EQ(42u, received.header.stamp.nanosec);
  EXPECT_DOUBLE_EQ(42.5, received.pose.position.x);
  EXPECT_DOUBLE_EQ(-3.5, received.pose.position.y);
  EXPECT_DOUBLE_EQ(0.125, received.pose.position.z);
  EXPECT_DOUBLE_EQ(0.7071067811865476, received.pose.orientation.z);
  EXPECT_DOUBLE_EQ(0.7071067811865476, received.pose.orientation.w);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
}

TEST(RmwMddsBridgeBackend, SerializedStringApiConvertsBridgePayloads) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_serialized_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_serialized_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_bridge_serialized_string",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_bridge_serialized_string",
      &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_serialized_message_t outgoing =
      rmw_get_zero_initialized_serialized_message();
  rmw_serialized_message_t incoming =
      rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RCUTILS_RET_OK,
            rmw_serialized_message_init(&outgoing, 1, &allocator));
  ASSERT_EQ(RCUTILS_RET_OK,
            rmw_serialized_message_init(&incoming, 1, &allocator));

  std_msgs::msg::String msg;
  msg.data = "serialized bridge payload";
  ASSERT_EQ(RMW_RET_OK, rmw_serialize(&msg, type_support, &outgoing));
  ASSERT_EQ(RMW_RET_OK,
            rmw_publish_serialized_message(publisher, &outgoing, nullptr));
  EXPECT_EQ(msg.data.size(), FakeMddsBridgeLastPayloadLen());
  EXPECT_EQ(0, std::memcmp(msg.data.data(), FakeMddsBridgeLastPayloadData(),
                           msg.data.size()));

  std_msgs::msg::String typed_received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK,
            rmw_take(subscription, &typed_received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(msg.data, typed_received.data);

  const char injected[] = "raw gateway payload";
  FakeMddsBridgeInject(injected, static_cast<uint32_t>(std::strlen(injected)));
  ASSERT_EQ(RMW_RET_OK, rmw_take_serialized_message(subscription, &incoming,
                                                    &taken, nullptr));
  ASSERT_TRUE(taken);
  std_msgs::msg::String serialized_received;
  ASSERT_EQ(RMW_RET_OK,
            rmw_deserialize(&incoming, type_support, &serialized_received))
      << "incoming bytes: "
      << testing::PrintToString(SerializedBytes(incoming));
  EXPECT_EQ(injected, serialized_received.data);

  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&incoming));
  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&outgoing));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
}

TEST(RmwMddsBridgeBackend, ReliableBridgePublisherWaitForAllAckedTimesOutWithoutAck) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_wait_for_acked_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_wait_for_acked_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_qos_profile_t reliable_qos = rmw_qos_profile_default;
  reliable_qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;

  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_bridge_wait_for_acked",
                           &reliable_qos, &publisher_options);
  ASSERT_NE(nullptr, publisher);

  rmw_time_t zero_timeout;
  zero_timeout.sec = 0;
  zero_timeout.nsec = 0;
  EXPECT_EQ(RMW_RET_OK, rmw_publisher_wait_for_all_acked(publisher, zero_timeout));

  std_msgs::msg::String msg;
  msg.data = "requires ack";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));
  EXPECT_EQ(1, FakeMddsBridgePublishCount());

  EXPECT_EQ(
      RMW_RET_TIMEOUT,
      rmw_publisher_wait_for_all_acked(publisher, zero_timeout));
  FakeMddsBridgeSetPublisherUnackedCount(
      "mdds_bridge_wait_for_acked", "std_msgs/msg/String", 0u);
  EXPECT_EQ(RMW_RET_OK, rmw_publisher_wait_for_all_acked(publisher, zero_timeout));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
}

TEST(RmwMddsBridgeBackend, ServiceClientRoundTripUsesBridgeTopics) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_service_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_service_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t *type_support =
      ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
          rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_service_t *service =
      rmw_create_service(node, type_support, "/mdds_bridge_add_two_ints",
                         &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t *client =
      rmw_create_client(node, type_support, "/mdds_bridge_add_two_ints",
                        &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  AddTwoIntsRequest request{11, 31};
  int64_t sequence_id = -1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &request, &sequence_id));
  EXPECT_EQ(1, FakeMddsBridgePublishCount());
  EXPECT_STREQ("rq/mdds_bridge_add_two_ints",
               FakeMddsBridgeLastPublisherTopic());
  EXPECT_STREQ("example_interfaces/srv/AddTwoInts_Request",
               FakeMddsBridgeLastPublisherType());

  void *service_handle = service->data;
  rmw_services_t services;
  services.service_count = 1;
  services.services = &service_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t *wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(RMW_RET_OK, rmw_wait(nullptr, nullptr, &services, nullptr, nullptr,
                                 wait_set, &timeout));
  ASSERT_NE(nullptr, services.services[0]);

  AddTwoIntsRequest received_request{0, 0};
  rmw_service_info_t request_header{};
  bool request_taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_request(service, &request_header,
                                         &received_request, &request_taken));
  ASSERT_TRUE(request_taken);
  EXPECT_EQ(sequence_id, request_header.request_id.sequence_number);
  EXPECT_EQ(11, received_request.a);
  EXPECT_EQ(31, received_request.b);

  AddTwoIntsResponse response{received_request.a + received_request.b};
  ASSERT_EQ(RMW_RET_OK,
            rmw_send_response(service, &request_header.request_id, &response));
  EXPECT_EQ(2, FakeMddsBridgePublishCount());
  EXPECT_STREQ("rr/mdds_bridge_add_two_ints",
               FakeMddsBridgeLastPublisherTopic());
  EXPECT_STREQ("example_interfaces/srv/AddTwoInts_Response",
               FakeMddsBridgeLastPublisherType());

  void *client_handle = client->data;
  rmw_clients_t clients;
  clients.client_count = 1;
  clients.clients = &client_handle;
  ASSERT_EQ(RMW_RET_OK, rmw_wait(nullptr, nullptr, nullptr, &clients, nullptr,
                                 wait_set, &timeout));
  ASSERT_NE(nullptr, clients.clients[0]);

  AddTwoIntsResponse received_response{0};
  rmw_service_info_t response_header{};
  bool response_taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_response(client, &response_header,
                                          &received_response, &response_taken));
  ASSERT_TRUE(response_taken);
  EXPECT_EQ(sequence_id, response_header.request_id.sequence_number);
  EXPECT_EQ(42, received_response.sum);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
}
