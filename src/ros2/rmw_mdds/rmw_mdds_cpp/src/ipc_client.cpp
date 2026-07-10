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

#include "ipc_client.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>

#include "bridge_backend.hpp"
#include "broker.hpp"
#include "context.hpp"
#include "ipc_broker.hpp"
#include "ipc_protocol.hpp"
#include "ipc_transport.hpp"
#include "rmw_dds_common/qos.hpp"
#include "rmw_mdds_cpp/identifier.hpp"

namespace rmw_mdds_cpp {
namespace {
constexpr uint64_t kRegisterRequestId = 1u;
constexpr std::chrono::milliseconds kGraphCacheWarmupTimeout{6500};
constexpr std::chrono::milliseconds kGraphCacheSettleWindow{100};
constexpr std::chrono::seconds kGraphCacheFreshWindow{5};
constexpr const char *kRemoteNodeSyncTopic = "_mdds_remote_node";
std::mutex g_graph_mutex;
std::vector<ipc::EndpointDescriptor> g_graph_endpoints;
std::chrono::steady_clock::time_point g_graph_cache_last_update;
std::chrono::steady_clock::time_point g_graph_cache_last_refresh;
uint64_t g_graph_cache_broker_id = 0u;
uint64_t g_graph_cache_epoch = 0u;
std::mutex g_auto_broker_mutex;
std::unique_ptr<ipc::IpcBroker> g_auto_broker;
// Process-wide count of live rmw contexts, guarded by g_auto_broker_mutex. The
// embedded broker + MDDS bridge are process singletons shared across every
// context, so they may only be torn down once the LAST context is fini'd —
// tearing them down on an earlier context's fini would break the others.
int g_active_context_count = 0;

void SetError(std::string *error, const std::string &message) {
  if (error != nullptr) {
    *error = message;
  }
}

uint64_t NowNanoseconds() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

bool EnvValueEnabled(const char *value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
         std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "on") == 0 ||
         std::strcmp(value, "ON") == 0 || std::strcmp(value, "yes") == 0 ||
         std::strcmp(value, "YES") == 0;
}

bool EnvValueDisabled(const char *value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
         std::strcmp(value, "FALSE") == 0 || std::strcmp(value, "off") == 0 ||
         std::strcmp(value, "OFF") == 0 || std::strcmp(value, "no") == 0 ||
         std::strcmp(value, "NO") == 0;
}

bool EnvValueConfigured(const char *value) {
  return value != nullptr && value[0] != '\0';
}

bool GraphDebugEnabled() {
  const char *value = std::getenv("RMW_MDDS_GRAPH_DEBUG");
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

uint64_t EntityIdFromPointer(const void *ptr) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
}

uint64_t LocalContextId(const rmw_context_t *context) {
  return (static_cast<uint64_t>(getpid()) << 32u) ^
         EntityIdFromPointer(context);
}

uint32_t DomainIdFromContext(const rmw_context_t *context) {
  if (context == nullptr) {
    return 0u;
  }
  return static_cast<uint32_t>(context->actual_domain_id);
}

bool EndpointMatchesDomain(const ipc::EndpointDescriptor &endpoint,
                           uint32_t domain_id) {
  return endpoint.domain_id == domain_id;
}

bool ConnectBrokerSocketWithAutoStart(const std::string &socket_path,
                                      ipc::UniqueFd *fd, std::string *error);

ipc::EndpointDescriptor MakePublisherEndpoint(PublisherData *publisher) {
  ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = EntityIdFromPointer(publisher);
  endpoint.local_context_id = LocalContextId(publisher->context);
  endpoint.domain_id = DomainIdFromContext(publisher->context);
  endpoint.kind = ipc::EndpointKind::kPublisher;
  endpoint.node_name = publisher->node_name;
  endpoint.node_namespace = publisher->node_namespace;
  endpoint.node_enclave = publisher->node_enclave;
  endpoint.topic_name = publisher->topic_name;
  endpoint.type_name = publisher->adapter.TypeName();
  endpoint.mdds_type_name = publisher->adapter.MddsTypeName();
  endpoint.type_hash = publisher->adapter.TypeHash();
  endpoint.qos = publisher->actual_qos;
  return endpoint;
}

ipc::EndpointDescriptor
MakeSubscriptionEndpoint(SubscriptionData *subscription) {
  ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = EntityIdFromPointer(subscription);
  endpoint.local_context_id = LocalContextId(subscription->context);
  endpoint.domain_id = DomainIdFromContext(subscription->context);
  endpoint.kind = ipc::EndpointKind::kSubscription;
  endpoint.node_name = subscription->node_name;
  endpoint.node_namespace = subscription->node_namespace;
  endpoint.node_enclave = subscription->node_enclave;
  endpoint.topic_name = subscription->topic_name;
  endpoint.type_name = subscription->adapter.TypeName();
  endpoint.mdds_type_name = subscription->adapter.MddsTypeName();
  endpoint.type_hash = subscription->adapter.TypeHash();
  endpoint.qos = subscription->actual_qos;
  endpoint.ignore_local_publications = subscription->ignore_local_publications;
  return endpoint;
}

ipc::EndpointDescriptor MakeClientEndpoint(ClientData *client) {
  ipc::EndpointDescriptor endpoint;
  endpoint.entity_id =
      client->entity_id != 0u ? client->entity_id : EntityIdFromPointer(client);
  endpoint.local_context_id = LocalContextId(client->context);
  endpoint.domain_id = DomainIdFromContext(client->context);
  endpoint.kind = ipc::EndpointKind::kClient;
  endpoint.node_name = client->node_name;
  endpoint.node_namespace = client->node_namespace;
  endpoint.node_enclave = client->node_enclave;
  endpoint.topic_name = client->service_name;
  endpoint.type_name = client->type_name;
  endpoint.mdds_type_name = client->type_name;
  endpoint.qos = client->actual_qos;
  return endpoint;
}

ipc::EndpointDescriptor MakeServiceEndpoint(ServiceData *service) {
  ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = EntityIdFromPointer(service);
  endpoint.local_context_id = LocalContextId(service->context);
  endpoint.domain_id = DomainIdFromContext(service->context);
  endpoint.kind = ipc::EndpointKind::kService;
  endpoint.node_name = service->node_name;
  endpoint.node_namespace = service->node_namespace;
  endpoint.node_enclave = service->node_enclave;
  endpoint.topic_name = service->service_name;
  endpoint.type_name = service->type_name;
  endpoint.mdds_type_name = service->type_name;
  endpoint.qos = service->actual_qos;
  return endpoint;
}

bool RegistrationKindForEndpoint(ipc::EndpointKind endpoint_kind,
                                 ipc::MessageKind *message_kind,
                                 std::string *error) {
  if (message_kind == nullptr) {
    SetError(error, "registration message kind output is null");
    return false;
  }
  switch (endpoint_kind) {
  case ipc::EndpointKind::kPublisher:
    *message_kind = ipc::MessageKind::kRegisterPublisher;
    return true;
  case ipc::EndpointKind::kSubscription:
    *message_kind = ipc::MessageKind::kRegisterSubscription;
    return true;
  case ipc::EndpointKind::kClient:
    *message_kind = ipc::MessageKind::kRegisterClient;
    return true;
  case ipc::EndpointKind::kService:
    *message_kind = ipc::MessageKind::kRegisterService;
    return true;
  }
  SetError(error, "endpoint kind is not supported by broker registration");
  return false;
}

void ClearGraphCache() {
  std::lock_guard<std::mutex> lock(g_graph_mutex);
  if (GraphDebugEnabled()) {
    std::fprintf(stderr,
                 "[rmw_mdds_graph] client cache clear endpoints=%zu "
                 "broker_id=%llu epoch=%llu\n",
                 g_graph_endpoints.size(),
                 static_cast<unsigned long long>(g_graph_cache_broker_id),
                 static_cast<unsigned long long>(g_graph_cache_epoch));
  }
  g_graph_endpoints.clear();
  g_graph_cache_last_update = std::chrono::steady_clock::time_point{};
  g_graph_cache_last_refresh = std::chrono::steady_clock::time_point{};
}

bool GraphCacheFresh() {
  std::lock_guard<std::mutex> lock(g_graph_mutex);
  if (g_graph_cache_last_refresh == std::chrono::steady_clock::time_point{} ||
      std::chrono::steady_clock::now() - g_graph_cache_last_refresh >=
          kGraphCacheFreshWindow) {
    return false;
  }
  return true;
}

bool GraphEndpointsContainNonSyntheticEndpoint(
    const std::vector<ipc::EndpointDescriptor> &endpoints) {
  return std::any_of(endpoints.begin(), endpoints.end(),
                     [](const ipc::EndpointDescriptor &endpoint) {
                       return endpoint.topic_name != kRemoteNodeSyncTopic;
                     });
}

bool UpdateGraphCache(std::vector<ipc::EndpointDescriptor> endpoints,
                      uint64_t broker_id, uint64_t epoch,
                      bool from_explicit_refresh = false) {
  const size_t endpoint_count = endpoints.size();
  const bool has_non_synthetic_endpoint =
      GraphEndpointsContainNonSyntheticEndpoint(endpoints);
  std::lock_guard<std::mutex> lock(g_graph_mutex);
  const auto now = std::chrono::steady_clock::now();
  if (epoch != 0u && g_graph_cache_broker_id == broker_id &&
      g_graph_cache_epoch != 0u && epoch <= g_graph_cache_epoch) {
    if (from_explicit_refresh) {
      g_graph_cache_last_refresh = now;
    }
    if (GraphDebugEnabled()) {
      std::fprintf(
          stderr,
          "[rmw_mdds_graph] client cache stale drop broker_id=%llu "
          "epoch=%llu current_broker=%llu current_epoch=%llu endpoints=%zu\n",
          static_cast<unsigned long long>(broker_id),
          static_cast<unsigned long long>(epoch),
          static_cast<unsigned long long>(g_graph_cache_broker_id),
          static_cast<unsigned long long>(g_graph_cache_epoch), endpoint_count);
    }
    return from_explicit_refresh;
  }
  g_graph_endpoints = std::move(endpoints);
  g_graph_cache_last_update = now;
  if (from_explicit_refresh) {
    g_graph_cache_last_refresh = g_graph_cache_last_update;
  }
  if (epoch != 0u) {
    g_graph_cache_broker_id = broker_id;
    g_graph_cache_epoch = epoch;
  }
  if (GraphDebugEnabled()) {
    std::fprintf(
        stderr,
        "[rmw_mdds_graph] client cache update broker_id=%llu epoch=%llu "
        "endpoints=%zu has_non_synthetic=%d\n",
        static_cast<unsigned long long>(broker_id),
        static_cast<unsigned long long>(epoch), endpoint_count,
        has_non_synthetic_endpoint ? 1 : 0);
  }
  return true;
}

bool UpdateGraphCache(ipc::GraphUpdateMessage update,
                      bool from_explicit_refresh = false) {
  return UpdateGraphCache(std::move(update.endpoints), update.broker_id,
                          update.epoch, from_explicit_refresh);
}

void RefreshGraphCacheFromBroker() {
  if (!BrokerModeEnabled() || GraphCacheFresh()) {
    if (GraphDebugEnabled()) {
      std::fprintf(stderr,
                   "[rmw_mdds_graph] client refresh skip broker_or_fresh\n");
    }
    return;
  }

  ipc::UniqueFd fd;
  std::string error;
  if (!ConnectBrokerSocketWithAutoStart(BrokerSocketPath(), &fd, &error)) {
    if (GraphDebugEnabled()) {
      std::fprintf(stderr,
                   "[rmw_mdds_graph] client refresh connect fail error=%s\n",
                   error.c_str());
    }
    return;
  }
  if (GraphDebugEnabled()) {
    std::fprintf(stderr, "[rmw_mdds_graph] client refresh connected\n");
  }

  const auto deadline =
      std::chrono::steady_clock::now() + kGraphCacheWarmupTimeout;
  bool saw_non_synthetic_update = false;
  bool saw_accepted_update = false;
  auto settle_deadline = deadline;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto now = std::chrono::steady_clock::now();
    const auto wait_deadline = (saw_non_synthetic_update || saw_accepted_update)
                                   ? std::min(deadline, settle_deadline)
                                   : deadline;
    if (now >= wait_deadline) {
      if (GraphDebugEnabled()) {
        std::fprintf(stderr,
                     "[rmw_mdds_graph] client refresh settle complete "
                     "saw_non_synthetic=%d\n",
                     saw_non_synthetic_update ? 1 : 0);
      }
      return;
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(wait_deadline -
                                                              now);
    pollfd pfd;
    pfd.fd = fd.get();
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int poll_timeout =
        static_cast<int>(std::max<std::chrono::milliseconds>(
                             remaining, std::chrono::milliseconds{1})
                             .count());
    const int ready = poll(&pfd, 1u, poll_timeout);
    if (ready <= 0 || (pfd.revents & POLLIN) == 0) {
      if (saw_non_synthetic_update) {
        if (GraphDebugEnabled()) {
          std::fprintf(stderr,
                       "[rmw_mdds_graph] client refresh poll settle timeout\n");
        }
        return;
      }
      continue;
    }

    ipc::Frame frame;
    if (ipc::ReadFrame(fd.get(), &frame, &error) != ipc::ReadFrameStatus::kOk) {
      if (GraphDebugEnabled()) {
        std::fprintf(stderr,
                     "[rmw_mdds_graph] client refresh read fail error=%s\n",
                     error.c_str());
      }
      return;
    }
    if (frame.kind != ipc::MessageKind::kGraphUpdate) {
      if (GraphDebugEnabled()) {
        std::fprintf(
            stderr,
            "[rmw_mdds_graph] client refresh ignore frame kind=%u bytes=%zu\n",
            static_cast<unsigned>(frame.kind), frame.payload.size());
      }
      continue;
    }

    ipc::GraphUpdateMessage update;
    std::string decode_error;
    if (ipc::DecodeGraphUpdate(frame.payload.data(), frame.payload.size(),
                               &update, &decode_error)) {
      const bool has_non_synthetic_endpoint =
          GraphEndpointsContainNonSyntheticEndpoint(update.endpoints);
      if (GraphDebugEnabled()) {
        std::fprintf(
            stderr,
            "[rmw_mdds_graph] client refresh graph frame broker_id=%llu "
            "epoch=%llu endpoints=%zu has_non_synthetic=%d bytes=%zu\n",
            static_cast<unsigned long long>(update.broker_id),
            static_cast<unsigned long long>(update.epoch),
            update.endpoints.size(), has_non_synthetic_endpoint ? 1 : 0,
            frame.payload.size());
      }
      const bool accepted = UpdateGraphCache(std::move(update), true);
      if (accepted) {
        saw_accepted_update = true;
      }
      if (accepted && has_non_synthetic_endpoint) {
        saw_non_synthetic_update = true;
        settle_deadline =
            std::chrono::steady_clock::now() + kGraphCacheSettleWindow;
      } else if (accepted) {
        settle_deadline =
            std::chrono::steady_clock::now() + kGraphCacheSettleWindow;
      }
    } else if (GraphDebugEnabled()) {
      std::fprintf(
          stderr,
          "[rmw_mdds_graph] client refresh decode fail bytes=%zu error=%s\n",
          frame.payload.size(), decode_error.c_str());
    }
  }
  if (GraphDebugEnabled()) {
    std::fprintf(stderr, "[rmw_mdds_graph] client refresh deadline expired\n");
  }
}

std::vector<ipc::EndpointDescriptor>
GetGraphCacheSnapshot(bool refresh = true) {
  if (refresh) {
    RefreshGraphCacheFromBroker();
  }
  std::lock_guard<std::mutex> lock(g_graph_mutex);
  return g_graph_endpoints;
}

bool EndpointMatchesNameAndType(const ipc::EndpointDescriptor &endpoint,
                                const std::string &name,
                                const std::string &type) {
  return endpoint.topic_name == name &&
         (endpoint.type_name.empty() || type.empty() ||
          endpoint.type_name == type);
}

size_t
CountGraphEndpointsInList(const std::vector<ipc::EndpointDescriptor> &endpoints,
                          const rmw_context_t *context, ipc::EndpointKind kind,
                          const std::string &name,
                          const std::string &type = {}) {
  const uint32_t domain_id = DomainIdFromContext(context);
  return static_cast<size_t>(std::count_if(
      endpoints.begin(), endpoints.end(),
      [domain_id, kind, &name, &type](const ipc::EndpointDescriptor &endpoint) {
        return EndpointMatchesDomain(endpoint, domain_id) &&
               endpoint.kind == kind &&
               EndpointMatchesNameAndType(endpoint, name, type);
      }));
}

size_t CountGraphEndpoints(const rmw_context_t *context, ipc::EndpointKind kind,
                           const std::string &name,
                           const std::string &type = {}) {
  const auto endpoints = GetGraphCacheSnapshot();
  return CountGraphEndpointsInList(endpoints, context, kind, name, type);
}

size_t CountServiceAvailabilityEndpointsInList(
    const std::vector<ipc::EndpointDescriptor> &endpoints,
    const ClientData *client) {
  if (client == nullptr || client->context == nullptr) {
    return 0u;
  }
  const uint32_t domain_id = DomainIdFromContext(client->context);
  return static_cast<size_t>(std::count_if(
      endpoints.begin(), endpoints.end(), [domain_id, client](const auto &endpoint) {
        return EndpointMatchesDomain(endpoint, domain_id) &&
               endpoint.kind == ipc::EndpointKind::kService &&
               endpoint.local_context_id != 0u &&
               EndpointMatchesNameAndType(endpoint, client->service_name,
                                          client->type_name);
      }));
}

size_t CountCachedServiceAvailabilityEndpoints(const ClientData *client) {
  const auto endpoints = GetGraphCacheSnapshot(false);
  return CountServiceAvailabilityEndpointsInList(endpoints, client);
}

bool QosProfilesCompatible(const rmw_qos_profile_t &offered,
                           const rmw_qos_profile_t &requested) {
  rmw_qos_compatibility_type_t compatibility = RMW_QOS_COMPATIBILITY_OK;
  if (rmw_dds_common::qos_profile_check_compatible(
          offered, requested, &compatibility, nullptr, 0) != RMW_RET_OK) {
    return false;
  }
  return compatibility != RMW_QOS_COMPATIBILITY_ERROR;
}

size_t CountCompatibleGraphPublishersForSubscription(
    const std::vector<ipc::EndpointDescriptor> &endpoints,
    const SubscriptionData &subscription) {
  return static_cast<size_t>(std::count_if(
      endpoints.begin(), endpoints.end(),
      [&subscription](const ipc::EndpointDescriptor &endpoint) {
        return EndpointMatchesDomain(
                   endpoint, DomainIdFromContext(subscription.context)) &&
               endpoint.kind == ipc::EndpointKind::kPublisher &&
               EndpointMatchesNameAndType(endpoint, subscription.topic_name,
                                          subscription.adapter.TypeName()) &&
               QosProfilesCompatible(endpoint.qos, subscription.actual_qos);
      }));
}

size_t CountCompatibleGraphSubscriptionsForPublisher(
    const std::vector<ipc::EndpointDescriptor> &endpoints,
    const PublisherData &publisher) {
  return static_cast<size_t>(std::count_if(
      endpoints.begin(), endpoints.end(),
      [&publisher](const ipc::EndpointDescriptor &endpoint) {
        return EndpointMatchesDomain(endpoint,
                                     DomainIdFromContext(publisher.context)) &&
               endpoint.kind == ipc::EndpointKind::kSubscription &&
               EndpointMatchesNameAndType(endpoint, publisher.topic_name,
                                          publisher.adapter.TypeName()) &&
               QosProfilesCompatible(publisher.actual_qos, endpoint.qos);
      }));
}

int32_t CountChange(size_t current_count, size_t previous_count) {
  const int64_t delta = static_cast<int64_t>(current_count) -
                        static_cast<int64_t>(previous_count);
  if (delta > std::numeric_limits<int32_t>::max()) {
    return std::numeric_limits<int32_t>::max();
  }
  if (delta < std::numeric_limits<int32_t>::min()) {
    return std::numeric_limits<int32_t>::min();
  }
  return static_cast<int32_t>(delta);
}

void FillBrokerMatchedStatus(size_t current_count, size_t *total_count,
                             size_t *last_total_count,
                             size_t *last_current_count,
                             rmw_matched_status_t *status) {
  if (total_count == nullptr || last_total_count == nullptr ||
      last_current_count == nullptr || status == nullptr) {
    return;
  }
  if (current_count > *last_current_count) {
    *total_count += current_count - *last_current_count;
  }
  status->total_count = *total_count;
  status->total_count_change = *total_count - *last_total_count;
  status->current_count = current_count;
  status->current_count_change =
      CountChange(current_count, *last_current_count);
  *last_total_count = *total_count;
  *last_current_count = current_count;
}

void AddGraphNameAndType(std::vector<NameAndTypes> *names_and_types,
                         const std::string &name, const std::string &type) {
  if (names_and_types == nullptr || name.empty() || type.empty()) {
    return;
  }
  auto it = std::find_if(
      names_and_types->begin(), names_and_types->end(),
      [&name](const NameAndTypes &entry) { return entry.name == name; });
  if (it == names_and_types->end()) {
    names_and_types->push_back(NameAndTypes{name, {type}});
    return;
  }
  if (std::find(it->types.begin(), it->types.end(), type) == it->types.end()) {
    it->types.push_back(type);
  }
}

bool EndpointBelongsToNode(const ipc::EndpointDescriptor &endpoint,
                           const char *node_name, const char *node_namespace) {
  return node_name != nullptr && node_namespace != nullptr &&
         endpoint.node_name == node_name &&
         endpoint.node_namespace == node_namespace;
}

std::vector<NameAndTypes> CollectGraphNamesAndTypesForKind(
    ipc::EndpointKind kind, const rmw_context_t *context,
    const char *node_name = nullptr, const char *node_namespace = nullptr) {
  std::vector<NameAndTypes> names_and_types;
  const auto endpoints = GetGraphCacheSnapshot();
  const uint32_t domain_id = DomainIdFromContext(context);
  for (const auto &endpoint : endpoints) {
    if (!EndpointMatchesDomain(endpoint, domain_id) || endpoint.kind != kind) {
      continue;
    }
    if ((node_name != nullptr || node_namespace != nullptr) &&
        !EndpointBelongsToNode(endpoint, node_name, node_namespace)) {
      continue;
    }
    AddGraphNameAndType(&names_and_types, endpoint.topic_name,
                        endpoint.type_name);
  }
  return names_and_types;
}

void AddGraphNode(std::vector<NodeGraphInfo> *nodes,
                  const ipc::EndpointDescriptor &endpoint) {
  if (nodes == nullptr || endpoint.node_name.empty()) {
    return;
  }
  const auto it = std::find_if(
      nodes->begin(), nodes->end(), [&endpoint](const NodeGraphInfo &node) {
        return node.node_name == endpoint.node_name &&
               node.node_namespace == endpoint.node_namespace &&
               node.enclave == endpoint.node_enclave;
      });
  if (it == nodes->end()) {
    nodes->push_back(NodeGraphInfo{endpoint.node_name, endpoint.node_namespace,
                                   endpoint.node_enclave});
  }
}

void FillGraphEndpointGid(const ipc::EndpointDescriptor &endpoint,
                          uint8_t discriminator, rmw_gid_t *gid) {
  if (gid == nullptr) {
    return;
  }
  *gid = {};
  gid->implementation_identifier = rmw_mdds_cpp_identifier;
  const uint64_t entity_id = endpoint.entity_id;
  std::memcpy(gid->data, &entity_id,
              std::min(sizeof(entity_id), sizeof(gid->data)));
  const size_t topic_size =
      std::min(endpoint.topic_name.size(), sizeof(gid->data));
  for (size_t i = 0; i < topic_size; ++i) {
    gid->data[i] ^= static_cast<uint8_t>(endpoint.topic_name[i]);
  }
  gid->data[sizeof(gid->data) - 1] ^= discriminator;
}

TopicEndpointInfo
MakeGraphTopicEndpointInfo(const ipc::EndpointDescriptor &endpoint,
                           rmw_endpoint_type_t endpoint_type,
                           uint8_t gid_discriminator) {
  TopicEndpointInfo info;
  info.node_name = endpoint.node_name;
  info.node_namespace = endpoint.node_namespace;
  info.topic_type = endpoint.type_name;
  info.topic_type_hash = endpoint.type_hash;
  info.endpoint_type = endpoint_type;
  info.qos_profile = endpoint.qos;
  FillGraphEndpointGid(endpoint, gid_discriminator, &info.gid);
  return info;
}

std::vector<TopicEndpointInfo>
CollectGraphEndpointInfosByTopic(ipc::EndpointKind kind, const char *topic_name,
                                 const rmw_context_t *context,
                                 rmw_endpoint_type_t endpoint_type,
                                 uint8_t gid_discriminator) {
  std::vector<TopicEndpointInfo> infos;
  if (topic_name == nullptr) {
    return infos;
  }
  const auto endpoints = GetGraphCacheSnapshot();
  const uint32_t domain_id = DomainIdFromContext(context);
  for (const auto &endpoint : endpoints) {
    if (EndpointMatchesDomain(endpoint, domain_id) && endpoint.kind == kind &&
        endpoint.topic_name == topic_name) {
      infos.push_back(MakeGraphTopicEndpointInfo(endpoint, endpoint_type,
                                                 gid_discriminator));
    }
  }
  return infos;
}

std::string FrameErrorMessage(const ipc::Frame &frame) {
  return std::string(frame.payload.begin(), frame.payload.end());
}

bool TryConnectBrokerSocket(const std::string &socket_path, ipc::UniqueFd *fd,
                            std::string *error) {
  if (fd == nullptr) {
    SetError(error, "broker client fd output is null");
    return false;
  }
  std::string connect_error;
  ipc::UniqueFd connected = ipc::ConnectUnixSocket(socket_path, &connect_error);
  if (connected) {
    *fd = std::move(connected);
    return true;
  }
  SetError(error, connect_error);
  return false;
}

std::string AutoBrokerStartLockPath(const std::string &socket_path) {
  return socket_path + ".autostart.lockdir";
}

class ScopedPathLock {
public:
  ScopedPathLock() = default;
  explicit ScopedPathLock(std::string path) : path_(std::move(path)) {}
  ~ScopedPathLock() { Reset(); }

