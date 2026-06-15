#ifndef DEMO_NODES_CPP_NATIVE__TALKER_HPP_
#define DEMO_NODES_CPP_NATIVE__TALKER_HPP_

#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include "demo_nodes_cpp_native/visibility_control.h"

namespace demo_nodes_cpp_native
{

class Talker : public rclcpp::Node
{
public:
  DEMO_NODES_CPP_NATIVE_PUBLIC
  explicit Talker(const rclcpp::NodeOptions & options);

private:
  size_t count_ = 1;
  std::unique_ptr<std_msgs::msg::String> msg_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace demo_nodes_cpp_native

#endif  // DEMO_NODES_CPP_NATIVE__TALKER_HPP_
