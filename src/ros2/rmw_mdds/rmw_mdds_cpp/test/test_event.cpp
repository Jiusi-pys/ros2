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

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include <geometry_msgs/msg/pose_stamped.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/int32_multi_array.hpp>
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/string.hpp>

#include "rmw/subscription_content_filter_options.h"

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/events_statuses/offered_deadline_missed.h"
#include "rmw/events_statuses/incompatible_type.h"
#include "rmw/events_statuses/liveliness_lost.h"
#include "rmw/events_statuses/matched.h"
#include "rmw/events_statuses/incompatible_qos.h"
#include "rmw/events_statuses/message_lost.h"
#include "rmw/events_statuses/requested_deadline_missed.h"
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

TEST(RmwMddsEvent, OfferedDeadlineMissedCountsAfterPublishDeadlineExpires)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_offered_deadline_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_offered_deadline_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.deadline.sec = 0;
  qos.deadline.nsec = 1000000;

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_offered_deadline", &qos, &publisher_options);
  ASSERT_NE(nullptr, publisher);

  rmw_event_t event = rmw_get_zero_initialized_event();
  ASSERT_EQ(
    RMW_RET_OK, rmw_publisher_event_init(&event, publisher, RMW_EVENT_OFFERED_DEADLINE_MISSED));

  rmw_offered_deadline_missed_status_t status{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(0, status.total_count);
  EXPECT_EQ(0, status.total_count_change);

  std::this_thread::sleep_for(std::chrono::milliseconds(3));

  status = {};
  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_GE(status.total_count, 1);
  EXPECT_GE(status.total_count_change, 1);

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, RequestedDeadlineMissedCountsForMatchedSubscriptionWithoutSamples)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_requested_deadline_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_requested_deadline_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.deadline.sec = 0;
  qos.deadline.nsec = 1000000;

  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_requested_deadline", &qos, &subscription_options);
  ASSERT_NE(nullptr, subscription);
  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_requested_deadline", &qos, &publisher_options);
  ASSERT_NE(nullptr, publisher);

  rmw_event_t event = rmw_get_zero_initialized_event();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_event_init(&event, subscription, RMW_EVENT_REQUESTED_DEADLINE_MISSED));

  rmw_requested_deadline_missed_status_t status{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(0, status.total_count);
  EXPECT_EQ(0, status.total_count_change);

  std::this_thread::sleep_for(std::chrono::milliseconds(3));

  status = {};
  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_GE(status.total_count, 1);
  EXPECT_GE(status.total_count_change, 1);

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, PublisherLivelinessLostCountsManualByTopicLeaseExpiry)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_liveliness_lost_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_liveliness_lost_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.liveliness = RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC;
  qos.liveliness_lease_duration.sec = 0;
  qos.liveliness_lease_duration.nsec = 1000000;

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_liveliness_lost", &qos, &publisher_options);
  ASSERT_NE(nullptr, publisher);

  rmw_event_t event = rmw_get_zero_initialized_event();
  ASSERT_EQ(RMW_RET_OK, rmw_publisher_event_init(&event, publisher, RMW_EVENT_LIVELINESS_LOST));

  std::this_thread::sleep_for(std::chrono::milliseconds(3));

  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  void * event_handle = &event;
  rmw_events_t events;
  events.event_count = 1;
  events.events = &event_handle;
  rmw_time_t zero_timeout;
  zero_timeout.sec = 0;
  zero_timeout.nsec = 0;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_wait(nullptr, nullptr, nullptr, nullptr, &events, wait_set, &zero_timeout));
  EXPECT_NE(nullptr, events.events[0]);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));

  rmw_liveliness_lost_status_t status{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(1, status.total_count);
  EXPECT_EQ(1, status.total_count_change);

  EXPECT_EQ(RMW_RET_OK, rmw_publisher_assert_liveliness(publisher));
  status = {};
  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(1, status.total_count);
  EXPECT_EQ(0, status.total_count_change);

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, NodeAssertLivelinessRefreshesManualPublishers)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_node_liveliness_assert_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_node_liveliness_assert_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.liveliness = RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC;
  qos.liveliness_lease_duration.sec = 0;
  qos.liveliness_lease_duration.nsec = 1000000;

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_node_liveliness_assert", &qos, &publisher_options);
  ASSERT_NE(nullptr, publisher);

  rmw_event_t event = rmw_get_zero_initialized_event();
  ASSERT_EQ(RMW_RET_OK, rmw_publisher_event_init(&event, publisher, RMW_EVENT_LIVELINESS_LOST));

  std::this_thread::sleep_for(std::chrono::milliseconds(3));
  ASSERT_EQ(RMW_RET_OK, rmw_node_assert_liveliness(node));

  rmw_liveliness_lost_status_t status{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(0, status.total_count);
  EXPECT_EQ(0, status.total_count_change);

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, PublisherIncompatibleTypeEventCountsSameTopicDifferentSubscriptionType)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_publisher_incompatible_type_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_publisher_incompatible_type_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * string_type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  const rosidl_message_type_support_t * int32_type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, string_type_support, "/mdds_incompatible_type_pub", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);

  rmw_event_t event = rmw_get_zero_initialized_event();
  ASSERT_EQ(
    RMW_RET_OK, rmw_publisher_event_init(&event, publisher,
    RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE));

  rmw_subscription_t * subscription = rmw_create_subscription(
    node, int32_type_support, "/mdds_incompatible_type_pub", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  void * event_handle = &event;
  rmw_events_t events;
  events.event_count = 1;
  events.events = &event_handle;
  rmw_time_t zero_timeout;
  zero_timeout.sec = 0;
  zero_timeout.nsec = 0;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_wait(nullptr, nullptr, nullptr, nullptr, &events, wait_set, &zero_timeout));
  EXPECT_NE(nullptr, events.events[0]);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));

  rmw_incompatible_type_status_t status{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(1, status.total_count);
  EXPECT_EQ(1, status.total_count_change);

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, SubscriptionIncompatibleTypeEventCountsSameTopicDifferentPublisherType)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_subscription_incompatible_type_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_subscription_incompatible_type_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * string_type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  const rosidl_message_type_support_t * int32_type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_subscription_t * subscription = rmw_create_subscription(
    node, string_type_support, "/mdds_incompatible_type_sub", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_event_t event = rmw_get_zero_initialized_event();
  ASSERT_EQ(
    RMW_RET_OK, rmw_subscription_event_init(&event, subscription,
    RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE));

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, int32_type_support, "/mdds_incompatible_type_sub", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);

  rmw_incompatible_type_status_t status{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(&event, &status, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(1, status.total_count);
  EXPECT_EQ(1, status.total_count_change);

  EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&event));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