  ScopedPathLock(const ScopedPathLock &) = delete;
  ScopedPathLock &operator=(const ScopedPathLock &) = delete;

  ScopedPathLock(ScopedPathLock &&other) noexcept
      : path_(std::move(other.path_)) {
    other.path_.clear();
  }

  ScopedPathLock &operator=(ScopedPathLock &&other) noexcept {
    if (this != &other) {
      Reset();
      path_ = std::move(other.path_);
      other.path_.clear();
    }
    return *this;
  }

  explicit operator bool() const { return !path_.empty(); }

private:
  void Reset() {
    if (!path_.empty()) {
      rmdir(path_.c_str());
      path_.clear();
    }
  }

  std::string path_;
};

ScopedPathLock AcquireAutoBrokerStartLock(const std::string &socket_path,
                                          std::string *error) {
  const std::string lock_path = AutoBrokerStartLockPath(socket_path);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    if (mkdir(lock_path.c_str(), 0755) == 0) {
      return ScopedPathLock(lock_path);
    }
    const int saved_errno = errno;
    if (saved_errno == EINTR) {
      continue;
    }
    if (saved_errno == EEXIST) {
      if (std::chrono::steady_clock::now() >= deadline) {
        if (rmdir(lock_path.c_str()) == 0) {
          deadline =
              std::chrono::steady_clock::now() + std::chrono::seconds(10);
          continue;
        }
        SetError(error, "timed out waiting for broker autostart lock");
        return ScopedPathLock();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    SetError(error, std::string("failed to acquire broker autostart lock: ") +
                        std::strerror(saved_errno));
    return ScopedPathLock();
  }
}

bool WaitForBrokerSocketReady(const std::string &socket_path,
                              std::chrono::milliseconds timeout,
                              std::string *error) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    ipc::UniqueFd ignored_fd;
    if (TryConnectBrokerSocket(socket_path, &ignored_fd, error)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

[[maybe_unused]] std::string DirectoryName(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return ".";
  }
  if (slash == 0u) {
    return "/";
  }
  return path.substr(0, slash);
}

bool IsExecutableFile(const std::string &path) {
  return !path.empty() && access(path.c_str(), X_OK) == 0;
}

std::string ResolveExternalBrokerExecutable() {
  const char *configured = std::getenv("RMW_MDDS_BROKER_EXECUTABLE");
  if (configured != nullptr && configured[0] != '\0') {
    return IsExecutableFile(configured) ? configured : std::string();
  }

#ifdef __OHOS__
  Dl_info info{};
  if (dladdr(reinterpret_cast<void *>(&ResolveExternalBrokerExecutable),
             &info) == 0 ||
      info.dli_fname == nullptr || info.dli_fname[0] == '\0') {
    return std::string();
  }
  const std::string library_dir = DirectoryName(info.dli_fname);
  const std::string installed_broker =
      library_dir + "/rmw_mdds_cpp/rmw_mdds_broker";
  if (IsExecutableFile(installed_broker)) {
    return installed_broker;
  }
  const std::string build_tree_broker = library_dir + "/rmw_mdds_broker";
  if (IsExecutableFile(build_tree_broker)) {
    return build_tree_broker;
  }
#endif
  return std::string();
}

void RedirectBrokerChildStdio() {
  const char *log_path = std::getenv("RMW_MDDS_BROKER_LOG");
  const int out_fd =
      (log_path != nullptr && log_path[0] != '\0')
          ? open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644)
          : open("/dev/null", O_WRONLY);
  if (out_fd >= 0) {
    dup2(out_fd, STDOUT_FILENO);
    dup2(out_fd, STDERR_FILENO);
    if (out_fd > STDERR_FILENO) {
      close(out_fd);
    }
  }
  const int in_fd = open("/dev/null", O_RDONLY);
  if (in_fd >= 0) {
    dup2(in_fd, STDIN_FILENO);
    if (in_fd > STDERR_FILENO) {
      close(in_fd);
    }
  }
}

void WriteExternalBrokerPid(pid_t pid) {
  const char *pid_path = std::getenv("RMW_MDDS_BROKER_PID_FILE");
  if (pid_path == nullptr || pid_path[0] == '\0') {
    return;
  }
  FILE *file = std::fopen(pid_path, "w");
  if (file == nullptr) {
    return;
  }
  std::fprintf(file, "%lld\n", static_cast<long long>(pid));
  std::fclose(file);
}

pid_t StartExternalBrokerProcess(const std::string &broker_path,
                                 const std::string &socket_path,
                                 std::string *error) {
  if (broker_path.empty()) {
    SetError(error, "external broker executable is not configured or executable");
    return -1;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    SetError(error, "failed to fork external broker process");
    return -1;
  }
  if (pid == 0) {
    setsid();
    RedirectBrokerChildStdio();
    execl(broker_path.c_str(), broker_path.c_str(), "--socket",
          socket_path.c_str(), static_cast<char *>(nullptr));
    _exit(127);
  }
  WriteExternalBrokerPid(pid);
  return pid;
}

std::string BrokerExitStatusMessage(int status) {
  if (WIFEXITED(status)) {
    return "external broker exited before socket became ready: exit=" +
           std::to_string(WEXITSTATUS(status));
  }
  if (WIFSIGNALED(status)) {
    return "external broker exited before socket became ready: signal=" +
           std::to_string(WTERMSIG(status));
  }
  return "external broker exited before socket became ready";
}

bool WaitForExternalBrokerReady(const std::string &socket_path, pid_t pid,
                                std::chrono::milliseconds timeout,
                                std::string *error) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    ipc::UniqueFd ignored_fd;
    if (TryConnectBrokerSocket(socket_path, &ignored_fd, error)) {
      return true;
    }
    int status = 0;
    const pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      SetError(error, BrokerExitStatusMessage(status));
      return false;
    }
    if (result < 0 && errno != ECHILD) {
      SetError(error, std::string("failed to wait for external broker: ") +
                          std::strerror(errno));
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  SetError(error, "timed out waiting for external broker socket");
  return false;
}

