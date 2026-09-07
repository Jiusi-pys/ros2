#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "example_interfaces/srv/add_two_ints.hpp"

using namespace std::chrono_literals;
using Service = example_interfaces::srv::AddTwoInts;

void run(const std::string & run_id, const std::string & role, const std::string & nonce)
{
  const std::string space = "/graph_waiters_" + run_id + "/" + role;
  auto first = std::make_shared<rclcpp::Node>("first", space);
  auto second = std::make_shared<rclcpp::Node>("second", space);
  if (first->get_node_base_interface()->get_context() != second->get_node_base_interface()->get_context())
    throw std::runtime_error("observer contexts differ");
  std::cout << "GRAPH_WAITER_CONTEXT_SHARED true" << std::endl;
  const std::string peer = role == "A" ? "B" : "A";
  auto peer_client = first->create_client<Service>("/ros_broker_" + run_id + "/" + peer + "/alpha/serve");
  if (!peer_client->wait_for_service(10s)) throw std::runtime_error("peer service unavailable");
  auto request = std::make_shared<Service::Request>();
  request->a = std::stoll(nonce.substr(0, 7), nullptr, 16) + (role == "A" ? 1 : 2);
  request->b = 170017;
  auto response = peer_client->async_send_request(request);
  if (rclcpp::spin_until_future_complete(first, response, 10s) != rclcpp::FutureReturnCode::SUCCESS)
    throw std::runtime_error("peer response timed out");
  const auto reply = response.get();
  if (reply->sum != request->a + request->b) throw std::runtime_error("peer response differs");
  std::cout << "GRAPH_WAITER_RPC " << request->a << " " << request->b << " " << reply->sum << std::endl;
  peer_client.reset();
  auto event_a = first->get_graph_event();
  auto event_b = second->get_graph_event();
  const auto phase = [&](const std::string & name, const std::function<void()> & change,
      const std::function<bool(rclcpp::Node &)> & check) {
      for (int attempt = 0; attempt < 5; ++attempt) {
        std::this_thread::sleep_for(100ms);
        event_a->check_and_clear(); event_b->check_and_clear();
        std::atomic<int> entered{0};
        auto a = std::async(std::launch::async, [&] {
          entered.fetch_add(1); first->wait_for_graph_change(event_a, 2s); return event_a->check();
        });
        auto b = std::async(std::launch::async, [&] {
          entered.fetch_add(1); second->wait_for_graph_change(event_b, 2s); return event_b->check();
        });
        while (entered.load() != 2) std::this_thread::yield();
        std::this_thread::sleep_for(50ms);
        if (a.wait_for(0ms) != std::future_status::timeout || b.wait_for(0ms) != std::future_status::timeout) {
          a.wait(); b.wait(); continue;  // Rearm before mutation after unrelated graph activity.
        }
        change();
        const bool ready_a = a.get(), ready_b = b.get();
        const bool snapshot = check(*first) && check(*second);
        std::cout << std::boolalpha << "GRAPH_WAITER_PHASE {\"phase\":\"" << name
                  << "\",\"first\":" << ready_a << ",\"second\":" << ready_b
                  << ",\"snapshot\":" << snapshot << "}" << std::endl;
        if (!ready_a || !ready_b || !snapshot) throw std::runtime_error("graph waiter missed " + name);
        return;
      }
      throw std::runtime_error("no quiet waiter admission for " + name);
    };
  const std::string topic = space + "/topic", service = space + "/service";
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher;
  phase("publisher_create", [&] {publisher = first->create_publisher<std_msgs::msg::String>(topic, 10);},
    [&](rclcpp::Node & node) {return node.count_publishers(topic) == 1;});
  phase("publisher_destroy", [&] {publisher.reset();}, [&](rclcpp::Node & node) {return node.count_publishers(topic) == 0;});
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription;
  phase("subscription_create", [&] {subscription = first->create_subscription<std_msgs::msg::String>(topic, 10, [](std_msgs::msg::String::ConstSharedPtr) {});},
    [&](rclcpp::Node & node) {return node.count_subscribers(topic) == 1;});
  phase("subscription_destroy", [&] {subscription.reset();}, [&](rclcpp::Node & node) {return node.count_subscribers(topic) == 0;});
  rclcpp::Service<Service>::SharedPtr server;
  phase("service_create", [&] {server = first->create_service<Service>(service, [](const Service::Request::SharedPtr req, Service::Response::SharedPtr res) {res->sum = req->a + req->b;});},
    [&](rclcpp::Node & node) {return node.count_services(service) == 1;});
  phase("service_destroy", [&] {server.reset();}, [&](rclcpp::Node & node) {return node.count_services(service) == 0;});
  rclcpp::Client<Service>::SharedPtr client;
  phase("client_create", [&] {client = first->create_client<Service>(service);}, [&](rclcpp::Node & node) {return node.count_clients(service) == 1;});
  phase("client_destroy", [&] {client.reset();}, [&](rclcpp::Node & node) {return node.count_clients(service) == 0;});
  rclcpp::Node::SharedPtr added;
  const auto has_added = [&](rclcpp::Node & node) {
      const auto names = node.get_node_names();
      return std::count(names.begin(), names.end(), space + "/added");
    };
  phase("node_create", [&] {added = std::make_shared<rclcpp::Node>("added", space);}, [&](rclcpp::Node & node) {return has_added(node) == 1;});
  phase("node_destroy", [&] {added.reset();}, [&](rclcpp::Node & node) {return has_added(node) == 0;});
  std::cout << "GRAPH_WAITERS_PASS " << run_id << " " << role << " " << nonce << std::endl;
}

int main(int argc, char ** argv)
{
  if (argc != 4) return 2;
  rclcpp::init(1, argv, rclcpp::InitOptions(), rclcpp::SignalHandlerOptions::None);
  int result = 0;
  try {run(argv[1], argv[2], argv[3]);}
  catch (const std::exception & error) {std::cerr << error.what() << std::endl; result = 1;}
  rclcpp::shutdown();
  return result;
}
