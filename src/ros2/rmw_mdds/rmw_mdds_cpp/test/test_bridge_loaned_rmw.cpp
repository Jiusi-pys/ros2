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
#include <std_msgs/msg/int32_multi_array.h>
#include <std_msgs/msg/multi_array_dimension.h>

#include <cstdlib>
#include <cstring>
#include <std_msgs/msg/int32_multi_array.hpp>
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/string.hpp>
#include <vector>

#include "../src/string_adapter.hpp"

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/subscription_options.h"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_runtime_c/primitives_sequence_functions.h"
#include "rosidl_runtime_c/string_functions.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"

extern "C" void FakeMddsBridgeReset(void);
extern "C" int FakeMddsBridgeBorrowLoanedCount(void);
extern "C" int FakeMddsBridgePublishLoanedCount(void);
extern "C" int FakeMddsBridgeReturnLoanedCount(void);
extern "C" void *FakeMddsBridgeLastBorrowedData(void);
extern "C" uint32_t FakeMddsBridgeLastBorrowedSize(void);
extern "C" const uint8_t *FakeMddsBridgeLastPayloadData(void);
extern "C" uint32_t FakeMddsBridgeLastPayloadLen(void);

namespace {
void SetEnclave(rmw_init_options_t *options, const char *enclave) {
  options->allocator.deallocate(options->enclave, options->allocator.state);
  options->enclave = rcutils_strdup(enclave, options->allocator);
  ASSERT_NE(nullptr, options->enclave);
}
} // namespace

TEST(RmwMddsBridgeLoanedRmw, StringAdapterDirectBufferEncodesGenericLegacyPayload) {
  rmw_mdds_cpp::StringAdapter adapter;
  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::Int32MultiArray>();
  ASSERT_TRUE(adapter.Init(type_support));

  std_msgs::msg::Int32MultiArray msg;
  msg.layout.dim.resize(1);
  msg.layout.dim[0].label = "cpp_axis";
  msg.layout.dim[0].size = 4;
  msg.layout.dim[0].stride = 4;
  msg.layout.data_offset = 1;
  msg.data = {1, 2, 3, 4};

  std::vector<uint8_t> expected;
  ASSERT_TRUE(adapter.Encode(&msg, &expected));
  ASSERT_FALSE(expected.empty());

  std::vector<uint8_t> actual(expected.size());
  size_t written_size = 0;
  ASSERT_TRUE(adapter.EncodeIntoBuffer(&msg, actual.data(), actual.size(),
                                       &written_size));
  EXPECT_EQ(expected.size(), written_size);
  EXPECT_EQ(expected, actual);

  std::vector<uint8_t> too_small(expected.size() - 1u);
  written_size = 0;
  EXPECT_FALSE(adapter.EncodeIntoBuffer(&msg, too_small.data(),
                                        too_small.size(), &written_size));
  EXPECT_EQ(expected.size(), written_size);
}

TEST(RmwMddsBridgeLoanedRmw, StringAdapterDirectBufferEncodesCGenericLegacyPayload) {
  rmw_mdds_cpp::StringAdapter adapter;
  const rosidl_message_type_support_t *type_support =
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32MultiArray);
  ASSERT_TRUE(adapter.Init(type_support));

  std_msgs__msg__Int32MultiArray msg;
  ASSERT_TRUE(std_msgs__msg__Int32MultiArray__init(&msg));
  ASSERT_TRUE(std_msgs__msg__MultiArrayDimension__Sequence__init(
      &msg.layout.dim, 1));
  ASSERT_TRUE(
      rosidl_runtime_c__String__assign(&msg.layout.dim.data[0].label, "c_axis"));
  msg.layout.dim.data[0].size = 3;
  msg.layout.dim.data[0].stride = 3;
  msg.layout.data_offset = 0;
  ASSERT_TRUE(rosidl_runtime_c__int32__Sequence__init(&msg.data, 3));
  msg.data.data[0] = 11;
  msg.data.data[1] = -22;
  msg.data.data[2] = 3588;

  std::vector<uint8_t> expected;
  ASSERT_TRUE(adapter.Encode(&msg, &expected));
  ASSERT_FALSE(expected.empty());

  std::vector<uint8_t> actual(expected.size());
  size_t written_size = 0;
  EXPECT_TRUE(adapter.EncodeIntoBuffer(&msg, actual.data(), actual.size(),
                                       &written_size));
  EXPECT_EQ(expected.size(), written_size);
  EXPECT_EQ(expected, actual);

  std::vector<uint8_t> too_small(expected.size() - 1u);
  written_size = 0;
  EXPECT_FALSE(adapter.EncodeIntoBuffer(&msg, too_small.data(),
                                        too_small.size(), &written_size));
  EXPECT_EQ(expected.size(), written_size);

  std_msgs__msg__Int32MultiArray__fini(&msg);
}

