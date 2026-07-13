// Copyright (c) 2026
// Licensed under the Apache License, Version 2.0.

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "action_msgs/srv/cancel_goal.hpp"
#include "action_tutorials_interfaces/action/fibonacci.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

namespace
{
using namespace std::chrono_literals;
using Fibonacci = action_tutorials_interfaces::action::Fibonacci;
using GoalHandleClient = rclcpp_action::ClientGoalHandle<Fibonacci>;
using GoalHandleServer = rclcpp_action::ServerGoalHandle<Fibonacci>;

constexpr char ACTION_NAME[] = "/rmw_mdds_action_bag";
std::mutex g_marker_mutex;

void PrintMarker(const std::string & marker)
{
  std::lock_guard<std::mutex> lock(g_marker_mutex);
  std::cout << marker << std::endl;
}

class ActionBagServer : public rclcpp::Node
{
public:
  ActionBagServer()
    : Node("rmw_mdds_action_bag_server")
  {
    using std::placeholders::_1;
    using std::placeholders::_2;

    server_ = rclcpp_action::create_server<Fibonacci>(
      this,
      ACTION_NAME,
      std::bind(&ActionBagServer::HandleGoal, this, _1, _2),
      std::bind(&ActionBagServer::HandleCancel, this, _1),
      std::bind(&ActionBagServer::HandleAccepted, this, _1));
    server_->configure_introspection(
      get_clock(), rclcpp::SystemDefaultsQoS(), RCL_SERVICE_INTROSPECTION_CONTENTS);
    PrintMarker(std::string("ACTION_BAG_SERVER_READY|action=") + ACTION_NAME);
  }

private:
  rclcpp_action::GoalResponse HandleGoal(
    const rclcpp_action::GoalUUID &,
    const std::shared_ptr<const Fibonacci::Goal> goal)
  {
    const auto count = goal_count_.fetch_add(1) + 1;
    PrintMarker(
      "ACTION_BAG_SERVER_GOAL|count=" + std::to_string(count) +
      "|order=" + std::to_string(goal->order));
    return goal->order >= 1 && goal->order <= 46 ?
           rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE :
           rclcpp_action::GoalResponse::REJECT;
  }

