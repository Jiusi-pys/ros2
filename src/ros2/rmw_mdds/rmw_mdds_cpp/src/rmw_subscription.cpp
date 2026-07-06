// Copyright 2026 Kaihong Digital Industry Development Co., Ltd.
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

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

#include "bridge_backend.hpp"
#include "broker.hpp"
#include "context.hpp"
#include "ipc_client.hpp"
#include "rcutils/strdup.h"
#include "rmw/allocators.h"
#include "rmw/error_handling.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/validate_full_topic_name.h"
#include "rmw_mdds_cpp/identifier.hpp"

namespace
{
bool AllowsLocalOnlyInternalTopic(const char * topic_name)
{
  return topic_name != nullptr &&
         (std::strcmp(topic_name, "/parameter_events") == 0 ||
          std::strcmp(topic_name, "/rosout") == 0);
}

bool QosDurationEquals(const rmw_time_t & lhs, const rmw_time_t & rhs)
{
  return lhs.sec == rhs.sec && lhs.nsec == rhs.nsec;
}

bool IsUnknownQosProfile(const rmw_qos_profile_t & qos_profile)
{
  return qos_profile.history == rmw_qos_profile_unknown.history &&
         qos_profile.depth == rmw_qos_profile_unknown.depth &&
         qos_profile.reliability == rmw_qos_profile_unknown.reliability &&
         qos_profile.durability == rmw_qos_profile_unknown.durability &&
         QosDurationEquals(qos_profile.deadline, rmw_qos_profile_unknown.deadline) &&
         QosDurationEquals(qos_profile.lifespan, rmw_qos_profile_unknown.lifespan) &&
         qos_profile.liveliness == rmw_qos_profile_unknown.liveliness &&
         QosDurationEquals(
           qos_profile.liveliness_lease_duration,
           rmw_qos_profile_unknown.liveliness_lease_duration) &&
         qos_profile.avoid_ros_namespace_conventions ==
           rmw_qos_profile_unknown.avoid_ros_namespace_conventions;
}

bool ValidateQosProfile(const rmw_qos_profile_t * qos_profile, const char * entity_name)
{
  if (qos_profile == nullptr) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("%s qos profile is null", entity_name);
    return false;
  }
  if (IsUnknownQosProfile(*qos_profile)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("%s qos profile is unknown", entity_name);
    return false;
  }
  return true;
}

rmw_qos_profile_t ResolveActualQosProfile(
  const rmw_qos_profile_t & qos_profile, const rmw_qos_profile_t & default_qos)
{
  rmw_qos_profile_t actual_qos = qos_profile;
  if (actual_qos.history == RMW_QOS_POLICY_HISTORY_SYSTEM_DEFAULT) {
    actual_qos.history =
      default_qos.history == RMW_QOS_POLICY_HISTORY_SYSTEM_DEFAULT ?
      RMW_QOS_POLICY_HISTORY_KEEP_LAST : default_qos.history;
  }
  if (actual_qos.depth == RMW_QOS_POLICY_DEPTH_SYSTEM_DEFAULT) {
    actual_qos.depth =
      default_qos.depth == RMW_QOS_POLICY_DEPTH_SYSTEM_DEFAULT ? 10u : default_qos.depth;
  }
  if (
    actual_qos.reliability == RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT ||
    actual_qos.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_AVAILABLE) {
    actual_qos.reliability =
      default_qos.reliability == RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT ?
      RMW_QOS_POLICY_RELIABILITY_RELIABLE : default_qos.reliability;
  }
  if (
    actual_qos.durability == RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT ||
    actual_qos.durability == RMW_QOS_POLICY_DURABILITY_BEST_AVAILABLE) {
    actual_qos.durability =
      default_qos.durability == RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT ?
      RMW_QOS_POLICY_DURABILITY_VOLATILE : default_qos.durability;
  }
  if (
    actual_qos.liveliness == RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT ||
    actual_qos.liveliness == RMW_QOS_POLICY_LIVELINESS_BEST_AVAILABLE) {
    actual_qos.liveliness =
      default_qos.liveliness == RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT ?
      RMW_QOS_POLICY_LIVELINESS_AUTOMATIC : default_qos.liveliness;
  }
  if (QosDurationEquals(actual_qos.deadline, RMW_QOS_DEADLINE_BEST_AVAILABLE)) {
    actual_qos.deadline = default_qos.deadline;
  }
  if (
    QosDurationEquals(
      actual_qos.liveliness_lease_duration,
      RMW_QOS_LIVELINESS_LEASE_DURATION_BEST_AVAILABLE)) {
    actual_qos.liveliness_lease_duration = default_qos.liveliness_lease_duration;
  }
  return actual_qos;
}

