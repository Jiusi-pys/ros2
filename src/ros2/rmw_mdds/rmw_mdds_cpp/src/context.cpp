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

#include "context.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "broker.hpp"
#include "rmw/error_handling.h"

#include "rmw_mdds_cpp/identifier.hpp"

namespace rmw_mdds_cpp
{
namespace
{
std::mutex g_node_graph_mutex;
std::vector<NodeData *> g_nodes;

void SetError(std::string * error, const std::string & message)
{
  if (error != nullptr) {
    *error = message;
  }
}

bool RtpsUserDataReceiverRunning(rmw_context_impl_t * impl)
{
  if (impl == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl->rtps_user_data_receiver_mutex);
  return impl->rtps_user_data_receiver_running;
}

bool RtpsDebugEnabled()
{
  return std::getenv("RMW_MDDS_RTPS_DEBUG_DISCOVERY") != nullptr;
}

std::string ReadTextFile(const std::string & path, std::string * error)
{
  std::ifstream input(path);
  if (!input.is_open()) {
    SetError(error, "cannot open " + path);
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::vector<std::string> ExtractTagValues(const std::string & text, const std::string & tag)
{
  std::vector<std::string> values;
  const std::string open_tag = "<" + tag + ">";
  const std::string close_tag = "</" + tag + ">";
  size_t pos = 0u;
  for (;;) {
    const size_t start = text.find(open_tag, pos);
    if (start == std::string::npos) {
      break;
    }
    const size_t value_start = start + open_tag.size();
    const size_t end = text.find(close_tag, value_start);
    if (end == std::string::npos) {
      break;
    }
    values.push_back(text.substr(value_start, end - value_start));
    pos = end + close_tag.size();
  }
  return values;
}

std::string NormalizeXmlValue(std::string value)
{
  value.erase(
    value.begin(),
    std::find_if(value.begin(), value.end(), [](unsigned char ch) {
      return !std::isspace(ch);
    }));
  value.erase(
    std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
      return !std::isspace(ch);
    }).base(),
    value.end());
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

bool GovernanceRequestsProtectedTransport(const std::string & governance)
{
  const char * protection_tags[] = {
    "discovery_protection_kind",
    "liveliness_protection_kind",
    "rtps_protection_kind",
    "metadata_protection_kind",
    "data_protection_kind",
  };
  for (const char * tag : protection_tags) {
    for (const auto & value : ExtractTagValues(governance, tag)) {
      const std::string normalized = NormalizeXmlValue(value);
      if (!normalized.empty() && normalized != "NONE") {
        return true;
      }
    }
  }
  return false;
}

std::vector<std::string> ExtractPermissionTopics(
  const std::string & permissions, const std::string & section_tag)
{
  std::vector<std::string> topics;
  const std::string open_tag = "<" + section_tag + ">";
  const std::string close_tag = "</" + section_tag + ">";
  size_t pos = 0u;
  for (;;) {
    const size_t start = permissions.find(open_tag, pos);
    if (start == std::string::npos) {
      break;
    }
    const size_t section_start = start + open_tag.size();
    const size_t end = permissions.find(close_tag, section_start);
    if (end == std::string::npos) {
      break;
    }
    const std::string section = permissions.substr(section_start, end - section_start);
    const auto section_topics = ExtractTagValues(section, "topic");
    topics.insert(topics.end(), section_topics.begin(), section_topics.end());
    pos = end + close_tag.size();
  }
  return topics;
}

bool ContainsTopic(const std::vector<std::string> & allowed_topics, const std::string & rtps_topic)
{
  return std::find(allowed_topics.begin(), allowed_topics.end(), rtps_topic) !=
         allowed_topics.end();
}

bool SecurityPolicyAllows(
  rmw_context_t * context, const char * topic_name,
  const std::vector<std::string> rmw_mdds_security_policy_s::* allowed_member,
  const char * direction)
{
  if (context == nullptr || context->impl == nullptr || topic_name == nullptr) {
    RMW_SET_ERROR_MSG("security policy check argument is null");
    return false;
  }
  const auto & policy = context->impl->security_policy;
  if (!policy.required) {
    return true;
  }
  if (!policy.policy_loaded) {
    RMW_SET_ERROR_MSG("ROS security policy is required but was not loaded");
    return false;
  }
  const std::string rtps_topic = ToRtpsTopicName(topic_name);
  if (ContainsTopic(policy.*allowed_member, rtps_topic)) {
    return true;
  }
  RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
    "ROS security policy denies %s access to topic %s", direction, topic_name);
  return false;
}

void PrintRtpsEntityId(const char * prefix, const rtps::EntityId & entity_id)
{
  std::fprintf(
    stderr, "%s%02x%02x%02x%02x",
    prefix, entity_id[0], entity_id[1], entity_id[2], entity_id[3]);
}

void RtpsUserDataReceiverLoop(rmw_context_impl_t * impl, uint32_t timeout_ms)
{
  if (impl == nullptr || impl->rtps_participant == nullptr) {
    return;
  }

  for (;;) {
    if (!RtpsUserDataReceiverRunning(impl)) {
      return;
    }

    rtps::ReceivedUserDataMessage message;
    rtps::UdpEndpoint remote;
    std::string ignored_error;
    if (!impl->rtps_participant->ReceiveUserDataMessage(
        &message, &remote, static_cast<int>(timeout_ms), &ignored_error)) {
      if (RtpsDebugEnabled() && ignored_error != "UDP receive timed out") {
        std::fprintf(stderr, "[mdds-user] receive failed: %s\n", ignored_error.c_str());
      }
      continue;
    }
    if (!RtpsUserDataReceiverRunning(impl)) {
      return;
    }

    size_t enqueued = 0;
    if (message.data.reader_id == rtps::kEntityIdUnknown) {
      // Third-party (e.g. Fast-DDS) best-effort DATA carries reader_id =
      // ENTITYID_UNKNOWN. Resolve the writer's DDS topic from the discovered
      // SEDP publications and deliver to local subscriptions on that topic.
      std::string dds_topic;
      for (const auto & ep : impl->rtps_participant->GetDiscoveredSedpEndpoints()) {
        if (ep.endpoint_kind == rtps::SedpEndpointKind::kPublication &&
            ep.participant_guid_prefix == message.header.guid_prefix &&
            ep.endpoint_entity_id == message.data.writer_id) {
          dds_topic = ep.topic_name;
          break;
        }
      }
      if (!dds_topic.empty()) {
        enqueued = EnqueueRtpsUserDataForTopic(
          dds_topic, message.header.guid_prefix, message.data.writer_id,
          message.data.writer_sequence_number, message.data.serialized_payload);
      }
    } else {
      enqueued = EnqueueRtpsUserDataForReader(
        message.data.reader_id, message.header.guid_prefix, message.data.writer_id,
        message.data.writer_sequence_number, message.data.serialized_payload);
    }
    if (RtpsDebugEnabled()) {
      std::fprintf(
        stderr,
        "[mdds-user] data remote=%s:%u seq=%lld payload=%zu enqueued=%zu ",
        remote.address.c_str(), remote.port,
        static_cast<long long>(message.data.writer_sequence_number),
        message.data.serialized_payload.size(), enqueued);
      PrintRtpsEntityId("reader=", message.data.reader_id);
      std::fprintf(stderr, " ");
      PrintRtpsEntityId("writer=", message.data.writer_id);
      std::fprintf(stderr, "\n");
    }
  }
}
}  // namespace

bool IsMddsIdentifier(const char * implementation_identifier)
{
  return implementation_identifier != nullptr &&
         std::strcmp(implementation_identifier, rmw_mdds_cpp_identifier) == 0;
}

bool LoadSecurityPolicy(
  rmw_context_impl_t * impl, const rmw_security_options_t & options, std::string * error)
{
  if (impl == nullptr) {
    SetError(error, "security policy context is null");
    return false;
  }
  impl->security_policy = rmw_mdds_security_policy_s{};
  if (options.enforce_security != RMW_SECURITY_ENFORCEMENT_ENFORCE) {
    return true;
  }

  impl->security_policy.required = true;
  if (options.security_root_path == nullptr || options.security_root_path[0] == '\0') {
    SetError(error, "security root path is empty");
    return false;
  }

  const std::string root(options.security_root_path);
  const std::string governance_path = root + "/governance.xml";
  const std::string permissions_path = root + "/permissions.xml";
  const std::string governance = ReadTextFile(governance_path, error);
  if (governance.empty()) {
    return false;
  }
  if (GovernanceRequestsProtectedTransport(governance)) {
    SetError(
      error,
      "protected SROS2 governance requires signed DDS Security artifacts and transport "
      "protection, but rmw_mdds_cpp currently supports only local XML topic policy");
    return false;
  }
  const std::string permissions = ReadTextFile(permissions_path, error);
  if (permissions.empty()) {
    return false;
  }

  impl->security_policy.publish_topics = ExtractPermissionTopics(permissions, "publish");
  impl->security_policy.subscribe_topics = ExtractPermissionTopics(permissions, "subscribe");
  if (
    impl->security_policy.publish_topics.empty() &&
    impl->security_policy.subscribe_topics.empty()) {
    SetError(error, "permissions.xml has no publish or subscribe topic grants");
    return false;
  }
  impl->security_policy.required = true;
  impl->security_policy.policy_loaded = true;
  impl->security_policy.security_root_path = root;
  return true;
}

bool SecurityPolicyAllowsPublish(rmw_context_t * context, const char * topic_name)
{
  return SecurityPolicyAllows(
    context, topic_name, &rmw_mdds_security_policy_s::publish_topics, "publish");
}

bool SecurityPolicyAllowsSubscribe(rmw_context_t * context, const char * topic_name)
{
  return SecurityPolicyAllows(
    context, topic_name, &rmw_mdds_security_policy_s::subscribe_topics, "subscribe");
}

rmw_ret_t CheckContext(rmw_context_t * context)
{
  if (context == nullptr) {
    RMW_SET_ERROR_MSG("context is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (context->implementation_identifier == nullptr && context->impl == nullptr) {
    RMW_SET_ERROR_MSG("context is zero-initialized");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!IsMddsIdentifier(context->implementation_identifier)) {
    RMW_SET_ERROR_MSG("context implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  if (context->impl == nullptr) {
    RMW_SET_ERROR_MSG("context implementation is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return RMW_RET_OK;
}

rmw_ret_t CheckContextNotShutdown(rmw_context_t * context)
{
  rmw_ret_t ret = CheckContext(context);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (context->impl->is_shutdown) {
    RMW_SET_ERROR_MSG("context is shutdown");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return RMW_RET_OK;
}

void RegisterNode(NodeData * node)
{
  if (node == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_node_graph_mutex);
  g_nodes.push_back(node);
}

void UnregisterNode(NodeData * node)
{
  std::lock_guard<std::mutex> lock(g_node_graph_mutex);
  g_nodes.erase(std::remove(g_nodes.begin(), g_nodes.end(), node), g_nodes.end());
}

std::vector<NodeGraphInfo> GetRegisteredNodes()
{
  std::vector<NodeGraphInfo> nodes;
  std::lock_guard<std::mutex> lock(g_node_graph_mutex);
  nodes.reserve(g_nodes.size());
  for (const auto * node : g_nodes) {
    if (node != nullptr) {
      nodes.push_back(NodeGraphInfo{node->node_name, node->node_namespace, node->enclave});
    }
  }
  return nodes;
}

rtps::EntityId AllocateRtpsEndpointEntityId(rmw_context_t * context, uint8_t entity_kind)
{
  rtps::EntityId entity_id{};
  if (context == nullptr || context->impl == nullptr) {
    return entity_id;
  }

  std::lock_guard<std::mutex> lock(context->impl->rtps_endpoint_mutex);
  const uint32_t key = context->impl->next_rtps_endpoint_entity_key++;
  entity_id[0] = static_cast<uint8_t>((key >> 16u) & 0xffu);
  entity_id[1] = static_cast<uint8_t>((key >> 8u) & 0xffu);
  entity_id[2] = static_cast<uint8_t>(key & 0xffu);
  entity_id[3] = entity_kind;
  return entity_id;
}

std::string ToRtpsTopicName(const char * topic_name)
{
  if (topic_name == nullptr) {
    return {};
  }
  std::string normalized(topic_name);
  const auto first_non_slash = normalized.find_first_not_of('/');
  if (first_non_slash == std::string::npos) {
    return {};
  }
  normalized.erase(0u, first_non_slash);
  return "rt/" + normalized;
}

void RegisterRtpsEndpoint(
  rmw_context_t * context, const rtps::EntityId & endpoint_entity_id,
  const std::string & topic_name, const std::string & type_name,
  rtps::SedpEndpointKind endpoint_kind)
{
  if (
    context == nullptr || context->impl == nullptr ||
    context->impl->rtps_participant == nullptr ||
    endpoint_entity_id == rtps::kEntityIdUnknown ||
    topic_name.empty() || type_name.empty()) {
    return;
  }

  rtps::SedpEndpointAnnouncement announcement;
  announcement.participant_guid_prefix = context->impl->rtps_participant->guid_prefix();
  announcement.endpoint_entity_id = endpoint_entity_id;
  announcement.topic_name = topic_name;
  announcement.type_name = type_name;
  announcement.endpoint_kind = endpoint_kind;
  std::string ignored_error;
  (void)context->impl->rtps_participant->RegisterSedpEndpointAnnouncement(
    announcement, &ignored_error);
}

void UnregisterRtpsEndpoint(rmw_context_t * context, const rtps::EntityId & endpoint_entity_id)
{
  if (
    context == nullptr || context->impl == nullptr ||
    context->impl->rtps_participant == nullptr ||
    endpoint_entity_id == rtps::kEntityIdUnknown) {
    return;
  }
  context->impl->rtps_participant->UnregisterSedpEndpointAnnouncement(endpoint_entity_id);
}

bool StartRtpsUserDataReceiver(rmw_context_t * context, uint32_t timeout_ms, std::string * error)
{
  if (
    context == nullptr || context->impl == nullptr ||
    context->impl->rtps_participant == nullptr) {
    SetError(error, "RTPS user DATA receiver context is not initialized");
    return false;
  }
  if (timeout_ms == 0u) {
    SetError(error, "RTPS user DATA receiver timeout is zero");
    return false;
  }
  if (timeout_ms > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    SetError(error, "RTPS user DATA receiver timeout exceeds poll timeout range");
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(context->impl->rtps_user_data_receiver_mutex);
    if (context->impl->rtps_user_data_receiver_running) {
      SetError(error, "RTPS user DATA receiver is already running");
      return false;
    }
    context->impl->rtps_user_data_receiver_running = true;
  }

  try {
    context->impl->rtps_user_data_receiver_thread =
      std::thread(RtpsUserDataReceiverLoop, context->impl, timeout_ms);
  } catch (const std::exception & e) {
    std::lock_guard<std::mutex> lock(context->impl->rtps_user_data_receiver_mutex);
    context->impl->rtps_user_data_receiver_running = false;
    SetError(error, std::string("failed to start RTPS user DATA receiver thread: ") + e.what());
    return false;
  }
  return true;
}

void StopRtpsUserDataReceiver(rmw_context_t * context)
{
  if (context == nullptr || context->impl == nullptr) {
    return;
  }
  const bool should_join = context->impl->rtps_user_data_receiver_thread.joinable();
  {
    std::lock_guard<std::mutex> lock(context->impl->rtps_user_data_receiver_mutex);
    if (!context->impl->rtps_user_data_receiver_running && !should_join) {
      return;
    }
    context->impl->rtps_user_data_receiver_running = false;
  }
  if (should_join) {
    context->impl->rtps_user_data_receiver_thread.join();
  }
}

}  // namespace rmw_mdds_cpp