bool EnsureAutoBrokerStarted(const std::string &socket_path,
                             std::string *error) {
  {
    std::lock_guard<std::mutex> lock(g_auto_broker_mutex);
    if (g_auto_broker != nullptr && g_auto_broker->IsRunning()) {
      return true;
    }
  }

  ScopedPathLock autostart_lock =
      AcquireAutoBrokerStartLock(socket_path, error);
  if (!autostart_lock) {
    return false;
  }

  ipc::UniqueFd existing_fd;
  if (TryConnectBrokerSocket(socket_path, &existing_fd, nullptr)) {
    return true;
  }
  if (access(socket_path.c_str(), F_OK) == 0 &&
      WaitForBrokerSocketReady(socket_path, std::chrono::seconds(2), nullptr)) {
    return true;
  }

  {
    std::lock_guard<std::mutex> lock(g_auto_broker_mutex);
    if (g_auto_broker != nullptr && g_auto_broker->IsRunning()) {
      return true;
    }
    const std::string broker_path = ResolveExternalBrokerExecutable();
    if (!broker_path.empty()) {
      std::string external_error;
      const pid_t external_pid =
          StartExternalBrokerProcess(broker_path, socket_path, &external_error);
      if (external_pid <= 0) {
        SetError(error, external_error);
        return false;
      }
      return WaitForExternalBrokerReady(socket_path, external_pid,
                                        std::chrono::seconds(15), error);
    }

    auto broker = std::make_unique<ipc::IpcBroker>();
    std::string start_error;
    if (broker->Start(socket_path, &start_error)) {
      g_auto_broker = std::move(broker);
      return true;
    }
    SetError(error, start_error);
  }

  ipc::UniqueFd ignored_fd;
  return TryConnectBrokerSocket(socket_path, &ignored_fd, error);
}