bool ValidateFullTopicName(const char * topic_name)
{
  int validation_result = RMW_TOPIC_VALID;
  size_t invalid_index = 0;
  const rmw_ret_t ret =
    rmw_validate_full_topic_name(topic_name, &validation_result, &invalid_index);
  if (ret == RMW_RET_OK && validation_result == RMW_TOPIC_VALID) {
    return true;
  }

  const char * reason = rmw_full_topic_name_validation_result_string(validation_result);
  if (reason == nullptr) {
    RMW_SET_ERROR_MSG("topic name is invalid");
  } else {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("topic name is invalid: %s", reason);
  }
  return false;
}

bool ValidateTopicName(const char * topic_name, const rmw_qos_profile_t * qos_policies)
{
  if (qos_policies != nullptr && qos_policies->avoid_ros_namespace_conventions) {
    if (topic_name[0] != '\0') {
      return true;
    }
    RMW_SET_ERROR_MSG("native topic name is invalid: empty string");
    return false;
  }
  return ValidateFullTopicName(topic_name);
}

void BridgeCallback(const rmw_mdds_cpp::BridgeSample * sample, void * user_data)
{
  auto * data = static_cast<rmw_mdds_cpp::SubscriptionData *>(user_data);
  if (data == nullptr || sample == nullptr || (sample->data == nullptr && sample->len != 0)) {
    return;
  }
  std::vector<uint8_t> payload(
    static_cast<const uint8_t *>(sample->data),
    static_cast<const uint8_t *>(sample->data) + sample->len);
  rmw_mdds_cpp::QueuedSample queued_sample;
  queued_sample.payload = std::move(payload);
  queued_sample.info = rmw_get_zero_initialized_message_info();
  queued_sample.info.publication_sequence_number = sample->sequenceNumber;
  queued_sample.info.publisher_gid.implementation_identifier = rmw_mdds_cpp_identifier;
  std::memcpy(
    queued_sample.info.publisher_gid.data, sample->senderGuid,
    std::min(sizeof(sample->senderGuid), sizeof(queued_sample.info.publisher_gid.data)));
  queued_sample.info.from_intra_process = false;
  queued_sample.from_bridge = true;
  rmw_mdds_cpp::EnqueueSample(data, queued_sample);
}
}  // namespace

