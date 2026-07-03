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
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bridge_backend.hpp"
#include "broker.hpp"
#include "context.hpp"
#include "ipc_client.hpp"
#include "rcutils/strdup.h"
#include "rcutils/time.h"
#include "rcutils/types/uint8_array.h"
#include "rcutils/types/string_array.h"
#include "rmw_dds_common/qos.hpp"
#include "rmw/allocators.h"
#include "rmw/error_handling.h"
#include "rmw/events_statuses/matched.h"
#include "rmw/events_statuses/liveliness_changed.h"
#include "rmw/events_statuses/liveliness_lost.h"
#include "rmw/events_statuses/requested_deadline_missed.h"
#include "rmw/events_statuses/offered_deadline_missed.h"
#include "rmw/events_statuses/incompatible_type.h"
#include "rmw/get_network_flow_endpoints.h"
#include "rmw/get_node_info_and_types.h"
#include "rmw/get_service_names_and_types.h"
#include "rmw/get_topic_endpoint_info.h"
#include "rmw/get_topic_names_and_types.h"
#include "rmw/names_and_types.h"
#include "rmw/network_flow_endpoint_array.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/sanity_checks.h"
#include "rmw/serialized_message.h"
#include "rmw/topic_endpoint_info_array.h"
#include "rmw/validate_full_topic_name.h"
#include "rmw/validate_namespace.h"
#include "rmw/validate_node_name.h"
#include "rmw_mdds_cpp/identifier.hpp"
#include "message_adapter.hpp"
#include "rosidl_runtime_c/service_type_support_struct.h"
#include "rosidl_dynamic_typesupport/api/dynamic_data.h"
#include "rosidl_dynamic_typesupport/api/serialization_support.h"
#include "rosidl_dynamic_typesupport/api/serialization_support_interface.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rosidl_typesupport_introspection_c/service_introspection.h"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "rosidl_typesupport_introspection_cpp/service_introspection.hpp"
#include "string_adapter.hpp"

namespace
{
std::mutex g_service_graph_mutex;
std::vector<rmw_mdds_cpp::ServiceData *> g_services;
std::vector<rmw_mdds_cpp::ClientData *> g_clients;

struct ServiceTypeInfo
{
  std::string type_name;
  rmw_mdds_cpp::ServiceMessageTypeInfo request_type;
  rmw_mdds_cpp::ServiceMessageTypeInfo response_type;
};

rmw_ret_t Unsupported(const char * api_name)
{
  RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("%s is not supported by rmw_mdds_cpp yet", api_name);
  return RMW_RET_UNSUPPORTED;
}

bool IsZeroTimeout(const rmw_time_t & wait_timeout)
{
  return wait_timeout.sec == 0 && wait_timeout.nsec == 0;
}

uint64_t TimeoutNanosecondsClamped(const rmw_time_t & wait_timeout)
{
  constexpr uint64_t kNanosecondsPerSecond = 1000000000ull;
  if (wait_timeout.sec > (std::numeric_limits<uint64_t>::max() - wait_timeout.nsec) /
    kNanosecondsPerSecond)
  {
    return std::numeric_limits<uint64_t>::max();
  }
  return wait_timeout.sec * kNanosecondsPerSecond + wait_timeout.nsec;
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

rmw_ret_t WaitForBridgeReliableAcks(
  rmw_mdds_cpp::PublisherData * data, rmw_time_t wait_timeout)
{
  if (data == nullptr || data->bridge_publisher == nullptr) {
    return RMW_RET_OK;
  }

  auto & backend = rmw_mdds_cpp::BridgeBackend::Instance();
  uint32_t unacked_count = 0;
  if (!backend.PublisherUnackedCount(data->bridge_publisher, &unacked_count)) {
    bool unacknowledged = false;
    {
      std::lock_guard<std::mutex> lock(data->mutex);
      unacknowledged = data->bridge_reliable_publication_unacknowledged;
    }
    return unacknowledged ? RMW_RET_TIMEOUT : RMW_RET_OK;
  }

  const uint64_t timeout_ns = TimeoutNanosecondsClamped(wait_timeout);
  const auto start = std::chrono::steady_clock::now();
  while (unacked_count != 0u) {
    {
      std::lock_guard<std::mutex> lock(data->mutex);
      data->bridge_reliable_publication_unacknowledged = true;
    }
    if (IsZeroTimeout(wait_timeout)) {
      return RMW_RET_TIMEOUT;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start).count();
    if (elapsed >= 0 && static_cast<uint64_t>(elapsed) >= timeout_ns) {
      return RMW_RET_TIMEOUT;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!backend.PublisherUnackedCount(data->bridge_publisher, &unacked_count)) {
      return RMW_RET_TIMEOUT;
    }
  }

  {
    std::lock_guard<std::mutex> lock(data->mutex);
    data->bridge_reliable_publication_unacknowledged = false;
  }
  return RMW_RET_OK;
}

using DynamicSerializationImplInit =
  rcutils_ret_t (*)(rcutils_allocator_t *, rosidl_dynamic_typesupport_serialization_support_impl_t *);
using DynamicSerializationInterfaceInit =
  rcutils_ret_t (*)(
    rcutils_allocator_t *, rosidl_dynamic_typesupport_serialization_support_interface_t *);

rmw_ret_t DynamicSupportRcutilsError(rcutils_ret_t ret, const char * what)
{
  if (ret == RCUTILS_RET_OK) {
    return RMW_RET_OK;
  }
  if (ret == RCUTILS_RET_INVALID_ARGUMENT) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("%s rejected invalid argument", what);
    return RMW_RET_INVALID_ARGUMENT;
  }
  RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("%s failed", what);
  return RMW_RET_ERROR;
}

bool IsFastCdrDynamicSerializationName(const char * serialization_lib_name)
{
  return serialization_lib_name != nullptr &&
         (std::strcmp(serialization_lib_name, "cdr") == 0 ||
         std::strcmp(serialization_lib_name, "fastcdr") == 0 ||
         std::strcmp(serialization_lib_name, "rosidl_dynamic_typesupport_fastrtps") == 0 ||
         std::strcmp(serialization_lib_name, "librosidl_dynamic_typesupport_fastrtps.so") == 0);
}

void * OpenFastRtpsDynamicTypesupport()
{
  int flags = RTLD_LAZY | RTLD_LOCAL;
#ifdef RTLD_NODELETE
  flags |= RTLD_NODELETE;
#endif
  void * handle = dlopen("librosidl_dynamic_typesupport_fastrtps.so", flags);
  if (handle == nullptr) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "failed to load FastRTPS dynamic typesupport: %s", dlerror());
  }
  return handle;
}

template<typename FunctionT>
FunctionT ResolveDynamicTypesupportSymbol(void * handle, const char * symbol_name)
{
  dlerror();
  auto * symbol = dlsym(handle, symbol_name);
  const char * error = dlerror();
  if (error != nullptr || symbol == nullptr) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "failed to resolve FastRTPS dynamic typesupport symbol %s: %s",
      symbol_name, error != nullptr ? error : "symbol is null");
    return nullptr;
  }
  return reinterpret_cast<FunctionT>(symbol);
}

bool IsSupportedMatchedEvent(rmw_event_type_t event_type)
{
  return event_type == RMW_EVENT_PUBLICATION_MATCHED ||
         event_type == RMW_EVENT_SUBSCRIPTION_MATCHED;
}

// QoS-status events rmw_mdds actually enforces and raises: offered-deadline-missed
// (publisher) and requested-deadline-missed / liveliness-changed (subscription).
// take_event reports real accumulated counts for these (see broker.cpp deadline /
// liveliness accounting), driven by the rmw_wait poll loop.
bool IsEnforcedPublisherEvent(rmw_event_type_t event_type)
{
  return event_type == RMW_EVENT_OFFERED_DEADLINE_MISSED ||
         event_type == RMW_EVENT_LIVELINESS_LOST ||
         event_type == RMW_EVENT_OFFERED_QOS_INCOMPATIBLE ||
         event_type == RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE;
}
bool IsEnforcedSubscriptionEvent(rmw_event_type_t event_type)
{
  return event_type == RMW_EVENT_REQUESTED_DEADLINE_MISSED ||
         event_type == RMW_EVENT_LIVELINESS_CHANGED ||
         event_type == RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE ||
         event_type == RMW_EVENT_MESSAGE_LOST ||
         event_type == RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE;
}

bool IsNoOpPublisherEvent(rmw_event_type_t event_type)
{
  (void)event_type;
  return false;
}
bool IsNoOpSubscriptionEvent(rmw_event_type_t event_type)
{
  (void)event_type;
  return false;
}

constexpr uint32_t kDefaultBridgeLoanedPayloadCapacity = 64u * 1024u;

bool BorrowPublisherBridgeLoan(
  rmw_mdds_cpp::PublisherData * data, rmw_mdds_cpp::BridgePublisherLoanRecord * loan)
{
  if (data == nullptr || data->bridge_publisher == nullptr || loan == nullptr) {
    return false;
  }
  void * bridge_loan = nullptr;
  void * bridge_data = nullptr;
  auto & backend = rmw_mdds_cpp::BridgeBackend::Instance();
  if (!backend.BorrowLoanedSample(
      data->bridge_publisher, kDefaultBridgeLoanedPayloadCapacity, &bridge_loan,
      &bridge_data)) {
    return false;
  }
  if (bridge_loan == nullptr || bridge_data == nullptr) {
    (void)backend.ReturnLoanedSample(data->bridge_publisher, bridge_loan);
    return false;
  }
  *loan = rmw_mdds_cpp::BridgePublisherLoanRecord{
    bridge_loan, bridge_data, kDefaultBridgeLoanedPayloadCapacity, false, false};
  return true;
}