TEST(RmwMddsBridgeLoanedRmw, BorrowLoanedMessageUsesBridgeOwnedTypedStorage) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_loaned_storage_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_loaned_storage_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_bridge_loaned_int32",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  ASSERT_TRUE(publisher->can_loan_messages);

  void *loaned_message = nullptr;
  ASSERT_EQ(RMW_RET_OK, rmw_borrow_loaned_message(publisher, type_support,
                                                  &loaned_message));
  ASSERT_NE(nullptr, loaned_message);
  EXPECT_EQ(1, FakeMddsBridgeBorrowLoanedCount());
  auto *bridge_begin = static_cast<uint8_t *>(FakeMddsBridgeLastBorrowedData());
  ASSERT_NE(nullptr, bridge_begin);
  const uint32_t bridge_size = FakeMddsBridgeLastBorrowedSize();
  EXPECT_LE(sizeof(std_msgs::msg::Int32), bridge_size);
  auto *loaned_bytes = static_cast<uint8_t *>(loaned_message);
  EXPECT_EQ(bridge_begin, loaned_bytes);
  EXPECT_LE(loaned_bytes + sizeof(std_msgs::msg::Int32), bridge_begin + bridge_size);

  auto *msg = static_cast<std_msgs::msg::Int32 *>(loaned_message);
  msg->data = 42;
  EXPECT_EQ(RMW_RET_OK,
            rmw_return_loaned_message_from_publisher(publisher,
                                                     loaned_message));
  EXPECT_EQ(1, FakeMddsBridgeReturnLoanedCount());

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedRmw, BorrowLoanedMessageRejectsMismatchedTypeSupport) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_loaned_type_mismatch_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_loaned_type_mismatch_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *int32_type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, int32_type_support,
                           "/mdds_bridge_loaned_type_mismatch",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  ASSERT_TRUE(publisher->can_loan_messages);

  void *loaned_message = nullptr;
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT,
            rmw_borrow_loaned_message(
                publisher,
                rosidl_typesupport_cpp::get_message_type_support_handle<
                    std_msgs::msg::String>(),
                                      &loaned_message));
  EXPECT_EQ(nullptr, loaned_message);
  EXPECT_EQ(0, FakeMddsBridgeBorrowLoanedCount());

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedRmw, PublishLoanedMessagePublishesRawBridgeLoan) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_loaned_publish_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_loaned_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_bridge_loaned_int32_publish",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  ASSERT_TRUE(publisher->can_loan_messages);

  void *loaned_message = nullptr;
  ASSERT_EQ(RMW_RET_OK, rmw_borrow_loaned_message(publisher, type_support,
                                                  &loaned_message));
  ASSERT_NE(nullptr, loaned_message);
  EXPECT_EQ(1, FakeMddsBridgeBorrowLoanedCount());
  EXPECT_EQ(0, FakeMddsBridgePublishLoanedCount());
  EXPECT_EQ(0, FakeMddsBridgeReturnLoanedCount());
  auto *msg = static_cast<std_msgs::msg::Int32 *>(loaned_message);
  msg->data = 3588;
  const std_msgs::msg::Int32 expected_payload = *msg;

  ASSERT_EQ(RMW_RET_OK,
            rmw_publish_loaned_message(publisher, loaned_message, nullptr));
  EXPECT_EQ(1, FakeMddsBridgeBorrowLoanedCount());
  EXPECT_EQ(1, FakeMddsBridgePublishLoanedCount());
  EXPECT_EQ(0, FakeMddsBridgeReturnLoanedCount());
  ASSERT_EQ(sizeof(expected_payload), FakeMddsBridgeLastPayloadLen());
  EXPECT_EQ(0, std::memcmp(&expected_payload,
                           FakeMddsBridgeLastPayloadData(),
                           sizeof(expected_payload)));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedRmw, PublisherDoesNotAdvertiseLoaningForUnboundedString) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_loaned_unbounded_string_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_loaned_unbounded_string_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_bridge_loaned_unbounded_string",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  EXPECT_FALSE(publisher->can_loan_messages);

  void *loaned_message = nullptr;
  EXPECT_EQ(RMW_RET_UNSUPPORTED,
            rmw_borrow_loaned_message(publisher, type_support,
                                      &loaned_message));
  EXPECT_EQ(nullptr, loaned_message);
  EXPECT_EQ(0, FakeMddsBridgeBorrowLoanedCount());
  rcutils_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedRmw, DISABLED_FullParityLoanedUnboundedStringPublishesWithoutCopyFallback) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_full_parity_loaned_string_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_full_parity_loaned_string_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_full_parity_loaned_string",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  EXPECT_TRUE(publisher->can_loan_messages)
      << "full parity requires an explicit decision before unbounded strings remain non-loanable";

  void *loaned_message = nullptr;
  EXPECT_EQ(RMW_RET_OK,
            rmw_borrow_loaned_message(publisher, type_support, &loaned_message));
  EXPECT_NE(nullptr, loaned_message);
  if (loaned_message != nullptr) {
    auto *msg = static_cast<std_msgs::msg::String *>(loaned_message);
    msg->data = "full parity string loan";
    EXPECT_EQ(RMW_RET_OK,
              rmw_publish_loaned_message(publisher, loaned_message, nullptr));
  }

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedRmw, DISABLED_FullParityLoanedSequencePublishesWithoutCopyFallback) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_full_parity_loaned_sequence_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_full_parity_loaned_sequence_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::Int32MultiArray>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_full_parity_loaned_sequence",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  EXPECT_TRUE(publisher->can_loan_messages)
      << "full parity requires an explicit decision before sequence messages remain non-loanable";

  void *loaned_message = nullptr;
  EXPECT_EQ(RMW_RET_OK,
            rmw_borrow_loaned_message(publisher, type_support, &loaned_message));
  EXPECT_NE(nullptr, loaned_message);
  if (loaned_message != nullptr) {
    auto *msg = static_cast<std_msgs::msg::Int32MultiArray *>(loaned_message);
    msg->data = {3, 5, 8};
    EXPECT_EQ(RMW_RET_OK,
              rmw_publish_loaned_message(publisher, loaned_message, nullptr));
  }

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedRmw, ReturnLoanedMessageReturnsBorrowedBridgeLoan) {
  FakeMddsBridgeReset();
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bridge_loaned_return_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_bridge_loaned_return_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_bridge_loaned_return",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  ASSERT_TRUE(publisher->can_loan_messages);

  void *loaned_message = nullptr;
  ASSERT_EQ(RMW_RET_OK, rmw_borrow_loaned_message(publisher, type_support,
                                                  &loaned_message));
  ASSERT_NE(nullptr, loaned_message);
  EXPECT_EQ(1, FakeMddsBridgeBorrowLoanedCount());
  EXPECT_EQ(0, FakeMddsBridgeReturnLoanedCount());

  EXPECT_EQ(RMW_RET_OK,
            rmw_return_loaned_message_from_publisher(publisher,
                                                     loaned_message));
  EXPECT_EQ(1, FakeMddsBridgeReturnLoanedCount());
  EXPECT_EQ(0, FakeMddsBridgePublishLoanedCount());

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedRmw, PublisherDoesNotAdvertiseLoaningWithoutBridge) {
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE", "0", 1));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_heap_loaned_return_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_heap_loaned_return_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_heap_loaned_return",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  EXPECT_FALSE(publisher->can_loan_messages);

  void *loaned_message = nullptr;
  EXPECT_EQ(RMW_RET_UNSUPPORTED,
            rmw_borrow_loaned_message(publisher, type_support,
                                      &loaned_message));
  EXPECT_EQ(nullptr, loaned_message);
  rcutils_reset_error();

  std_msgs::msg::String foreign_message;
  EXPECT_EQ(RMW_RET_UNSUPPORTED,
            rmw_return_loaned_message_from_publisher(publisher,
                                                     &foreign_message));
  rcutils_reset_error();
  EXPECT_EQ(RMW_RET_UNSUPPORTED,
            rmw_publish_loaned_message(publisher, &foreign_message, nullptr));
  rcutils_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE");
  unsetenv("RMW_IMPLEMENTATION");
}

TEST(RmwMddsBridgeLoanedRmw, PublisherLoanedApisRejectForeignPointers) {
  ASSERT_EQ(0, setenv("RMW_IMPLEMENTATION", "rmw_mdds_cpp", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE", "0", 1));
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_foreign_loaned_pointer_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_foreign_loaned_pointer_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, "/mdds_foreign_loaned_pointer",
                           &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);

  std_msgs::msg::String foreign_message;
  foreign_message.data = "not borrowed from rmw_mdds";
  EXPECT_EQ(RMW_RET_UNSUPPORTED,
            rmw_return_loaned_message_from_publisher(publisher,
                                                     &foreign_message));
  rcutils_reset_error();
  EXPECT_EQ(RMW_RET_UNSUPPORTED,
            rmw_publish_loaned_message(publisher, &foreign_message, nullptr));
  rcutils_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_BRIDGE");
  unsetenv("RMW_IMPLEMENTATION");
}
