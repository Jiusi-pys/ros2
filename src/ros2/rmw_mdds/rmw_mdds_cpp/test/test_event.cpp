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
#include <string>

#include <std_msgs/msg/string.hpp>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/events_statuses/matched.h"
#include "rmw/events_statuses/incompatible_qos.h"
#include "rmw/events_statuses/message_lost.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/subscription_options.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"

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

void ExpectMatchedStatus(
  const rmw_event_t & event, size_t total_count, size_t total_count_change,
  size_t current_count, int32_t current_count_change)
{
  rmw_matched_status_t status{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(total_count, status.total_count);
  EXPECT_EQ(total_count_change, status.total_count_change);
  EXPECT_EQ(current_count, status.current_count);
  EXPECT_EQ(current_count_change, status.current_count_change);
}

void CountEventCallback(const void * user_data, size_t number_of_events)
{
  auto * callback_count = static_cast<size_t *>(const_cast<void *>(user_data));
  if (callback_count != nullptr) {
    *callback_count += number_of_events;
  }
}
}  // namespace

TEST(RmwMddsEvent, ReportsImplementedEventsAsSupported)
{
  EXPECT_TRUE(rmw_event_type_is_supported(RMW_EVENT_PUBLICATION_MATCHED));
  EXPECT_TRUE(rmw_event_type_is_supported(RMW_EVENT_SUBSCRIPTION_MATCHED));
  // Incompatible-QoS and message-lost are now enforced events (counts accrued at match / ingress),
  // so they report as supported (previously these returned RMW_RET_UNSUPPORTED / were no-ops).
  EXPECT_TRUE(rmw_event_type_is_supported(RMW_EVENT_OFFERED_QOS_INCOMPATIBLE));
  EXPECT_TRUE(rmw_event_type_is_supported(RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE));
  EXPECT_TRUE(rmw_event_type_is_supported(RMW_EVENT_MESSAGE_LOST));
}

TEST(RmwMddsEvent, PublisherMatchedEventTracksLocalSubscriptions)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_publisher_event_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_publisher_event_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_event_string", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);

  rmw_event_t matched_event = rmw_get_zero_initialized_event();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_publisher_event_init(&matched_event, publisher, RMW_EVENT_PUBLICATION_MATCHED));
  ExpectMatchedStatus(matched_event, 0, 0, 0, 0);

  size_t callback_count = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_event_set_callback(&matched_event, CountEventCallback, &callback_count));

  rmw_subscription_t * first_subscription = rmw_create_subscription(
    node, type_support, "/mdds_event_string", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, first_subscription);
  EXPECT_EQ(1u, callback_count);

  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  void * event_handle = &matched_event;
  rmw_events_t events;
  events.event_count = 1;
  events.events = &event_handle;
  rmw_time_t zero_timeout;
  zero_timeout.sec = 0;
  zero_timeout.nsec = 0;
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(nullptr, nullptr, nullptr, nullptr, &events, wait_set, &zero_timeout));
  EXPECT_NE(nullptr, events.events[0]);
  ExpectMatchedStatus(matched_event, 1, 1, 1, 1);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));

  rmw_subscription_t * second_subscription = rmw_create_subscription(
    node, type_support, "/mdds_event_string", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, second_subscription);
  EXPECT_EQ(2u, callback_count);
  ExpectMatchedStatus(matched_event, 2, 1, 2, 1);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, first_subscription));
  EXPECT_EQ(3u, callback_count);
  ExpectMatchedStatus(matched_event, 2, 0, 1, -1);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, second_subscription));
  EXPECT_EQ(4u, callback_count);
  ExpectMatchedStatus(matched_event, 2, 0, 0, -1);

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&matched_event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, SubscriptionMatchedEventTracksLocalPublishers)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_subscription_event_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_subscription_event_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_event_string_sub", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_event_t matched_event = rmw_get_zero_initialized_event();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_event_init(&matched_event, subscription, RMW_EVENT_SUBSCRIPTION_MATCHED));
  ExpectMatchedStatus(matched_event, 0, 0, 0, 0);

  rmw_publisher_t * first_publisher = rmw_create_publisher(
    node, type_support, "/mdds_event_string_sub", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, first_publisher);
  rmw_publisher_t * second_publisher = rmw_create_publisher(
    node, type_support, "/mdds_event_string_sub", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, second_publisher);
  ExpectMatchedStatus(matched_event, 2, 2, 2, 2);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, first_publisher));
  ExpectMatchedStatus(matched_event, 2, 0, 1, -1);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, second_publisher));
  ExpectMatchedStatus(matched_event, 2, 0, 0, -1);

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&matched_event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, SubscriptionNewMessageCallbackReportsUnreadSamples)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_subscription_new_message_callback_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_subscription_callback_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_new_message_callback", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_new_message_callback", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs::msg::String first;
  first.data = "first";
  std_msgs::msg::String second;
  second.data = "second";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &first, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &second, nullptr));

  size_t callback_count = 0;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_set_on_new_message_callback(
      subscription, CountEventCallback, &callback_count));
  EXPECT_EQ(2u, callback_count);

  std_msgs::msg::String third;
  third.data = "third";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &third, nullptr));
  EXPECT_EQ(3u, callback_count);

  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_on_new_message_callback(subscription, nullptr, nullptr));
  std_msgs::msg::String fourth;
  fourth.data = "fourth";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &fourth, nullptr));
  EXPECT_EQ(3u, callback_count);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