bool ConnectBrokerSocketWithAutoStart(const std::string &socket_path,
                                      ipc::UniqueFd *fd, std::string *error) {
  if (TryConnectBrokerSocket(socket_path, fd, error)) {
    return true;
  }
  if (!EnsureAutoBrokerStarted(socket_path, error)) {
    return false;
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (TryConnectBrokerSocket(socket_path, fd, error)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

class IpcClient {
public:
  IpcClient() = default;

  ~IpcClient() { Stop(); }

  IpcClient(const IpcClient &) = delete;
  IpcClient &operator=(const IpcClient &) = delete;

  bool ConnectAndRegister(const ipc::EndpointDescriptor &endpoint,
                          std::string *error) {
    ipc::UniqueFd fd;
    if (!ConnectBrokerSocketWithAutoStart(BrokerSocketPath(), &fd, error)) {
      return false;
    }

    ipc::MessageKind kind = ipc::MessageKind::kError;
    if (!RegistrationKindForEndpoint(endpoint.kind, &kind, error)) {
      return false;
    }
    if (!ipc::WriteFrame(fd.get(),
                         ipc::Frame{kind, kRegisterRequestId,
                                    ipc::EncodeEndpointDescriptor(endpoint)},
                         error)) {
      return false;
    }

    // The broker may interleave kGraphUpdate frames (triggered by another
    // endpoint's matched-count change) ahead of the registration ACK. Apply
    // those to the graph cache and keep waiting for the ACK rather than
    // treating an early graph update as an unexpected response.
    ipc::Frame ack;
    for (;;) {
      const ipc::ReadFrameStatus status = ipc::ReadFrame(fd.get(), &ack, error);
      if (status != ipc::ReadFrameStatus::kOk) {
        return false;
      }
      if (ack.kind == ipc::MessageKind::kGraphUpdate) {
        ipc::GraphUpdateMessage update;
        std::string decode_error;
        if (ipc::DecodeGraphUpdate(ack.payload.data(), ack.payload.size(),
                                   &update, &decode_error)) {
          UpdateGraphCache(std::move(update));
        }
        continue;
      }
      if (ack.kind == ipc::MessageKind::kDeliverSample) {
        pending_frames_.push_back(std::move(ack));
        continue;
      }
      break;
    }
    if (ack.kind == ipc::MessageKind::kError) {
      SetError(error, "broker registration failed: " + FrameErrorMessage(ack));
      return false;
    }
    if (ack.kind != ipc::MessageKind::kAck ||
        ack.request_id != kRegisterRequestId) {
      SetError(error, "broker registration returned an unexpected response");
      return false;
    }

    fd_ = std::move(fd);
    entity_id_ = endpoint.entity_id;
    return true;
  }

  void StartReader(SubscriptionData *subscription) {
    subscription_ = subscription;
    StartReaderThread();
  }

  void StartReader(BrokerDeliveryCallback callback, void *user_data) {
    delivery_callback_ = callback;
    delivery_user_data_ = user_data;
    StartReaderThread();
  }

  void StartReaderThread() {
    running_.store(true);
    reader_thread_ = std::thread(&IpcClient::ReaderLoop, this);
  }

  void Stop() {
    bool clear_graph_after_reader_stops = false;
    if (entity_id_ != 0u) {
      if (GraphDebugEnabled()) {
        std::fprintf(stderr, "[rmw_mdds_graph] client stop entity=%llu\n",
                     static_cast<unsigned long long>(entity_id_));
      }
      entity_id_ = 0u;
      clear_graph_after_reader_stops = true;
    }
    running_.store(false);
    {
      std::lock_guard<std::mutex> lock(write_mutex_);
      if (fd_.get() >= 0) {
        shutdown(fd_.get(), SHUT_RDWR);
        fd_.reset();
      }
    }
    if (reader_thread_.joinable()) {
      reader_thread_.join();
    }
    if (clear_graph_after_reader_stops) {
      ClearGraphCache();
    }
  }

  bool Publish(const std::vector<uint8_t> &payload, uint64_t sequence_number,
               bool mdds_payload, std::string *error) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_.get() < 0) {
      SetError(error, "broker client socket is not connected");
      return false;
    }
    ipc::SampleMessage sample;
    sample.entity_id = entity_id_;
    sample.sequence_number = sequence_number;
    sample.mdds_payload = mdds_payload;
    sample.payload = payload;
    ipc::Frame frame;
    frame.kind = ipc::MessageKind::kPublishSample;
    frame.request_id = next_request_id_++;
    frame.payload = ipc::EncodeSampleMessage(sample);
    return ipc::WriteFrame(fd_.get(), frame, error);
  }

private:
  void HandleIncomingFrame(ipc::Frame frame) {
    std::string error;
    if (frame.kind == ipc::MessageKind::kGraphUpdate) {
      ipc::GraphUpdateMessage update;
      if (ipc::DecodeGraphUpdate(frame.payload.data(), frame.payload.size(),
                                 &update, &error)) {
        UpdateGraphCache(std::move(update));
      }
      return;
    }
    if (frame.kind != ipc::MessageKind::kDeliverSample) {
      return;
    }
    ipc::SampleMessage sample;
    if (!ipc::DecodeSampleMessage(frame.payload.data(), frame.payload.size(),
                                  &sample, &error)) {
      return;
    }

    if (subscription_ != nullptr) {
      QueuedSample queued_sample;
      queued_sample.payload = std::move(sample.payload);
      queued_sample.info = rmw_get_zero_initialized_message_info();
      queued_sample.info.publisher_gid.implementation_identifier =
          rmw_mdds_cpp_identifier;
      queued_sample.info.source_timestamp = NowNanoseconds();
      queued_sample.info.publication_sequence_number = sample.sequence_number;
      queued_sample.info.from_intra_process = false;
      queued_sample.from_bridge = sample.mdds_payload;
      EnqueueSample(subscription_, queued_sample);
      return;
    }
    if (delivery_callback_ != nullptr) {
      delivery_callback_(sample.payload, delivery_user_data_);
    }
  }

  void ReaderLoop() {
    for (auto &frame : pending_frames_) {
      if (!running_.load()) {
        break;
      }
      HandleIncomingFrame(std::move(frame));
    }
    pending_frames_.clear();
    while (running_.load()) {
      ipc::Frame frame;
      std::string error;
      const ipc::ReadFrameStatus status =
          ipc::ReadFrame(fd_.get(), &frame, &error);
      if (status != ipc::ReadFrameStatus::kOk) {
        break;
      }
      HandleIncomingFrame(std::move(frame));
    }
  }

  ipc::UniqueFd fd_;
  std::mutex write_mutex_;
  std::thread reader_thread_;
  std::atomic<bool> running_{false};
  uint64_t entity_id_ = 0u;
  uint64_t next_request_id_ = 2u;
  std::vector<ipc::Frame> pending_frames_;
  SubscriptionData *subscription_ = nullptr;
  BrokerDeliveryCallback delivery_callback_ = nullptr;
  void *delivery_user_data_ = nullptr;
};
} // namespace

