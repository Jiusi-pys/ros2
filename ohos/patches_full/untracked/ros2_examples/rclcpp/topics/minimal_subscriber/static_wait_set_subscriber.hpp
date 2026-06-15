// Copyright 2021, Apex.AI Inc.
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

#ifndef EXAMPLES_RCLCPP_MINIMAL_SUBSCRIBER_STATIC_WAIT_SET_SUBSCRIBER_HPP_
#define EXAMPLES_RCLCPP_MINIMAL_SUBSCRIBER_STATIC_WAIT_SET_SUBSCRIBER_HPP_

#include <array>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

class StaticWaitSetSubscriber : public rclcpp::Node
{
  using MyStaticWaitSet = rclcpp::StaticWaitSet<1, 0, 0, 0, 0, 0>;

public:
  explicit StaticWaitSetSubscriber(rclcpp::NodeOptions options);
  ~StaticWaitSetSubscriber();

  void spin_wait_set();

private:
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
  MyStaticWaitSet wait_set_;
  std::thread thread_;
};

#endif  // EXAMPLES_RCLCPP_MINIMAL_SUBSCRIBER_STATIC_WAIT_SET_SUBSCRIBER_HPP_
