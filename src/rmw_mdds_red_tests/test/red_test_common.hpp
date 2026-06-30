// Copyright 2026 Kaihong Digital Industry Development Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
//
// Shared helpers for the rmw_mdds QoS-event / content-filter RED test suite.
// These are TDD red tests: they encode the behaviour ROS 2 *should* exhibit
// over rmw_mdds_cpp. They FAIL today because the corresponding rmw_mdds paths
// are no-op stubs (events advertised-but-never-raised) or QoS-blind matching.
// When the stubs are implemented they flip GREEN with no edit to the test.
//
// Run host-only / in-process: a single node owns both publisher and
// subscription and they match through the in-proc broker (RMW_MDDS_BROKER=0).

#ifndef RMW_MDDS_RED_TESTS__RED_TEST_COMMON_HPP_
#define RMW_MDDS_RED_TESTS__RED_TEST_COMMON_HPP_

#include <chrono>
#include <thread>

#include "rclcpp/rclcpp.hpp"

namespace rmw_mdds_red
{
using namespace std::chrono_literals;

// Spin `node` for `duration`, pumping the executor so QoS-event handlers and
// subscription callbacks run. Mirrors the proven loop in mdds_qos_probe.cpp.
inline void SpinFor(const rclcpp::Node::SharedPtr & node, std::chrono::milliseconds duration)
{
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  const auto end = std::chrono::steady_clock::now() + duration;
  while (rclcpp::ok() && std::chrono::steady_clock::now() < end) {
    exec.spin_some(20ms);
    std::this_thread::sleep_for(5ms);
  }
  exec.remove_node(node);
}

// RAII guard: bring rclcpp up for one test, tear it down on scope exit.
// Each RED test binary holds exactly one of these in its single TEST body.
class RclcppScope
{
public:
  RclcppScope()
  {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
  }
  ~RclcppScope()
  {
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
  }
  RclcppScope(const RclcppScope &) = delete;
  RclcppScope & operator=(const RclcppScope &) = delete;
};
}  // namespace rmw_mdds_red

#endif  // RMW_MDDS_RED_TESTS__RED_TEST_COMMON_HPP_