void NoteContextInitialized() {
  std::lock_guard<std::mutex> lock(g_auto_broker_mutex);
  ++g_active_context_count;
}

void ShutdownEmbeddedBrokerIfLastContext() {
  bool shutdown_bridge = false;
  {
    std::lock_guard<std::mutex> lock(g_auto_broker_mutex);
    // Defensive: never underflow if fini is somehow called more times than
    // init.
    if (g_active_context_count > 0) {
      --g_active_context_count;
    }
    if (g_active_context_count != 0) {
      // Other contexts still share the embedded broker + bridge — leave them
      // up.
      return;
    }
    // Last context in this process. Stop the auto-started broker first, under
    // the lock so a concurrent EnsureAutoBrokerStarted cannot observe a
    // half-stopped broker: IpcBroker::Stop() unsubscribes node-sync and
    // destroys all bridge endpoints (so nothing new is queued). The MDDS
    // runtime (which the endpoints rely on) is torn down afterwards, below.
    if (g_auto_broker != nullptr) {
      g_auto_broker->Stop();
      g_auto_broker.reset();
    }
    ClearGraphCache();
    shutdown_bridge = true;
  }
  // Bridge runtime teardown lives on the separate BridgeBackend singleton, so
  // do it outside the broker lock. This joins the MDDS spin + lane-worker
  // threads before the process's atexit phase reaches openssl (the SIGSEGV this
  // whole path prevents). No-op when this process never loaded the bridge.
  if (shutdown_bridge) {
    BridgeBackend::Instance().Shutdown();
  }
}

