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
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/string.hpp>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/dynamic_message_type_support.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/serialized_message.h"
#include "rmw/subscription_content_filter_options.h"
#include "rmw/subscription_options.h"
#include "rosidl_dynamic_typesupport/api/dynamic_data.h"
#include "rosidl_dynamic_typesupport/api/dynamic_type.h"
#include "rosidl_dynamic_typesupport/api/serialization_support.h"
#include "rosidl_dynamic_typesupport/types.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"

extern "C" void FakeMddsBridgeReset(void);
extern "C" int FakeMddsBridgeSubscriberTakeLoanedCount(void);
extern "C" int FakeMddsBridgeSubscriberTakeLoanedWithStorageCount(void);
extern "C" int FakeMddsBridgeSubscriberReturnLoanedCount(void);
extern "C" void *FakeMddsBridgeLastSubscriberTypedStorage(void);
extern "C" uint32_t FakeMddsBridgeLastSubscriberTypedStorageSize(void);
extern "C" int FakeMddsBridgeQueueLoanedFor(const char *topicName,
                                            const char *typeName,
                                            const void *data, uint32_t len,
                                            uint64_t sequenceNumber,
                                            const uint8_t *senderGuid);

namespace {
void SetEnclave(rmw_init_options_t *options, const char *enclave) {
  options->allocator.deallocate(options->enclave, options->allocator.state);
  options->enclave = rcutils_strdup(enclave, options->allocator);
  ASSERT_NE(nullptr, options->enclave);
}
} // namespace

