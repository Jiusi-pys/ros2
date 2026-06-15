#ifndef ACTION_TUTORIALS_CPP__FIBONACCI_ACTION_SERVER_HPP_
#define ACTION_TUTORIALS_CPP__FIBONACCI_ACTION_SERVER_HPP_

#include <memory>

#include "action_tutorials_interfaces/action/fibonacci.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

#include "action_tutorials_cpp/visibility_control.h"

namespace action_tutorials_cpp
{

class FibonacciActionServer : public rclcpp::Node
{
public:
  using Fibonacci = action_tutorials_interfaces::action::Fibonacci;
  using GoalHandleFibonacci = rclcpp_action::ServerGoalHandle<Fibonacci>;

  ACTION_TUTORIALS_CPP_PUBLIC
  explicit FibonacciActionServer(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

private:
  rclcpp_action::Server<Fibonacci>::SharedPtr action_server_;

  ACTION_TUTORIALS_CPP_LOCAL
  void execute(const std::shared_ptr<GoalHandleFibonacci> goal_handle);
};

}  // namespace action_tutorials_cpp

#endif  // ACTION_TUTORIALS_CPP__FIBONACCI_ACTION_SERVER_HPP_