// R1: a RELIABLE subscription matched with a BEST_EFFORT publisher is QoS-incompatible. The match must
// raise both requested-qos-incompatible (subscription) and offered-qos-incompatible (publisher), with
// RELIABILITY as the offending policy. Previously these events were no-ops (counts stayed 0).
TEST(RmwMddsEvent, IncompatibleQosEventsCountAtMatch)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_qos_incompatible_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_qos_incompatible_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_qos_profile_t best_effort = rmw_qos_profile_default;  // offered: BEST_EFFORT
  best_effort.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;

  // Reliable subscription first (rmw_qos_profile_default is RELIABLE).
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_qos_incompat", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_event_t req_event = rmw_get_zero_initialized_event();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_event_init(&req_event, subscription, RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE));

  // No incompatible publisher yet: supported, taken, count 0.
  rmw_qos_incompatible_event_status_t status{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&req_event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(0, status.total_count);

  size_t callback_count = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_event_set_callback(&req_event, CountEventCallback, &callback_count));

  // Matching a BEST_EFFORT publisher raises the requested-qos-incompatible event.
  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_qos_incompat", &best_effort, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  EXPECT_GE(callback_count, 1u);

  status = {};
  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&req_event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_GE(status.total_count, 1);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY, status.last_policy_kind);

  // The publisher side mirrors it via offered-qos-incompatible.
  rmw_event_t off_event = rmw_get_zero_initialized_event();
  ASSERT_EQ(
    RMW_RET_OK, rmw_publisher_event_init(&off_event, publisher, RMW_EVENT_OFFERED_QOS_INCOMPATIBLE));
  rmw_qos_incompatible_event_status_t off_status{};
  bool off_taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&off_event, &off_status, &off_taken));
  EXPECT_TRUE(off_taken);
  EXPECT_GE(off_status.total_count, 1);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY, off_status.last_policy_kind);

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&off_event));
  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&req_event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

// R1: message-lost is now a supported, takeable event. In-process delivery is lossless, so the count
// stays 0; the event fires (count>0) only on the lossy RTPS/bridge ingress path with a writer-sequence
// gap. This verifies the event is wired (init + take succeed) and reports 0 with no loss.
TEST(RmwMddsEvent, MessageLostEventSupportedAndZeroWithoutLoss)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_message_lost_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_message_lost_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_message_lost", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_message_lost", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_event_t lost_event = rmw_get_zero_initialized_event();
  ASSERT_EQ(
    RMW_RET_OK, rmw_subscription_event_init(&lost_event, subscription, RMW_EVENT_MESSAGE_LOST));

  for (int i = 0; i < 5; ++i) {
    std_msgs::msg::String msg;
    msg.data = "m" + std::to_string(i);
    ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));
  }

  rmw_message_lost_status_t status{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&lost_event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(0u, status.total_count);  // contiguous in-process delivery: no loss

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&lost_event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}
