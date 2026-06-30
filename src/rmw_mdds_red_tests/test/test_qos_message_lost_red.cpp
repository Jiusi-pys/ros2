// Copyright 2026 Kaihong Digital Industry Development Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
//
// TDD-RED: MESSAGE_LOST subscription event callback.
//
// Expected (correct RMW): a KEEP_LAST depth-1 subscription that lets the queue
// overflow (many samples published before any are taken) drops the overwritten
// samples and reports them via the message-lost event. The rclcpp
// message_lost_callback must fire with total_count >= 1.
//
// Current rmw_mdds stub: RMW_EVENT_MESSAGE_LOST is an "advertised-but-never-raised"
// no-op (rmw_unsupported.cpp:114-119 IsNoOpSubscriptionEvent; rmw_take_event
// default case :2385-2392 returns taken=false). No dropped-sample accounting
// exists, the callback NEVER fires -> the EXPECT_GT below FAILS -> RED.

#include <atomic>

#include <gtest/gtest.h>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include "red_test_common.hpp"

TEST(RmwMddsRedMessageLost, OverflowingKeepLastDepthOneReportsLostSamples)
{
  rmw_mdds_red::RclcppScope rclcpp_scope;
  auto node = std::make_shared<rclcpp::Node>("rmw_mdds_red_message_lost");

  std::atomic<int> lost_events{0};
  std::atomic<int> lost_total{0};
  std::atomic<int> received{0};

  // Shallow history: depth 1. Any unread sample is overwritten -> "lost".
  rclcpp::QoS qos(rclcpp::KeepLast(1));
  qos.reliable();

  rclcpp::SubscriptionOptions sopts;
  sopts.event_callbacks.message_lost_callback =
    [&](rclcpp::QOSMessageLostInfo & info) {
      ++lost_events;
      lost_total = info.total_count;
    };
  auto sub = node->create_subscription<std_msgs::msg::String>(
    "/rmw_mdds_red_message_lost", qos,
    [&received](const std_msgs::msg::String &) { ++received; }, sopts);
  auto pub = node->create_publisher<std_msgs::msg::String>("/rmw_mdds_red_message_lost", qos);

  // Match first.
  rmw_mdds_red::SpinFor(node, 1500ms);

  // Flood 10 samples back-to-back WITHOUT spinning so the depth-1 queue overflows
  // and 9 of them are overwritten before any take.
  for (int i = 0; i < 10; ++i) {
    std_msgs::msg::String msg;
    msg.data = "M" + std::to_string(i);
    pub->publish(msg);
  }

  // Now spin: the message-lost event for the overwritten samples must surface.
  rmw_mdds_red::SpinFor(node, 1500ms);

  // TDD-RED: dropped samples must be reported as message-lost; the no-op stub
  // reports nothing regardless of how many samples were overwritten.
  EXPECT_GT(lost_events.load(), 0)
    << "message-lost callback never fired despite depth-1 overflow (no-op stub)";
  EXPECT_GT(lost_total.load(), 0)
    << "message-lost total_count stayed 0 (no-op stub)";
}