bool StorePublisherLoanRecord(
  rmw_mdds_cpp::PublisherData * data, void * ros_message,
  const rmw_mdds_cpp::BridgePublisherLoanRecord & loan)
{
  if (data == nullptr || ros_message == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  return data->bridge_publisher_loans.emplace(ros_message, loan).second;
}

bool TakePublisherLoanRecord(
  rmw_mdds_cpp::PublisherData * data, void * ros_message,
  rmw_mdds_cpp::BridgePublisherLoanRecord * loan)
{
  if (data == nullptr || ros_message == nullptr || loan == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  auto it = data->bridge_publisher_loans.find(ros_message);
  if (it == data->bridge_publisher_loans.end()) {
    return false;
  }
  *loan = it->second;
  data->bridge_publisher_loans.erase(it);
  return true;
}

bool ReturnPublisherBridgeLoan(
  rmw_mdds_cpp::PublisherData * data, const rmw_mdds_cpp::BridgePublisherLoanRecord & loan)
{
  if (data == nullptr || data->bridge_publisher == nullptr || loan.loan == nullptr) {
    return true;
  }
  return rmw_mdds_cpp::BridgeBackend::Instance().ReturnLoanedSample(
    data->bridge_publisher, loan.loan);
}

void DestroyPublisherLoanedRosMessage(
  rmw_mdds_cpp::PublisherData * data, void * ros_message,
  const rmw_mdds_cpp::BridgePublisherLoanRecord & bridge_loan)
{
  if (data == nullptr || ros_message == nullptr) {
    return;
  }
  if (bridge_loan.raw_message_in_loan) {
    return;
  }
  if (bridge_loan.message_in_loan) {
    data->adapter.DestroyMessageInPlace(ros_message);
  } else {
    data->adapter.DestroyMessage(ros_message);
  }
}

bool IsSampleExpiredByLifespan(
  const rmw_qos_profile_t & qos, const rmw_message_info_t & info)
{
  const rmw_time_t lifespan = qos.lifespan;
  constexpr uint64_t kInfiniteSec = 9223372036ULL;  // RMW_DURATION_INFINITE.sec
  if ((lifespan.sec == 0 && lifespan.nsec == 0) || lifespan.sec >= kInfiniteSec) {
    return false;
  }
  if (info.source_timestamp <= 0) {
    return false;
  }
  rcutils_time_point_value_t now = 0;
  if (rcutils_system_time_now(&now) != RCUTILS_RET_OK) {
    return false;
  }
  const int64_t lifespan_ns =
    static_cast<int64_t>(lifespan.sec) * 1000000000LL + static_cast<int64_t>(lifespan.nsec);
  return (now - static_cast<int64_t>(info.source_timestamp)) > lifespan_ns;
}

bool TakeNextLiveQueuedSample(
  rmw_mdds_cpp::SubscriptionData * data, rmw_mdds_cpp::QueuedSample * sample)
{
  while (rmw_mdds_cpp::TakeQueuedSample(data, sample)) {
    if (!IsSampleExpiredByLifespan(data->actual_qos, sample->info)) {
      return true;
    }
  }
  return false;
}

std::string MakeRosServiceTypeName(
  const char * service_namespace, const char * service_name, const char * separator)
{
  if (service_namespace == nullptr || service_name == nullptr || separator == nullptr) {
    return {};
  }
  std::string ns(service_namespace);
  size_t pos = 0;
  while ((pos = ns.find(separator, pos)) != std::string::npos) {
    ns.replace(pos, std::strlen(separator), "/");
    pos += 1;
  }
  return ns + "/" + service_name;
}

ServiceTypeInfo ResolveServiceTypeInfo(const rosidl_service_type_support_t * type_support)
{
  ServiceTypeInfo info;
  if (type_support == nullptr) {
    return info;
  }

  const rosidl_service_type_support_t * cpp_introspection = get_service_typesupport_handle(
    type_support, rosidl_typesupport_introspection_cpp::typesupport_identifier);
  if (cpp_introspection != nullptr && cpp_introspection->data != nullptr) {
    const auto * members =
      static_cast<const rosidl_typesupport_introspection_cpp::ServiceMembers *>(
        cpp_introspection->data);
    info.type_name =
      MakeRosServiceTypeName(members->service_namespace_, members->service_name_, "::");
    if (members->request_members_ != nullptr) {
      info.request_type.kind = rmw_mdds_cpp::ServiceMessageMembersKind::Cpp;
      info.request_type.members = members->request_members_;
      info.request_type.size = members->request_members_->size_of_;
    }
    if (members->response_members_ != nullptr) {
      info.response_type.kind = rmw_mdds_cpp::ServiceMessageMembersKind::Cpp;
      info.response_type.members = members->response_members_;
      info.response_type.size = members->response_members_->size_of_;
    }
    return info;
  }

  rmw_reset_error();
  const rosidl_service_type_support_t * c_introspection =
    get_service_typesupport_handle(type_support, rosidl_typesupport_introspection_c__identifier);
  if (c_introspection != nullptr && c_introspection->data != nullptr) {
    const auto * members = static_cast<const rosidl_typesupport_introspection_c__ServiceMembers *>(
      c_introspection->data);
    info.type_name =
      MakeRosServiceTypeName(members->service_namespace_, members->service_name_, "__");
    if (members->request_members_ != nullptr) {
      info.request_type.kind = rmw_mdds_cpp::ServiceMessageMembersKind::C;
      info.request_type.members = members->request_members_;
      info.request_type.size = members->request_members_->size_of_;
    }
    if (members->response_members_ != nullptr) {
      info.response_type.kind = rmw_mdds_cpp::ServiceMessageMembersKind::C;
      info.response_type.members = members->response_members_;
      info.response_type.size = members->response_members_->size_of_;
    }
    return info;
  }

  rmw_reset_error();
  return info;
}

bool ServiceTypeInfoIsValid(const ServiceTypeInfo & info)
{
  return !info.type_name.empty() &&
         info.request_type.kind != rmw_mdds_cpp::ServiceMessageMembersKind::None &&
         info.request_type.members != nullptr && info.request_type.size != 0 &&
         info.response_type.kind != rmw_mdds_cpp::ServiceMessageMembersKind::None &&
         info.response_type.members != nullptr && info.response_type.size != 0;
}

rmw_ret_t CheckNode(const rmw_node_t * node)
{
  if (node == nullptr) {
    RMW_SET_ERROR_MSG("node is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(node->implementation_identifier)) {
    RMW_SET_ERROR_MSG("node implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  return RMW_RET_OK;
}

rmw_ret_t CheckPublisher(const rmw_publisher_t * publisher)
{
  if (publisher == nullptr) {
    RMW_SET_ERROR_MSG("publisher is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(publisher->implementation_identifier)) {
    RMW_SET_ERROR_MSG("publisher implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  return RMW_RET_OK;
}

rmw_ret_t CheckSubscription(const rmw_subscription_t * subscription)
{
  if (subscription == nullptr) {
    RMW_SET_ERROR_MSG("subscription is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(subscription->implementation_identifier)) {
    RMW_SET_ERROR_MSG("subscription implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  return RMW_RET_OK;
}

rmw_ret_t CheckService(const rmw_service_t * service)
{
  if (service == nullptr) {
    RMW_SET_ERROR_MSG("service is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(service->implementation_identifier)) {
    RMW_SET_ERROR_MSG("service implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  return RMW_RET_OK;
}

rmw_ret_t CheckClient(const rmw_client_t * client)
{
  if (client == nullptr) {
    RMW_SET_ERROR_MSG("client is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(client->implementation_identifier)) {
    RMW_SET_ERROR_MSG("client implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  return RMW_RET_OK;
}

rmw_ret_t AbortNamesAndTypesInit(rmw_names_and_types_t * names_and_types, const char * message)
{
  const rmw_ret_t fini_ret = rmw_names_and_types_fini(names_and_types);
  (void)fini_ret;
  RMW_SET_ERROR_MSG(message);
  return RMW_RET_BAD_ALLOC;
}

rmw_ret_t InitNamesAndTypes(
  const rmw_node_t * node, rcutils_allocator_t * allocator, rmw_names_and_types_t * names_and_types,
  const std::vector<rmw_mdds_cpp::NameAndTypes> & entries)
{
  rmw_ret_t ret = CheckNode(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (allocator == nullptr || names_and_types == nullptr) {
    RMW_SET_ERROR_MSG("names and types argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rcutils_allocator_is_valid(allocator)) {
    RMW_SET_ERROR_MSG("names and types allocator is invalid");
    return RMW_RET_INVALID_ARGUMENT;
  }
  ret = rmw_names_and_types_check_zero(names_and_types);
  if (ret != RMW_RET_OK) {
    return RMW_RET_INVALID_ARGUMENT;
  }

  ret = rmw_names_and_types_init(names_and_types, entries.size(), allocator);
  if (ret != RMW_RET_OK) {
    return ret;
  }

  for (size_t i = 0; i < entries.size(); ++i) {
    names_and_types->names.data[i] = rcutils_strdup(entries[i].name.c_str(), *allocator);
    if (names_and_types->names.data[i] == nullptr) {
      return AbortNamesAndTypesInit(names_and_types, "failed to allocate graph name");
    }

    if (
      rcutils_string_array_init(&names_and_types->types[i], entries[i].types.size(), allocator) !=
      RCUTILS_RET_OK) {
      return AbortNamesAndTypesInit(names_and_types, "failed to allocate graph type array");
    }

    for (size_t j = 0; j < entries[i].types.size(); ++j) {
      names_and_types->types[i].data[j] = rcutils_strdup(entries[i].types[j].c_str(), *allocator);
      if (names_and_types->types[i].data[j] == nullptr) {
        return AbortNamesAndTypesInit(names_and_types, "failed to allocate graph type name");
      }
    }
  }

  return RMW_RET_OK;
}

void AddServiceNameAndType(
  std::vector<rmw_mdds_cpp::NameAndTypes> * names_and_types, const std::string & name,
  const std::string & type)
{
  if (names_and_types == nullptr || name.empty() || type.empty()) {
    return;
  }
  auto it = std::find_if(
    names_and_types->begin(), names_and_types->end(),
    [&name](const rmw_mdds_cpp::NameAndTypes & entry) { return entry.name == name; });
  if (it == names_and_types->end()) {
    names_and_types->push_back(rmw_mdds_cpp::NameAndTypes{name, {type}});
    return;
  }
  if (std::find(it->types.begin(), it->types.end(), type) == it->types.end()) {
    it->types.push_back(type);
  }
}

bool BelongsToNode(
  const std::string & entity_node_name, const std::string & entity_node_namespace,
  const char * node_name, const char * node_namespace)
{
  return node_name != nullptr && node_namespace != nullptr && entity_node_name == node_name &&
         entity_node_namespace == node_namespace;
}

void AddUniqueNodeGraphInfo(
  std::vector<rmw_mdds_cpp::NodeGraphInfo> * nodes,
  const rmw_mdds_cpp::NodeGraphInfo & candidate)
{
  if (nodes == nullptr || candidate.node_name.empty()) {
    return;
  }
  const auto it = std::find_if(
    nodes->begin(), nodes->end(), [&candidate](const rmw_mdds_cpp::NodeGraphInfo & current) {
      return current.node_name == candidate.node_name &&
             current.node_namespace == candidate.node_namespace &&
             current.enclave == candidate.enclave;
    });
  if (it == nodes->end()) {
    nodes->push_back(candidate);
  }
}

bool GraphNodeExists(const char * node_name, const char * node_namespace)
{
  auto nodes = rmw_mdds_cpp::GetRegisteredNodes();
  if (rmw_mdds_cpp::BrokerModeEnabled()) {
    for (const auto & broker_node : rmw_mdds_cpp::GetBrokerGraphNodes()) {
      AddUniqueNodeGraphInfo(&nodes, broker_node);
    }
  }
  for (const auto & node_info : nodes) {
    if (node_info.node_name == node_name && node_info.node_namespace == node_namespace) {
      return true;
    }
  }
  return false;
}

rmw_ret_t ValidateNamesAndTypesByNodeQuery(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, rmw_names_and_types_t * names_and_types)
{
  rmw_ret_t ret = CheckNode(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (allocator == nullptr) {
    RMW_SET_ERROR_MSG("names and types allocator is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rcutils_allocator_is_valid(allocator)) {
    RMW_SET_ERROR_MSG("names and types allocator is invalid");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (names_and_types == nullptr) {
    RMW_SET_ERROR_MSG("names and types argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  ret = rmw_names_and_types_check_zero(names_and_types);
  if (ret != RMW_RET_OK) {
    return RMW_RET_INVALID_ARGUMENT;
  }

  int validation_result = RMW_NODE_NAME_VALID;
  size_t invalid_index = 0;
  ret = rmw_validate_node_name(node_name, &validation_result, &invalid_index);
  if (ret != RMW_RET_OK || validation_result != RMW_NODE_NAME_VALID) {
    RMW_SET_ERROR_MSG("node name is invalid");
    return RMW_RET_INVALID_ARGUMENT;
  }
  validation_result = RMW_NAMESPACE_VALID;
  invalid_index = 0;
  ret = rmw_validate_namespace(node_namespace, &validation_result, &invalid_index);
  if (ret != RMW_RET_OK || validation_result != RMW_NAMESPACE_VALID) {
    RMW_SET_ERROR_MSG("node namespace is invalid");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!GraphNodeExists(node_name, node_namespace)) {
    RMW_SET_ERROR_MSG("node name was not found");
    return RMW_RET_NODE_NAME_NON_EXISTENT;
  }
  return RMW_RET_OK;
}

rmw_ret_t ValidateFullyQualifiedName(const char * name, const char * argument_name)
{
  int validation_result = RMW_TOPIC_VALID;
  size_t invalid_index = 0;
  const rmw_ret_t ret = rmw_validate_full_topic_name(
    name, &validation_result, &invalid_index);
  if (ret == RMW_RET_OK && validation_result == RMW_TOPIC_VALID) {
    return RMW_RET_OK;
  }

  const char * reason = rmw_full_topic_name_validation_result_string(validation_result);
  if (reason == nullptr) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("%s is invalid", argument_name);
  } else {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("%s is invalid: %s", argument_name, reason);
  }
  return RMW_RET_INVALID_ARGUMENT;
}

bool SameServiceNameAndType(
  const rmw_mdds_cpp::ServiceData * service, const rmw_mdds_cpp::ClientData * client)
{
  if (service == nullptr || client == nullptr || service->service_name != client->service_name) {
    return false;
  }
  return service->type_name.empty() || client->type_name.empty() ||
         service->type_name == client->type_name;
}

void RegisterService(rmw_mdds_cpp::ServiceData * service)
{
  if (service == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  g_services.push_back(service);
}

void UnregisterService(rmw_mdds_cpp::ServiceData * service)
{
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  g_services.erase(std::remove(g_services.begin(), g_services.end(), service), g_services.end());
}

void RegisterClient(rmw_mdds_cpp::ClientData * client)
{
  if (client == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  g_clients.push_back(client);
}

void UnregisterClient(rmw_mdds_cpp::ClientData * client)
{
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  g_clients.erase(std::remove(g_clients.begin(), g_clients.end(), client), g_clients.end());
}

size_t CountServicesByName(const char * service_name)
{
  if (service_name == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  return static_cast<size_t>(std::count_if(
    g_services.begin(), g_services.end(),
    [service_name](const rmw_mdds_cpp::ServiceData * service) {
      return service != nullptr && service->service_name == service_name;
    }));
}

size_t CountClientsByName(const char * service_name)
{
  if (service_name == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  return static_cast<size_t>(std::count_if(
    g_clients.begin(), g_clients.end(), [service_name](const rmw_mdds_cpp::ClientData * client) {
      return client != nullptr && client->service_name == service_name;
    }));
}

bool HasMatchingService(const rmw_mdds_cpp::ClientData * client)
{
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  return std::any_of(
    g_services.begin(), g_services.end(), [client](const rmw_mdds_cpp::ServiceData * service) {
      return SameServiceNameAndType(service, client);
    });
}

void FillClientGuid(const rmw_mdds_cpp::ClientData * client, uint8_t guid[RMW_GID_STORAGE_SIZE])
{
  std::memset(guid, 0, RMW_GID_STORAGE_SIZE);
  const uintptr_t address = reinterpret_cast<uintptr_t>(client);
  std::memcpy(guid, &address, std::min(sizeof(address), static_cast<size_t>(RMW_GID_STORAGE_SIZE)));
}

bool ClientMatchesGuid(
  const rmw_mdds_cpp::ClientData * client, const uint8_t guid[RMW_GID_STORAGE_SIZE])
{
  uint8_t expected[RMW_GID_STORAGE_SIZE];
  FillClientGuid(client, expected);
  return std::memcmp(expected, guid, RMW_GID_STORAGE_SIZE) == 0;
}

rmw_mdds_cpp::ClientData * FindClientByGuid(const uint8_t guid[RMW_GID_STORAGE_SIZE])
{
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  const auto it = std::find_if(
    g_clients.begin(), g_clients.end(), [guid](const rmw_mdds_cpp::ClientData * client) {
      return client != nullptr && ClientMatchesGuid(client, guid);
    });
  return it == g_clients.end() ? nullptr : *it;
}

std::vector<rmw_mdds_cpp::ServiceData *> GetMatchingServices(
  const rmw_mdds_cpp::ClientData * client)
{
  std::vector<rmw_mdds_cpp::ServiceData *> services;
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  for (auto * service : g_services) {
    if (SameServiceNameAndType(service, client)) {
      services.push_back(service);
    }
  }
  return services;
}

std::string MakeServiceBridgeTopic(const char * prefix, const std::string & service_name)
{
  std::string topic(prefix == nullptr ? "" : prefix);
  topic += rmw_mdds_cpp::ToMddsTopicName(service_name.c_str());
  return topic;
}

std::string MakeNodeServicePrefix(const char * node_name, const char * node_namespace)
{
  if (node_name == nullptr || node_name[0] == '\0') {
    return {};
  }
  std::string prefix;
  if (node_namespace == nullptr || node_namespace[0] == '\0' ||
    std::strcmp(node_namespace, "/") == 0) {
    prefix = "/";
  } else {
    prefix = node_namespace;
    if (prefix.back() != '/') {
      prefix += "/";
    }
  }
  prefix += node_name;
  return prefix;
}

bool AllowsLocalOnlyInternalService(
  const char * service_name, const char * node_name, const char * node_namespace)
{
  if (service_name == nullptr) {
    return false;
  }
  const std::string prefix = MakeNodeServicePrefix(node_name, node_namespace);
  if (prefix.empty()) {
    return false;
  }
  static constexpr const char * kInternalServiceSuffixes[] = {
    "/get_type_description",
    "/describe_parameters",
    "/get_parameters",
    "/get_parameter_types",
    "/list_parameters",
    "/set_parameters",
    "/set_parameters_atomically",
    "/get_logger_levels",
    "/set_logger_levels",
  };
  for (const char * suffix : kInternalServiceSuffixes) {
    const std::string expected = prefix + suffix;
    if (expected == service_name) {
      return true;
    }
  }
  return false;
}

std::string MakeServiceBridgeType(const std::string & service_type_name, const char * suffix)
{
  if (service_type_name.empty()) {
    return {};
  }
  std::string type_name(service_type_name);
  type_name += suffix == nullptr ? "" : suffix;
  return type_name;
}

void AppendI64(std::vector<uint8_t> * payload, int64_t value)
{
  if (payload == nullptr) {
    return;
  }
  uint64_t bits = static_cast<uint64_t>(value);
  for (size_t i = 0; i < sizeof(bits); ++i) {
    payload->push_back(static_cast<uint8_t>((bits >> (8u * i)) & 0xffu));
  }
}

bool ReadI64(const uint8_t * data, size_t len, size_t * offset, int64_t * value)
{
  if (data == nullptr || offset == nullptr || value == nullptr || len - *offset < sizeof(uint64_t)) {
    return false;
  }
  uint64_t bits = 0;
  for (size_t i = 0; i < sizeof(bits); ++i) {
    bits |= static_cast<uint64_t>(data[*offset + i]) << (8u * i);
  }
  *offset += sizeof(bits);
  *value = static_cast<int64_t>(bits);
  return true;
}

bool EncodeServiceWirePayload(
  const rmw_request_id_t & request_id, rmw_time_point_value_t source_timestamp,
  const std::vector<uint8_t> & payload, std::vector<uint8_t> * wire_payload)
{
  if (wire_payload == nullptr) {
    return false;
  }
  wire_payload->clear();
  wire_payload->reserve(sizeof(int64_t) + RMW_GID_STORAGE_SIZE + sizeof(int64_t) + payload.size());
  AppendI64(wire_payload, request_id.sequence_number);
  wire_payload->insert(
    wire_payload->end(), request_id.writer_guid, request_id.writer_guid + RMW_GID_STORAGE_SIZE);
  AppendI64(wire_payload, static_cast<int64_t>(source_timestamp));
  wire_payload->insert(wire_payload->end(), payload.begin(), payload.end());
  return true;
}

bool DecodeServiceWirePayloadBytes(
  const uint8_t * data, size_t len, rmw_service_info_t * info, std::vector<uint8_t> * payload)
{
  if (info == nullptr || payload == nullptr || (data == nullptr && len != 0)) {
    return false;
  }
  size_t offset = 0;
  int64_t sequence_number = 0;
  int64_t source_timestamp = 0;
  if (!ReadI64(data, len, &offset, &sequence_number)) {
    return false;
  }
  if (len - offset < RMW_GID_STORAGE_SIZE) {
    return false;
  }
  *info = {};
  info->request_id.sequence_number = sequence_number;
  std::memcpy(info->request_id.writer_guid, data + offset, RMW_GID_STORAGE_SIZE);
  offset += RMW_GID_STORAGE_SIZE;
  if (!ReadI64(data, len, &offset, &source_timestamp)) {
    return false;
  }
  info->source_timestamp = static_cast<rmw_time_point_value_t>(source_timestamp);
  payload->assign(data + offset, data + len);
  return true;
}

bool DecodeServiceWirePayload(
  const rmw_mdds_cpp::BridgeSample * sample, rmw_service_info_t * info,
  std::vector<uint8_t> * payload)
{
  if (sample == nullptr) {
    return false;
  }
  return DecodeServiceWirePayloadBytes(
    static_cast<const uint8_t *>(sample->data), sample->len, info, payload);
}

bool ScalarSizeForType(uint8_t type_id, size_t * size)
{
  if (size == nullptr) {
    return false;
  }
  switch (type_id) {
    case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
      *size = sizeof(float);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE:
      *size = sizeof(double);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_LONG_DOUBLE:
      *size = sizeof(long double);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_CHAR:
      *size = sizeof(char);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_WCHAR:
      *size = sizeof(char16_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN:
      *size = sizeof(bool);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_OCTET:
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
      *size = sizeof(uint8_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
      *size = sizeof(int8_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
      *size = sizeof(uint16_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
      *size = sizeof(int16_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
      *size = sizeof(uint32_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
      *size = sizeof(int32_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
      *size = sizeof(uint64_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
      *size = sizeof(int64_t);
      return true;
    default:
      return false;
  }
}

bool AddSerializedSize(size_t value, size_t * total)
{
  if (total == nullptr || value > std::numeric_limits<size_t>::max() - *total) {
    return false;
  }
  *total += value;
  return true;
}

bool AddCdrAlignment(size_t alignment, size_t * total)
{
  if (alignment == 0 || total == nullptr) {
    return false;
  }
  const size_t remainder = *total % alignment;
  if (remainder == 0) {
    return true;
  }
  return AddSerializedSize(alignment - remainder, total);
}

bool AddCdrAlignedSize(size_t value_size, size_t * total)
{
  if (!AddCdrAlignment(value_size, total)) {
    return false;
  }
  return AddSerializedSize(value_size, total);
}

bool AddCdrStringSize(size_t string_upper_bound, size_t * total)
{
  if (string_upper_bound == 0 || total == nullptr) {
    return false;
  }
  if (!AddCdrAlignment(sizeof(uint32_t), total) || !AddSerializedSize(sizeof(uint32_t), total)) {
    return false;
  }
  if (!AddSerializedSize(string_upper_bound, total)) {
    return false;
  }
  return AddSerializedSize(1u, total);
}

bool AddFixedValueSerializedSizeC(
  const rosidl_typesupport_introspection_c__MessageMember & member, size_t * total);

bool AddFixedMessageSerializedSizeC(
  const rosidl_typesupport_introspection_c__MessageMembers * members, size_t * total)
{
  if (
    members == nullptr || total == nullptr ||
    (members->member_count_ != 0 && members->members_ == nullptr)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    if (member.is_array_) {
      if (member.array_size_ == 0 && !member.is_upper_bound_) {
        return false;
      }
      if (member.is_upper_bound_ && !AddCdrAlignedSize(sizeof(uint32_t), total)) {
        return false;
      }
      for (size_t j = 0; j < member.array_size_; ++j) {
        if (!AddFixedValueSerializedSizeC(member, total)) {
          return false;
        }
      }
      continue;
    }
    if (!AddFixedValueSerializedSizeC(member, total)) {
      return false;
    }
  }
  return true;
}

bool FixedMessageSerializedSizeC(
  const rosidl_typesupport_introspection_c__MessageMembers * members, size_t * size)
{
  if (size == nullptr) {
    return false;
  }
  size_t total = 0;
  if (!AddFixedMessageSerializedSizeC(members, &total)) {
    return false;
  }
  *size = total;
  return true;
}

bool AddFixedValueSerializedSizeC(
  const rosidl_typesupport_introspection_c__MessageMember & member, size_t * total)
{
  if (total == nullptr) {
    return false;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_STRING) {
    return AddCdrStringSize(member.string_upper_bound_, total);
  }
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
    const rosidl_message_type_support_t * nested = get_message_typesupport_handle(
      member.members_, rosidl_typesupport_introspection_c__identifier);
    if (nested == nullptr || nested->data == nullptr) {
      return false;
    }
    return AddFixedMessageSerializedSizeC(
      static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(nested->data), total);
  }
  size_t scalar_size = 0;
  if (!ScalarSizeForType(member.type_id_, &scalar_size)) {
    return false;
  }
  return AddCdrAlignedSize(scalar_size, total);
}

bool AddFixedValueSerializedSizeCpp(
  const rosidl_typesupport_introspection_cpp::MessageMember & member, size_t * total);

bool AddFixedMessageSerializedSizeCpp(
  const rosidl_typesupport_introspection_cpp::MessageMembers * members, size_t * total)
{
  if (
    members == nullptr || total == nullptr ||
    (members->member_count_ != 0 && members->members_ == nullptr)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    if (member.is_array_) {
      if (member.array_size_ == 0 && !member.is_upper_bound_) {
        return false;
      }
      if (member.is_upper_bound_ && !AddCdrAlignedSize(sizeof(uint32_t), total)) {
        return false;
      }
      for (size_t j = 0; j < member.array_size_; ++j) {
        if (!AddFixedValueSerializedSizeCpp(member, total)) {
          return false;
        }
      }
      continue;
    }
    if (!AddFixedValueSerializedSizeCpp(member, total)) {
      return false;
    }
  }
  return true;
}

bool FixedMessageSerializedSizeCpp(
  const rosidl_typesupport_introspection_cpp::MessageMembers * members, size_t * size)
{
  if (size == nullptr) {
    return false;
  }
  size_t total = 0;
  if (!AddFixedMessageSerializedSizeCpp(members, &total)) {
    return false;
  }
  *size = total;
  return true;
}

bool AddFixedValueSerializedSizeCpp(
  const rosidl_typesupport_introspection_cpp::MessageMember & member, size_t * total)
{
  if (total == nullptr) {
    return false;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING) {
    return AddCdrStringSize(member.string_upper_bound_, total);
  }
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE) {
    const rosidl_message_type_support_t * nested = get_message_typesupport_handle(
      member.members_, rosidl_typesupport_introspection_cpp::typesupport_identifier);
    if (nested == nullptr || nested->data == nullptr) {
      return false;
    }
    return AddFixedMessageSerializedSizeCpp(
      static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(nested->data),
      total);
  }
  size_t scalar_size = 0;
  if (!ScalarSizeForType(member.type_id_, &scalar_size)) {
    return false;
  }
  return AddCdrAlignedSize(scalar_size, total);
}

bool FixedSerializedMessageSize(const rosidl_message_type_support_t * type_support, size_t * size)
{
  if (type_support == nullptr || size == nullptr) {
    return false;
  }
  const rosidl_message_type_support_t * cpp_introspection = get_message_typesupport_handle(
    type_support, rosidl_typesupport_introspection_cpp::typesupport_identifier);
  if (cpp_introspection != nullptr && cpp_introspection->data != nullptr) {
    size_t payload_size = 0;
    if (!FixedMessageSerializedSizeCpp(
      static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(
        cpp_introspection->data),
      &payload_size)) {
      return false;
    }
    if (!AddSerializedSize(4u, &payload_size)) {
      return false;
    }
    *size = payload_size;
    return true;
  }
  rmw_reset_error();
  const rosidl_message_type_support_t * c_introspection =
    get_message_typesupport_handle(type_support, rosidl_typesupport_introspection_c__identifier);
  if (c_introspection != nullptr && c_introspection->data != nullptr) {
    size_t payload_size = 0;
    if (!FixedMessageSerializedSizeC(
      static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(
        c_introspection->data),
      &payload_size)) {
      return false;
    }
    if (!AddSerializedSize(4u, &payload_size)) {
      return false;
    }
    *size = payload_size;
    return true;
  }
  rmw_reset_error();
  return false;
}

bool CopyFromRosMessage(
  const void * ros_message, const rmw_mdds_cpp::ServiceMessageTypeInfo & type,
  std::vector<uint8_t> * payload)
{
  if (payload == nullptr || (ros_message == nullptr && type.size != 0)) {
    return false;
  }
  if (type.kind == rmw_mdds_cpp::ServiceMessageMembersKind::C) {
    rmw_mdds_cpp::StringAdapter adapter;
    return adapter.InitC(
             static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(
               type.members)) &&
           adapter.Encode(ros_message, payload);
  }
  if (type.kind == rmw_mdds_cpp::ServiceMessageMembersKind::Cpp) {
    rmw_mdds_cpp::StringAdapter adapter;
    return adapter.InitCpp(
             static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(
               type.members)) &&
           adapter.Encode(ros_message, payload);
  }
  const auto * bytes = static_cast<const uint8_t *>(ros_message);
  payload->assign(bytes, bytes + type.size);
  return true;
}

bool CopyToRosMessage(
  const std::vector<uint8_t> & payload, const rmw_mdds_cpp::ServiceMessageTypeInfo & type,
  void * ros_message)
{
  if (ros_message == nullptr) {
    return false;
  }
  if (type.kind == rmw_mdds_cpp::ServiceMessageMembersKind::C) {
    rmw_mdds_cpp::StringAdapter adapter;
    return adapter.InitC(
             static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(
               type.members)) &&
           adapter.Decode(payload.data(), payload.size(), ros_message);
  }
  if (type.kind == rmw_mdds_cpp::ServiceMessageMembersKind::Cpp) {
    rmw_mdds_cpp::StringAdapter adapter;
    return adapter.InitCpp(
             static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(
               type.members)) &&
           adapter.Decode(payload.data(), payload.size(), ros_message);
  }
  if (payload.size() != type.size) {
    return false;
  }
  if (type.size != 0) {
    std::memcpy(ros_message, payload.data(), type.size);
  }
  return true;
}

std::vector<rmw_mdds_cpp::NameAndTypes> GetServiceNamesAndTypes()
{
  std::vector<rmw_mdds_cpp::NameAndTypes> names_and_types;
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  for (const auto * service : g_services) {
    if (service != nullptr) {
      AddServiceNameAndType(&names_and_types, service->service_name, service->type_name);
    }
  }
  return names_and_types;
}

std::vector<rmw_mdds_cpp::NameAndTypes> GetServiceNamesAndTypesByNode(
  const char * node_name, const char * node_namespace)
{
  std::vector<rmw_mdds_cpp::NameAndTypes> names_and_types;
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  for (const auto * service : g_services) {
    if (
      service != nullptr &&
      BelongsToNode(service->node_name, service->node_namespace, node_name, node_namespace)) {
      AddServiceNameAndType(&names_and_types, service->service_name, service->type_name);
    }
  }
  return names_and_types;
}

std::vector<rmw_mdds_cpp::NameAndTypes> GetClientNamesAndTypesByNode(
  const char * node_name, const char * node_namespace)
{
  std::vector<rmw_mdds_cpp::NameAndTypes> names_and_types;
  std::lock_guard<std::mutex> lock(g_service_graph_mutex);
  for (const auto * client : g_clients) {
    if (
      client != nullptr &&
      BelongsToNode(client->node_name, client->node_namespace, node_name, node_namespace)) {
      AddServiceNameAndType(&names_and_types, client->service_name, client->type_name);
    }
  }
  return names_and_types;
}

rmw_ret_t InitStringArray(
  rcutils_allocator_t * allocator, const std::vector<std::string> & values,
  rcutils_string_array_t * string_array)
{
  if (allocator == nullptr || string_array == nullptr) {
    RMW_SET_ERROR_MSG("string array argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (rcutils_string_array_init(string_array, values.size(), allocator) != RCUTILS_RET_OK) {
    RMW_SET_ERROR_MSG("failed to initialize string array");
    return RMW_RET_BAD_ALLOC;
  }
  for (size_t i = 0; i < values.size(); ++i) {
    string_array->data[i] = rcutils_strdup(values[i].c_str(), *allocator);
    if (string_array->data[i] == nullptr) {
      const rcutils_ret_t fini_ret = rcutils_string_array_fini(string_array);
      (void)fini_ret;
      RMW_SET_ERROR_MSG("failed to copy string array value");
      return RMW_RET_BAD_ALLOC;
    }
  }
  return RMW_RET_OK;
}

rmw_ret_t InitNodeGraphStringArrays(
  const rmw_node_t * node, rcutils_string_array_t * node_names,
  rcutils_string_array_t * node_namespaces, rcutils_string_array_t * enclaves)
{
  rmw_ret_t ret = CheckNode(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (node_names == nullptr || node_namespaces == nullptr) {
    RMW_SET_ERROR_MSG("node graph string array argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  ret = rmw_check_zero_rmw_string_array(node_names);
  if (ret != RMW_RET_OK) {
    return RMW_RET_INVALID_ARGUMENT;
  }
  ret = rmw_check_zero_rmw_string_array(node_namespaces);
  if (ret != RMW_RET_OK) {
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (enclaves != nullptr) {
    ret = rmw_check_zero_rmw_string_array(enclaves);
    if (ret != RMW_RET_OK) {
      return RMW_RET_INVALID_ARGUMENT;
    }
  }

  auto nodes = rmw_mdds_cpp::GetRegisteredNodes();
  if (rmw_mdds_cpp::BrokerModeEnabled()) {
    for (const auto & broker_node : rmw_mdds_cpp::GetBrokerGraphNodes()) {
      AddUniqueNodeGraphInfo(&nodes, broker_node);
    }
  }
  std::vector<std::string> names;
  std::vector<std::string> namespaces;
  std::vector<std::string> enclave_values;
  names.reserve(nodes.size());
  namespaces.reserve(nodes.size());
  enclave_values.reserve(nodes.size());
  for (const auto & node_info : nodes) {
    names.push_back(node_info.node_name);
    namespaces.push_back(node_info.node_namespace);
    enclave_values.push_back(node_info.enclave);
  }

  rcutils_allocator_t allocator = node->context->options.allocator;
  ret = InitStringArray(&allocator, names, node_names);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  ret = InitStringArray(&allocator, namespaces, node_namespaces);
  if (ret != RMW_RET_OK) {
    const rcutils_ret_t fini_ret = rcutils_string_array_fini(node_names);
    (void)fini_ret;
    return ret;
  }
  if (enclaves != nullptr) {
    ret = InitStringArray(&allocator, enclave_values, enclaves);
    if (ret != RMW_RET_OK) {
      const rcutils_ret_t namespaces_fini_ret = rcutils_string_array_fini(node_namespaces);
      const rcutils_ret_t names_fini_ret = rcutils_string_array_fini(node_names);
      (void)namespaces_fini_ret;
      (void)names_fini_ret;
      return ret;
    }
  }
  return RMW_RET_OK;
}

rmw_ret_t PopulateTopicEndpointInfo(
  const rmw_mdds_cpp::TopicEndpointInfo & source, rcutils_allocator_t * allocator,
  rmw_topic_endpoint_info_t * destination)
{
  if (allocator == nullptr || destination == nullptr) {
    RMW_SET_ERROR_MSG("topic endpoint populate argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_ret_t ret =
    rmw_topic_endpoint_info_set_node_name(destination, source.node_name.c_str(), allocator);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  ret = rmw_topic_endpoint_info_set_node_namespace(
    destination, source.node_namespace.c_str(), allocator);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  ret = rmw_topic_endpoint_info_set_topic_type(destination, source.topic_type.c_str(), allocator);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  ret = rmw_topic_endpoint_info_set_topic_type_hash(destination, &source.topic_type_hash);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  ret = rmw_topic_endpoint_info_set_endpoint_type(destination, source.endpoint_type);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  ret = rmw_topic_endpoint_info_set_gid(destination, source.gid.data, RMW_GID_STORAGE_SIZE);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  return rmw_topic_endpoint_info_set_qos_profile(destination, &source.qos_profile);
}

rmw_ret_t InitTopicEndpointInfoArray(
  const rmw_node_t * node, rcutils_allocator_t * allocator,
  const std::vector<rmw_mdds_cpp::TopicEndpointInfo> & infos,
  rmw_topic_endpoint_info_array_t * info_array)
{
  rmw_ret_t ret = CheckNode(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (allocator == nullptr || info_array == nullptr) {
    RMW_SET_ERROR_MSG("topic endpoint info argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rcutils_allocator_is_valid(allocator)) {
    RMW_SET_ERROR_MSG("topic endpoint info allocator is invalid");
    return RMW_RET_INVALID_ARGUMENT;
  }
  ret = rmw_topic_endpoint_info_array_check_zero(info_array);
  if (ret != RMW_RET_OK) {
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (infos.empty()) {
    return RMW_RET_OK;
  }
  ret = rmw_topic_endpoint_info_array_init_with_size(info_array, infos.size(), allocator);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  for (size_t i = 0; i < infos.size(); ++i) {
    ret = PopulateTopicEndpointInfo(infos[i], allocator, &info_array->info_array[i]);
    if (ret != RMW_RET_OK) {
      const rmw_ret_t fini_ret = rmw_topic_endpoint_info_array_fini(info_array, allocator);
      (void)fini_ret;
      return ret;
    }
  }
  return RMW_RET_OK;
}

void FillPublisherGid(const rmw_publisher_t * publisher, rmw_gid_t * gid)
{
  const auto * data = publisher == nullptr
                        ? nullptr
                        : static_cast<const rmw_mdds_cpp::PublisherData *>(publisher->data);
  rmw_mdds_cpp::FillPublisherGid(data, gid);
}

rmw_ret_t InitRtpsUserDataNetworkFlowEndpoints(
  const rmw_context_t * context, rcutils_allocator_t * allocator,
  rmw_network_flow_endpoint_array_t * network_flow_endpoint_array)
{
  rmw_ret_t ret = rmw_network_flow_endpoint_array_check_zero(network_flow_endpoint_array);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (context == nullptr || context->impl == nullptr) {
    return RMW_RET_OK;
  }
  if (context->impl->rtps_participant == nullptr) {
    if (!rmw_mdds_cpp::BrokerModeEnabled()) {
      return RMW_RET_OK;
    }
    ret = rmw_network_flow_endpoint_array_init(network_flow_endpoint_array, 1u, allocator);
    if (ret != RMW_RET_OK) {
      return ret;
    }
    rmw_network_flow_endpoint_t & endpoint =
      network_flow_endpoint_array->network_flow_endpoint[0];
    endpoint.transport_protocol = RMW_TRANSPORT_PROTOCOL_UNKNOWN;
    endpoint.internet_protocol = RMW_INTERNET_PROTOCOL_UNKNOWN;
    endpoint.transport_port = 0u;
    constexpr char kBrokerEndpoint[] = "rmw_mdds_broker";
    ret = rmw_network_flow_endpoint_set_internet_address(
      &endpoint, kBrokerEndpoint, std::strlen(kBrokerEndpoint));
    if (ret != RMW_RET_OK) {
      const rmw_ret_t fini_ret =
        rmw_network_flow_endpoint_array_fini(network_flow_endpoint_array);
      (void)fini_ret;
    }
    return ret;
  }

  const uint16_t user_data_port = context->impl->rtps_participant->local_user_unicast_port();
  if (user_data_port == 0u) {
    return RMW_RET_OK;
  }

  ret = rmw_network_flow_endpoint_array_init(network_flow_endpoint_array, 1u, allocator);
  if (ret != RMW_RET_OK) {
    return ret;
  }

  rmw_network_flow_endpoint_t & endpoint =
    network_flow_endpoint_array->network_flow_endpoint[0];
  endpoint.transport_protocol = RMW_TRANSPORT_PROTOCOL_UDP;
  endpoint.internet_protocol = RMW_INTERNET_PROTOCOL_IPV4;
  endpoint.transport_port = user_data_port;

  const std::string & address =
    context->impl->rtps_participant_config.advertised_address.empty()
    ? context->impl->rtps_participant_config.bind_address
    : context->impl->rtps_participant_config.advertised_address;
  ret = rmw_network_flow_endpoint_set_internet_address(
    &endpoint, address.c_str(), address.size());
  if (ret != RMW_RET_OK) {
    const rmw_ret_t fini_ret =
      rmw_network_flow_endpoint_array_fini(network_flow_endpoint_array);
    (void)fini_ret;
  }
  return ret;
}

void FillClientGid(const rmw_mdds_cpp::ClientData * client, rmw_gid_t * gid)
{
  if (gid == nullptr) {
    return;
  }
  *gid = {};
  gid->implementation_identifier = rmw_mdds_cpp_identifier;
  FillClientGuid(client, gid->data);
}

struct EventCallbackInvocation
{
  rmw_event_callback_t callback = nullptr;
  const void * user_data = nullptr;
  size_t event_count = 0;
};

rmw_time_point_value_t NowNanoseconds()
{
  rcutils_time_point_value_t now = 0;
  return rcutils_system_time_now(&now) == RCUTILS_RET_OK ? now : 0;
}

rmw_time_point_value_t ReceivedTimestampFor(rmw_time_point_value_t source_timestamp)
{
  const rmw_time_point_value_t received_timestamp = NowNanoseconds();
  return received_timestamp < source_timestamp ? source_timestamp : received_timestamp;
}

void InvokeEventCallback(const EventCallbackInvocation & invocation)
{
  if (invocation.callback != nullptr && invocation.event_count != 0) {
    invocation.callback(invocation.user_data, invocation.event_count);
  }
}

void EnqueueServiceRequest(
  rmw_mdds_cpp::ServiceData * service, rmw_mdds_cpp::ServiceRequestSample sample)
{
  if (service == nullptr) {
    return;
  }
  EventCallbackInvocation callback;
  {
    std::lock_guard<std::mutex> lock(service->mutex);
    sample.info.received_timestamp = ReceivedTimestampFor(sample.info.source_timestamp);
    service->requests.push_back(std::move(sample));
    callback = EventCallbackInvocation{
      service->request_callback, service->request_callback_user_data, 1};
  }
  InvokeEventCallback(callback);
}

void EnqueueClientResponse(
  rmw_mdds_cpp::ClientData * client, rmw_mdds_cpp::ServiceResponseSample sample)
{
  if (client == nullptr) {
    return;
  }
  EventCallbackInvocation callback;
  {
    std::lock_guard<std::mutex> lock(client->mutex);
    sample.info.received_timestamp = ReceivedTimestampFor(sample.info.source_timestamp);
    client->responses.push_back(std::move(sample));
    callback = EventCallbackInvocation{
      client->response_callback, client->response_callback_user_data, 1};
  }
  InvokeEventCallback(callback);
}

void ServiceRequestBridgeCallback(const rmw_mdds_cpp::BridgeSample * sample, void * user_data)
{
  auto * service = static_cast<rmw_mdds_cpp::ServiceData *>(user_data);
  if (service == nullptr) {
    return;
  }
  rmw_service_info_t info{};
  std::vector<uint8_t> payload;
  if (!DecodeServiceWirePayload(sample, &info, &payload)) {
    return;
  }
  rmw_mdds_cpp::ServiceRequestSample request_sample{info, std::move(payload)};
  EnqueueServiceRequest(service, std::move(request_sample));
}

void ClientResponseBridgeCallback(const rmw_mdds_cpp::BridgeSample * sample, void * user_data)
{
  auto * client = static_cast<rmw_mdds_cpp::ClientData *>(user_data);
  if (client == nullptr) {
    return;
  }
  rmw_service_info_t info{};
  std::vector<uint8_t> payload;
  if (!DecodeServiceWirePayload(sample, &info, &payload) ||
    !ClientMatchesGuid(client, info.request_id.writer_guid)) {
    return;
  }
  rmw_mdds_cpp::ServiceResponseSample response_sample{info, std::move(payload)};
  EnqueueClientResponse(client, std::move(response_sample));
}

void ServiceRequestBrokerCallback(const std::vector<uint8_t> & wire_payload, void * user_data)
{
  auto * service = static_cast<rmw_mdds_cpp::ServiceData *>(user_data);
  if (service == nullptr) {
    return;
  }
  rmw_service_info_t info{};
  std::vector<uint8_t> payload;
  if (!DecodeServiceWirePayloadBytes(
      wire_payload.data(), wire_payload.size(), &info, &payload)) {
    return;
  }
  rmw_mdds_cpp::ServiceRequestSample request_sample{info, std::move(payload)};
  EnqueueServiceRequest(service, std::move(request_sample));
}

void ClientResponseBrokerCallback(const std::vector<uint8_t> & wire_payload, void * user_data)
{
  auto * client = static_cast<rmw_mdds_cpp::ClientData *>(user_data);
  if (client == nullptr) {
    return;
  }
  rmw_service_info_t info{};
  std::vector<uint8_t> payload;
  if (
    !DecodeServiceWirePayloadBytes(wire_payload.data(), wire_payload.size(), &info, &payload) ||
    !ClientMatchesGuid(client, info.request_id.writer_guid)) {
    return;
  }
  rmw_mdds_cpp::ServiceResponseSample response_sample{info, std::move(payload)};
  EnqueueClientResponse(client, std::move(response_sample));
}

void DestroyServiceBridgeEndpoints(rmw_mdds_cpp::ServiceData * service)
{
  if (service == nullptr) {
    return;
  }
  auto & bridge_backend = rmw_mdds_cpp::BridgeBackend::Instance();
  if (service->bridge_request_subscription != nullptr) {
    bridge_backend.Unsubscribe(service->bridge_request_subscription);
    service->bridge_request_subscription = nullptr;
  }
  if (service->bridge_response_publisher != nullptr) {
    bridge_backend.DestroyPublisher(service->bridge_response_publisher);
    service->bridge_response_publisher = nullptr;
  }
}

void DestroyClientBridgeEndpoints(rmw_mdds_cpp::ClientData * client)
{
  if (client == nullptr) {
    return;
  }
  auto & bridge_backend = rmw_mdds_cpp::BridgeBackend::Instance();
  if (client->bridge_response_subscription != nullptr) {
    bridge_backend.Unsubscribe(client->bridge_response_subscription);
    client->bridge_response_subscription = nullptr;
  }
  if (client->bridge_request_publisher != nullptr) {
    bridge_backend.DestroyPublisher(client->bridge_request_publisher);
    client->bridge_request_publisher = nullptr;
  }
}

void DestroyServiceBrokerEndpoint(rmw_mdds_cpp::ServiceData * service)
{
  if (service == nullptr || service->broker_client == nullptr) {
    return;
  }
  rmw_mdds_cpp::DestroyBrokerClient(service->broker_client);
  service->broker_client = nullptr;
}

void DestroyClientBrokerEndpoint(rmw_mdds_cpp::ClientData * client)
{
  if (client == nullptr || client->broker_client == nullptr) {
    return;
  }
  rmw_mdds_cpp::DestroyBrokerClient(client->broker_client);
  client->broker_client = nullptr;
}

size_t SetServiceRequestCallback(
  rmw_mdds_cpp::ServiceData * service, rmw_event_callback_t callback, const void * user_data)
{
  if (service == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(service->mutex);
  service->request_callback = callback;
  service->request_callback_user_data = user_data;
  return callback == nullptr ? 0 : service->requests.size();
}

size_t SetClientResponseCallback(
  rmw_mdds_cpp::ClientData * client, rmw_event_callback_t callback, const void * user_data)
{
  if (client == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(client->mutex);
  client->response_callback = callback;
  client->response_callback_user_data = user_data;
  return callback == nullptr ? 0 : client->responses.size();
}
}  // namespace

namespace rmw_mdds_cpp
{
bool HasQueuedServiceRequest(ServiceData * service)
{
  if (service == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(service->mutex);
  return !service->requests.empty();
}

bool HasQueuedClientResponse(ClientData * client)
{
  if (client == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(client->mutex);
  return !client->responses.empty();
}
}  // namespace rmw_mdds_cpp

extern "C" {
rmw_ret_t rmw_init_publisher_allocation(
  const rosidl_message_type_support_t * type_support,
  const rosidl_runtime_c__Sequence__bound * message_bounds, rmw_publisher_allocation_t * allocation)
{
  (void)message_bounds;
  if (type_support == nullptr || allocation == nullptr) {
    RMW_SET_ERROR_MSG("publisher allocation argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_mdds_cpp::MessageAdapter adapter;
  if (!adapter.Init(type_support)) {
    RMW_SET_ERROR_MSG("publisher allocation type support is not supported by rmw_mdds_cpp");
    return RMW_RET_UNSUPPORTED;
  }
  allocation->implementation_identifier = rmw_mdds_cpp_identifier;
  allocation->data = nullptr;
  return RMW_RET_OK;
}

rmw_ret_t rmw_fini_publisher_allocation(rmw_publisher_allocation_t * allocation)
{
  if (allocation == nullptr) {
    RMW_SET_ERROR_MSG("publisher allocation is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(allocation->implementation_identifier)) {
    RMW_SET_ERROR_MSG("publisher allocation implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  allocation->implementation_identifier = nullptr;
  allocation->data = nullptr;
  return RMW_RET_OK;
}

// Loaned messages expose the ROS typed object required by the rmw API. When a
// bridge publisher is active, borrow also holds an MDDS bridge loan so publish
// can write the serialized payload into that transport loan instead of borrowing
// one only at publish time.
rmw_ret_t rmw_borrow_loaned_message(
  const rmw_publisher_t * publisher, const rosidl_message_type_support_t * type_support,
  void ** ros_message)
{
  rmw_ret_t ret = CheckPublisher(publisher);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (type_support == nullptr) {
    RMW_SET_ERROR_MSG("loaned message type support is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (ros_message == nullptr) {
    RMW_SET_ERROR_MSG("ros_message output pointer is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (*ros_message != nullptr) {
    RMW_SET_ERROR_MSG("ros_message is already allocated");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto * data = static_cast<rmw_mdds_cpp::PublisherData *>(publisher->data);
  if (data == nullptr || !data->adapter.IsValid() || !publisher->can_loan_messages) {
    RMW_SET_ERROR_MSG("publisher does not support loaned messages");
    return RMW_RET_UNSUPPORTED;
  }
  rmw_mdds_cpp::MessageAdapter requested_adapter;
  if (!requested_adapter.Init(type_support)) {
    RMW_SET_ERROR_MSG("loaned message type support is not supported by rmw_mdds_cpp");
    return RMW_RET_UNSUPPORTED;
  }
  if (requested_adapter.TypeName() != data->adapter.TypeName()) {
    RMW_SET_ERROR_MSG("loaned message type support does not match publisher type");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!data->adapter.SupportsRawLoanedMessage()) {
    RMW_SET_ERROR_MSG("loaned messages require a flat fixed-size scalar type");
    return RMW_RET_UNSUPPORTED;
  }
  rmw_mdds_cpp::BridgePublisherLoanRecord bridge_loan;
  if (BorrowPublisherBridgeLoan(data, &bridge_loan)) {
    void * message = data->adapter.ConstructMessageInPlace(
      bridge_loan.data, bridge_loan.capacity);
    if (message != nullptr) {
      bridge_loan.message_in_loan = true;
      bridge_loan.raw_message_in_loan = true;
      if (StorePublisherLoanRecord(data, message, bridge_loan)) {
        *ros_message = message;
        return RMW_RET_OK;
      }
      data->adapter.DestroyMessageInPlace(message);
    }
    (void)ReturnPublisherBridgeLoan(data, bridge_loan);
  }
  RMW_SET_ERROR_MSG("failed to borrow bridge-backed loaned message");
  return RMW_RET_ERROR;
}

rmw_ret_t rmw_return_loaned_message_from_publisher(
  const rmw_publisher_t * publisher, void * loaned_message)
{
  rmw_ret_t ret = CheckPublisher(publisher);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  auto * data = static_cast<rmw_mdds_cpp::PublisherData *>(publisher->data);
  if (data == nullptr || !data->adapter.IsValid() || !publisher->can_loan_messages) {
    RMW_SET_ERROR_MSG("publisher does not support loaned messages");
    return RMW_RET_UNSUPPORTED;
  }
  if (loaned_message == nullptr) {
    RMW_SET_ERROR_MSG("loaned message is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_mdds_cpp::BridgePublisherLoanRecord bridge_loan;
  if (!TakePublisherLoanRecord(data, loaned_message, &bridge_loan)) {
    RMW_SET_ERROR_MSG("loaned message was not borrowed from this publisher");
    return RMW_RET_INVALID_ARGUMENT;
  }
  DestroyPublisherLoanedRosMessage(data, loaned_message, bridge_loan);
  const bool returned_bridge_loan = ReturnPublisherBridgeLoan(data, bridge_loan);
  if (bridge_loan.loan != nullptr && !returned_bridge_loan) {
    RMW_SET_ERROR_MSG("MddsBridgeReturnLoanedSample failed");
    return RMW_RET_ERROR;
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_publish_loaned_message(
  const rmw_publisher_t * publisher, void * ros_message, rmw_publisher_allocation_t * allocation)
{
  (void)allocation;
  rmw_ret_t ret = CheckPublisher(publisher);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (ros_message == nullptr) {
    RMW_SET_ERROR_MSG("loaned message is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto * data = static_cast<rmw_mdds_cpp::PublisherData *>(publisher->data);
  if (data == nullptr || !data->adapter.IsValid() || !publisher->can_loan_messages) {
    RMW_SET_ERROR_MSG("publisher does not support loaned messages");
    return RMW_RET_UNSUPPORTED;
  }
  rmw_mdds_cpp::BridgePublisherLoanRecord bridge_loan;
  if (!TakePublisherLoanRecord(data, ros_message, &bridge_loan)) {
    RMW_SET_ERROR_MSG("loaned message was not borrowed from this publisher");
    return RMW_RET_INVALID_ARGUMENT;
  }
  const size_t payload_size = data->adapter.MessageSize();
  const bool can_publish_raw_loan =
    data->bridge_publisher != nullptr && bridge_loan.loan != nullptr &&
    bridge_loan.raw_message_in_loan && payload_size != 0 &&
    payload_size <= bridge_loan.capacity &&
    payload_size <= std::numeric_limits<uint32_t>::max();
  if (!can_publish_raw_loan) {
    DestroyPublisherLoanedRosMessage(data, ros_message, bridge_loan);
    (void)ReturnPublisherBridgeLoan(data, bridge_loan);
    RMW_SET_ERROR_MSG("loaned publish requires a raw fixed-size bridge loan");
    return RMW_RET_UNSUPPORTED;
  }
  DestroyPublisherLoanedRosMessage(data, ros_message, bridge_loan);
  auto & backend = rmw_mdds_cpp::BridgeBackend::Instance();
  if (backend.PublishLoaned(
      data->bridge_publisher, bridge_loan.loan, static_cast<uint32_t>(payload_size))) {
    if (data->actual_qos.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE) {
      std::lock_guard<std::mutex> lock(data->mutex);
      data->bridge_reliable_publication_unacknowledged = true;
    }
    return RMW_RET_OK;
  }
  if (!ReturnPublisherBridgeLoan(data, bridge_loan)) {
    RMW_SET_ERROR_MSG("MddsBridgeReturnLoanedSample failed");
    return RMW_RET_ERROR;
  }
  RMW_SET_ERROR_MSG("MddsBridgePublishLoaned failed");
  return RMW_RET_ERROR;
}

rmw_ret_t rmw_publisher_count_matched_subscriptions(
  const rmw_publisher_t * publisher, size_t * subscription_count)
{
  rmw_ret_t ret = CheckPublisher(publisher);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (subscription_count == nullptr) {
    RMW_SET_ERROR_MSG("subscription count is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto * data = static_cast<rmw_mdds_cpp::PublisherData *>(publisher->data);
  if (data == nullptr) {
    *subscription_count = 0;
  } else if (rmw_mdds_cpp::BrokerModeEnabled()) {
    *subscription_count = rmw_mdds_cpp::CountBrokerGraphSubscriptionsForPublisher(data);
  } else {
    *subscription_count = rmw_mdds_cpp::CountSubscriptionsForPublisher(*data);
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_publisher_get_actual_qos(const rmw_publisher_t * publisher, rmw_qos_profile_t * qos)
{
  rmw_ret_t ret = CheckPublisher(publisher);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (qos == nullptr) {
    RMW_SET_ERROR_MSG("publisher qos output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto * data = static_cast<rmw_mdds_cpp::PublisherData *>(publisher->data);
  *qos = data == nullptr ? rmw_qos_profile_default : data->actual_qos;
  return RMW_RET_OK;
}

rmw_ret_t rmw_publisher_event_init(
  rmw_event_t * rmw_event, const rmw_publisher_t * publisher, rmw_event_type_t event_type)
{
  rmw_ret_t ret = CheckPublisher(publisher);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (rmw_event == nullptr) {
    RMW_SET_ERROR_MSG("publisher event is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (event_type != RMW_EVENT_PUBLICATION_MATCHED && !IsEnforcedPublisherEvent(event_type) &&
    !IsNoOpPublisherEvent(event_type)) {
    return Unsupported("rmw_publisher_event_init");
  }
  if (publisher->data == nullptr) {
    RMW_SET_ERROR_MSG("publisher event data is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_event->implementation_identifier = rmw_mdds_cpp_identifier;
  rmw_event->data = publisher->data;
  rmw_event->event_type = event_type;
  return RMW_RET_OK;
}

rmw_ret_t rmw_get_serialized_message_size(
  const rosidl_message_type_support_t * type_support,
  const rosidl_runtime_c__Sequence__bound * message_bounds, size_t * size)
{
  (void)message_bounds;
  if (type_support == nullptr || size == nullptr) {
    RMW_SET_ERROR_MSG("serialized message size argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::CdrMaxSerializedMessageSize(type_support, size) &&
    !FixedSerializedMessageSize(type_support, size)) {
    RMW_SET_ERROR_MSG("serialized message size is not fixed or type support is unsupported");
    return RMW_RET_UNSUPPORTED;
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_publisher_assert_liveliness(const rmw_publisher_t * publisher)
{
  rmw_ret_t ret = CheckPublisher(publisher);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  rmw_mdds_cpp::NotePublisherLivelinessAsserted(
    static_cast<rmw_mdds_cpp::PublisherData *>(publisher->data),
    rmw_mdds_cpp::MddsNowNanoseconds());
  return RMW_RET_OK;
}

rmw_ret_t rmw_publisher_wait_for_all_acked(
  const rmw_publisher_t * publisher, rmw_time_t wait_timeout)
{
  rmw_ret_t ret = CheckPublisher(publisher);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  auto * data = static_cast<rmw_mdds_cpp::PublisherData *>(publisher->data);
  if (data == nullptr || data->actual_qos.reliability != RMW_QOS_POLICY_RELIABILITY_RELIABLE) {
    return RMW_RET_OK;
  }
  if (data->bridge_publisher != nullptr) {
    return WaitForBridgeReliableAcks(data, wait_timeout);
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_serialize(
  const void * ros_message, const rosidl_message_type_support_t * type_support,
  rmw_serialized_message_t * serialized_message)
{
  if (ros_message == nullptr || type_support == nullptr || serialized_message == nullptr) {
    RMW_SET_ERROR_MSG("serialize argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_mdds_cpp::MessageAdapter adapter;
  if (!adapter.Init(type_support)) {
    RMW_SET_ERROR_MSG("message type support is not supported by rmw_mdds_cpp serialization");
    return RMW_RET_UNSUPPORTED;
  }
  std::vector<uint8_t> payload;
  if (!adapter.Encode(ros_message, &payload)) {
    RMW_SET_ERROR_MSG("failed to serialize message");
    return RMW_RET_ERROR;
  }
  if (payload.size() > serialized_message->buffer_capacity) {
    if (rmw_serialized_message_resize(serialized_message, payload.size()) != RCUTILS_RET_OK) {
      RMW_SET_ERROR_MSG("failed to resize serialized message buffer");
      return RMW_RET_ERROR;
    }
  }
  if (!payload.empty()) {
    if (serialized_message->buffer == nullptr) {
      RMW_SET_ERROR_MSG("serialized message buffer is null");
      return RMW_RET_INVALID_ARGUMENT;
    }
    std::memcpy(serialized_message->buffer, payload.data(), payload.size());
  }
  serialized_message->buffer_length = payload.size();
  return RMW_RET_OK;
}

rmw_ret_t rmw_deserialize(
  const rmw_serialized_message_t * serialized_message,
  const rosidl_message_type_support_t * type_support, void * ros_message)
{
  if (serialized_message == nullptr || type_support == nullptr || ros_message == nullptr) {
    RMW_SET_ERROR_MSG("deserialize argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (serialized_message->buffer == nullptr && serialized_message->buffer_length != 0) {
    RMW_SET_ERROR_MSG("serialized message buffer is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_mdds_cpp::MessageAdapter adapter;
  if (!adapter.Init(type_support)) {
    RMW_SET_ERROR_MSG("message type support is not supported by rmw_mdds_cpp serialization");
    return RMW_RET_UNSUPPORTED;
  }
  if (!adapter.Decode(serialized_message->buffer, serialized_message->buffer_length, ros_message)) {
    RMW_SET_ERROR_MSG("failed to deserialize message");
    return RMW_RET_ERROR;
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_init_subscription_allocation(
  const rosidl_message_type_support_t * type_support,
  const rosidl_runtime_c__Sequence__bound * message_bounds,
  rmw_subscription_allocation_t * allocation)
{
  (void)message_bounds;
  if (type_support == nullptr || allocation == nullptr) {
    RMW_SET_ERROR_MSG("subscription allocation argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_mdds_cpp::MessageAdapter adapter;
  if (!adapter.Init(type_support)) {
    RMW_SET_ERROR_MSG("subscription allocation type support is not supported by rmw_mdds_cpp");
    return RMW_RET_UNSUPPORTED;
  }
  allocation->implementation_identifier = rmw_mdds_cpp_identifier;
  allocation->data = nullptr;
  return RMW_RET_OK;
}

rmw_ret_t rmw_fini_subscription_allocation(rmw_subscription_allocation_t * allocation)
{
  if (allocation == nullptr) {
    RMW_SET_ERROR_MSG("subscription allocation is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(allocation->implementation_identifier)) {
    RMW_SET_ERROR_MSG(
      "subscription allocation implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  allocation->implementation_identifier = nullptr;
  allocation->data = nullptr;
  return RMW_RET_OK;
}

rmw_ret_t rmw_subscription_count_matched_publishers(
  const rmw_subscription_t * subscription, size_t * publisher_count)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (publisher_count == nullptr) {
    RMW_SET_ERROR_MSG("publisher count is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  const auto * data = static_cast<const rmw_mdds_cpp::SubscriptionData *>(subscription->data);
  if (data == nullptr) {
    *publisher_count = 0;
  } else if (rmw_mdds_cpp::BrokerModeEnabled()) {
    *publisher_count = rmw_mdds_cpp::CountBrokerGraphPublishersForSubscription(data);
  } else {
    *publisher_count = rmw_mdds_cpp::CountPublishersForSubscription(*data);
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_subscription_get_actual_qos(
  const rmw_subscription_t * subscription, rmw_qos_profile_t * qos)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (qos == nullptr) {
    RMW_SET_ERROR_MSG("subscription qos output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  const auto * data = static_cast<const rmw_mdds_cpp::SubscriptionData *>(subscription->data);
  *qos = data == nullptr ? rmw_qos_profile_default : data->actual_qos;
  return RMW_RET_OK;
}

rmw_ret_t rmw_subscription_event_init(
  rmw_event_t * rmw_event, const rmw_subscription_t * subscription, rmw_event_type_t event_type)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (rmw_event == nullptr) {
    RMW_SET_ERROR_MSG("subscription event is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (event_type != RMW_EVENT_SUBSCRIPTION_MATCHED && !IsEnforcedSubscriptionEvent(event_type) &&
    !IsNoOpSubscriptionEvent(event_type)) {
    return Unsupported("rmw_subscription_event_init");
  }
  if (subscription->data == nullptr) {
    RMW_SET_ERROR_MSG("subscription event data is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_event->implementation_identifier = rmw_mdds_cpp_identifier;
  rmw_event->data = subscription->data;
  rmw_event->event_type = event_type;
  return RMW_RET_OK;
}

rmw_ret_t rmw_subscription_set_content_filter(
  rmw_subscription_t * subscription, const rmw_subscription_content_filter_options_t * options)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  auto * data = static_cast<rmw_mdds_cpp::SubscriptionData *>(subscription->data);
  ret = rmw_mdds_cpp::SetSubscriptionContentFilter(data, options);
  if (ret == RMW_RET_OK) {
    subscription->is_cft_enabled =
      options != nullptr && options->filter_expression != nullptr && options->filter_expression[0] != '\0';
  }
  return ret;
}

rmw_ret_t rmw_subscription_get_content_filter(
  const rmw_subscription_t * subscription, rcutils_allocator_t * allocator,
  rmw_subscription_content_filter_options_t * options)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  auto * data = static_cast<rmw_mdds_cpp::SubscriptionData *>(subscription->data);
  return rmw_mdds_cpp::GetSubscriptionContentFilter(data, allocator, options);
}

rmw_ret_t TryTakeBridgeLoanedMessageCopy(
  rmw_mdds_cpp::SubscriptionData * data, void * message, bool * taken,
  rmw_message_info_t * message_info, bool * attempted);

rmw_ret_t rmw_take_sequence(
  const rmw_subscription_t * subscription, size_t count, rmw_message_sequence_t * message_sequence,
  rmw_message_info_sequence_t * message_info_sequence, size_t * taken,
  rmw_subscription_allocation_t * allocation)
{
  (void)allocation;
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (message_sequence == nullptr || message_info_sequence == nullptr || taken == nullptr) {
    RMW_SET_ERROR_MSG("take sequence argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (count == 0) {
    RMW_SET_ERROR_MSG("take sequence count is zero");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (message_sequence->capacity < count || message_info_sequence->capacity < count) {
    RMW_SET_ERROR_MSG("take sequence capacity is smaller than requested count");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (message_sequence->data == nullptr || message_info_sequence->data == nullptr) {
    RMW_SET_ERROR_MSG("take sequence data is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  for (size_t i = 0; i < count; ++i) {
    if (message_sequence->data[i] == nullptr) {
      RMW_SET_ERROR_MSG("take sequence message slot is null");
      return RMW_RET_INVALID_ARGUMENT;
    }
  }

  *taken = 0;
  message_sequence->size = 0;
  message_info_sequence->size = 0;
  auto * data = static_cast<rmw_mdds_cpp::SubscriptionData *>(subscription->data);
  for (size_t i = 0; i < count; ++i) {
    rmw_mdds_cpp::QueuedSample sample;
    if (TakeNextLiveQueuedSample(data, &sample)) {
      const bool decoded = sample.from_bridge ?
        data->adapter.DecodeMdds(sample.payload.data(), sample.payload.size(), message_sequence->data[i]) :
        data->adapter.Decode(sample.payload.data(), sample.payload.size(), message_sequence->data[i]);
      if (!decoded) {
        RMW_SET_ERROR_MSG("failed to decode message sequence sample");
        return RMW_RET_ERROR;
      }
      message_info_sequence->data[i] = sample.info;
    } else {
      bool bridge_taken = false;
      bool attempted_bridge_loaned = false;
      ret = TryTakeBridgeLoanedMessageCopy(
        data, message_sequence->data[i], &bridge_taken, &message_info_sequence->data[i],
        &attempted_bridge_loaned);
      if (ret != RMW_RET_OK) {
        return ret;
      }
      if (!bridge_taken) {
        break;
      }
    }
    ++(*taken);
  }
  message_sequence->size = *taken;
  message_info_sequence->size = *taken;
  return RMW_RET_OK;
}

rmw_message_info_t MakeBridgeLoanedMessageInfo(
  rmw_mdds_cpp::SubscriptionData * data, const rmw_mdds_cpp::BridgeLoanedMessage & bridge_message)
{
  rmw_message_info_t info = rmw_get_zero_initialized_message_info();
  info.publication_sequence_number = bridge_message.sequenceNumber;
  info.publisher_gid.implementation_identifier = rmw_mdds_cpp_identifier;
  std::memcpy(
    info.publisher_gid.data, bridge_message.senderGuid,
    std::min(sizeof(bridge_message.senderGuid), sizeof(info.publisher_gid.data)));
  info.source_timestamp = static_cast<rmw_time_point_value_t>(bridge_message.timestamp);
  info.received_timestamp = ReceivedTimestampFor(info.source_timestamp);
  info.from_intra_process = false;

  std::lock_guard<std::mutex> lock(data->mutex);
  info.reception_sequence_number = data->next_reception_sequence_number++;
  data->requested_deadline_last_active_ns = static_cast<int64_t>(info.received_timestamp);
  return info;
}

rmw_mdds_cpp::BridgeLoanedMessage ToBridgeLoanedMessage(
  const rmw_mdds_cpp::BridgeLoanedMessageRecord & record)
{
  rmw_mdds_cpp::BridgeLoanedMessage bridge_message{};
  bridge_message.data = record.data;
  bridge_message.len = record.len;
  bridge_message.timestamp = record.timestamp;
  bridge_message.sequenceNumber = record.sequenceNumber;
  std::memcpy(
    bridge_message.senderGuid, record.senderGuid.data(),
    std::min(sizeof(bridge_message.senderGuid), record.senderGuid.size()));
  bridge_message.loanHandle = record.loanHandle;
  bridge_message.loanKind = record.loanKind;
  return bridge_message;
}

rmw_ret_t TryTakeBridgeLoanedMessageCopy(
  rmw_mdds_cpp::SubscriptionData * data, void * message, bool * taken,
  rmw_message_info_t * message_info, bool * attempted)
{
  *taken = false;
  *attempted = false;
  if (
    data == nullptr || data->bridge_subscription == nullptr || rmw_mdds_cpp::HasQueuedSample(data)) {
    return RMW_RET_OK;
  }

  rmw_mdds_cpp::BridgeLoanedMessage bridge_message{};
  while (rmw_mdds_cpp::BridgeBackend::Instance().SubscriberTakeLoaned(
      data->bridge_subscription, &bridge_message)) {
    *attempted = true;
    if (bridge_message.data == nullptr && bridge_message.len != 0u) {
      (void)rmw_mdds_cpp::BridgeBackend::Instance().SubscriberReturnLoaned(
        data->bridge_subscription, &bridge_message);
      RMW_SET_ERROR_MSG("bridge loaned message payload is null");
      return RMW_RET_ERROR;
    }
    std::vector<uint8_t> payload;
    if (bridge_message.data != nullptr && bridge_message.len != 0u) {
      const auto * begin = static_cast<const uint8_t *>(bridge_message.data);
      payload.assign(begin, begin + bridge_message.len);
    }
    bool matches_filter = true;
    {
      std::lock_guard<std::mutex> lock(data->mutex);
      matches_filter = rmw_mdds_cpp::PayloadMatchesContentFilter(*data, payload, true);
    }
    if (!matches_filter) {
      (void)rmw_mdds_cpp::BridgeBackend::Instance().SubscriberReturnLoaned(
        data->bridge_subscription, &bridge_message);
      bridge_message = {};
      continue;
    }
    if (!data->adapter.DecodeMdds(
        static_cast<const uint8_t *>(bridge_message.data), bridge_message.len, message)) {
      (void)rmw_mdds_cpp::BridgeBackend::Instance().SubscriberReturnLoaned(
        data->bridge_subscription, &bridge_message);
      RMW_SET_ERROR_MSG("failed to decode bridge loaned message");
      return RMW_RET_ERROR;
    }
    const rmw_message_info_t info = MakeBridgeLoanedMessageInfo(data, bridge_message);
    if (!rmw_mdds_cpp::BridgeBackend::Instance().SubscriberReturnLoaned(
        data->bridge_subscription, &bridge_message)) {
      RMW_SET_ERROR_MSG("MddsBridgeSubscriberReturnLoaned failed");
      return RMW_RET_ERROR;
    }
    if (message_info != nullptr) {
      *message_info = info;
    }
    *taken = true;
    return RMW_RET_OK;
  }
  return RMW_RET_OK;
}

bool BridgeLoanedMessageMatchesContentFilter(
  rmw_mdds_cpp::SubscriptionData * data,
  const rmw_mdds_cpp::BridgeLoanedMessage & bridge_message)
{
  if (data == nullptr) {
    return true;
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  if (!data->content_filter_enabled && !data->numeric_filter_enabled) {
    return true;
  }

  std::vector<uint8_t> payload;
  if (bridge_message.data != nullptr && bridge_message.len != 0u) {
    const auto * begin = static_cast<const uint8_t *>(bridge_message.data);
    payload.assign(begin, begin + bridge_message.len);
  }
  return rmw_mdds_cpp::PayloadMatchesContentFilter(*data, payload, true);
}

rmw_ret_t TryTakeBridgeLoanedMessage(
  rmw_mdds_cpp::SubscriptionData * data, void ** message, bool * taken,
  rmw_message_info_t * message_info, bool * attempted)
{
  if (message == nullptr) {
    RMW_SET_ERROR_MSG("bridge loaned message output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  *message = nullptr;
  *taken = false;
  *attempted = false;
  if (
    data == nullptr || data->bridge_subscription == nullptr || rmw_mdds_cpp::HasQueuedSample(data)) {
    return RMW_RET_OK;
  }
  const size_t message_size = data->adapter.MessageSize();
  if (!data->adapter.SupportsRawLoanedMessage() || message_size == 0) {
    RMW_SET_ERROR_MSG("loaned take requires a raw fixed-size bridge loan");
    return RMW_RET_UNSUPPORTED;
  }
  if (data->content_filter_enabled || data->numeric_filter_enabled) {
    RMW_SET_ERROR_MSG("loaned take with content filters is not supported for raw bridge loans");
    return RMW_RET_UNSUPPORTED;
  }

  rmw_mdds_cpp::BridgeLoanedMessage bridge_message{};
  auto & backend = rmw_mdds_cpp::BridgeBackend::Instance();
  while (backend.SubscriberTakeLoaned(data->bridge_subscription, &bridge_message)) {
    *attempted = true;
    if (bridge_message.data == nullptr) {
      (void)backend.SubscriberReturnLoaned(data->bridge_subscription, &bridge_message);
      RMW_SET_ERROR_MSG("bridge loaned message payload is null");
      return RMW_RET_ERROR;
    }
    if (bridge_message.len < message_size) {
      (void)backend.SubscriberReturnLoaned(data->bridge_subscription, &bridge_message);
      RMW_SET_ERROR_MSG("bridge loaned message is smaller than the ROS message type");
      return RMW_RET_ERROR;
    }

    rmw_mdds_cpp::BridgeLoanedMessageRecord record;
    record.data = bridge_message.data;
    record.len = bridge_message.len;
    record.timestamp = bridge_message.timestamp;
    record.sequenceNumber = bridge_message.sequenceNumber;
    std::memcpy(
    record.senderGuid.data(), bridge_message.senderGuid,
      std::min(record.senderGuid.size(), sizeof(bridge_message.senderGuid)));
    record.loanHandle = bridge_message.loanHandle;
    record.loanKind = bridge_message.loanKind;
    record.messageInBridgeStorage = false;
    record.rawMessageInBridgeLoan = true;

    const rmw_message_info_t info = MakeBridgeLoanedMessageInfo(data, bridge_message);
    void * raw_message = const_cast<void *>(bridge_message.data);
    {
      std::lock_guard<std::mutex> lock(data->mutex);
      data->bridge_loaned_messages[raw_message] = record;
    }
    if (message_info != nullptr) {
      *message_info = info;
    }
    *message = raw_message;
    *taken = true;
    return RMW_RET_OK;
  }
  return RMW_RET_OK;
}

rmw_ret_t TryTakeQueuedLoanedMessage(
  rmw_mdds_cpp::SubscriptionData * data, void * message, bool * taken,
  rmw_message_info_t * message_info)
{
  *taken = false;
  rmw_mdds_cpp::QueuedSample sample;
  if (!TakeNextLiveQueuedSample(data, &sample)) {
    return RMW_RET_OK;
  }
  const bool decoded = sample.from_bridge ?
    data->adapter.DecodeMdds(sample.payload.data(), sample.payload.size(), message) :
    data->adapter.Decode(sample.payload.data(), sample.payload.size(), message);
  if (!decoded) {
    RMW_SET_ERROR_MSG("failed to decode queued loaned message");
    return RMW_RET_ERROR;
  }
  if (message_info != nullptr) {
    *message_info = sample.info;
  }
  *taken = true;
  return RMW_RET_OK;
}

// Loaned take prefers a bridge-owned typed storage block tied to the MDDS loan.
// Older bridge libraries do not expose that optional symbol, so fallback stays
// the heap-backed queue/take path.
static rmw_ret_t TakeLoanedCommon(
  const rmw_subscription_t * subscription, void ** loaned_message, bool * taken,
  rmw_message_info_t * message_info, rmw_subscription_allocation_t * allocation)
{
  if (loaned_message == nullptr || taken == nullptr) {
    RMW_SET_ERROR_MSG("take_loaned argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  *taken = false;
  if (*loaned_message != nullptr) {
    RMW_SET_ERROR_MSG("loaned message is already allocated");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto * data = static_cast<rmw_mdds_cpp::SubscriptionData *>(subscription->data);
  if (data == nullptr || !data->adapter.IsValid() || !subscription->can_loan_messages) {
    RMW_SET_ERROR_MSG("subscription does not support loaned messages");
    return RMW_RET_UNSUPPORTED;
  }
  bool attempted_bridge_loaned = false;
  void * message = nullptr;
  rmw_ret_t ret = TryTakeBridgeLoanedMessage(
    data, &message, taken, message_info, &attempted_bridge_loaned);
  if (attempted_bridge_loaned) {
    if (ret != RMW_RET_OK || !*taken) {
      if (message != nullptr) {
        data->adapter.DestroyMessageInPlace(message);
      }
      *taken = false;
      return ret;
    }
    *loaned_message = message;
    return RMW_RET_OK;
  }
  (void)message_info;
  (void)allocation;
  *taken = false;
  return RMW_RET_OK;
}

rmw_ret_t rmw_take_loaned_message(
  const rmw_subscription_t * subscription, void ** loaned_message, bool * taken,
  rmw_subscription_allocation_t * allocation)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  return TakeLoanedCommon(subscription, loaned_message, taken, nullptr, allocation);
}

rmw_ret_t rmw_take_loaned_message_with_info(
  const rmw_subscription_t * subscription, void ** loaned_message, bool * taken,
  rmw_message_info_t * message_info, rmw_subscription_allocation_t * allocation)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (message_info == nullptr) {
    RMW_SET_ERROR_MSG("message info is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return TakeLoanedCommon(subscription, loaned_message, taken, message_info, allocation);
}

rmw_ret_t rmw_return_loaned_message_from_subscription(
  const rmw_subscription_t * subscription, void * loaned_message)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  auto * data = static_cast<rmw_mdds_cpp::SubscriptionData *>(subscription->data);
  if (data == nullptr || !data->adapter.IsValid() || !subscription->can_loan_messages) {
    RMW_SET_ERROR_MSG("subscription does not support loaned messages");
    return RMW_RET_UNSUPPORTED;
  }
  if (loaned_message == nullptr) {
    RMW_SET_ERROR_MSG("loaned message is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_mdds_cpp::BridgeLoanedMessageRecord bridge_record;
  bool has_bridge_loan = false;
  {
    std::lock_guard<std::mutex> lock(data->mutex);
    const auto it = data->bridge_loaned_messages.find(loaned_message);
    if (it != data->bridge_loaned_messages.end()) {
      bridge_record = it->second;
      data->bridge_loaned_messages.erase(it);
      has_bridge_loan = true;
    }
  }

  rmw_ret_t bridge_ret = RMW_RET_OK;
  const bool message_in_bridge_storage = has_bridge_loan && bridge_record.messageInBridgeStorage;
  const bool raw_message_in_bridge_loan = has_bridge_loan && bridge_record.rawMessageInBridgeLoan;
  if (message_in_bridge_storage && !raw_message_in_bridge_loan) {
    data->adapter.DestroyMessageInPlace(loaned_message);
  }
  if (has_bridge_loan) {
    auto bridge_message = ToBridgeLoanedMessage(bridge_record);
    if (!rmw_mdds_cpp::BridgeBackend::Instance().SubscriberReturnLoaned(
        data->bridge_subscription, &bridge_message)) {
      RMW_SET_ERROR_MSG("MddsBridgeSubscriberReturnLoaned failed");
      bridge_ret = RMW_RET_ERROR;
    }
  }
  if (!message_in_bridge_storage && !raw_message_in_bridge_loan) {
    data->adapter.DestroyMessage(loaned_message);
  }
  return bridge_ret;
}

rmw_client_t * rmw_create_client(
  const rmw_node_t * node, const rosidl_service_type_support_t * type_support,
  const char * service_name, const rmw_qos_profile_t * qos_profile)
{
  if (CheckNode(node) != RMW_RET_OK) {
    return nullptr;
  }
  if (service_name == nullptr) {
    RMW_SET_ERROR_MSG("client service name is null");
    return nullptr;
  }
  if (ValidateFullyQualifiedName(service_name, "service name") != RMW_RET_OK) {
    return nullptr;
  }
  if (!ValidateQosProfile(qos_profile, "client")) {
    return nullptr;
  }
  auto * data = new (std::nothrow) rmw_mdds_cpp::ClientData();
  if (data == nullptr) {
    return nullptr;
  }
  const ServiceTypeInfo type_info = ResolveServiceTypeInfo(type_support);
  if (!ServiceTypeInfoIsValid(type_info)) {
    rmw_reset_error();
    delete data;
    RMW_SET_ERROR_MSG("service type support is not supported by rmw_mdds_cpp client");
    return nullptr;
  }
  data->context = node->context;
  data->service_name = service_name;
  data->type_name = type_info.type_name;
  data->node_name = node->name == nullptr ? "" : node->name;
  data->node_namespace = node->namespace_ == nullptr ? "" : node->namespace_;
  auto * node_data = static_cast<rmw_mdds_cpp::NodeData *>(node->data);
  data->node_enclave = node_data == nullptr ? "" : node_data->enclave;
  data->request_type = type_info.request_type;
  data->response_type = type_info.response_type;
  data->request_size = type_info.request_type.size;
  data->response_size = type_info.response_type.size;
  data->next_sequence_id = 1;
  data->actual_qos = ResolveActualQosProfile(*qos_profile, rmw_qos_profile_services_default);
  const bool allow_local_only =
    AllowsLocalOnlyInternalService(service_name, node->name, node->namespace_);
  auto & bridge_backend = rmw_mdds_cpp::BridgeBackend::Instance();
  if (rmw_mdds_cpp::BrokerModeEnabled()) {
    std::string error;
    data->broker_client = rmw_mdds_cpp::CreateClientBrokerClient(
      data, ClientResponseBrokerCallback, data, &error);
    if (data->broker_client == nullptr) {
      if (!allow_local_only) {
        delete data;
        RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
          "MDDS broker client registration failed: %s", error.c_str());
        return nullptr;
      }
      rmw_reset_error();
    }
  } else {
    const std::string request_topic = MakeServiceBridgeTopic("rq/", data->service_name);
    const std::string response_topic = MakeServiceBridgeTopic("rr/", data->service_name);
    const std::string request_type = MakeServiceBridgeType(data->type_name, "_Request");
    const std::string response_type = MakeServiceBridgeType(data->type_name, "_Response");
    if (
      !request_topic.empty() && !response_topic.empty() && !request_type.empty() &&
      !response_type.empty()) {
      data->bridge_request_publisher = bridge_backend.CreatePublisher(
        request_topic.c_str(), request_type.c_str(), &data->actual_qos);
      data->bridge_response_subscription = bridge_backend.Subscribe(
        response_topic.c_str(), response_type.c_str(), &data->actual_qos,
        ClientResponseBridgeCallback, data);
    }
    if (
      bridge_backend.Required() &&
      (data->bridge_request_publisher == nullptr ||
       data->bridge_response_subscription == nullptr)) {
      DestroyClientBridgeEndpoints(data);
      if (!allow_local_only) {
        delete data;
        RMW_SET_ERROR_MSG("explicit MDDS bridge library is configured but client creation failed");
        return nullptr;
      }
      rmw_reset_error();
    }
  }
  rmw_client_t * client = rmw_client_allocate();
  if (client == nullptr) {
    DestroyClientBridgeEndpoints(data);
    DestroyClientBrokerEndpoint(data);
    delete data;
    return nullptr;
  }
  rcutils_allocator_t allocator = node->context->options.allocator;
  client->implementation_identifier = rmw_mdds_cpp_identifier;
  client->data = data;
  client->service_name = rcutils_strdup(service_name, allocator);
  if (client->service_name == nullptr) {
    DestroyClientBridgeEndpoints(data);
    DestroyClientBrokerEndpoint(data);
    delete data;
    rmw_client_free(client);
    return nullptr;
  }
  RegisterClient(data);
  return client;
}

rmw_ret_t rmw_destroy_client(rmw_node_t * node, rmw_client_t * client)
{
  rmw_ret_t ret = CheckNode(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  ret = CheckClient(client);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  rcutils_allocator_t allocator = node->context->options.allocator;
  auto * data = static_cast<rmw_mdds_cpp::ClientData *>(client->data);
  UnregisterClient(data);
  DestroyClientBridgeEndpoints(data);
  DestroyClientBrokerEndpoint(data);
  allocator.deallocate(const_cast<char *>(client->service_name), allocator.state);
  delete data;
  rmw_client_free(client);
  return RMW_RET_OK;
}

rmw_ret_t rmw_send_request(
  const rmw_client_t * client, const void * ros_request, int64_t * sequence_id)
{
  rmw_ret_t ret = CheckClient(client);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (ros_request == nullptr || sequence_id == nullptr) {
    RMW_SET_ERROR_MSG("send request argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto * data = static_cast<rmw_mdds_cpp::ClientData *>(client->data);
  if (data == nullptr || data->request_size == 0) {
    RMW_SET_ERROR_MSG("client request type support is invalid");
    return RMW_RET_ERROR;
  }

  rmw_service_info_t info{};
  {
    std::lock_guard<std::mutex> lock(data->mutex);
    *sequence_id = data->next_sequence_id++;
  }
  FillClientGuid(data, info.request_id.writer_guid);
  info.request_id.sequence_number = *sequence_id;
  info.source_timestamp = NowNanoseconds();

  std::vector<uint8_t> payload;
  if (!CopyFromRosMessage(ros_request, data->request_type, &payload)) {
    RMW_SET_ERROR_MSG("failed to copy service request");
    return RMW_RET_ERROR;
  }

  if (data->broker_client != nullptr) {
    std::vector<uint8_t> wire_payload;
    if (!EncodeServiceWirePayload(info.request_id, info.source_timestamp, payload, &wire_payload)) {
      RMW_SET_ERROR_MSG("failed to encode broker service request");
      return RMW_RET_ERROR;
    }
    std::string error;
    if (!rmw_mdds_cpp::BrokerClientPublish(
        data->broker_client, wire_payload, static_cast<uint64_t>(*sequence_id), &error)) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "failed to publish broker service request: %s", error.c_str());
      return RMW_RET_ERROR;
    }
    return RMW_RET_OK;
  }

  if (data->bridge_request_publisher != nullptr) {
    std::vector<uint8_t> wire_payload;
    if (!EncodeServiceWirePayload(info.request_id, info.source_timestamp, payload, &wire_payload)) {
      RMW_SET_ERROR_MSG("failed to encode bridge service request");
      return RMW_RET_ERROR;
    }
    if (wire_payload.size() > std::numeric_limits<uint32_t>::max()) {
      RMW_SET_ERROR_MSG("bridge service request is too large");
      return RMW_RET_ERROR;
    }
    if (
      rmw_mdds_cpp::BridgeBackend::Instance().Publish(
        data->bridge_request_publisher, wire_payload.data(),
        static_cast<uint32_t>(wire_payload.size())) != 0) {
      RMW_SET_ERROR_MSG("failed to publish bridge service request");
      return RMW_RET_ERROR;
    }
    return RMW_RET_OK;
  }

  for (auto * service : GetMatchingServices(data)) {
    rmw_mdds_cpp::ServiceRequestSample sample{info, payload};
    EnqueueServiceRequest(service, std::move(sample));
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_take_response(
  const rmw_client_t * client, rmw_service_info_t * request_header, void * ros_response,
  bool * taken)
{
  rmw_ret_t ret = CheckClient(client);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (request_header == nullptr || ros_response == nullptr || taken == nullptr) {
    RMW_SET_ERROR_MSG("take response argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto * data = static_cast<rmw_mdds_cpp::ClientData *>(client->data);
  if (data == nullptr || data->response_size == 0) {
    RMW_SET_ERROR_MSG("client response type support is invalid");
    return RMW_RET_ERROR;
  }

  std::lock_guard<std::mutex> lock(data->mutex);
  if (data->responses.empty()) {
    *taken = false;
    return RMW_RET_OK;
  }
  auto sample = std::move(data->responses.front());
  data->responses.pop_front();
  if (!CopyToRosMessage(sample.payload, data->response_type, ros_response)) {
    RMW_SET_ERROR_MSG("failed to copy service response");
    return RMW_RET_ERROR;
  }
  *request_header = sample.info;
  *taken = true;
  return RMW_RET_OK;
}

rmw_ret_t rmw_client_request_publisher_get_actual_qos(
  const rmw_client_t * client, rmw_qos_profile_t * qos)
{
  rmw_ret_t ret = CheckClient(client);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (qos == nullptr) {
    RMW_SET_ERROR_MSG("client qos output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  const auto * data = static_cast<const rmw_mdds_cpp::ClientData *>(client->data);
  *qos = data == nullptr ? rmw_qos_profile_services_default : data->actual_qos;
  return RMW_RET_OK;
}

rmw_ret_t rmw_client_response_subscription_get_actual_qos(
  const rmw_client_t * client, rmw_qos_profile_t * qos)
{
  return rmw_client_request_publisher_get_actual_qos(client, qos);
}

rmw_service_t * rmw_create_service(
  const rmw_node_t * node, const rosidl_service_type_support_t * type_support,
  const char * service_name, const rmw_qos_profile_t * qos_profile)
{
  if (CheckNode(node) != RMW_RET_OK) {
    return nullptr;
  }
  if (service_name == nullptr) {
    RMW_SET_ERROR_MSG("service name is null");
    return nullptr;
  }
  if (ValidateFullyQualifiedName(service_name, "service name") != RMW_RET_OK) {
    return nullptr;
  }
  if (!ValidateQosProfile(qos_profile, "service")) {
    return nullptr;
  }
  auto * data = new (std::nothrow) rmw_mdds_cpp::ServiceData();
  if (data == nullptr) {
    return nullptr;
  }
  const ServiceTypeInfo type_info = ResolveServiceTypeInfo(type_support);
  if (!ServiceTypeInfoIsValid(type_info)) {
    rmw_reset_error();
    delete data;
    RMW_SET_ERROR_MSG("service type support is not supported by rmw_mdds_cpp service");
    return nullptr;
  }
  data->context = node->context;
  data->service_name = service_name;
  data->type_name = type_info.type_name;
  data->node_name = node->name == nullptr ? "" : node->name;
  data->node_namespace = node->namespace_ == nullptr ? "" : node->namespace_;
  auto * node_data = static_cast<rmw_mdds_cpp::NodeData *>(node->data);
  data->node_enclave = node_data == nullptr ? "" : node_data->enclave;
  data->request_type = type_info.request_type;
  data->response_type = type_info.response_type;
  data->request_size = type_info.request_type.size;
  data->response_size = type_info.response_type.size;
  data->actual_qos = ResolveActualQosProfile(*qos_profile, rmw_qos_profile_services_default);
  const bool allow_local_only =
    AllowsLocalOnlyInternalService(service_name, node->name, node->namespace_);
  auto & bridge_backend = rmw_mdds_cpp::BridgeBackend::Instance();
  if (rmw_mdds_cpp::BrokerModeEnabled()) {
    std::string error;
    data->broker_client = rmw_mdds_cpp::CreateServiceBrokerClient(
      data, ServiceRequestBrokerCallback, data, &error);
    if (data->broker_client == nullptr) {
      if (!allow_local_only) {
        delete data;
        RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
          "MDDS broker service registration failed: %s", error.c_str());
        return nullptr;
      }
      rmw_reset_error();
    }
  } else {
    const std::string request_topic = MakeServiceBridgeTopic("rq/", data->service_name);
    const std::string response_topic = MakeServiceBridgeTopic("rr/", data->service_name);
    const std::string request_type = MakeServiceBridgeType(data->type_name, "_Request");
    const std::string response_type = MakeServiceBridgeType(data->type_name, "_Response");
    if (
      !request_topic.empty() && !response_topic.empty() && !request_type.empty() &&
      !response_type.empty()) {
      data->bridge_response_publisher = bridge_backend.CreatePublisher(
        response_topic.c_str(), response_type.c_str(), &data->actual_qos);
      data->bridge_request_subscription = bridge_backend.Subscribe(
        request_topic.c_str(), request_type.c_str(), &data->actual_qos,
        ServiceRequestBridgeCallback, data);
    }
    if (
      bridge_backend.Required() &&
      (data->bridge_response_publisher == nullptr ||
       data->bridge_request_subscription == nullptr)) {
      DestroyServiceBridgeEndpoints(data);
      if (!allow_local_only) {
        delete data;
        RMW_SET_ERROR_MSG("explicit MDDS bridge library is configured but service creation failed");
        return nullptr;
      }
      rmw_reset_error();
    }
  }
  rmw_service_t * service = rmw_service_allocate();
  if (service == nullptr) {
    DestroyServiceBridgeEndpoints(data);
    DestroyServiceBrokerEndpoint(data);
    delete data;
    return nullptr;
  }
  rcutils_allocator_t allocator = node->context->options.allocator;
  service->implementation_identifier = rmw_mdds_cpp_identifier;
  service->data = data;
  service->service_name = rcutils_strdup(service_name, allocator);
  if (service->service_name == nullptr) {
    DestroyServiceBridgeEndpoints(data);
    DestroyServiceBrokerEndpoint(data);
    delete data;
    rmw_service_free(service);
    return nullptr;
  }
  RegisterService(data);
  return service;
}

rmw_ret_t rmw_destroy_service(rmw_node_t * node, rmw_service_t * service)
{
  rmw_ret_t ret = CheckNode(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  ret = CheckService(service);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  rcutils_allocator_t allocator = node->context->options.allocator;
  auto * data = static_cast<rmw_mdds_cpp::ServiceData *>(service->data);
  UnregisterService(data);
  DestroyServiceBridgeEndpoints(data);
  DestroyServiceBrokerEndpoint(data);
  allocator.deallocate(const_cast<char *>(service->service_name), allocator.state);
  delete data;
  rmw_service_free(service);
  return RMW_RET_OK;
}

rmw_ret_t rmw_take_request(
  const rmw_service_t * service, rmw_service_info_t * request_header, void * ros_request,
  bool * taken)
{
  rmw_ret_t ret = CheckService(service);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (request_header == nullptr || ros_request == nullptr || taken == nullptr) {
    RMW_SET_ERROR_MSG("take request argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto * data = static_cast<rmw_mdds_cpp::ServiceData *>(service->data);
  if (data == nullptr || data->request_size == 0) {
    RMW_SET_ERROR_MSG("service request type support is invalid");
    return RMW_RET_ERROR;
  }

  std::lock_guard<std::mutex> lock(data->mutex);
  if (data->requests.empty()) {
    *taken = false;
    return RMW_RET_OK;
  }
  auto sample = std::move(data->requests.front());
  data->requests.pop_front();
  if (!CopyToRosMessage(sample.payload, data->request_type, ros_request)) {
    RMW_SET_ERROR_MSG("failed to copy service request");
    return RMW_RET_ERROR;
  }
  *request_header = sample.info;
  *taken = true;
  return RMW_RET_OK;
}

rmw_ret_t rmw_send_response(
  const rmw_service_t * service, rmw_request_id_t * request_header, void * ros_response)
{
  rmw_ret_t ret = CheckService(service);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (request_header == nullptr || ros_response == nullptr) {
    RMW_SET_ERROR_MSG("send response argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  const auto * data = static_cast<const rmw_mdds_cpp::ServiceData *>(service->data);
  if (data == nullptr || data->response_size == 0) {
    RMW_SET_ERROR_MSG("service response type support is invalid");
    return RMW_RET_ERROR;
  }

  rmw_service_info_t info{};
  info.request_id = *request_header;
  info.source_timestamp = NowNanoseconds();
  std::vector<uint8_t> payload;
  if (!CopyFromRosMessage(ros_response, data->response_type, &payload)) {
    RMW_SET_ERROR_MSG("failed to copy service response");
    return RMW_RET_ERROR;
  }

  if (data->broker_client != nullptr) {
    std::vector<uint8_t> wire_payload;
    if (!EncodeServiceWirePayload(info.request_id, info.source_timestamp, payload, &wire_payload)) {
      RMW_SET_ERROR_MSG("failed to encode broker service response");
      return RMW_RET_ERROR;
    }
    std::string error;
    if (!rmw_mdds_cpp::BrokerClientPublish(
        data->broker_client, wire_payload,
        static_cast<uint64_t>(request_header->sequence_number), &error)) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "failed to publish broker service response: %s", error.c_str());
      return RMW_RET_ERROR;
    }
    return RMW_RET_OK;
  }

  if (data->bridge_response_publisher != nullptr) {
    std::vector<uint8_t> wire_payload;
    if (!EncodeServiceWirePayload(info.request_id, info.source_timestamp, payload, &wire_payload)) {
      RMW_SET_ERROR_MSG("failed to encode bridge service response");
      return RMW_RET_ERROR;
    }
    if (wire_payload.size() > std::numeric_limits<uint32_t>::max()) {
      RMW_SET_ERROR_MSG("bridge service response is too large");
      return RMW_RET_ERROR;
    }
    if (
      rmw_mdds_cpp::BridgeBackend::Instance().Publish(
        data->bridge_response_publisher, wire_payload.data(),
        static_cast<uint32_t>(wire_payload.size())) != 0) {
      RMW_SET_ERROR_MSG("failed to publish bridge service response");
      return RMW_RET_ERROR;
    }
    return RMW_RET_OK;
  }

  auto * client = FindClientByGuid(request_header->writer_guid);
  if (client == nullptr) {
    return RMW_RET_TIMEOUT;
  }

  rmw_mdds_cpp::ServiceResponseSample sample{info, std::move(payload)};
  EnqueueClientResponse(client, std::move(sample));
  return RMW_RET_OK;
}

rmw_ret_t rmw_service_response_publisher_get_actual_qos(
  const rmw_service_t * service, rmw_qos_profile_t * qos)
{
  rmw_ret_t ret = CheckService(service);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (qos == nullptr) {
    RMW_SET_ERROR_MSG("service qos output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  const auto * data = static_cast<const rmw_mdds_cpp::ServiceData *>(service->data);
  *qos = data == nullptr ? rmw_qos_profile_services_default : data->actual_qos;
  return RMW_RET_OK;
}

rmw_ret_t rmw_service_request_subscription_get_actual_qos(
  const rmw_service_t * service, rmw_qos_profile_t * qos)
{
  return rmw_service_response_publisher_get_actual_qos(service, qos);
}

static size_t CurrentMatchedPublishersForSubscription(rmw_mdds_cpp::SubscriptionData * subscription)
{
  if (subscription == nullptr) {
    return 0;
  }
  return rmw_mdds_cpp::BrokerModeEnabled() ?
    rmw_mdds_cpp::CountBrokerGraphPublishersForSubscription(subscription) :
    rmw_mdds_cpp::CountPublishersForSubscription(*subscription);
}

rmw_ret_t rmw_take_event(const rmw_event_t * event_handle, void * event_info, bool * taken)
{
  if (taken != nullptr) {
    *taken = false;
  }
  if (event_handle == nullptr || event_info == nullptr || taken == nullptr) {
    RMW_SET_ERROR_MSG("take event argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(event_handle->implementation_identifier)) {
    RMW_SET_ERROR_MSG("event implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  if (event_handle->data == nullptr) {
    RMW_SET_ERROR_MSG("event data is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto * status = static_cast<rmw_matched_status_t *>(event_info);
  bool ok = false;
  switch (event_handle->event_type) {
    case RMW_EVENT_PUBLICATION_MATCHED:
      ok = rmw_mdds_cpp::BrokerModeEnabled() ?
        rmw_mdds_cpp::TakeBrokerGraphPublisherMatchedStatus(
          static_cast<rmw_mdds_cpp::PublisherData *>(event_handle->data), status) :
        rmw_mdds_cpp::TakePublisherMatchedStatus(
          static_cast<rmw_mdds_cpp::PublisherData *>(event_handle->data), status);
      break;
    case RMW_EVENT_SUBSCRIPTION_MATCHED:
      ok = rmw_mdds_cpp::BrokerModeEnabled() ?
        rmw_mdds_cpp::TakeBrokerGraphSubscriptionMatchedStatus(
          static_cast<rmw_mdds_cpp::SubscriptionData *>(event_handle->data), status) :
        rmw_mdds_cpp::TakeSubscriptionMatchedStatus(
          static_cast<rmw_mdds_cpp::SubscriptionData *>(event_handle->data), status);
      break;
    case RMW_EVENT_LIVELINESS_CHANGED: {
      auto * subscription = static_cast<rmw_mdds_cpp::SubscriptionData *>(event_handle->data);
      ok = rmw_mdds_cpp::TakeSubscriptionLivelinessStatus(
        subscription, CurrentMatchedPublishersForSubscription(subscription),
        static_cast<rmw_liveliness_changed_status_t *>(event_info));
      break;
    }
    case RMW_EVENT_REQUESTED_DEADLINE_MISSED: {
      auto * subscription = static_cast<rmw_mdds_cpp::SubscriptionData *>(event_handle->data);
      ok = rmw_mdds_cpp::TakeSubscriptionDeadlineStatus(
        subscription, rmw_mdds_cpp::MddsNowNanoseconds(),
        CurrentMatchedPublishersForSubscription(subscription) > 0,
        static_cast<rmw_requested_deadline_missed_status_t *>(event_info));
      break;
    }
    case RMW_EVENT_OFFERED_DEADLINE_MISSED:
      ok = rmw_mdds_cpp::TakePublisherDeadlineStatus(
        static_cast<rmw_mdds_cpp::PublisherData *>(event_handle->data),
        rmw_mdds_cpp::MddsNowNanoseconds(),
        static_cast<rmw_offered_deadline_missed_status_t *>(event_info));
      break;
    case RMW_EVENT_LIVELINESS_LOST:
      ok = rmw_mdds_cpp::TakePublisherLivelinessLostStatus(
        static_cast<rmw_mdds_cpp::PublisherData *>(event_handle->data),
        rmw_mdds_cpp::MddsNowNanoseconds(),
        static_cast<rmw_liveliness_lost_status_t *>(event_info));
      break;
    case RMW_EVENT_OFFERED_QOS_INCOMPATIBLE:
      ok = rmw_mdds_cpp::TakePublisherQosIncompatibleStatus(
        static_cast<rmw_mdds_cpp::PublisherData *>(event_handle->data),
        static_cast<rmw_qos_incompatible_event_status_t *>(event_info));
      break;
    case RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE:
      ok = rmw_mdds_cpp::TakeSubscriptionQosIncompatibleStatus(
        static_cast<rmw_mdds_cpp::SubscriptionData *>(event_handle->data),
        static_cast<rmw_qos_incompatible_event_status_t *>(event_info));
      break;
    case RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE:
      ok = rmw_mdds_cpp::TakePublisherIncompatibleTypeStatus(
        static_cast<rmw_mdds_cpp::PublisherData *>(event_handle->data),
        static_cast<rmw_incompatible_type_status_t *>(event_info));
      break;
    case RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE:
      ok = rmw_mdds_cpp::TakeSubscriptionIncompatibleTypeStatus(
        static_cast<rmw_mdds_cpp::SubscriptionData *>(event_handle->data),
        static_cast<rmw_incompatible_type_status_t *>(event_info));
      break;
    case RMW_EVENT_MESSAGE_LOST:
      ok = rmw_mdds_cpp::TakeSubscriptionMessageLostStatus(
        static_cast<rmw_mdds_cpp::SubscriptionData *>(event_handle->data),
        static_cast<rmw_message_lost_status_t *>(event_info));
      break;
    default:
      if (IsNoOpPublisherEvent(event_handle->event_type) ||
        IsNoOpSubscriptionEvent(event_handle->event_type)) {
        // No-op QoS event: rmw_mdds never raises it, so report nothing pending.
        *taken = false;
        return RMW_RET_OK;
      }
      return Unsupported("rmw_take_event");
  }
  if (!ok) {
    RMW_SET_ERROR_MSG("failed to take event status");
    return RMW_RET_ERROR;
  }
  *taken = true;
  return RMW_RET_OK;
}

rmw_ret_t rmw_get_publisher_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, bool no_demangle, rmw_names_and_types_t * names_and_types)
{
  (void)no_demangle;
  rmw_ret_t ret =
    ValidateNamesAndTypesByNodeQuery(node, allocator, node_name, node_namespace, names_and_types);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  return InitNamesAndTypes(
    node, allocator, names_and_types,
    rmw_mdds_cpp::BrokerModeEnabled() ?
    rmw_mdds_cpp::GetBrokerGraphPublisherNamesAndTypesByNode(node_name, node_namespace) :
    rmw_mdds_cpp::GetPublisherNamesAndTypesByNode(node_name, node_namespace));
}

rmw_ret_t rmw_get_subscriber_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, bool no_demangle, rmw_names_and_types_t * names_and_types)
{
  (void)no_demangle;
  rmw_ret_t ret =
    ValidateNamesAndTypesByNodeQuery(node, allocator, node_name, node_namespace, names_and_types);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  return InitNamesAndTypes(
    node, allocator, names_and_types,
    rmw_mdds_cpp::BrokerModeEnabled() ?
    rmw_mdds_cpp::GetBrokerGraphSubscriptionNamesAndTypesByNode(node_name, node_namespace) :
    rmw_mdds_cpp::GetSubscriptionNamesAndTypesByNode(node_name, node_namespace));
}

rmw_ret_t rmw_get_service_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, rmw_names_and_types_t * names_and_types)
{
  rmw_ret_t ret =
    ValidateNamesAndTypesByNodeQuery(node, allocator, node_name, node_namespace, names_and_types);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  return InitNamesAndTypes(
    node, allocator, names_and_types,
    rmw_mdds_cpp::BrokerModeEnabled() ?
    rmw_mdds_cpp::GetBrokerGraphServiceNamesAndTypesByNode(node_name, node_namespace) :
    GetServiceNamesAndTypesByNode(node_name, node_namespace));
}

rmw_ret_t rmw_get_client_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, rmw_names_and_types_t * names_and_types)
{
  rmw_ret_t ret =
    ValidateNamesAndTypesByNodeQuery(node, allocator, node_name, node_namespace, names_and_types);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  return InitNamesAndTypes(
    node, allocator, names_and_types,
    rmw_mdds_cpp::BrokerModeEnabled() ?
    rmw_mdds_cpp::GetBrokerGraphClientNamesAndTypesByNode(node_name, node_namespace) :
    GetClientNamesAndTypesByNode(node_name, node_namespace));
}

rmw_ret_t rmw_get_topic_names_and_types(
  const rmw_node_t * node, rcutils_allocator_t * allocator, bool no_demangle,
  rmw_names_and_types_t * topic_names_and_types)
{
  (void)no_demangle;
  return InitNamesAndTypes(
    node, allocator, topic_names_and_types,
    rmw_mdds_cpp::BrokerModeEnabled() ? rmw_mdds_cpp::GetBrokerGraphTopicNamesAndTypes() :
                                        rmw_mdds_cpp::GetTopicNamesAndTypes());
}

rmw_ret_t rmw_get_service_names_and_types(
  const rmw_node_t * node, rcutils_allocator_t * allocator,
  rmw_names_and_types_t * service_names_and_types)
{
  return InitNamesAndTypes(
    node, allocator, service_names_and_types,
    rmw_mdds_cpp::BrokerModeEnabled() ? rmw_mdds_cpp::GetBrokerGraphServiceNamesAndTypes() :
                                        GetServiceNamesAndTypes());
}

rmw_ret_t rmw_get_node_names(
  const rmw_node_t * node, rcutils_string_array_t * node_names,
  rcutils_string_array_t * node_namespaces)
{
  return InitNodeGraphStringArrays(node, node_names, node_namespaces, nullptr);
}

rmw_ret_t rmw_get_node_names_with_enclaves(
  const rmw_node_t * node, rcutils_string_array_t * node_names,
  rcutils_string_array_t * node_namespaces, rcutils_string_array_t * enclaves)
{
  if (enclaves == nullptr) {
    RMW_SET_ERROR_MSG("enclaves argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return InitNodeGraphStringArrays(node, node_names, node_namespaces, enclaves);
}

rmw_ret_t rmw_count_publishers(const rmw_node_t * node, const char * topic_name, size_t * count)
{
  rmw_ret_t ret = CheckNode(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (topic_name == nullptr || count == nullptr) {
    RMW_SET_ERROR_MSG("count publishers argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  ret = ValidateFullyQualifiedName(topic_name, "topic name");
  if (ret != RMW_RET_OK) {
    return ret;
  }
  *count = rmw_mdds_cpp::BrokerModeEnabled() ?
    rmw_mdds_cpp::CountBrokerGraphPublishersByTopic(topic_name) :
    rmw_mdds_cpp::CountPublishersByTopic(topic_name);
  return RMW_RET_OK;
}

rmw_ret_t rmw_count_subscribers(const rmw_node_t * node, const char * topic_name, size_t * count)
{
  rmw_ret_t ret = CheckNode(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (topic_name == nullptr || count == nullptr) {
    RMW_SET_ERROR_MSG("count subscribers argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  ret = ValidateFullyQualifiedName(topic_name, "topic name");
  if (ret != RMW_RET_OK) {
    return ret;
  }
  *count = rmw_mdds_cpp::BrokerModeEnabled() ?
    rmw_mdds_cpp::CountBrokerGraphSubscriptionsByTopic(topic_name) :
    rmw_mdds_cpp::CountSubscriptionsByTopic(topic_name);
  return RMW_RET_OK;
}

rmw_ret_t rmw_count_clients(const rmw_node_t * node, const char * service_name, size_t * count)
{
  rmw_ret_t ret = CheckNode(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (service_name == nullptr || count == nullptr) {
    RMW_SET_ERROR_MSG("count clients argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  ret = ValidateFullyQualifiedName(service_name, "service name");
  if (ret != RMW_RET_OK) {
    return ret;
  }
  *count = rmw_mdds_cpp::BrokerModeEnabled() ?
    rmw_mdds_cpp::CountBrokerGraphClientsByName(service_name) :
    CountClientsByName(service_name);
  return RMW_RET_OK;
}

rmw_ret_t rmw_count_services(const rmw_node_t * node, const char * service_name, size_t * count)
{
  rmw_ret_t ret = CheckNode(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (service_name == nullptr || count == nullptr) {
    RMW_SET_ERROR_MSG("count services argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  ret = ValidateFullyQualifiedName(service_name, "service name");
  if (ret != RMW_RET_OK) {
    return ret;
  }
  *count = rmw_mdds_cpp::BrokerModeEnabled() ?
    rmw_mdds_cpp::CountBrokerGraphServicesByName(service_name) :
    CountServicesByName(service_name);
  return RMW_RET_OK;
}

rmw_ret_t rmw_get_gid_for_client(const rmw_client_t * client, rmw_gid_t * gid)
{
  rmw_ret_t ret = CheckClient(client);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (gid == nullptr) {
    RMW_SET_ERROR_MSG("gid output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  FillClientGid(static_cast<const rmw_mdds_cpp::ClientData *>(client->data), gid);
  return RMW_RET_OK;
}

rmw_ret_t rmw_get_gid_for_publisher(const rmw_publisher_t * publisher, rmw_gid_t * gid)
{
  rmw_ret_t ret = CheckPublisher(publisher);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (gid == nullptr) {
    RMW_SET_ERROR_MSG("gid output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  FillPublisherGid(publisher, gid);
  return RMW_RET_OK;
}

rmw_ret_t rmw_compare_gids_equal(const rmw_gid_t * gid1, const rmw_gid_t * gid2, bool * result)
{
  if (gid1 == nullptr || gid2 == nullptr || result == nullptr) {
    RMW_SET_ERROR_MSG("gid compare argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (
    !rmw_mdds_cpp::IsMddsIdentifier(gid1->implementation_identifier) ||
    !rmw_mdds_cpp::IsMddsIdentifier(gid2->implementation_identifier)) {
    RMW_SET_ERROR_MSG("gid implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  *result = std::memcmp(gid1->data, gid2->data, sizeof(gid1->data)) == 0;
  return RMW_RET_OK;
}

rmw_ret_t rmw_service_server_is_available(
  const rmw_node_t * node, const rmw_client_t * client, bool * is_available)
{
  if (node == nullptr) {
    RMW_SET_ERROR_MSG("node is null");
    return RMW_RET_ERROR;
  }
  if (client == nullptr) {
    RMW_SET_ERROR_MSG("client is null");
    return RMW_RET_ERROR;
  }
  if (is_available == nullptr) {
    RMW_SET_ERROR_MSG("service availability output is null");
    return RMW_RET_ERROR;
  }
  rmw_ret_t ret = CheckNode(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  ret = CheckClient(client);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  const auto * client_data = static_cast<const rmw_mdds_cpp::ClientData *>(client->data);
  *is_available = HasMatchingService(client_data) ||
                  (rmw_mdds_cpp::BrokerModeEnabled() &&
                   rmw_mdds_cpp::BrokerGraphHasMatchingService(client_data));
  return RMW_RET_OK;
}

rmw_ret_t rmw_set_log_severity(rmw_log_severity_t severity)
{
  switch (severity) {
    case RMW_LOG_SEVERITY_DEBUG:
    case RMW_LOG_SEVERITY_INFO:
    case RMW_LOG_SEVERITY_WARN:
    case RMW_LOG_SEVERITY_ERROR:
    case RMW_LOG_SEVERITY_FATAL:
      return RMW_RET_OK;
    default:
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("invalid log severity: %d", severity);
      return RMW_RET_INVALID_ARGUMENT;
  }
}

rmw_ret_t rmw_get_publishers_info_by_topic(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * topic_name, bool no_mangle,
  rmw_topic_endpoint_info_array_t * publishers_info)
{
  (void)no_mangle;
  if (topic_name == nullptr) {
    RMW_SET_ERROR_MSG("topic name is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_ret_t ret = ValidateFullyQualifiedName(topic_name, "topic name");
  if (ret != RMW_RET_OK) {
    return ret;
  }
  return InitTopicEndpointInfoArray(
    node, allocator,
    rmw_mdds_cpp::BrokerModeEnabled() ?
    rmw_mdds_cpp::GetBrokerGraphPublisherEndpointInfosByTopic(topic_name) :
    rmw_mdds_cpp::GetPublisherEndpointInfosByTopic(topic_name),
    publishers_info);
}

rmw_ret_t rmw_get_subscriptions_info_by_topic(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * topic_name, bool no_mangle,
  rmw_topic_endpoint_info_array_t * subscriptions_info)
{
  (void)no_mangle;
  if (topic_name == nullptr) {
    RMW_SET_ERROR_MSG("topic name is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_ret_t ret = ValidateFullyQualifiedName(topic_name, "topic name");
  if (ret != RMW_RET_OK) {
    return ret;
  }
  return InitTopicEndpointInfoArray(
    node, allocator,
    rmw_mdds_cpp::BrokerModeEnabled() ?
    rmw_mdds_cpp::GetBrokerGraphSubscriptionEndpointInfosByTopic(topic_name) :
    rmw_mdds_cpp::GetSubscriptionEndpointInfosByTopic(topic_name),
    subscriptions_info);
}

rmw_ret_t rmw_qos_profile_check_compatible(
  const rmw_qos_profile_t publisher_profile, const rmw_qos_profile_t subscription_profile,
  rmw_qos_compatibility_type_t * compatibility, char * reason, size_t reason_size)
{
  return rmw_dds_common::qos_profile_check_compatible(
    publisher_profile, subscription_profile, compatibility, reason, reason_size);
}

rmw_ret_t rmw_publisher_get_network_flow_endpoints(
  const rmw_publisher_t * publisher, rcutils_allocator_t * allocator,
  rmw_network_flow_endpoint_array_t * network_flow_endpoint_array)
{
  rmw_ret_t ret = CheckPublisher(publisher);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (allocator == nullptr || network_flow_endpoint_array == nullptr) {
    RMW_SET_ERROR_MSG("network flow endpoint argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  const auto * data = static_cast<const rmw_mdds_cpp::PublisherData *>(publisher->data);
  if (data == nullptr) {
    RMW_SET_ERROR_MSG("publisher data is null");
    return RMW_RET_ERROR;
  }
  return InitRtpsUserDataNetworkFlowEndpoints(
    data->context, allocator, network_flow_endpoint_array);
}

rmw_ret_t rmw_subscription_get_network_flow_endpoints(
  const rmw_subscription_t * subscription, rcutils_allocator_t * allocator,
  rmw_network_flow_endpoint_array_t * network_flow_endpoint_array)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (allocator == nullptr || network_flow_endpoint_array == nullptr) {
    RMW_SET_ERROR_MSG("network flow endpoint argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  const auto * data = static_cast<const rmw_mdds_cpp::SubscriptionData *>(subscription->data);
  if (data == nullptr) {
    RMW_SET_ERROR_MSG("subscription data is null");
    return RMW_RET_ERROR;
  }
  return InitRtpsUserDataNetworkFlowEndpoints(
    data->context, allocator, network_flow_endpoint_array);
}

rmw_ret_t rmw_subscription_set_on_new_message_callback(
  rmw_subscription_t * subscription, rmw_event_callback_t callback, const void * user_data)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  const size_t pending_message_count = rmw_mdds_cpp::SetSubscriptionNewMessageCallback(
    static_cast<rmw_mdds_cpp::SubscriptionData *>(subscription->data), callback, user_data);
  if (callback != nullptr && pending_message_count > 0) {
    callback(user_data, pending_message_count);
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_service_set_on_new_request_callback(
  rmw_service_t * service, rmw_event_callback_t callback, const void * user_data)
{
  rmw_ret_t ret = CheckService(service);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  const size_t pending_request_count =
    SetServiceRequestCallback(
      static_cast<rmw_mdds_cpp::ServiceData *>(service->data), callback, user_data);
  if (callback != nullptr && pending_request_count > 0) {
    callback(user_data, pending_request_count);
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_client_set_on_new_response_callback(
  rmw_client_t * client, rmw_event_callback_t callback, const void * user_data)
{
  rmw_ret_t ret = CheckClient(client);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  const size_t pending_response_count =
    SetClientResponseCallback(
      static_cast<rmw_mdds_cpp::ClientData *>(client->data), callback, user_data);
  if (callback != nullptr && pending_response_count > 0) {
    callback(user_data, pending_response_count);
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_event_set_callback(
  rmw_event_t * event, rmw_event_callback_t callback, const void * user_data)
{
  if (event == nullptr) {
    RMW_SET_ERROR_MSG("event callback handle is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(event->implementation_identifier)) {
    RMW_SET_ERROR_MSG("event callback implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  if (event->data == nullptr) {
    RMW_SET_ERROR_MSG("event callback data is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  size_t pending_event_count = 0;
  switch (event->event_type) {
    case RMW_EVENT_PUBLICATION_MATCHED:
      pending_event_count = rmw_mdds_cpp::SetPublisherMatchedCallback(
        static_cast<rmw_mdds_cpp::PublisherData *>(event->data), callback, user_data);
      break;
    case RMW_EVENT_SUBSCRIPTION_MATCHED:
      pending_event_count = rmw_mdds_cpp::SetSubscriptionMatchedCallback(
        static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data), callback, user_data);
      break;
    case RMW_EVENT_LIVELINESS_CHANGED: {
      auto * subscription = static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data);
      pending_event_count = rmw_mdds_cpp::SetSubscriptionLivelinessCallback(
        subscription, CurrentMatchedPublishersForSubscription(subscription), callback, user_data);
      break;
    }
    case RMW_EVENT_REQUESTED_DEADLINE_MISSED: {
      auto * subscription = static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data);
      pending_event_count = rmw_mdds_cpp::SetSubscriptionDeadlineCallback(
        subscription, rmw_mdds_cpp::MddsNowNanoseconds(),
        CurrentMatchedPublishersForSubscription(subscription) > 0, callback, user_data);
      break;
    }
    case RMW_EVENT_OFFERED_DEADLINE_MISSED:
      pending_event_count = rmw_mdds_cpp::SetPublisherDeadlineCallback(
        static_cast<rmw_mdds_cpp::PublisherData *>(event->data),
        rmw_mdds_cpp::MddsNowNanoseconds(), callback, user_data);
      break;
    case RMW_EVENT_LIVELINESS_LOST:
      pending_event_count = rmw_mdds_cpp::SetPublisherLivelinessLostCallback(
        static_cast<rmw_mdds_cpp::PublisherData *>(event->data),
        rmw_mdds_cpp::MddsNowNanoseconds(), callback, user_data);
      break;
    case RMW_EVENT_OFFERED_QOS_INCOMPATIBLE:
      pending_event_count = rmw_mdds_cpp::SetPublisherQosIncompatibleCallback(
        static_cast<rmw_mdds_cpp::PublisherData *>(event->data), callback, user_data);
      break;
    case RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE:
      pending_event_count = rmw_mdds_cpp::SetSubscriptionQosIncompatibleCallback(
        static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data), callback, user_data);
      break;
    case RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE:
      pending_event_count = rmw_mdds_cpp::SetPublisherIncompatibleTypeCallback(
        static_cast<rmw_mdds_cpp::PublisherData *>(event->data), callback, user_data);
      break;
    case RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE:
      pending_event_count = rmw_mdds_cpp::SetSubscriptionIncompatibleTypeCallback(
        static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data), callback, user_data);
      break;
    case RMW_EVENT_MESSAGE_LOST:
      pending_event_count = rmw_mdds_cpp::SetSubscriptionMessageLostCallback(
        static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data), callback, user_data);
      break;
    default:
      if (IsNoOpPublisherEvent(event->event_type) || IsNoOpSubscriptionEvent(event->event_type)) {
        // No-op QoS event: accept the callback registration but never invoke it.
        return RMW_RET_OK;
      }
      return Unsupported("rmw_event_set_callback");
  }
  if (callback != nullptr && pending_event_count > 0) {
    callback(user_data, pending_event_count);
  }
  return RMW_RET_OK;
}

bool rmw_event_type_is_supported(rmw_event_type_t rmw_event_type)
{
  return IsSupportedMatchedEvent(rmw_event_type) ||
         IsEnforcedPublisherEvent(rmw_event_type) ||
         IsEnforcedSubscriptionEvent(rmw_event_type) ||
         IsNoOpPublisherEvent(rmw_event_type) ||
         IsNoOpSubscriptionEvent(rmw_event_type);
}

static rmw_ret_t TakeDynamicCommon(
  const rmw_subscription_t * subscription, rosidl_dynamic_typesupport_dynamic_data_t * dynamic_data,
  bool * taken, rmw_message_info_t * message_info, rmw_subscription_allocation_t * allocation)
{
  if (dynamic_data == nullptr || taken == nullptr) {
    RMW_SET_ERROR_MSG("take dynamic message argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  *taken = false;
  if (
    dynamic_data->serialization_support == nullptr ||
    dynamic_data->serialization_support->methods.dynamic_data_deserialize == nullptr) {
    RMW_SET_ERROR_MSG("dynamic data has no deserialize support");
    return RMW_RET_INVALID_ARGUMENT;
  }

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_serialized_message_t serialized = rmw_get_zero_initialized_serialized_message();
  if (rmw_serialized_message_init(&serialized, 0, &allocator) != RCUTILS_RET_OK) {
    RMW_SET_ERROR_MSG("failed to initialize dynamic take serialized buffer");
    return RMW_RET_ERROR;
  }

  rmw_ret_t ret = message_info != nullptr ?
    rmw_take_serialized_message_with_info(subscription, &serialized, taken, message_info, allocation) :
    rmw_take_serialized_message(subscription, &serialized, taken, allocation);
  if (ret != RMW_RET_OK || !*taken) {
    (void)rmw_serialized_message_fini(&serialized);
    return ret;
  }

  rcutils_ret_t dyn_ret =
    rosidl_dynamic_typesupport_dynamic_data_deserialize(dynamic_data, &serialized);
  (void)rmw_serialized_message_fini(&serialized);
  if (dyn_ret != RCUTILS_RET_OK) {
    *taken = false;
    return DynamicSupportRcutilsError(dyn_ret, "dynamic data deserialize");
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_take_dynamic_message(
  const rmw_subscription_t * subscription, rosidl_dynamic_typesupport_dynamic_data_t * dynamic_data,
  bool * taken, rmw_subscription_allocation_t * allocation)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  return TakeDynamicCommon(subscription, dynamic_data, taken, nullptr, allocation);
}

rmw_ret_t rmw_take_dynamic_message_with_info(
  const rmw_subscription_t * subscription, rosidl_dynamic_typesupport_dynamic_data_t * dynamic_data,
  bool * taken, rmw_message_info_t * message_info, rmw_subscription_allocation_t * allocation)
{
  rmw_ret_t ret = CheckSubscription(subscription);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (message_info == nullptr) {
    RMW_SET_ERROR_MSG("message info is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  *message_info = rmw_get_zero_initialized_message_info();
  return TakeDynamicCommon(subscription, dynamic_data, taken, message_info, allocation);
}

rmw_ret_t rmw_serialization_support_init(
  const char * serialization_lib_name, rcutils_allocator_t * allocator,
  rosidl_dynamic_typesupport_serialization_support_t * serialization_support)
{
  if (serialization_lib_name == nullptr || allocator == nullptr || serialization_support == nullptr) {
    RMW_SET_ERROR_MSG("serialization support init argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rcutils_allocator_is_valid(allocator)) {
    RMW_SET_ERROR_MSG("serialization support init allocator is invalid");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!IsFastCdrDynamicSerializationName(serialization_lib_name)) {
    return Unsupported("rmw_serialization_support_init");
  }

  void * handle = OpenFastRtpsDynamicTypesupport();
  if (handle == nullptr) {
    return RMW_RET_UNSUPPORTED;
  }
  auto init_impl = ResolveDynamicTypesupportSymbol<DynamicSerializationImplInit>(
    handle, "rosidl_dynamic_typesupport_fastrtps_init_serialization_support_impl");
  auto init_interface = ResolveDynamicTypesupportSymbol<DynamicSerializationInterfaceInit>(
    handle, "rosidl_dynamic_typesupport_fastrtps_init_serialization_support_interface");
  if (init_impl == nullptr || init_interface == nullptr) {
    dlclose(handle);
    return RMW_RET_UNSUPPORTED;
  }

  rosidl_dynamic_typesupport_serialization_support_impl_t impl =
    rosidl_dynamic_typesupport_get_zero_initialized_serialization_support_impl();
  rosidl_dynamic_typesupport_serialization_support_interface_t methods =
    rosidl_dynamic_typesupport_get_zero_initialized_serialization_support_interface();
  rcutils_ret_t ret = init_impl(allocator, &impl);
  rmw_ret_t rmw_ret = DynamicSupportRcutilsError(ret, "FastRTPS dynamic serialization impl init");
  if (rmw_ret != RMW_RET_OK) {
    dlclose(handle);
    return rmw_ret;
  }
  ret = init_interface(allocator, &methods);
  rmw_ret = DynamicSupportRcutilsError(ret, "FastRTPS dynamic serialization interface init");
  if (rmw_ret != RMW_RET_OK) {
    if (methods.serialization_support_impl_fini != nullptr) {
      (void)methods.serialization_support_impl_fini(&impl);
    }
    dlclose(handle);
    return rmw_ret;
  }
  ret = rosidl_dynamic_typesupport_serialization_support_init(
    &impl, &methods, allocator, serialization_support);
  rmw_ret = DynamicSupportRcutilsError(ret, "dynamic serialization support init");
  if (rmw_ret != RMW_RET_OK) {
    if (methods.serialization_support_impl_fini != nullptr) {
      (void)methods.serialization_support_impl_fini(&impl);
    }
    if (methods.serialization_support_interface_fini != nullptr) {
      (void)methods.serialization_support_interface_fini(&methods);
    }
    dlclose(handle);
    return rmw_ret;
  }
  return RMW_RET_OK;
}
}  // extern "C"
