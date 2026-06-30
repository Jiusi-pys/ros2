// Copyright 2026 Kaihong Digital Industry Development Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
//
// TDD-RED: LIVELINESS_LOST publisher event callback.
//
// Expected (correct RMW): a publisher with MANUAL_BY_TOPIC liveliness and a short
// lease that never asserts liveliness (no publish, no assert_liveliness) must be
// declared not-alive once the lease elapses. The publisher's liveliness-lost
// event_callback must fire with total_count >= 1.
//
// Current rmw_mdds stub: RMW_EVENT_LIVELINESS_LOST is an
// "advertised-but-never-raised" no-op (rmw_unsupported.cpp:108-113
// IsNoOpPublisherEvent; rmw_publisher_assert_liveliness :1495-1499 is a pure
// no-op; rmw_take_event default case :2385-2392 returns taken=false). The lease
// is never tracked, the callback NEVER fires -> the EXPECT_GT below FAILS -> RED.

#include <atomic>

#include <gtest/gtest.h>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include "red_test_common.hpp"

TEST(RmwMddsRedLivelinessLost, ManualByTopicPublisherLosesLivelinessAfterLease)
{
  rmw_mdds_red::RclcppScope rclcpp_scope;
  auto node = std::make_shared<rclcpp::Node>("rmw_mdds_red_liveliness_lost");

  std::atomic<int> lost_events{0};
  std::atomic<int> lost_total{0};

  // MANUAL_BY_TOPIC publisher with a 500ms liveliness lease. Liveliness is only
  // asserted by publishing or assert_liveliness(); we deliberately do neither.
  rclcpp::QoS qos(rclcpp::KeepLast(10));
  qos.reliable();
  qos.liveliness(RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC);
  qos.liveliness_lease_duration(rclcpp::Duration(500ms));

  rclcpp::PublisherOptions popts;
  popts.event_callbacks.liveliness_callback =
    [&](rclcpp::QOSLivelinessLostInfo & info) {
      ++lost_events;
      lost_total = info.total_count;
    };
  auto pub = node->create_publisher<std_msgs::msg::String>(
    "/rmw_mdds_red_liveliness_lost", qos, popts);

  // A compatible matched reader (AUTOMATIC is weaker than MANUAL_BY_TOPIC).
  rclcpp::QoS sub_qos(rclcpp::KeepLast(10));
  sub_qos.reliable();
  sub_qos.liveliness(RMW_QOS_POLICY_LIVELINESS_AUTOMATIC);
  auto sub = node->create_subscription<std_msgs::msg::String>(
    "/rmw_mdds_red_liveliness_lost", sub_qos, [](const std_msgs::msg::String &) {});

  // Spin well past the 500ms lease WITHOUT ever asserting liveliness.
  rmw_mdds_red::SpinFor(node, 2500ms);

  // TDD-RED: lease expiry must surface a liveliness-lost event; the no-op stub
  // never tracks the lease so the callback stays silent.
  EXPECT_GT(lost_events.load(), 0)
    << "publisher liveliness-lost callback never fired (no-op stub)";
  EXPECT_GT(lost_total.load(), 0)
    << "liveliness-lost total_count stayed 0 (no-op stub)";
}