  rclcpp_action::CancelResponse HandleCancel(
    const std::shared_ptr<GoalHandleServer>)
  {
    const auto count = cancel_count_.fetch_add(1) + 1;
    PrintMarker("ACTION_BAG_SERVER_CANCEL|count=" + std::to_string(count));
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  static void Execute(const std::shared_ptr<GoalHandleServer> goal_handle)
  {
    const auto goal = goal_handle->get_goal();
    auto feedback = std::make_shared<Fibonacci::Feedback>();
    feedback->partial_sequence = {0, 1};

    for (int32_t index = 1; index < goal->order && rclcpp::ok(); ++index) {
      if (goal_handle->is_canceling()) {
        auto result = std::make_shared<Fibonacci::Result>();
        result->sequence = feedback->partial_sequence;
        goal_handle->canceled(result);
        PrintMarker("ACTION_BAG_SERVER_RESULT|status=canceled");
        return;
      }
      const auto & sequence = feedback->partial_sequence;
      feedback->partial_sequence.push_back(sequence[index] + sequence[index - 1]);
      goal_handle->publish_feedback(feedback);
      std::this_thread::sleep_for(100ms);
    }

    if (rclcpp::ok()) {
      auto result = std::make_shared<Fibonacci::Result>();
      result->sequence = feedback->partial_sequence;
      goal_handle->succeed(result);
      PrintMarker("ACTION_BAG_SERVER_RESULT|status=succeeded");
    }
  }

  void HandleAccepted(const std::shared_ptr<GoalHandleServer> goal_handle)
  {
    std::thread(&ActionBagServer::Execute, goal_handle).detach();
  }

  std::atomic<uint32_t> goal_count_{0};
  std::atomic<uint32_t> cancel_count_{0};
  rclcpp_action::Server<Fibonacci>::SharedPtr server_;
};

template<typename FutureT>
void WaitForFuture(
  const rclcpp::Node::SharedPtr & node,
  FutureT & future,
  const std::chrono::seconds timeout,
  const char * operation)
{
  if (rclcpp::spin_until_future_complete(node, future, timeout) !=
    rclcpp::FutureReturnCode::SUCCESS)
  {
    throw std::runtime_error(std::string(operation) + " timed out");
  }
}

void RunClient()
{
  auto node = std::make_shared<rclcpp::Node>("rmw_mdds_action_bag_client");
  auto client = rclcpp_action::create_client<Fibonacci>(node, ACTION_NAME);
  if (!client->wait_for_action_server(15s)) {
    throw std::runtime_error("action server unavailable");
  }

  std::atomic<uint32_t> feedback_count{0};
  rclcpp_action::Client<Fibonacci>::SendGoalOptions options;
  options.feedback_callback = [&feedback_count](
    GoalHandleClient::SharedPtr,
    const std::shared_ptr<const Fibonacci::Feedback>)
    {
      feedback_count.fetch_add(1);
    };

  Fibonacci::Goal complete_goal;
  complete_goal.order = 5;
  auto complete_goal_future = client->async_send_goal(complete_goal, options);
  WaitForFuture(node, complete_goal_future, 15s, "complete send_goal");
  const auto complete_handle = complete_goal_future.get();
  if (!complete_handle) {
    throw std::runtime_error("complete goal rejected");
  }

  auto complete_result_future = client->async_get_result(complete_handle);
  WaitForFuture(node, complete_result_future, 15s, "complete get_result");
  const auto complete_result = complete_result_future.get();
  const std::vector<int32_t> expected{0, 1, 1, 2, 3, 5};
  if (complete_result.code != rclcpp_action::ResultCode::SUCCEEDED ||
    complete_result.result->sequence != expected)
  {
    throw std::runtime_error("unexpected completed goal result");
  }
  PrintMarker("ACTION_BAG_CLIENT_GOAL|status=succeeded");

  feedback_count.store(0);
  Fibonacci::Goal cancel_goal;
  cancel_goal.order = 30;
  auto cancel_goal_future = client->async_send_goal(cancel_goal, options);
  WaitForFuture(node, cancel_goal_future, 15s, "cancel send_goal");
  const auto cancel_handle = cancel_goal_future.get();
  if (!cancel_handle) {
    throw std::runtime_error("cancel goal rejected");
  }

  auto canceled_result_future = client->async_get_result(cancel_handle);
  const auto feedback_deadline = std::chrono::steady_clock::now() + 5s;
  while (feedback_count.load() == 0 && std::chrono::steady_clock::now() < feedback_deadline) {
    rclcpp::spin_some(node);
    std::this_thread::sleep_for(20ms);
  }
  if (feedback_count.load() == 0) {
    throw std::runtime_error("cancel goal produced no feedback");
  }

  auto cancel_future = client->async_cancel_goal(cancel_handle);
  WaitForFuture(node, cancel_future, 15s, "cancel_goal");
  const auto cancel_response = cancel_future.get();
  if (cancel_response->return_code != action_msgs::srv::CancelGoal::Response::ERROR_NONE ||
    cancel_response->goals_canceling.empty())
  {
    throw std::runtime_error("cancel goal was not accepted");
  }

  WaitForFuture(node, canceled_result_future, 15s, "canceled get_result");
  if (canceled_result_future.get().code != rclcpp_action::ResultCode::CANCELED) {
    throw std::runtime_error("unexpected canceled goal result");
  }
  PrintMarker("ACTION_BAG_CLIENT_GOAL|status=canceled");
  PrintMarker(
    "ACTION_BAG_CLIENT_PASS|completed=1|canceled=1|feedback=" +
    std::to_string(feedback_count.load()));
}
}  // namespace

int main(int argc, char ** argv)
{
  if (argc != 2 || (std::string(argv[1]) != "server" && std::string(argv[1]) != "client")) {
    std::cerr << "Usage: rmw_mdds_action_bag_probe <server|client>" << std::endl;
    return 2;
  }

  rclcpp::init(argc, argv);
  try {
    if (std::string(argv[1]) == "server") {
      rclcpp::spin(std::make_shared<ActionBagServer>());
    } else {
      RunClient();
    }
  } catch (const std::exception & error) {
    PrintMarker(std::string("ACTION_BAG_CLIENT_FAIL|reason=") + error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