bool BrokerModeEnabled() {
  const char *broker_mode = std::getenv("RMW_MDDS_BROKER");
  if (EnvValueConfigured(broker_mode)) {
    return EnvValueEnabled(broker_mode) && !EnvValueDisabled(broker_mode);
  }
  return !EnvValueConfigured(std::getenv("RMW_MDDS_BRIDGE_LIBRARY"));
}

bool BrokerBridgePayloadEnabled() {
  if (!BrokerModeEnabled() ||
      EnvValueDisabled(std::getenv("RMW_MDDS_BRIDGE"))) {
    return false;
  }
  // When a broker client is explicitly pointed at a bridge library, encode and
  // flag topic samples for the broker-owned bridge transport without requiring
  // this client process to initialize the DSoftBus bridge itself.
  if (EnvValueConfigured(std::getenv("RMW_MDDS_BRIDGE_LIBRARY"))) {
    return true;
  }
  return BridgeBackend::Instance().Available();
}

std::string BrokerSocketPath() {
  const char *socket_path = std::getenv("RMW_MDDS_BROKER_SOCKET");
  if (socket_path != nullptr && socket_path[0] != '\0') {
    return socket_path;
  }
#ifdef __OHOS__
  return "/data/local/tmp/rmw_mdds_cpp.sock";
#else
  return "/tmp/rmw_mdds_cpp.sock";
#endif
}

