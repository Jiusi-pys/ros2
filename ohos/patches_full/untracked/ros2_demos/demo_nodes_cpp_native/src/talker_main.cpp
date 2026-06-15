#include <exception>
#include <memory>
#include <vector>

#include "rclcpp/rclcpp.hpp"

#include "demo_nodes_cpp_native/talker.hpp"

int main(int argc, char * argv[])
{
  std::vector<std::string> args = rclcpp::init_and_remove_ros_arguments(argc, argv);
  rclcpp::executors::SingleThreadedExecutor executor;
  rclcpp::NodeOptions options;
  options.arguments(args);

  try {
    auto node = std::make_shared<demo_nodes_cpp_native::Talker>(options);
    executor.add_node(node);
    executor.spin();
    executor.remove_node(node);
  } catch (const std::exception & ex) {
    RCLCPP_ERROR(rclcpp::get_logger("demo_nodes_cpp_native::Talker"), "%s", ex.what());
    rclcpp::shutdown();
    return 1;
  }

  rclcpp::shutdown();
  return 0;
}