// R2: a numeric content filter on a non-String message type ("data > 5" over std_msgs/Int32). Setting
// it must succeed (previously RMW_RET_UNSUPPORTED for any non-String type) and the filter must take
// effect: a sample whose field fails the predicate is dropped before it reaches the reader's queue.
TEST(RmwMddsEvent, NumericContentFilterDropsNonMatchingSamples)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_numeric_content_filter_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_numeric_filter_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_numeric_filter", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_numeric_filter", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  // "data > 5" — numeric field comparison on a non-String type.
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data > 5", 0, nullptr, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t roundtrip_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &roundtrip_options));
  EXPECT_STREQ("data > 5", roundtrip_options.filter_expression);
  EXPECT_EQ(0u, roundtrip_options.expression_parameters.size);
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&roundtrip_options, &allocator));

  std_msgs::msg::Int32 low;
  low.data = 3;  // fails "data > 5" -> dropped
  std_msgs::msg::Int32 high;
  high.data = 10;  // passes -> delivered
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &low, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &high, nullptr));

  std_msgs::msg::Int32 received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(10, received.data);  // only the matching sample survived the filter

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);  // the non-matching sample (3) never entered the queue

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, NumericContentFilterSupportsNestedScalarField)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_numeric_content_filter_nested_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_numeric_filter_nested_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32MultiArray>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_numeric_filter_nested", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_numeric_filter_nested", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "layout.data_offset > 5", 0, nullptr, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::Int32MultiArray rejected;
  rejected.layout.data_offset = 3;
  rejected.data = {3};
  std_msgs::msg::Int32MultiArray accepted;
  accepted.layout.data_offset = 7;
  accepted.data = {7};
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::Int32MultiArray received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(7u, received.layout.data_offset);
  ASSERT_EQ(1u, received.data.size());
  EXPECT_EQ(7, received.data[0]);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, BoolContentFilterSupportsTrueLiteral)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_bool_content_filter_true_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_bool_filter_true_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Bool>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_bool_filter_true", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_bool_filter_true", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data = TRUE", 0, nullptr, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::Bool rejected;
  rejected.data = false;
  std_msgs::msg::Bool accepted;
  accepted.data = true;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::Bool received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_TRUE(received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsNestedScalarField)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_nested_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_nested_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<geometry_msgs::msg::PoseStamped>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_nested", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_nested", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "header.frame_id = 'map'", 0, nullptr, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  geometry_msgs::msg::PoseStamped rejected;
  rejected.header.frame_id = "odom";
  rejected.pose.position.x = 3.0;
  geometry_msgs::msg::PoseStamped accepted;
  accepted.header.frame_id = "map";
  accepted.pose.position.x = 7.0;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  geometry_msgs::msg::PoseStamped received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("map", received.header.frame_id);
  EXPECT_DOUBLE_EQ(7.0, received.pose.position.x);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsNotEqual)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_ne_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_ne_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_ne", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_ne", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"drop"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data != %0", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t roundtrip_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &roundtrip_options));
  EXPECT_STREQ("data != %0", roundtrip_options.filter_expression);
  ASSERT_EQ(1u, roundtrip_options.expression_parameters.size);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[0]);
  EXPECT_STREQ("drop", roundtrip_options.expression_parameters.data[0]);
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&roundtrip_options, &allocator));

  std_msgs::msg::String rejected;
  rejected.data = "drop";
  std_msgs::msg::String accepted;
  accepted.data = "keep";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsDoubleEqual)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_eqeq_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_eqeq_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_eqeq", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_eqeq", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"keep"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data == %0", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::String rejected;
  rejected.data = "drop";
  std_msgs::msg::String accepted;
  accepted.data = "keep";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsSqlNotEqual)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_sql_ne_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_sql_ne_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_sql_ne", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_sql_ne", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"drop"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data <> %0", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::String rejected;
  rejected.data = "drop";
  std_msgs::msg::String accepted;
  accepted.data = "keep";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsSingleQuotedLiteral)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_literal_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_literal_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_literal", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_literal", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data = 'keep'", 0, nullptr, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t roundtrip_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &roundtrip_options));
  EXPECT_STREQ("data = 'keep'", roundtrip_options.filter_expression);
  EXPECT_EQ(0u, roundtrip_options.expression_parameters.size);
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&roundtrip_options, &allocator));

  std_msgs::msg::String rejected;
  rejected.data = "drop";
  std_msgs::msg::String accepted;
  accepted.data = "keep";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsIndexedParameter)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_indexed_param_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_indexed_param_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_indexed_param", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_indexed_param", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"drop", "keep"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data = %1", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t roundtrip_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &roundtrip_options));
  EXPECT_STREQ("data = %1", roundtrip_options.filter_expression);
  ASSERT_EQ(2u, roundtrip_options.expression_parameters.size);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[0]);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[1]);
  EXPECT_STREQ("drop", roundtrip_options.expression_parameters.data[0]);
  EXPECT_STREQ("keep", roundtrip_options.expression_parameters.data[1]);
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&roundtrip_options, &allocator));

  std_msgs::msg::String rejected;
  rejected.data = "drop";
  std_msgs::msg::String accepted;
  accepted.data = "keep";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsLikeIndexedParameter)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_like_param_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_like_param_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_like_param", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_like_param", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"keep%"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data LIKE %0", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t roundtrip_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &roundtrip_options));
  EXPECT_STREQ("data LIKE %0", roundtrip_options.filter_expression);
  ASSERT_EQ(1u, roundtrip_options.expression_parameters.size);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[0]);
  EXPECT_STREQ("keep%", roundtrip_options.expression_parameters.data[0]);
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&roundtrip_options, &allocator));

  std_msgs::msg::String rejected;
  rejected.data = "drop-one";
  std_msgs::msg::String accepted;
  accepted.data = "keep-one";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-one", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterLikeSupportsEscapedWildcard)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_like_escape_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_like_escape_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_like_escape", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_like_escape", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"keep\\_one"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data LIKE %0", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::String rejected;
  rejected.data = "keepXone";
  std_msgs::msg::String accepted;
  accepted.data = "keep_one";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep_one", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterLikeSupportsEscapeClause)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_like_escape_clause_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(
    &context, "mdds_string_filter_like_escape_clause_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_like_escape_clause", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_like_escape_clause", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"keep!_one"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data LIKE %0 ESCAPE '!'", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::String rejected;
  rejected.data = "keepXone";
  std_msgs::msg::String accepted;
  accepted.data = "keep_one";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep_one", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsNotLikeIndexedParameter)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_not_like_param_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_not_like_param_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_not_like_param", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_not_like_param", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"drop%"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data NOT LIKE %0", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t roundtrip_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &roundtrip_options));
  EXPECT_STREQ("data NOT LIKE %0", roundtrip_options.filter_expression);
  ASSERT_EQ(1u, roundtrip_options.expression_parameters.size);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[0]);
  EXPECT_STREQ("drop%", roundtrip_options.expression_parameters.data[0]);
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&roundtrip_options, &allocator));

  std_msgs::msg::String rejected;
  rejected.data = "drop-one";
  std_msgs::msg::String accepted;
  accepted.data = "keep-one";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-one", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsAndConjunction)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_and_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_and_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_and", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_and", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"keep%", "keep-block"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data LIKE %0 AND data != %1", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t roundtrip_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &roundtrip_options));
  EXPECT_STREQ("data LIKE %0 AND data != %1", roundtrip_options.filter_expression);
  ASSERT_EQ(2u, roundtrip_options.expression_parameters.size);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[0]);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[1]);
  EXPECT_STREQ("keep%", roundtrip_options.expression_parameters.data[0]);
  EXPECT_STREQ("keep-block", roundtrip_options.expression_parameters.data[1]);
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&roundtrip_options, &allocator));

  std_msgs::msg::String rejected_prefix;
  rejected_prefix.data = "drop-one";
  std_msgs::msg::String rejected_exclusion;
  rejected_exclusion.data = "keep-block";
  std_msgs::msg::String accepted;
  accepted.data = "keep-one";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected_prefix, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected_exclusion, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-one", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsOrDisjunction)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_or_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_or_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_or", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_or", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"keep-one", "keep-two"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data = %0 OR data = %1", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t roundtrip_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &roundtrip_options));
  EXPECT_STREQ("data = %0 OR data = %1", roundtrip_options.filter_expression);
  ASSERT_EQ(2u, roundtrip_options.expression_parameters.size);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[0]);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[1]);
  EXPECT_STREQ("keep-one", roundtrip_options.expression_parameters.data[0]);
  EXPECT_STREQ("keep-two", roundtrip_options.expression_parameters.data[1]);
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&roundtrip_options, &allocator));

  std_msgs::msg::String rejected;
  rejected.data = "drop";
  std_msgs::msg::String accepted_one;
  accepted_one.data = "keep-one";
  std_msgs::msg::String accepted_two;
  accepted_two.data = "keep-two";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_one, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_two, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-one", received.data);

  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-two", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsMixedLiteralAndParameterDisjunction)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_mixed_literal_param_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_mixed_literal_param_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_mixed_literal_param", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_mixed_literal_param", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"keep-param"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data = 'keep-literal' OR data = %0", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::String rejected;
  rejected.data = "drop";
  std_msgs::msg::String accepted_literal;
  accepted_literal.data = "keep-literal";
  std_msgs::msg::String accepted_parameter;
  accepted_parameter.data = "keep-param";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_literal, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_parameter, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-literal", received.data);

  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-param", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsInOperator)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_in_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_in_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_in", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_in", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"keep-one", "keep-two"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data IN (%0, %1)", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t roundtrip_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &roundtrip_options));
  EXPECT_STREQ("data IN (%0, %1)", roundtrip_options.filter_expression);
  ASSERT_EQ(2u, roundtrip_options.expression_parameters.size);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[0]);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[1]);
  EXPECT_STREQ("keep-one", roundtrip_options.expression_parameters.data[0]);
  EXPECT_STREQ("keep-two", roundtrip_options.expression_parameters.data[1]);
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&roundtrip_options, &allocator));

  std_msgs::msg::String rejected;
  rejected.data = "drop";
  std_msgs::msg::String accepted_one;
  accepted_one.data = "keep-one";
  std_msgs::msg::String accepted_two;
  accepted_two.data = "keep-two";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_one, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_two, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-one", received.data);

  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-two", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsParenthesizedDisjunction)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_parenthesized_or_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_string_filter_parenthesized_or_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_parenthesized_or", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_parenthesized_or", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"keep-left", "keep-right"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "(data = %0) OR (data = %1)", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::String rejected;
  rejected.data = "drop";
  std_msgs::msg::String accepted_left;
  accepted_left.data = "keep-left";
  std_msgs::msg::String accepted_right;
  accepted_right.data = "keep-right";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_left, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_right, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-left", received.data);

  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-right", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsNotInOperator)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_not_in_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_string_filter_not_in_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_not_in", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_not_in", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"drop-left", "drop-right"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data NOT IN (%0, %1)", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::String rejected_left;
  rejected_left.data = "drop-left";
  std_msgs::msg::String accepted;
  accepted.data = "keep";
  std_msgs::msg::String rejected_right;
  rejected_right.data = "drop-right";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected_left, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected_right, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsGroupedDisjunctionConjunction)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_grouped_bool_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_string_filter_grouped_bool_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_grouped_bool", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_grouped_bool", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"keep-left", "keep-right", "keep-right"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "(data = %0 OR data = %1) AND data != %2", 3, parameters, &allocator,
      &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::String rejected_drop;
  rejected_drop.data = "drop";
  std_msgs::msg::String rejected_right;
  rejected_right.data = "keep-right";
  std_msgs::msg::String accepted_left;
  accepted_left.data = "keep-left";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected_drop, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected_right, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_left, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep-left", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, NumericContentFilterSupportsOrDisjunction)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_numeric_content_filter_or_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_numeric_filter_or_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_numeric_filter_or", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_numeric_filter_or", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"3", "10"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data = %0 OR data = %1", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t roundtrip_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &roundtrip_options));
  EXPECT_STREQ("data = %0 OR data = %1", roundtrip_options.filter_expression);
  ASSERT_EQ(2u, roundtrip_options.expression_parameters.size);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[0]);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[1]);
  EXPECT_STREQ("3", roundtrip_options.expression_parameters.data[0]);
  EXPECT_STREQ("10", roundtrip_options.expression_parameters.data[1]);
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&roundtrip_options, &allocator));

  std_msgs::msg::Int32 rejected;
  rejected.data = 1;
  std_msgs::msg::Int32 accepted_one;
  accepted_one.data = 3;
  std_msgs::msg::Int32 accepted_two;
  accepted_two.data = 10;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_one, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_two, nullptr));

  std_msgs::msg::Int32 received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(3, received.data);

  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(10, received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, NumericContentFilterSupportsInOperator)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_numeric_content_filter_in_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_numeric_filter_in_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_numeric_filter_in", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_numeric_filter_in", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"3", "10"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data IN (%0, %1)", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t roundtrip_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &roundtrip_options));
  EXPECT_STREQ("data IN (%0, %1)", roundtrip_options.filter_expression);
  ASSERT_EQ(2u, roundtrip_options.expression_parameters.size);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[0]);
  ASSERT_NE(nullptr, roundtrip_options.expression_parameters.data[1]);
  EXPECT_STREQ("3", roundtrip_options.expression_parameters.data[0]);
  EXPECT_STREQ("10", roundtrip_options.expression_parameters.data[1]);
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&roundtrip_options, &allocator));

  std_msgs::msg::Int32 rejected;
  rejected.data = 1;
  std_msgs::msg::Int32 accepted_one;
  accepted_one.data = 3;
  std_msgs::msg::Int32 accepted_two;
  accepted_two.data = 10;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_one, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_two, nullptr));

  std_msgs::msg::Int32 received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(3, received.data);

  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(10, received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, NumericContentFilterSupportsNotInOperator)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_numeric_content_filter_not_in_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_numeric_filter_not_in_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_numeric_filter_not_in", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_numeric_filter_not_in", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"3", "10"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data NOT IN (%0, %1)", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::Int32 rejected_left;
  rejected_left.data = 3;
  std_msgs::msg::Int32 accepted;
  accepted.data = 7;
  std_msgs::msg::Int32 rejected_right;
  rejected_right.data = 10;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected_left, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected_right, nullptr));

  std_msgs::msg::Int32 received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(7, received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, NumericContentFilterSupportsBetweenOperator)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_numeric_content_filter_between_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_numeric_filter_between_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_numeric_filter_between", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_numeric_filter_between", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"3", "10"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data BETWEEN %0 AND %1", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  for (int value : {2, 3, 7, 10, 11}) {
    std_msgs::msg::Int32 msg;
    msg.data = value;
    ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));
  }

  std_msgs::msg::Int32 received;
  bool taken = false;
  for (int expected : {3, 7, 10}) {
    ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
    ASSERT_TRUE(taken);
    EXPECT_EQ(expected, received.data);
    taken = false;
  }

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, NumericContentFilterSupportsNotBetweenOperator)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_numeric_content_filter_not_between_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_numeric_filter_not_between_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_numeric_filter_not_between", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_numeric_filter_not_between", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"3", "10"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data NOT BETWEEN %0 AND %1", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  for (int value : {2, 3, 7, 10, 11}) {
    std_msgs::msg::Int32 msg;
    msg.data = value;
    ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));
  }

  std_msgs::msg::Int32 received;
  bool taken = false;
  for (int expected : {2, 11}) {
    ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
    ASSERT_TRUE(taken);
    EXPECT_EQ(expected, received.data);
    taken = false;
  }

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, NumericContentFilterSupportsParenthesizedDisjunction)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_numeric_content_filter_parenthesized_or_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_numeric_filter_parenthesized_or_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_numeric_filter_parenthesized_or", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_numeric_filter_parenthesized_or", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"3", "10"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "(data = %0) OR (data = %1)", 2, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::Int32 rejected;
  rejected.data = 1;
  std_msgs::msg::Int32 accepted_one;
  accepted_one.data = 3;
  std_msgs::msg::Int32 accepted_two;
  accepted_two.data = 10;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_one, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted_two, nullptr));

  std_msgs::msg::Int32 received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(3, received.data);

  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(10, received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, NumericContentFilterSupportsGroupedDisjunctionConjunction)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_numeric_content_filter_grouped_bool_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_numeric_filter_grouped_bool_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_numeric_filter_grouped_bool", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_numeric_filter_grouped_bool", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"3", "10", "10"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "(data = %0 OR data = %1) AND data != %2", 3, parameters, &allocator,
      &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::Int32 rejected_outside_group;
  rejected_outside_group.data = 2;
  std_msgs::msg::Int32 rejected_exclusion;
  rejected_exclusion.data = 10;
  std_msgs::msg::Int32 accepted;
  accepted.data = 3;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected_outside_group, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected_exclusion, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::Int32 received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(3, received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, StringContentFilterSupportsUnaryNot)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_string_content_filter_unary_not_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_string_filter_unary_not_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_string_filter_unary_not", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_string_filter_unary_not", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"drop"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "NOT (data = %0)", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::String rejected;
  rejected.data = "drop";
  std_msgs::msg::String accepted;
  accepted.data = "keep";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("keep", received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsEvent, NumericContentFilterSupportsUnaryNot)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_numeric_content_filter_unary_not_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_numeric_filter_unary_not_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_numeric_filter_unary_not", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_numeric_filter_unary_not", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"3"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "NOT (data = %0)", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  std_msgs::msg::Int32 rejected;
  rejected.data = 3;
  std_msgs::msg::Int32 accepted;
  accepted.data = 7;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &rejected, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &accepted, nullptr));

  std_msgs::msg::Int32 received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(7, received.data);

  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}
