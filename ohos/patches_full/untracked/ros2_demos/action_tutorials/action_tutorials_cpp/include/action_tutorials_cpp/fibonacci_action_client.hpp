#ifndef ACTION_TUTORIALS_CPP__FIBONACCI_ACTION_CLIENT_HPP_
#define ACTION_TUTORIALS_CPP__FIBONACCI_ACTION_CLIENT_HPP_

#include <memory>

#include "action_tutorials_interfaces/action/fibonacci.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

#include "action_tutorials_cpp/visibility_control.h"

namespace action_tutorials_cpp
{

class FibonacciActionClient : public rclcpp::Node
{
public:
  using Fibonacci = action_tutorials_interfaces::action::Fibonacci;
  using GoalHandleFibonacci = rclcpp_action::ClientGoalHandle<Fibonacci>;

  ACTION_TUTORIALS_CPP_PUBLIC
  explicit FibonacciActionClient(
    const rclcpp::NodeOptions & node_options = rclcpp::NodeOptions());

  ACTION_TUTORIALS_CPP_PUBLIC
  void send_goal();

private:
  rclcpp_action::Client<Fibonacci>::SharedPtr client_ptr_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace action_tutorials_cpp

#endif  // ACTION_TUTORIALS_CPP__FIBONACCI_ACTION_CLIENT_HPP_