extern "C" {
rmw_subscription_t * rmw_create_subscription(
  const rmw_node_t * node, const rosidl_message_type_support_t * type_support,
  const char * topic_name, const rmw_qos_profile_t * qos_policies,
  const rmw_subscription_options_t * subscription_options)
{
  if (node == nullptr || topic_name == nullptr || subscription_options == nullptr) {
    RMW_SET_ERROR_MSG("subscription creation argument is null");
    return nullptr;
  }
  if (
    !rmw_mdds_cpp::IsMddsIdentifier(node->implementation_identifier) ||
    rmw_mdds_cpp::CheckContextNotShutdown(node->context) != RMW_RET_OK) {
    return nullptr;
  }
  if (!ValidateQosProfile(qos_policies, "subscription")) {
    return nullptr;
  }
  if (!ValidateTopicName(topic_name, qos_policies)) {
    return nullptr;
  }
  if (!rmw_mdds_cpp::SecurityPolicyAllowsSubscribe(node->context, topic_name)) {
    return nullptr;
  }

  auto * data = new (std::nothrow) rmw_mdds_cpp::SubscriptionData();
  if (data == nullptr) {
    return nullptr;
  }
  data->context = node->context;
  data->topic_name = topic_name;
  data->node_name = node->name == nullptr ? "" : node->name;
  data->node_namespace = node->namespace_ == nullptr ? "" : node->namespace_;
  auto * node_data = static_cast<rmw_mdds_cpp::NodeData *>(node->data);
  data->node_enclave = node_data == nullptr ? "" : node_data->enclave;
  data->actual_qos = ResolveActualQosProfile(*qos_policies, rmw_qos_profile_default);
  data->ignore_local_publications = subscription_options->ignore_local_publications;
  data->mdds_topic_name = rmw_mdds_cpp::ToMddsTopicName(topic_name);
  data->rtps_entity_id = rmw_mdds_cpp::AllocateRtpsEndpointEntityId(
    node->context, rmw_mdds_cpp::rtps::kEntityKindUserReaderNoKey);
  if (!data->adapter.Init(type_support)) {
    rmw_reset_error();
    delete data;
    RMW_SET_ERROR_MSG(
      "rmw_mdds_cpp currently supports single-field scalar or string ROS messages only");
    return nullptr;
  }
  if (rmw_mdds_cpp::BrokerModeEnabled()) {
    std::string error;
    data->broker_client = rmw_mdds_cpp::CreateSubscriptionBrokerClient(data, &error);
    if (data->broker_client == nullptr && !AllowsLocalOnlyInternalTopic(topic_name)) {
      delete data;
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("MDDS broker subscription registration failed: %s", error.c_str());
      return nullptr;
    }
  } else {
    auto & bridge_backend = rmw_mdds_cpp::BridgeBackend::Instance();
    data->bridge_subscription = bridge_backend.Subscribe(
      data->mdds_topic_name.c_str(), data->adapter.MddsTypeName().c_str(), &data->actual_qos,
      BridgeCallback, data);
    if (
      data->bridge_subscription == nullptr && bridge_backend.Required() &&
      !AllowsLocalOnlyInternalTopic(topic_name)) {
      delete data;
      RMW_SET_ERROR_MSG("explicit MDDS bridge library is configured but subscription creation failed");
      return nullptr;
    }
  }

  rmw_subscription_t * subscription = rmw_subscription_allocate();
  if (subscription == nullptr) {
    rmw_mdds_cpp::DestroyBrokerClient(data->broker_client);
    delete data;
    return nullptr;
  }
  rcutils_allocator_t allocator = node->context->options.allocator;
  subscription->implementation_identifier = rmw_mdds_cpp_identifier;
  subscription->data = data;
  subscription->topic_name = rcutils_strdup(topic_name, allocator);
  subscription->options = *subscription_options;
  const bool bridge_can_loan =
      data->broker_client == nullptr &&
      rmw_mdds_cpp::BridgeBackend::Instance().SupportsSubscriberLoanedMessages(
          data->bridge_subscription);
  subscription->can_loan_messages =
      data->adapter.IsValid() && data->adapter.SupportsRawLoanedMessage() &&
      bridge_can_loan;
  subscription->is_cft_enabled = false;
  if (subscription->topic_name == nullptr) {
    rmw_mdds_cpp::DestroyBrokerClient(data->broker_client);
    delete data;
    rmw_subscription_free(subscription);
    return nullptr;
  }
  if (
    subscription_options->content_filter_options != nullptr &&
    subscription_options->content_filter_options->filter_expression != nullptr &&
    subscription_options->content_filter_options->filter_expression[0] != '\0') {
    const rmw_ret_t content_filter_ret =
      rmw_mdds_cpp::SetSubscriptionContentFilter(data, subscription_options->content_filter_options);
    if (content_filter_ret == RMW_RET_OK) {
      subscription->is_cft_enabled = true;
    } else {
      rmw_reset_error();
    }
  }
  rmw_mdds_cpp::RegisterSubscription(data);
  rmw_mdds_cpp::RegisterRtpsEndpoint(
    node->context, data->rtps_entity_id, rmw_mdds_cpp::ToRtpsTopicName(topic_name),
    data->adapter.WireTypeName(), rmw_mdds_cpp::rtps::SedpEndpointKind::kSubscription);
  return subscription;
}

rmw_ret_t rmw_destroy_subscription(rmw_node_t * node, rmw_subscription_t * subscription)
{
  if (node == nullptr || subscription == nullptr) {
    RMW_SET_ERROR_MSG("subscription destroy argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (
    !rmw_mdds_cpp::IsMddsIdentifier(node->implementation_identifier) ||
    !rmw_mdds_cpp::IsMddsIdentifier(subscription->implementation_identifier)) {
    RMW_SET_ERROR_MSG("subscription implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  auto * data = static_cast<rmw_mdds_cpp::SubscriptionData *>(subscription->data);
  if (data != nullptr) {
    rmw_mdds_cpp::UnregisterRtpsEndpoint(node->context, data->rtps_entity_id);
  }
  rmw_mdds_cpp::UnregisterSubscription(data);
  if (data != nullptr && data->broker_client != nullptr) {
    rmw_mdds_cpp::DestroyBrokerClient(data->broker_client);
  }
  if (data != nullptr && data->bridge_subscription != nullptr) {
    rmw_mdds_cpp::BridgeBackend::Instance().Unsubscribe(data->bridge_subscription);
  }
  rcutils_allocator_t allocator = node->context->options.allocator;
  allocator.deallocate(const_cast<char *>(subscription->topic_name), allocator.state);
  delete data;
  rmw_subscription_free(subscription);
  return RMW_RET_OK;
}
}  // extern "C"