TEST(RmwMddsBridgeLoanedTakeRmw,
     TakeLoanedMessageUsesBridgeLoanedTakeAndReturn) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_loaned_take_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_loaned_take_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::Int32>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_bridge_loaned_take_int32",
      &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);
  ASSERT_TRUE(subscription->can_loan_messages);

  std_msgs::msg::Int32 payload;
  payload.data = 3588;
  uint8_t sender_guid[16];
  for (size_t i = 0; i < sizeof(sender_guid); ++i) {
    sender_guid[i] = static_cast<uint8_t>(0xb0u + i);
  }
  ASSERT_EQ(1, FakeMddsBridgeQueueLoanedFor(
                   "mdds_bridge_loaned_take_int32", "std_msgs/msg/Int32",
                   &payload, static_cast<uint32_t>(sizeof(payload)), 88u,
                   sender_guid));

  void *loaned_message = nullptr;
  bool taken = false;
  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(RMW_RET_OK,
            rmw_take_loaned_message_with_info(
                subscription, &loaned_message, &taken, &message_info, nullptr));
  ASSERT_TRUE(taken) << "take_count="
                     << FakeMddsBridgeSubscriberTakeLoanedCount()
                     << " return_count="
                     << FakeMddsBridgeSubscriberReturnLoanedCount();
  ASSERT_NE(nullptr, loaned_message);
  EXPECT_EQ(1, FakeMddsBridgeSubscriberTakeLoanedCount());
  EXPECT_EQ(0, FakeMddsBridgeSubscriberTakeLoanedWithStorageCount());
  EXPECT_EQ(nullptr, FakeMddsBridgeLastSubscriberTypedStorage());
  EXPECT_EQ(0u, FakeMddsBridgeLastSubscriberTypedStorageSize());
  const auto *msg = static_cast<const std_msgs::msg::Int32 *>(loaned_message);
  EXPECT_EQ(payload.data, msg->data);
  EXPECT_EQ(88u, message_info.publication_sequence_number);
  EXPECT_EQ(1u, message_info.reception_sequence_number);
  EXPECT_STREQ("rmw_mdds_cpp",
               message_info.publisher_gid.implementation_identifier);
  EXPECT_EQ(0, std::memcmp(sender_guid, message_info.publisher_gid.data,
                           sizeof(sender_guid)));
  EXPECT_EQ(0, FakeMddsBridgeSubscriberReturnLoanedCount());

  EXPECT_EQ(RMW_RET_OK,
            rmw_return_loaned_message_from_subscription(subscription,
                                                        loaned_message));
  EXPECT_EQ(1, FakeMddsBridgeSubscriberReturnLoanedCount());

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedTakeRmw,
     ReturnLoanedMessageRejectsForeignPointerWithoutFreeingIt) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_foreign_loan_return_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node = rmw_create_node(
      &context, "mdds_bridge_foreign_loan_return_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::Int32>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_bridge_foreign_loan_return_int32",
      &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);
  ASSERT_TRUE(subscription->can_loan_messages);

  std_msgs::msg::Int32 foreign_message;
  foreign_message.data = 3588;
  EXPECT_EQ(RMW_RET_ERROR, rmw_return_loaned_message_from_subscription(
                               subscription, &foreign_message));
  EXPECT_EQ(3588, foreign_message.data);
  EXPECT_EQ(0, FakeMddsBridgeSubscriberReturnLoanedCount());
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedTakeRmw,
     TakeLoanedMessageDoesNotHeapFallbackToQueuedLocalSample) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_loaned_take_local_fallback_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_loaned_take_local_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_publisher_t *publisher = rmw_create_publisher(
      node, type_support, "/mdds_bridge_loaned_take_no_heap_fallback",
      &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_bridge_loaned_take_no_heap_fallback",
      &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);
  ASSERT_TRUE(subscription->can_loan_messages);

  std_msgs::msg::Int32 outgoing;
  outgoing.data = 42;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &outgoing, nullptr));

  void *loaned_message = nullptr;
  bool taken = false;
  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(RMW_RET_OK,
            rmw_take_loaned_message_with_info(
                subscription, &loaned_message, &taken, &message_info, nullptr));
  EXPECT_FALSE(taken);
  EXPECT_EQ(nullptr, loaned_message);
  EXPECT_EQ(0, FakeMddsBridgeSubscriberTakeLoanedCount());
  EXPECT_EQ(0, FakeMddsBridgeSubscriberReturnLoanedCount());

  std_msgs::msg::Int32 received;
  bool copied_taken = false;
  ASSERT_EQ(RMW_RET_OK,
            rmw_take_with_info(subscription, &received, &copied_taken,
                               &message_info, nullptr));
  ASSERT_TRUE(copied_taken);
  EXPECT_EQ(outgoing.data, received.data);
  EXPECT_EQ(1u, message_info.publication_sequence_number);
  EXPECT_EQ(1u, message_info.reception_sequence_number);
  EXPECT_EQ(0, FakeMddsBridgeSubscriberReturnLoanedCount());

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedTakeRmw,
     TakeWithInfoConsumesBridgeLoanedPayloadAndReturnsLoan) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_loaned_take_copy_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_loaned_take_copy_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_bridge_loaned_take_copy_string",
      &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char payload[] = "loaned typed take bridge";
  uint8_t sender_guid[16];
  for (size_t i = 0; i < sizeof(sender_guid); ++i) {
    sender_guid[i] = static_cast<uint8_t>(0x70u + i);
  }
  ASSERT_EQ(1, FakeMddsBridgeQueueLoanedFor(
                   "mdds_bridge_loaned_take_copy_string",
                   "std_msgs/msg/String", payload,
                   static_cast<uint32_t>(std::strlen(payload)), 1888u,
                   sender_guid));

  std_msgs::msg::String received;
  bool taken = false;
  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(RMW_RET_OK,
            rmw_take_with_info(subscription, &received, &taken, &message_info,
                               nullptr));
  ASSERT_TRUE(taken) << "take_count="
                     << FakeMddsBridgeSubscriberTakeLoanedCount()
                     << " return_count="
                     << FakeMddsBridgeSubscriberReturnLoanedCount();
  EXPECT_EQ(payload, received.data);
  EXPECT_EQ(1, FakeMddsBridgeSubscriberTakeLoanedCount());
  EXPECT_EQ(1, FakeMddsBridgeSubscriberReturnLoanedCount());
  EXPECT_EQ(1888u, message_info.publication_sequence_number);
  EXPECT_EQ(1u, message_info.reception_sequence_number);
  EXPECT_STREQ("rmw_mdds_cpp",
               message_info.publisher_gid.implementation_identifier);
  EXPECT_EQ(0, std::memcmp(sender_guid, message_info.publisher_gid.data,
                           sizeof(sender_guid)));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedTakeRmw,
     TakeSequenceConsumesBridgeLoanedPayloadsAndReturnsLoans) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_loaned_take_sequence_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_loaned_take_sequence_node",
                      "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_bridge_loaned_take_sequence_string",
      &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  uint8_t first_guid[16];
  uint8_t second_guid[16];
  for (size_t i = 0; i < sizeof(first_guid); ++i) {
    first_guid[i] = static_cast<uint8_t>(0xa0u + i);
    second_guid[i] = static_cast<uint8_t>(0xb0u + i);
  }
  const char first_payload[] = "loaned sequence first";
  const char second_payload[] = "loaned sequence second";
  ASSERT_EQ(1, FakeMddsBridgeQueueLoanedFor(
                   "mdds_bridge_loaned_take_sequence_string",
                   "std_msgs/msg/String", first_payload,
                   static_cast<uint32_t>(std::strlen(first_payload)), 301u,
                   first_guid));
  ASSERT_EQ(1, FakeMddsBridgeQueueLoanedFor(
                   "mdds_bridge_loaned_take_sequence_string",
                   "std_msgs/msg/String", second_payload,
                   static_cast<uint32_t>(std::strlen(second_payload)), 302u,
                   second_guid));

  std_msgs::msg::String first;
  std_msgs::msg::String second;
  rmw_message_sequence_t messages = rmw_get_zero_initialized_message_sequence();
  ASSERT_EQ(RMW_RET_OK, rmw_message_sequence_init(&messages, 2, &allocator));
  messages.data[0] = &first;
  messages.data[1] = &second;
  rmw_message_info_sequence_t infos =
      rmw_get_zero_initialized_message_info_sequence();
  ASSERT_EQ(RMW_RET_OK, rmw_message_info_sequence_init(&infos, 2, &allocator));

  size_t taken = 0;
  ASSERT_EQ(RMW_RET_OK,
            rmw_take_sequence(subscription, 2, &messages, &infos, &taken,
                              nullptr));
  EXPECT_EQ(2u, taken) << "take_count="
                       << FakeMddsBridgeSubscriberTakeLoanedCount()
                       << " return_count="
                       << FakeMddsBridgeSubscriberReturnLoanedCount();
  EXPECT_EQ(2u, messages.size);
  EXPECT_EQ(2u, infos.size);
  EXPECT_EQ(first_payload, first.data);
  EXPECT_EQ(second_payload, second.data);
  EXPECT_EQ(301u, infos.data[0].publication_sequence_number);
  EXPECT_EQ(302u, infos.data[1].publication_sequence_number);
  EXPECT_EQ(1u, infos.data[0].reception_sequence_number);
  EXPECT_EQ(2u, infos.data[1].reception_sequence_number);
  EXPECT_EQ(0, std::memcmp(first_guid, infos.data[0].publisher_gid.data,
                           sizeof(first_guid)));
  EXPECT_EQ(0, std::memcmp(second_guid, infos.data[1].publisher_gid.data,
                           sizeof(second_guid)));
  EXPECT_EQ(2, FakeMddsBridgeSubscriberTakeLoanedCount());
  EXPECT_EQ(2, FakeMddsBridgeSubscriberReturnLoanedCount());

  EXPECT_EQ(RMW_RET_OK, rmw_message_info_sequence_fini(&infos));
  EXPECT_EQ(RMW_RET_OK, rmw_message_sequence_fini(&messages));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedTakeRmw,
     SerializedTakeWithInfoConsumesBridgeLoanedPayloadAndReturnsLoan) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_serialized_loaned_take_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_serialized_loaned_take_node",
                      "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_bridge_serialized_loaned_take_string",
      &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char payload[] = "loaned serialized bridge";
  uint8_t sender_guid[16];
  for (size_t i = 0; i < sizeof(sender_guid); ++i) {
    sender_guid[i] = static_cast<uint8_t>(0xd0u + i);
  }
  ASSERT_EQ(1, FakeMddsBridgeQueueLoanedFor(
                   "mdds_bridge_serialized_loaned_take_string",
                   "std_msgs/msg/String", payload,
                   static_cast<uint32_t>(std::strlen(payload)), 188u,
                   sender_guid));

  rmw_serialized_message_t serialized =
      rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RCUTILS_RET_OK,
            rmw_serialized_message_init(&serialized, 0, &allocator));
  bool taken = false;
  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(RMW_RET_OK, rmw_take_serialized_message_with_info(
                            subscription, &serialized, &taken, &message_info,
                            nullptr));
  ASSERT_TRUE(taken) << "take_count="
                     << FakeMddsBridgeSubscriberTakeLoanedCount()
                     << " return_count="
                     << FakeMddsBridgeSubscriberReturnLoanedCount();
  EXPECT_EQ(1, FakeMddsBridgeSubscriberTakeLoanedCount());
  EXPECT_EQ(1, FakeMddsBridgeSubscriberReturnLoanedCount());

  std_msgs::msg::String received;
  ASSERT_EQ(RMW_RET_OK, rmw_deserialize(&serialized, type_support, &received));
  EXPECT_EQ(payload, received.data);
  EXPECT_EQ(188u, message_info.publication_sequence_number);
  EXPECT_EQ(1u, message_info.reception_sequence_number);
  EXPECT_EQ(0, std::memcmp(sender_guid, message_info.publisher_gid.data,
                           sizeof(sender_guid)));

  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&serialized));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedTakeRmw,
     SerializedTakeWithInfoAppliesContentFilterToBridgeLoanedPayloads) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_serialized_loaned_take_filter_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_serialized_loaned_take_filter_node",
                      "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_bridge_serialized_loaned_take_filter_string",
      &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char *parameters[] = {"keep%"};
  rmw_subscription_content_filter_options_t filter_options =
      rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(RMW_RET_OK,
            rmw_subscription_content_filter_options_init(
                "data LIKE %0", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK,
            rmw_subscription_set_content_filter(subscription, &filter_options));

  uint8_t dropped_guid[16];
  uint8_t accepted_guid[16];
  for (size_t i = 0; i < sizeof(accepted_guid); ++i) {
    dropped_guid[i] = static_cast<uint8_t>(0x90u + i);
    accepted_guid[i] = static_cast<uint8_t>(0xa0u + i);
  }
  const char dropped_payload[] = "drop-serialized";
  const char accepted_payload[] = "keep-serialized";
  ASSERT_EQ(1, FakeMddsBridgeQueueLoanedFor(
                   "mdds_bridge_serialized_loaned_take_filter_string",
                   "std_msgs/msg/String", dropped_payload,
                   static_cast<uint32_t>(std::strlen(dropped_payload)), 288u,
                   dropped_guid));
  ASSERT_EQ(1, FakeMddsBridgeQueueLoanedFor(
                   "mdds_bridge_serialized_loaned_take_filter_string",
                   "std_msgs/msg/String", accepted_payload,
                   static_cast<uint32_t>(std::strlen(accepted_payload)), 289u,
                   accepted_guid));

  rmw_serialized_message_t serialized =
      rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RCUTILS_RET_OK,
            rmw_serialized_message_init(&serialized, 0, &allocator));
  bool taken = false;
  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(RMW_RET_OK, rmw_take_serialized_message_with_info(
                            subscription, &serialized, &taken, &message_info,
                            nullptr));
  ASSERT_TRUE(taken) << "take_count="
                     << FakeMddsBridgeSubscriberTakeLoanedCount()
                     << " return_count="
                     << FakeMddsBridgeSubscriberReturnLoanedCount();
  EXPECT_EQ(2, FakeMddsBridgeSubscriberTakeLoanedCount());
  EXPECT_EQ(2, FakeMddsBridgeSubscriberReturnLoanedCount());

  std_msgs::msg::String received;
  ASSERT_EQ(RMW_RET_OK, rmw_deserialize(&serialized, type_support, &received));
  EXPECT_EQ(accepted_payload, received.data);
  EXPECT_EQ(289u, message_info.publication_sequence_number);
  EXPECT_EQ(1u, message_info.reception_sequence_number);
  EXPECT_EQ(0, std::memcmp(accepted_guid, message_info.publisher_gid.data,
                           sizeof(accepted_guid)));

  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&serialized));
  EXPECT_EQ(RMW_RET_OK,
            rmw_subscription_content_filter_options_fini(&filter_options,
                                                         &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedTakeRmw,
     DynamicTakeWithInfoConsumesBridgeLoanedPayloadAndReturnsLoan) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rosidl_dynamic_typesupport_serialization_support_t support =
      rosidl_dynamic_typesupport_get_zero_initialized_serialization_support();
  ASSERT_EQ(RMW_RET_OK, rmw_serialization_support_init("fastcdr", &allocator,
                                                       &support));

  rosidl_dynamic_typesupport_dynamic_type_builder_t builder =
      rosidl_dynamic_typesupport_get_zero_initialized_dynamic_type_builder();
  constexpr const char *type_name = "std_msgs::msg::dds_::String_";
  constexpr const char *member_name = "data";
  ASSERT_EQ(RCUTILS_RET_OK,
            rosidl_dynamic_typesupport_dynamic_type_builder_init(
                &support, type_name, std::strlen(type_name), &allocator,
                &builder));
  ASSERT_EQ(RCUTILS_RET_OK,
            rosidl_dynamic_typesupport_dynamic_type_builder_add_string_member(
                &builder, 0, member_name, std::strlen(member_name), "", 0));

  rosidl_dynamic_typesupport_dynamic_data_t dynamic_data =
      rosidl_dynamic_typesupport_get_zero_initialized_dynamic_data();
  ASSERT_EQ(RCUTILS_RET_OK,
            rosidl_dynamic_typesupport_dynamic_data_init_from_dynamic_type_builder(
                &builder, &allocator, &dynamic_data));

  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_dynamic_loaned_take_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_dynamic_loaned_take_node",
                      "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_bridge_dynamic_loaned_take_string",
      &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char payload[] = "loaned dynamic bridge";
  uint8_t sender_guid[16];
  for (size_t i = 0; i < sizeof(sender_guid); ++i) {
    sender_guid[i] = static_cast<uint8_t>(0xc0u + i);
  }
  ASSERT_EQ(1, FakeMddsBridgeQueueLoanedFor(
                   "mdds_bridge_dynamic_loaned_take_string",
                   "std_msgs/msg/String", payload,
                   static_cast<uint32_t>(std::strlen(payload)), 144u,
                   sender_guid));

  bool taken = false;
  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(RMW_RET_OK, rmw_take_dynamic_message_with_info(
                            subscription, &dynamic_data, &taken,
                            &message_info, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(1, FakeMddsBridgeSubscriberTakeLoanedCount());
  EXPECT_EQ(1, FakeMddsBridgeSubscriberReturnLoanedCount());
  EXPECT_EQ(144u, message_info.publication_sequence_number);
  EXPECT_EQ(1u, message_info.reception_sequence_number);
  EXPECT_EQ(0, std::memcmp(sender_guid, message_info.publisher_gid.data,
                           sizeof(sender_guid)));

  rosidl_dynamic_typesupport_member_id_t data_member = 0;
  ASSERT_EQ(RCUTILS_RET_OK,
            rosidl_dynamic_typesupport_dynamic_data_get_member_id_by_name(
                &dynamic_data, member_name, std::strlen(member_name),
                &data_member));
  char *value = nullptr;
  size_t value_length = 0;
  ASSERT_EQ(RCUTILS_RET_OK,
            rosidl_dynamic_typesupport_dynamic_data_get_string_value(
                &dynamic_data, data_member, &value, &value_length));
  ASSERT_NE(nullptr, value);
  EXPECT_EQ(payload, std::string(value, value_length));
  delete[] value;

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  EXPECT_EQ(RCUTILS_RET_OK,
            rosidl_dynamic_typesupport_dynamic_data_fini(&dynamic_data));
  EXPECT_EQ(RCUTILS_RET_OK,
            rosidl_dynamic_typesupport_dynamic_type_builder_fini(&builder));
  EXPECT_EQ(RCUTILS_RET_OK,
            rosidl_dynamic_typesupport_serialization_support_fini(&support));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedTakeRmw,
     TakeLoanedMessageRejectsUnboundedStringContentFilterShape) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_loaned_take_filter_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_loaned_take_filter_node",
                      "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_bridge_loaned_take_filter_string",
      &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);
  EXPECT_FALSE(subscription->can_loan_messages);

  const char *parameters[] = {"keep%"};
  rmw_subscription_content_filter_options_t filter_options =
      rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(RMW_RET_OK,
            rmw_subscription_content_filter_options_init(
                "data LIKE %0", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK,
            rmw_subscription_set_content_filter(subscription, &filter_options));

  uint8_t dropped_guid[16];
  uint8_t accepted_guid[16];
  for (size_t i = 0; i < sizeof(accepted_guid); ++i) {
    dropped_guid[i] = static_cast<uint8_t>(0xe0u + i);
    accepted_guid[i] = static_cast<uint8_t>(0xf0u + i);
  }
  const char dropped_payload[] = "drop-one";
  const char accepted_payload[] = "keep-one";
  ASSERT_EQ(1, FakeMddsBridgeQueueLoanedFor(
                   "mdds_bridge_loaned_take_filter_string",
                   "std_msgs/msg/String", dropped_payload,
                   static_cast<uint32_t>(std::strlen(dropped_payload)), 199u,
                   dropped_guid));
  ASSERT_EQ(1, FakeMddsBridgeQueueLoanedFor(
                   "mdds_bridge_loaned_take_filter_string",
                   "std_msgs/msg/String", accepted_payload,
                   static_cast<uint32_t>(std::strlen(accepted_payload)), 200u,
                   accepted_guid));

  void *loaned_message = nullptr;
  bool taken = false;
  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(RMW_RET_UNSUPPORTED,
            rmw_take_loaned_message_with_info(
                subscription, &loaned_message, &taken, &message_info, nullptr));
  EXPECT_FALSE(taken);
  EXPECT_EQ(nullptr, loaned_message);
  EXPECT_EQ(0, FakeMddsBridgeSubscriberTakeLoanedCount());
  EXPECT_EQ(0, FakeMddsBridgeSubscriberReturnLoanedCount());
  rcutils_reset_error();

  EXPECT_EQ(RMW_RET_OK,
            rmw_subscription_content_filter_options_fini(&filter_options,
                                                         &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedTakeRmw,
     SubscriptionDoesNotHeapBackLoanedTakeWithoutBridge) {
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "0", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE", "0", 1));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_no_heap_loaned_take_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_no_heap_loaned_take_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_no_heap_loaned_take",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  EXPECT_FALSE(publisher->can_loan_messages);
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_no_heap_loaned_take",
      &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);
  EXPECT_FALSE(subscription->can_loan_messages);

  std_msgs::msg::String sample;
  sample.data = "normal queue sample";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &sample, nullptr));

  void *loaned_message = nullptr;
  bool taken = true;
  EXPECT_EQ(RMW_RET_UNSUPPORTED,
            rmw_take_loaned_message(subscription, &loaned_message, &taken,
                                    nullptr));
  EXPECT_FALSE(taken);
  EXPECT_EQ(nullptr, loaned_message);
  rcutils_reset_error();

  EXPECT_EQ(RMW_RET_UNSUPPORTED,
            rmw_return_loaned_message_from_subscription(subscription,
                                                        loaned_message));
  rcutils_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE");
  unsetenv("RMW_MDDS_BROKER");
  unsetenv("RMW_IMPLEMENTATION");
}
