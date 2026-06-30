// Copyright 2026 Kaihong Digital Industry Development Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
//
// TDD-RED: content-filtered topic must REALLY filter.
//
// Expected (correct RMW): a subscription created with content_filter_options
// ("data = %0", parameter "keep") delivers only matching samples. So
// sub->is_cft_enabled() == true, and after publishing "keep"/"drop"/"keep"/"drop"
// the subscription receives the two "keep" samples and zero "drop" samples.
//
// Current rmw_mdds stub: rmw_create_subscription ignores
// subscription_options->content_filter_options entirely and hardcodes
// is_cft_enabled = false (rmw_subscription.cpp:136). The filter is therefore never
// installed on the standard rclcpp creation path -- the subscription receives
// EVERYTHING. (The post-creation rmw_subscription_set_content_filter path only
// supports std_msgs/String "data=%0" anyway; broker.cpp:262-281,566-596.)
// Both EXPECT below FAIL -> RED.

#include <atomic>
#include <vector>

#include <gtest/gtest.h>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include "red_test_common.hpp"

TEST(RmwMddsRedContentFilter, FilteredSubscriptionDropsNonMatchingSamples)
{
  rmw_mdds_red::RclcppScope rclcpp_scope;
  auto node = std::make_shared<rclcpp::Node>("rmw_mdds_red_content_filter");

  std::atomic<int> keep_received{0};
  std::atomic<int> drop_received{0};

  rclcpp::QoS qos(rclcpp::KeepLast(10));
  qos.reliable();

  // Standard rclcpp content-filter request: keep only samples whose data == "keep".
  rclcpp::SubscriptionOptions sopts;
  sopts.content_filter_options.filter_expression = "data = %0";
  sopts.content_filter_options.expression_parameters = {"keep"};

  auto sub = node->create_subscription<std_msgs::msg::String>(
    "/rmw_mdds_red_content_filter", qos,
    [&](const std_msgs::msg::String & msg) {
      if (msg.data == "keep") {
        ++keep_received;
      } else {
        ++drop_received;
      }
    },
    sopts);
  auto pub = node->create_publisher<std_msgs::msg::String>("/rmw_mdds_red_content_filter", qos);

  rmw_mdds_red::SpinFor(node, 1500ms);  // match

  for (const char * value : {"keep", "drop", "keep", "drop"}) {
    std_msgs::msg::String msg;
    msg.data = value;
    pub->publish(msg);
    rmw_mdds_red::SpinFor(node, 200ms);
  }
  rmw_mdds_red::SpinFor(node, 500ms);

  // TDD-RED: the middleware must honour the content filter. The stub ignores it,
  // so is_cft_enabled() is false and the "drop" samples leak through.
  EXPECT_TRUE(sub->is_cft_enabled())
    << "content filter was not installed on the subscription (rmw ignores content_filter_options)";
  EXPECT_EQ(0, drop_received.load())
    << "non-matching 'drop' samples were delivered (content filter not applied)";
  EXPECT_EQ(2, keep_received.load())
    << "matching 'keep' samples were not delivered as expected";
}
