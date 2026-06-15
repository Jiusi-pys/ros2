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

#include <exception>
#include <memory>
#include <vector>

#include "rclcpp/rclcpp.hpp"

#include "time_triggered_wait_set_subscriber.hpp"

int main(int argc, char * argv[])
{
  std::vector<std::string> args = rclcpp::init_and_remove_ros_arguments(argc, argv);
  rclcpp::executors::SingleThreadedExecutor executor;
  rclcpp::NodeOptions options;
  options.arguments(args);

  try {
    auto node = std::make_shared<TimeTriggeredWaitSetSubscriber>(options);
    executor.add_node(node);
    executor.spin();
    executor.remove_node(node);
  } catch (const std::exception & ex) {
    RCLCPP_ERROR(rclcpp::get_logger("TimeTriggeredWaitSetSubscriber"), "%s", ex.what());
    rclcpp::shutdown();
    return 1;
  }

  rclcpp::shutdown();
  return 0;
}
