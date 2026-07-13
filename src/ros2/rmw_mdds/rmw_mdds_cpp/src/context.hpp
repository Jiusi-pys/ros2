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

#ifndef RMW_MDDS_CPP_SRC__CONTEXT_HPP_
#define RMW_MDDS_CPP_SRC__CONTEXT_HPP_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rmw/event_callback_type.h"
#include "rmw/init.h"
#include "rmw/ret_types.h"
#include "rmw/security_options.h"
#include "rmw/types.h"

#include "rtps_participant.hpp"

struct rmw_mdds_security_policy_s
{
  bool required = false;
  bool policy_loaded = false;
  std::string security_root_path;
  std::vector<std::string> publish_topics;
  std::vector<std::string> subscribe_topics;
};

struct rmw_context_impl_s
{
  bool is_shutdown = false;
  size_t node_count = 0;
  rmw_mdds_security_policy_s security_policy;
  rmw_mdds_cpp::rtps::ParticipantConfig rtps_participant_config;
  std::unique_ptr<rmw_mdds_cpp::rtps::RtpsParticipant> rtps_participant;
  std::mutex rtps_user_data_receiver_mutex;
  bool rtps_user_data_receiver_running = false;
  std::thread rtps_user_data_receiver_thread;
  std::mutex rtps_endpoint_mutex;
  uint32_t next_rtps_endpoint_entity_key = 0x10u;
};

namespace rmw_mdds_cpp
{

struct NodeData
{
  rmw_context_t * context;
  rmw_guard_condition_t * graph_guard_condition;
  std::string node_name;
  std::string node_namespace;
  std::string enclave;
};

struct NodeGraphInfo
{
  std::string node_name;
  std::string node_namespace;
  std::string enclave;
};

struct GuardConditionData
{
  std::atomic<bool> triggered{false};
};

struct WaitSetData
{
  rmw_context_t * context;
  size_t max_conditions;
};

struct ServiceRequestSample
{
  rmw_service_info_t info;
  std::vector<uint8_t> payload;
};

struct ServiceResponseSample
{
  rmw_service_info_t info;
  std::vector<uint8_t> payload;
};

struct ServiceRequestIdentity
{
  int64_t sequence_number = 0;
  std::array<uint8_t, RMW_GID_STORAGE_SIZE> writer_guid{};
};

enum class ServiceMessageMembersKind
{
  None,
  C,
  Cpp,
};

struct ServiceMessageTypeInfo
{
  ServiceMessageMembersKind kind = ServiceMessageMembersKind::None;
  const void * members = nullptr;
  size_t size = 0;
};

struct ServiceData
{
  rmw_context_t * context;
  std::string service_name;
  std::string type_name;
  std::string node_name;
  std::string node_namespace;
  std::string node_enclave;
  size_t request_size;
  size_t response_size;
  ServiceMessageTypeInfo request_type;
  ServiceMessageTypeInfo response_type;
  rmw_qos_profile_t actual_qos;
  rmw_event_callback_t request_callback = nullptr;
  const void * request_callback_user_data = nullptr;
  std::mutex mutex;
  std::deque<ServiceRequestSample> requests;
  std::deque<ServiceRequestIdentity> recent_request_identities;
  uint64_t next_response_sequence_number = 1u;
  void * bridge_request_subscription = nullptr;
  void * bridge_response_publisher = nullptr;
  void * broker_client = nullptr;
};

struct ClientData
{
  rmw_context_t * context;
  std::string service_name;
  std::string type_name;
  std::string node_name;
  std::string node_namespace;
  std::string node_enclave;
  size_t request_size;
  size_t response_size;
  ServiceMessageTypeInfo request_type;
  ServiceMessageTypeInfo response_type;
  uint64_t entity_id = 0u;
  int64_t next_sequence_id;
  rmw_qos_profile_t actual_qos;
  rmw_event_callback_t response_callback = nullptr;
  const void * response_callback_user_data = nullptr;
  std::mutex mutex;
  std::deque<ServiceResponseSample> responses;
  void * bridge_request_publisher = nullptr;
  void * bridge_response_subscription = nullptr;
  void * broker_client = nullptr;
};

bool IsMddsIdentifier(const char * implementation_identifier);

bool LoadSecurityPolicy(
  rmw_context_impl_t * impl, const rmw_security_options_t & options, std::string * error);

bool SecurityPolicyAllowsPublish(rmw_context_t * context, const char * topic_name);

bool SecurityPolicyAllowsSubscribe(rmw_context_t * context, const char * topic_name);

rmw_ret_t CheckContext(rmw_context_t * context);

rmw_ret_t CheckContextNotShutdown(rmw_context_t * context);

void RegisterNode(NodeData * node);

void UnregisterNode(NodeData * node);

void TriggerGraphGuardConditions();

std::vector<NodeGraphInfo> GetRegisteredNodes();

bool HasQueuedServiceRequest(ServiceData * service);

bool HasQueuedClientResponse(ClientData * client);

rtps::EntityId AllocateRtpsEndpointEntityId(rmw_context_t * context, uint8_t entity_kind);

std::string ToRtpsTopicName(const char * topic_name);

void RegisterRtpsEndpoint(
  rmw_context_t * context, const rtps::EntityId & endpoint_entity_id,
  const std::string & topic_name, const std::string & type_name,
  rtps::SedpEndpointKind endpoint_kind);

void UnregisterRtpsEndpoint(rmw_context_t * context, const rtps::EntityId & endpoint_entity_id);

bool StartRtpsUserDataReceiver(rmw_context_t * context, uint32_t timeout_ms, std::string * error);

void StopRtpsUserDataReceiver(rmw_context_t * context);

}  // namespace rmw_mdds_cpp

#endif  // RMW_MDDS_CPP_SRC__CONTEXT_HPP_