bool BrokerGraphHasMatchingService(const ClientData *client) {
  if (client == nullptr || client->context == nullptr) {
    return false;
  }
  return CountCachedServiceAvailabilityEndpoints(client) != 0u;
}

size_t CountBrokerGraphPublishersByTopic(const rmw_context_t *context,
                                         const char *topic_name) {
  if (topic_name == nullptr) {
    return 0u;
  }
  return CountGraphEndpoints(context, ipc::EndpointKind::kPublisher,
                             topic_name);
}

size_t CountBrokerGraphSubscriptionsByTopic(const rmw_context_t *context,
                                            const char *topic_name) {
  if (topic_name == nullptr) {
    return 0u;
  }
  return CountGraphEndpoints(context, ipc::EndpointKind::kSubscription,
                             topic_name);
}

size_t CountBrokerGraphPublishersForSubscription(
    const SubscriptionData *subscription) {
  if (subscription == nullptr) {
    return 0u;
  }
  return CountCompatibleGraphPublishersForSubscription(GetGraphCacheSnapshot(),
                                                       *subscription);
}

size_t
CountBrokerGraphSubscriptionsForPublisher(const PublisherData *publisher) {
  if (publisher == nullptr) {
    return 0u;
  }
  return CountCompatibleGraphSubscriptionsForPublisher(GetGraphCacheSnapshot(),
                                                       *publisher);
}

bool HasUnreadBrokerGraphPublisherMatchedStatus(PublisherData *publisher) {
  if (publisher == nullptr) {
    return false;
  }
  const size_t current_count =
      CountBrokerGraphSubscriptionsForPublisher(publisher);
  std::lock_guard<std::mutex> lock(publisher->mutex);
  return current_count != publisher->broker_matched_last_current_count;
}

bool HasUnreadBrokerGraphSubscriptionMatchedStatus(
    SubscriptionData *subscription) {
  if (subscription == nullptr) {
    return false;
  }
  const size_t current_count =
      CountBrokerGraphPublishersForSubscription(subscription);
  std::lock_guard<std::mutex> lock(subscription->mutex);
  return current_count != subscription->broker_matched_last_current_count;
}

bool TakeBrokerGraphPublisherMatchedStatus(PublisherData *publisher,
                                           rmw_matched_status_t *status) {
  if (publisher == nullptr || status == nullptr) {
    return false;
  }
  const size_t current_count =
      CountBrokerGraphSubscriptionsForPublisher(publisher);
  std::lock_guard<std::mutex> lock(publisher->mutex);
  FillBrokerMatchedStatus(current_count, &publisher->broker_matched_total_count,
                          &publisher->broker_matched_last_total_count,
                          &publisher->broker_matched_last_current_count,
                          status);
  return true;
}

bool TakeBrokerGraphSubscriptionMatchedStatus(SubscriptionData *subscription,
                                              rmw_matched_status_t *status) {
  if (subscription == nullptr || status == nullptr) {
    return false;
  }
  const size_t current_count =
      CountBrokerGraphPublishersForSubscription(subscription);
  std::lock_guard<std::mutex> lock(subscription->mutex);
  FillBrokerMatchedStatus(
      current_count, &subscription->broker_matched_total_count,
      &subscription->broker_matched_last_total_count,
      &subscription->broker_matched_last_current_count, status);
  return true;
}

