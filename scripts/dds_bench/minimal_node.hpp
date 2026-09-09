// Service-free node interfaces for the benchmark application.
// Copyright (c) 2026 Kaihong Digital Industry Development Co., Ltd.
// Licensed under the Apache License, Version 2.0.
#pragma once
#include <rclcpp/create_publisher.hpp>
#include <rclcpp/create_subscription.hpp>
#include <rclcpp/node_interfaces/node_base.hpp>
#include <rclcpp/node_interfaces/node_parameters_interface.hpp>
#include <rclcpp/node_interfaces/node_timers.hpp>
#include <rclcpp/node_interfaces/node_topics.hpp>
#include <rclcpp/rclcpp.hpp>
#include <utility>

namespace bench {
class MinimalNode {
public:
  MinimalNode(const std::string &name,rclcpp::Context::SharedPtr context) {
    rclcpp::NodeOptions options;
    options.context(std::move(context));
    options.enable_rosout(false);
    options.start_parameter_services(false);
    options.start_parameter_event_publisher(false);
    base_=std::make_shared<rclcpp::node_interfaces::NodeBase>(
      name,"",options.context(),*options.get_rcl_node_options(),false,false);
    timers_=std::make_shared<rclcpp::node_interfaces::NodeTimers>(base_.get());
    topics_=std::make_shared<rclcpp::node_interfaces::NodeTopics>(base_.get(),timers_.get());
  }
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr get_node_base_interface() { return base_; }
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr get_node_topics_interface() { return topics_; }
  rclcpp::node_interfaces::NodeParametersInterface::SharedPtr get_node_parameters_interface() { return nullptr; }
  template<class T>
  auto create_publisher(const std::string &topic,const rclcpp::QoS &qos) {
    return rclcpp::create_publisher<T>(*this,topic,qos);
  }
  template<class T,class Callback>
  auto create_subscription(const std::string &topic,const rclcpp::QoS &qos,Callback &&callback) {
    return rclcpp::create_subscription<T>(*this,topic,qos,std::forward<Callback>(callback));
  }
private:
  rclcpp::node_interfaces::NodeBase::SharedPtr base_;
  rclcpp::node_interfaces::NodeTimers::SharedPtr timers_;
  rclcpp::node_interfaces::NodeTopics::SharedPtr topics_;
};
}
