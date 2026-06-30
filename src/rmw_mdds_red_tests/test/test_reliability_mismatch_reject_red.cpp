// Copyright 2026 Kaihong Digital Industry Development Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
//
// TDD-RED: reliability-contract mismatch must REJECT the match.
//
// Expected (correct RMW): a BEST_EFFORT publisher cannot satisfy a RELIABLE
// subscription, so the two endpoints are incompatible and MUST NOT match. The
// subscription therefore sees get_publisher_count() == 0 and receives none of the
// publisher's samples.
//
// Current rmw_mdds stub: matching is QoS-blind -- it pairs endpoints purely on
// topic name + type name (broker.cpp:55-77 SameTopicAndType, used by
// CountMatchingPublishersLocked :71-77; surfaced through
// rmw_subscription_count_matched_publishers, rmw_unsupported.cpp:1599-1619).
// The incompatible pair is matched anyway: get_publisher_count() == 1 and the
// reliable subscription wrongly receives the best-effort data -> the EXPECT_EQ
// assertions below FAIL -> RED.

#include <atomic>

#include <gtest/gtest.h>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include "red_test_common.hpp"

TEST(RmwMddsRedReliabilityMismatch, ReliableSubDoesNotMatchBestEffortPub)
{
  rmw_mdds_red::RclcppScope rclcpp_scope;
  auto node = std::make_shared<rclcpp::Node>("rmw_mdds_red_reliability_mismatch");

  std::atomic<int> received{0};

  // RELIABLE subscription.
  rclcpp::QoS sub_qos(rclcpp::KeepLast(10));
  sub_qos.reliable();
  auto sub = node->create_subscription<std_msgs::msg::String>(
    "/rmw_mdds_red_reliability_mismatch", sub_qos,
    [&received](const std_msgs::msg::String &) { ++received; });

  // BEST_EFFORT publisher -> incompatible offered reliability.
  rclcpp::QoS pub_qos(rclcpp::KeepLast(10));
  pub_qos.best_effort();
  auto pub = node->create_publisher<std_msgs::msg::String>(
    "/rmw_mdds_red_reliability_mismatch", pub_qos);

  // Allow matching to settle, then send traffic the reliable sub must NOT receive.
  rmw_mdds_red::SpinFor(node, 1500ms);
  for (int i = 0; i < 5; ++i) {
    std_msgs::msg::String msg;
    msg.data = "X" + std::to_string(i);
    pub->publish(msg);
    rmw_mdds_red::SpinFor(node, 200ms);
  }
  rmw_mdds_red::SpinFor(node, 500ms);

  // TDD-RED: incompatible QoS must prevent the match; the QoS-blind stub matches
  // anyway, so both of these EXPECT_EQ fail.
  EXPECT_EQ(0u, sub->get_publisher_count())
    << "reliable subscription matched an incompatible best-effort publisher (QoS-blind stub)";
  EXPECT_EQ(0, received.load())
    << "reliable subscription received data from an incompatible best-effort publisher (QoS-blind stub)";
}
