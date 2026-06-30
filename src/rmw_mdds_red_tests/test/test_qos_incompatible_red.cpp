// Copyright 2026 Kaihong Digital Industry Development Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
//
// TDD-RED: REQUESTED_QOS_INCOMPATIBLE / OFFERED_QOS_INCOMPATIBLE event callbacks.
//
// Expected (correct RMW): a BEST_EFFORT publisher and a RELIABLE subscription on
// the same topic have incompatible RELIABILITY QoS. The subscription must raise
// requested-incompatible-qos (and the publisher offered-incompatible-qos), so the
// rclcpp event_callbacks fire with total_count >= 1 and last_policy_kind ==
// RELIABILITY.
//
// Current rmw_mdds stub: RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE /
// RMW_EVENT_OFFERED_QOS_INCOMPATIBLE are "advertised-but-never-raised" no-ops
// (rmw_unsupported.cpp:101-119 IsNoOpSubscriptionEvent/IsNoOpPublisherEvent;
// rmw_take_event default case at :2385-2392 returns taken=false; matching is
// QoS-blind, broker.cpp:55-77 SameTopicAndType). The callbacks NEVER fire, so the
// EXPECT_GT(..., 0u) assertions below FAIL -> RED.

#include <atomic>

#include <gtest/gtest.h>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include "red_test_common.hpp"

TEST(RmwMddsRedIncompatibleQos, RequestedAndOfferedIncompatibleQosFire)
{
  rmw_mdds_red::RclcppScope rclcpp_scope;
  auto node = std::make_shared<rclcpp::Node>("rmw_mdds_red_incompatible_qos");

  std::atomic<int> requested_incompatible_events{0};
  std::atomic<int> requested_incompatible_total{0};
  std::atomic<int> offered_incompatible_events{0};

  // RELIABLE subscription that demands reliable delivery.
  rclcpp::QoS sub_qos(rclcpp::KeepLast(10));
  sub_qos.reliable();
  rclcpp::SubscriptionOptions sopts;
  sopts.event_callbacks.incompatible_qos_callback =
    [&](rclcpp::QOSRequestedIncompatibleQoSInfo & info) {
      ++requested_incompatible_events;
      requested_incompatible_total = info.total_count;
    };
  auto sub = node->create_subscription<std_msgs::msg::String>(
    "/rmw_mdds_red_incompatible_qos", sub_qos, [](const std_msgs::msg::String &) {}, sopts);

  // BEST_EFFORT publisher: cannot satisfy the reliable subscription -> incompatible.
  rclcpp::QoS pub_qos(rclcpp::KeepLast(10));
  pub_qos.best_effort();
  rclcpp::PublisherOptions popts;
  popts.event_callbacks.incompatible_qos_callback =
    [&](rclcpp::QOSOfferedIncompatibleQoSInfo &) { ++offered_incompatible_events; };
  auto pub = node->create_publisher<std_msgs::msg::String>(
    "/rmw_mdds_red_incompatible_qos", pub_qos, popts);

  // Let discovery/matching run so the incompatibility is detected and reported.
  rmw_mdds_red::SpinFor(node, 2000ms);

  // TDD-RED: a correct RMW reports the QoS clash; the no-op stub reports nothing.
  EXPECT_GT(requested_incompatible_events.load(), 0)
    << "requested-incompatible-qos callback never fired (no-op stub)";
  EXPECT_GT(requested_incompatible_total.load(), 0)
    << "requested-incompatible-qos total_count stayed 0 (no-op stub)";
  EXPECT_GT(offered_incompatible_events.load(), 0)
    << "offered-incompatible-qos callback never fired (no-op stub)";
}