size_t CountBrokerGraphClientsByName(const rmw_context_t *context,
                                     const char *service_name) {
  if (service_name == nullptr) {
    return 0u;
  }
  return CountGraphEndpoints(context, ipc::EndpointKind::kClient, service_name);
}

size_t CountBrokerGraphServicesByName(const rmw_context_t *context,
                                      const char *service_name) {
  if (service_name == nullptr) {
    return 0u;
  }
  return CountGraphEndpoints(context, ipc::EndpointKind::kService,
                             service_name);
}

std::vector<NameAndTypes>
GetBrokerGraphTopicNamesAndTypes(const rmw_context_t *context) {
  std::vector<NameAndTypes> names_and_types;
  const auto endpoints = GetGraphCacheSnapshot();
  const uint32_t domain_id = DomainIdFromContext(context);
  for (const auto &endpoint : endpoints) {
    if (EndpointMatchesDomain(endpoint, domain_id) &&
        (endpoint.kind == ipc::EndpointKind::kPublisher ||
         endpoint.kind == ipc::EndpointKind::kSubscription)) {
      AddGraphNameAndType(&names_and_types, endpoint.topic_name,
                          endpoint.type_name);
    }
  }
  return names_and_types;
}

std::vector<NameAndTypes>
GetBrokerGraphPublisherNamesAndTypesByNode(const rmw_context_t *context,
                                           const char *node_name,
                                           const char *node_namespace) {
  return CollectGraphNamesAndTypesForKind(ipc::EndpointKind::kPublisher,
                                          context, node_name, node_namespace);
}

std::vector<NameAndTypes>
GetBrokerGraphSubscriptionNamesAndTypesByNode(const rmw_context_t *context,
                                              const char *node_name,
                                              const char *node_namespace) {
  return CollectGraphNamesAndTypesForKind(ipc::EndpointKind::kSubscription,
                                          context, node_name, node_namespace);
}

std::vector<NameAndTypes>
GetBrokerGraphServiceNamesAndTypes(const rmw_context_t *context) {
  return CollectGraphNamesAndTypesForKind(ipc::EndpointKind::kService, context);
}

std::vector<NameAndTypes>
GetBrokerGraphServiceNamesAndTypesByNode(const rmw_context_t *context,
                                         const char *node_name,
                                         const char *node_namespace) {
  return CollectGraphNamesAndTypesForKind(ipc::EndpointKind::kService, context,
                                          node_name, node_namespace);
}

std::vector<NameAndTypes>
GetBrokerGraphClientNamesAndTypesByNode(const rmw_context_t *context,
                                        const char *node_name,
                                        const char *node_namespace) {
  return CollectGraphNamesAndTypesForKind(ipc::EndpointKind::kClient, context,
                                          node_name, node_namespace);
}

std::vector<NodeGraphInfo> GetBrokerGraphNodes(const rmw_context_t *context) {
  std::vector<NodeGraphInfo> nodes;
  const auto endpoints = GetGraphCacheSnapshot();
  const uint32_t domain_id = DomainIdFromContext(context);
  for (const auto &endpoint : endpoints) {
    if (EndpointMatchesDomain(endpoint, domain_id)) {
      AddGraphNode(&nodes, endpoint);
    }
  }
  return nodes;
}

std::vector<TopicEndpointInfo>
GetBrokerGraphPublisherEndpointInfosByTopic(const rmw_context_t *context,
                                            const char *topic_name) {
  return CollectGraphEndpointInfosByTopic(ipc::EndpointKind::kPublisher,
                                          topic_name, context,
                                          RMW_ENDPOINT_PUBLISHER, 0);
}

std::vector<TopicEndpointInfo>
GetBrokerGraphSubscriptionEndpointInfosByTopic(const rmw_context_t *context,
                                               const char *topic_name) {
  return CollectGraphEndpointInfosByTopic(ipc::EndpointKind::kSubscription,
                                          topic_name, context,
                                          RMW_ENDPOINT_SUBSCRIPTION, 0x5a);
}

void *CreatePublisherBrokerClient(PublisherData *publisher,
                                  std::string *error) {
  if (publisher == nullptr) {
    SetError(error, "publisher data is null");
    return nullptr;
  }
  auto client = std::make_unique<IpcClient>();
  if (!client->ConnectAndRegister(MakePublisherEndpoint(publisher), error)) {
    return nullptr;
  }
  client->StartReaderThread();
  return client.release();
}

void *CreateSubscriptionBrokerClient(SubscriptionData *subscription,
                                     std::string *error) {
  if (subscription == nullptr) {
    SetError(error, "subscription data is null");
    return nullptr;
  }
  auto client = std::make_unique<IpcClient>();
  if (!client->ConnectAndRegister(MakeSubscriptionEndpoint(subscription),
                                  error)) {
    return nullptr;
  }
  client->StartReader(subscription);
  return client.release();
}

void *CreateClientBrokerClient(ClientData *client_data,
                               BrokerDeliveryCallback callback, void *user_data,
                               std::string *error) {
  if (client_data == nullptr) {
    SetError(error, "client data is null");
    return nullptr;
  }
  if (callback == nullptr) {
    SetError(error, "client broker delivery callback is null");
    return nullptr;
  }
  auto client = std::make_unique<IpcClient>();
  if (!client->ConnectAndRegister(MakeClientEndpoint(client_data), error)) {
    return nullptr;
  }
  client->StartReader(callback, user_data);
  return client.release();
}

void *CreateServiceBrokerClient(ServiceData *service,
                                BrokerDeliveryCallback callback,
                                void *user_data, std::string *error) {
  if (service == nullptr) {
    SetError(error, "service data is null");
    return nullptr;
  }
  if (callback == nullptr) {
    SetError(error, "service broker delivery callback is null");
    return nullptr;
  }
  auto client = std::make_unique<IpcClient>();
  if (!client->ConnectAndRegister(MakeServiceEndpoint(service), error)) {
    return nullptr;
  }
  client->StartReader(callback, user_data);
  return client.release();
}

void DestroyBrokerClient(void *client) {
  delete static_cast<IpcClient *>(client);
}

bool BrokerClientPublish(void *client, const std::vector<uint8_t> &payload,
                         uint64_t sequence_number, std::string *error,
                         bool mdds_payload) {
  auto *ipc_client = static_cast<IpcClient *>(client);
  if (ipc_client == nullptr) {
    SetError(error, "broker client is null");
    return false;
  }
  return ipc_client->Publish(payload, sequence_number, mdds_payload, error);
}

} // namespace rmw_mdds_cpp
