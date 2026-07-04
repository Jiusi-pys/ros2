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

#include <sys/socket.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <unistd.h>

#include "bridge_backend.hpp"
#include "broker.hpp"
#include "context.hpp"
#include "ipc_broker.hpp"
#include "ipc_protocol.hpp"
#include "ipc_transport.hpp"
#include "rmw_dds_common/qos.hpp"
#include "rmw_mdds_cpp/identifier.hpp"

namespace rmw_mdds_cpp
{
namespace
{
constexpr uint64_t kRegisterRequestId = 1u;
std::mutex g_graph_mutex;
std::vector<ipc::EndpointDescriptor> g_graph_endpoints;
std::mutex g_auto_broker_mutex;
std::unique_ptr<ipc::IpcBroker> g_auto_broker;
// Process-wide count of live rmw contexts, guarded by g_auto_broker_mutex. The
// embedded broker + MDDS bridge are process singletons shared across every
// context, so they may only be torn down once the LAST context is fini'd —
// tearing them down on an earlier context's fini would break the others.
int g_active_context_count = 0;

void SetError(std::string * error, const std::string & message)
{
  if (error != nullptr) {
    *error = message;
  }
}

uint64_t NowNanoseconds()
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

bool EnvValueEnabled(const char * value)
{
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
         std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "on") == 0 ||
         std::strcmp(value, "ON") == 0 || std::strcmp(value, "yes") == 0 ||
         std::strcmp(value, "YES") == 0;
}

bool EnvValueDisabled(const char * value)
{
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
         std::strcmp(value, "FALSE") == 0 || std::strcmp(value, "off") == 0 ||
         std::strcmp(value, "OFF") == 0 || std::strcmp(value, "no") == 0 ||
         std::strcmp(value, "NO") == 0;
}

bool EnvValueConfigured(const char * value)
{
  return value != nullptr && value[0] != '\0';
}

uint64_t EntityIdFromPointer(const void * ptr)
{
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
}

uint64_t LocalContextId(const rmw_context_t * context)
{
  return (static_cast<uint64_t>(getpid()) << 32u) ^ EntityIdFromPointer(context);
}

ipc::EndpointDescriptor MakePublisherEndpoint(PublisherData * publisher)
{
  ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = EntityIdFromPointer(publisher);
  endpoint.local_context_id = LocalContextId(publisher->context);
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

ipc::EndpointDescriptor MakeSubscriptionEndpoint(SubscriptionData * subscription)
{
  ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = EntityIdFromPointer(subscription);
  endpoint.local_context_id = LocalContextId(subscription->context);
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

ipc::EndpointDescriptor MakeClientEndpoint(ClientData * client)
{
  ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = EntityIdFromPointer(client);
  endpoint.local_context_id = LocalContextId(client->context);
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

ipc::EndpointDescriptor MakeServiceEndpoint(ServiceData * service)
{
  ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = EntityIdFromPointer(service);
  endpoint.local_context_id = LocalContextId(service->context);
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

bool RegistrationKindForEndpoint(
  ipc::EndpointKind endpoint_kind, ipc::MessageKind * message_kind, std::string * error)
{
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

void UpdateGraphCache(std::vector<ipc::EndpointDescriptor> endpoints)
{
  std::lock_guard<std::mutex> lock(g_graph_mutex);
  g_graph_endpoints = std::move(endpoints);
}

void ClearGraphCache()
{
  std::lock_guard<std::mutex> lock(g_graph_mutex);
  g_graph_endpoints.clear();
}

std::vector<ipc::EndpointDescriptor> GetGraphCacheSnapshot()
{
  std::lock_guard<std::mutex> lock(g_graph_mutex);
  return g_graph_endpoints;
}

bool EndpointMatchesNameAndType(
  const ipc::EndpointDescriptor & endpoint, const std::string & name, const std::string & type)
{
  return endpoint.topic_name == name && (endpoint.type_name.empty() || type.empty() ||
                                         endpoint.type_name == type);
}

size_t CountGraphEndpoints(
  ipc::EndpointKind kind, const std::string & name, const std::string & type = {})
{
  const auto endpoints = GetGraphCacheSnapshot();
  return static_cast<size_t>(std::count_if(
    endpoints.begin(), endpoints.end(), [kind, &name, &type](const ipc::EndpointDescriptor & endpoint) {
      return endpoint.kind == kind && EndpointMatchesNameAndType(endpoint, name, type);
    }));
}

bool QosProfilesCompatible(
  const rmw_qos_profile_t & offered, const rmw_qos_profile_t & requested)
{
  rmw_qos_compatibility_type_t compatibility = RMW_QOS_COMPATIBILITY_OK;
  if (
    rmw_dds_common::qos_profile_check_compatible(
      offered, requested, &compatibility, nullptr, 0) != RMW_RET_OK) {
    return false;
  }
  return compatibility != RMW_QOS_COMPATIBILITY_ERROR;
}

size_t CountCompatibleGraphPublishersForSubscription(
  const std::vector<ipc::EndpointDescriptor> & endpoints,
  const SubscriptionData & subscription)
{
  return static_cast<size_t>(std::count_if(
    endpoints.begin(), endpoints.end(),
    [&subscription](const ipc::EndpointDescriptor & endpoint) {
      return endpoint.kind == ipc::EndpointKind::kPublisher &&
             EndpointMatchesNameAndType(
               endpoint, subscription.topic_name, subscription.adapter.TypeName()) &&
             QosProfilesCompatible(endpoint.qos, subscription.actual_qos);
    }));
}

size_t CountCompatibleGraphSubscriptionsForPublisher(
  const std::vector<ipc::EndpointDescriptor> & endpoints,
  const PublisherData & publisher)
{
  return static_cast<size_t>(std::count_if(
    endpoints.begin(), endpoints.end(),
    [&publisher](const ipc::EndpointDescriptor & endpoint) {
      return endpoint.kind == ipc::EndpointKind::kSubscription &&
             EndpointMatchesNameAndType(
               endpoint, publisher.topic_name, publisher.adapter.TypeName()) &&
             QosProfilesCompatible(publisher.actual_qos, endpoint.qos);
    }));
}

int32_t CountChange(size_t current_count, size_t previous_count)
{
  const int64_t delta =
    static_cast<int64_t>(current_count) - static_cast<int64_t>(previous_count);
  if (delta > std::numeric_limits<int32_t>::max()) {
    return std::numeric_limits<int32_t>::max();
  }
  if (delta < std::numeric_limits<int32_t>::min()) {
    return std::numeric_limits<int32_t>::min();
  }
  return static_cast<int32_t>(delta);
}

void FillBrokerMatchedStatus(
  size_t current_count, size_t * total_count, size_t * last_total_count,
  size_t * last_current_count, rmw_matched_status_t * status)
{
  if (
    total_count == nullptr || last_total_count == nullptr || last_current_count == nullptr ||
    status == nullptr) {
    return;
  }
  if (current_count > *last_current_count) {
    *total_count += current_count - *last_current_count;
  }
  status->total_count = *total_count;
  status->total_count_change = *total_count - *last_total_count;
  status->current_count = current_count;
  status->current_count_change = CountChange(current_count, *last_current_count);
  *last_total_count = *total_count;
  *last_current_count = current_count;
}

void AddGraphNameAndType(
  std::vector<NameAndTypes> * names_and_types, const std::string & name,
  const std::string & type)
{
  if (names_and_types == nullptr || name.empty() || type.empty()) {
    return;
  }
  auto it = std::find_if(
    names_and_types->begin(), names_and_types->end(),
    [&name](const NameAndTypes & entry) { return entry.name == name; });
  if (it == names_and_types->end()) {
    names_and_types->push_back(NameAndTypes{name, {type}});
    return;
  }
  if (std::find(it->types.begin(), it->types.end(), type) == it->types.end()) {
    it->types.push_back(type);
  }
}

bool EndpointBelongsToNode(
  const ipc::EndpointDescriptor & endpoint, const char * node_name,
  const char * node_namespace)
{
  return node_name != nullptr && node_namespace != nullptr && endpoint.node_name == node_name &&
         endpoint.node_namespace == node_namespace;
}

std::vector<NameAndTypes> CollectGraphNamesAndTypesForKind(
  ipc::EndpointKind kind, const char * node_name = nullptr, const char * node_namespace = nullptr)
{
  std::vector<NameAndTypes> names_and_types;
  const auto endpoints = GetGraphCacheSnapshot();
  for (const auto & endpoint : endpoints) {
    if (endpoint.kind != kind) {
      continue;
    }
    if (
      (node_name != nullptr || node_namespace != nullptr) &&
      !EndpointBelongsToNode(endpoint, node_name, node_namespace)) {
      continue;
    }
    AddGraphNameAndType(&names_and_types, endpoint.topic_name, endpoint.type_name);
  }
  return names_and_types;
}

void AddGraphNode(std::vector<NodeGraphInfo> * nodes, const ipc::EndpointDescriptor & endpoint)
{
  if (nodes == nullptr || endpoint.node_name.empty()) {
    return;
  }
  const auto it = std::find_if(
    nodes->begin(), nodes->end(), [&endpoint](const NodeGraphInfo & node) {
      return node.node_name == endpoint.node_name &&
             node.node_namespace == endpoint.node_namespace &&
             node.enclave == endpoint.node_enclave;
    });
  if (it == nodes->end()) {
    nodes->push_back(
      NodeGraphInfo{endpoint.node_name, endpoint.node_namespace, endpoint.node_enclave});
  }
}

void FillGraphEndpointGid(
  const ipc::EndpointDescriptor & endpoint, uint8_t discriminator, rmw_gid_t * gid)
{
  if (gid == nullptr) {
    return;
  }
  *gid = {};
  gid->implementation_identifier = rmw_mdds_cpp_identifier;
  const uint64_t entity_id = endpoint.entity_id;
  std::memcpy(gid->data, &entity_id, std::min(sizeof(entity_id), sizeof(gid->data)));
  const size_t topic_size = std::min(endpoint.topic_name.size(), sizeof(gid->data));
  for (size_t i = 0; i < topic_size; ++i) {
    gid->data[i] ^= static_cast<uint8_t>(endpoint.topic_name[i]);
  }
  gid->data[sizeof(gid->data) - 1] ^= discriminator;
}

TopicEndpointInfo MakeGraphTopicEndpointInfo(
  const ipc::EndpointDescriptor & endpoint, rmw_endpoint_type_t endpoint_type,
  uint8_t gid_discriminator)
{
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

std::vector<TopicEndpointInfo> CollectGraphEndpointInfosByTopic(
  ipc::EndpointKind kind, const char * topic_name, rmw_endpoint_type_t endpoint_type,
  uint8_t gid_discriminator)
{
  std::vector<TopicEndpointInfo> infos;
  if (topic_name == nullptr) {
    return infos;
  }
  const auto endpoints = GetGraphCacheSnapshot();
  for (const auto & endpoint : endpoints) {
    if (endpoint.kind == kind && endpoint.topic_name == topic_name) {
      infos.push_back(MakeGraphTopicEndpointInfo(endpoint, endpoint_type, gid_discriminator));
    }
  }
  return infos;
}

std::string FrameErrorMessage(const ipc::Frame & frame)
{
  return std::string(frame.payload.begin(), frame.payload.end());
}

bool TryConnectBrokerSocket(const std::string & socket_path, ipc::UniqueFd * fd, std::string * error)
{
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

bool EnsureAutoBrokerStarted(const std::string & socket_path, std::string * error)
{
  {
    std::lock_guard<std::mutex> lock(g_auto_broker_mutex);
    if (g_auto_broker != nullptr && g_auto_broker->IsRunning()) {
      return true;
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

bool ConnectBrokerSocketWithAutoStart(
  const std::string & socket_path, ipc::UniqueFd * fd, std::string * error)
{
  if (TryConnectBrokerSocket(socket_path, fd, error)) {
    return true;
  }
  if (!EnsureAutoBrokerStarted(socket_path, error)) {
    return false;
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (TryConnectBrokerSocket(socket_path, fd, error)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

class IpcClient
{
public:
  IpcClient() = default;

  ~IpcClient()
  {
    Stop();
  }

  IpcClient(const IpcClient &) = delete;
  IpcClient & operator=(const IpcClient &) = delete;

  bool ConnectAndRegister(const ipc::EndpointDescriptor & endpoint, std::string * error)
  {
    ipc::UniqueFd fd;
    if (!ConnectBrokerSocketWithAutoStart(BrokerSocketPath(), &fd, error)) {
      return false;
    }

    ipc::MessageKind kind = ipc::MessageKind::kError;
    if (!RegistrationKindForEndpoint(endpoint.kind, &kind, error)) {
      return false;
    }
    if (!ipc::WriteFrame(
        fd.get(),
        ipc::Frame{kind, kRegisterRequestId, ipc::EncodeEndpointDescriptor(endpoint)}, error)) {
      return false;
    }

    // The broker may interleave kGraphUpdate frames (triggered by another
    // endpoint's matched-count change) ahead of the registration ACK. Apply
    // those to the graph cache and keep waiting for the ACK rather than treating
    // an early graph update as an unexpected response.
    ipc::Frame ack;
    for (;;) {
      const ipc::ReadFrameStatus status = ipc::ReadFrame(fd.get(), &ack, error);
      if (status != ipc::ReadFrameStatus::kOk) {
        return false;
      }
      if (ack.kind == ipc::MessageKind::kGraphUpdate) {
        std::vector<ipc::EndpointDescriptor> endpoints;
        std::string decode_error;
        if (ipc::DecodeEndpointList(ack.payload.data(), ack.payload.size(), &endpoints, &decode_error)) {
          UpdateGraphCache(std::move(endpoints));
        }
        continue;
      }
      break;
    }
    if (ack.kind == ipc::MessageKind::kError) {
      SetError(error, "broker registration failed: " + FrameErrorMessage(ack));
      return false;
    }
    if (ack.kind != ipc::MessageKind::kAck || ack.request_id != kRegisterRequestId) {
      SetError(error, "broker registration returned an unexpected response");
      return false;
    }

    fd_ = std::move(fd);
    entity_id_ = endpoint.entity_id;
    return true;
  }

  void StartReader(SubscriptionData * subscription)
  {
    subscription_ = subscription;
    StartReaderThread();
  }

  void StartReader(BrokerDeliveryCallback callback, void * user_data)
  {
    delivery_callback_ = callback;
    delivery_user_data_ = user_data;
    StartReaderThread();
  }

  void StartReaderThread()
  {
    running_.store(true);
    reader_thread_ = std::thread(&IpcClient::ReaderLoop, this);
  }

  void Stop()
  {
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
  }

  bool Publish(
    const std::vector<uint8_t> & payload, uint64_t sequence_number, bool mdds_payload,
    std::string * error)
  {
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
  void ReaderLoop()
  {
    while (running_.load()) {
      ipc::Frame frame;
      std::string error;
      const ipc::ReadFrameStatus status = ipc::ReadFrame(fd_.get(), &frame, &error);
      if (status != ipc::ReadFrameStatus::kOk) {
        break;
      }
      if (frame.kind == ipc::MessageKind::kGraphUpdate) {
        std::vector<ipc::EndpointDescriptor> endpoints;
        if (ipc::DecodeEndpointList(frame.payload.data(), frame.payload.size(), &endpoints, &error)) {
          UpdateGraphCache(std::move(endpoints));
        }
        continue;
      }
      if (frame.kind != ipc::MessageKind::kDeliverSample) {
        continue;
      }
      ipc::SampleMessage sample;
      if (!ipc::DecodeSampleMessage(frame.payload.data(), frame.payload.size(), &sample, &error)) {
        continue;
      }

      if (subscription_ != nullptr) {
        QueuedSample queued_sample;
        queued_sample.payload = std::move(sample.payload);
        queued_sample.info = rmw_get_zero_initialized_message_info();
        queued_sample.info.publisher_gid.implementation_identifier = rmw_mdds_cpp_identifier;
        queued_sample.info.source_timestamp = NowNanoseconds();
        queued_sample.info.publication_sequence_number = sample.sequence_number;
        queued_sample.info.from_intra_process = false;
        queued_sample.from_bridge = sample.mdds_payload;
        EnqueueSample(subscription_, queued_sample);
        continue;
      }
      if (delivery_callback_ != nullptr) {
        delivery_callback_(sample.payload, delivery_user_data_);
      }
    }
  }

  ipc::UniqueFd fd_;
  std::mutex write_mutex_;
  std::thread reader_thread_;
  std::atomic<bool> running_{false};
  uint64_t entity_id_ = 0u;
  uint64_t next_request_id_ = 2u;
  SubscriptionData * subscription_ = nullptr;
  BrokerDeliveryCallback delivery_callback_ = nullptr;
  void * delivery_user_data_ = nullptr;
};
}  // namespace

void NoteContextInitialized()
{
  std::lock_guard<std::mutex> lock(g_auto_broker_mutex);
  ++g_active_context_count;
}

void ShutdownEmbeddedBrokerIfLastContext()
{
  bool shutdown_bridge = false;
  {
    std::lock_guard<std::mutex> lock(g_auto_broker_mutex);
    // Defensive: never underflow if fini is somehow called more times than init.
    if (g_active_context_count > 0) {
      --g_active_context_count;
    }
    if (g_active_context_count != 0) {
      // Other contexts still share the embedded broker + bridge — leave them up.
      return;
    }
    // Last context in this process. Stop the auto-started broker first, under the
    // lock so a concurrent EnsureAutoBrokerStarted cannot observe a half-stopped
    // broker: IpcBroker::Stop() unsubscribes node-sync and destroys all bridge
    // endpoints (so nothing new is queued). The MDDS runtime (which the endpoints
    // rely on) is torn down afterwards, below.
    if (g_auto_broker != nullptr) {
      g_auto_broker->Stop();
      g_auto_broker.reset();
    }
    ClearGraphCache();
    shutdown_bridge = true;
  }
  // Bridge runtime teardown lives on the separate BridgeBackend singleton, so do
  // it outside the broker lock. This joins the MDDS spin + lane-worker threads
  // before the process's atexit phase reaches openssl (the SIGSEGV this whole
  // path prevents). No-op when this process never loaded the bridge.
  if (shutdown_bridge) {
    BridgeBackend::Instance().Shutdown();
  }
}

bool BrokerModeEnabled()
{
  const char * broker_mode = std::getenv("RMW_MDDS_BROKER");
  if (EnvValueConfigured(broker_mode)) {
    return EnvValueEnabled(broker_mode) && !EnvValueDisabled(broker_mode);
  }
  return !EnvValueConfigured(std::getenv("RMW_MDDS_BRIDGE_LIBRARY"));
}

bool BrokerBridgePayloadEnabled()
{
  if (!BrokerModeEnabled() || EnvValueDisabled(std::getenv("RMW_MDDS_BRIDGE"))) {
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

std::string BrokerSocketPath()
{
  const char * socket_path = std::getenv("RMW_MDDS_BROKER_SOCKET");
  if (socket_path != nullptr && socket_path[0] != '\0') {
    return socket_path;
  }
#ifdef __OHOS__
  return "/data/local/tmp/rmw_mdds_cpp.sock";
#else
  return "/tmp/rmw_mdds_cpp.sock";
#endif
}

bool BrokerGraphHasMatchingService(const ClientData * client)
{
  if (client == nullptr) {
    return false;
  }
  return CountGraphEndpoints(
           ipc::EndpointKind::kService, client->service_name, client->type_name) != 0u;
}

size_t CountBrokerGraphPublishersByTopic(const char * topic_name)
{
  if (topic_name == nullptr) {
    return 0u;
  }
  return CountGraphEndpoints(ipc::EndpointKind::kPublisher, topic_name);
}

size_t CountBrokerGraphSubscriptionsByTopic(const char * topic_name)
{
  if (topic_name == nullptr) {
    return 0u;
  }
  return CountGraphEndpoints(ipc::EndpointKind::kSubscription, topic_name);
}

size_t CountBrokerGraphPublishersForSubscription(const SubscriptionData * subscription)
{
  if (subscription == nullptr) {
    return 0u;
  }
  return CountCompatibleGraphPublishersForSubscription(
    GetGraphCacheSnapshot(), *subscription);
}

size_t CountBrokerGraphSubscriptionsForPublisher(const PublisherData * publisher)
{
  if (publisher == nullptr) {
    return 0u;
  }
  return CountCompatibleGraphSubscriptionsForPublisher(GetGraphCacheSnapshot(), *publisher);
}

bool HasUnreadBrokerGraphPublisherMatchedStatus(PublisherData * publisher)
{
  if (publisher == nullptr) {
    return false;
  }
  const size_t current_count = CountBrokerGraphSubscriptionsForPublisher(publisher);
  std::lock_guard<std::mutex> lock(publisher->mutex);
  return current_count != publisher->broker_matched_last_current_count;
}

bool HasUnreadBrokerGraphSubscriptionMatchedStatus(SubscriptionData * subscription)
{
  if (subscription == nullptr) {
    return false;
  }
  const size_t current_count = CountBrokerGraphPublishersForSubscription(subscription);
  std::lock_guard<std::mutex> lock(subscription->mutex);
  return current_count != subscription->broker_matched_last_current_count;
}

bool TakeBrokerGraphPublisherMatchedStatus(
  PublisherData * publisher, rmw_matched_status_t * status)
{
  if (publisher == nullptr || status == nullptr) {
    return false;
  }
  const size_t current_count = CountBrokerGraphSubscriptionsForPublisher(publisher);
  std::lock_guard<std::mutex> lock(publisher->mutex);
  FillBrokerMatchedStatus(
    current_count, &publisher->broker_matched_total_count,
    &publisher->broker_matched_last_total_count,
    &publisher->broker_matched_last_current_count, status);
  return true;
}

bool TakeBrokerGraphSubscriptionMatchedStatus(
  SubscriptionData * subscription, rmw_matched_status_t * status)
{
  if (subscription == nullptr || status == nullptr) {
    return false;
  }
  const size_t current_count = CountBrokerGraphPublishersForSubscription(subscription);
  std::lock_guard<std::mutex> lock(subscription->mutex);
  FillBrokerMatchedStatus(
    current_count, &subscription->broker_matched_total_count,
    &subscription->broker_matched_last_total_count,
    &subscription->broker_matched_last_current_count, status);
  return true;
}

size_t CountBrokerGraphClientsByName(const char * service_name)
{
  if (service_name == nullptr) {
    return 0u;
  }
  return CountGraphEndpoints(ipc::EndpointKind::kClient, service_name);
}

size_t CountBrokerGraphServicesByName(const char * service_name)
{
  if (service_name == nullptr) {
    return 0u;
  }
  return CountGraphEndpoints(ipc::EndpointKind::kService, service_name);
}

std::vector<NameAndTypes> GetBrokerGraphTopicNamesAndTypes()
{
  std::vector<NameAndTypes> names_and_types;
  const auto endpoints = GetGraphCacheSnapshot();
  for (const auto & endpoint : endpoints) {
    if (
      endpoint.kind == ipc::EndpointKind::kPublisher ||
      endpoint.kind == ipc::EndpointKind::kSubscription) {
      AddGraphNameAndType(&names_and_types, endpoint.topic_name, endpoint.type_name);
    }
  }
  return names_and_types;
}

std::vector<NameAndTypes> GetBrokerGraphPublisherNamesAndTypesByNode(
  const char * node_name, const char * node_namespace)
{
  return CollectGraphNamesAndTypesForKind(
    ipc::EndpointKind::kPublisher, node_name, node_namespace);
}

std::vector<NameAndTypes> GetBrokerGraphSubscriptionNamesAndTypesByNode(
  const char * node_name, const char * node_namespace)
{
  return CollectGraphNamesAndTypesForKind(
    ipc::EndpointKind::kSubscription, node_name, node_namespace);
}

std::vector<NameAndTypes> GetBrokerGraphServiceNamesAndTypes()
{
  return CollectGraphNamesAndTypesForKind(ipc::EndpointKind::kService);
}

std::vector<NameAndTypes> GetBrokerGraphServiceNamesAndTypesByNode(
  const char * node_name, const char * node_namespace)
{
  return CollectGraphNamesAndTypesForKind(
    ipc::EndpointKind::kService, node_name, node_namespace);
}

std::vector<NameAndTypes> GetBrokerGraphClientNamesAndTypesByNode(
  const char * node_name, const char * node_namespace)
{
  return CollectGraphNamesAndTypesForKind(
    ipc::EndpointKind::kClient, node_name, node_namespace);
}

std::vector<NodeGraphInfo> GetBrokerGraphNodes()
{
  std::vector<NodeGraphInfo> nodes;
  const auto endpoints = GetGraphCacheSnapshot();
  for (const auto & endpoint : endpoints) {
    AddGraphNode(&nodes, endpoint);
  }
  return nodes;
}

std::vector<TopicEndpointInfo> GetBrokerGraphPublisherEndpointInfosByTopic(
  const char * topic_name)
{
  return CollectGraphEndpointInfosByTopic(
    ipc::EndpointKind::kPublisher, topic_name, RMW_ENDPOINT_PUBLISHER, 0);
}

std::vector<TopicEndpointInfo> GetBrokerGraphSubscriptionEndpointInfosByTopic(
  const char * topic_name)
{
  return CollectGraphEndpointInfosByTopic(
    ipc::EndpointKind::kSubscription, topic_name, RMW_ENDPOINT_SUBSCRIPTION, 0x5a);
}

void * CreatePublisherBrokerClient(PublisherData * publisher, std::string * error)
{
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

void * CreateSubscriptionBrokerClient(SubscriptionData * subscription, std::string * error)
{
  if (subscription == nullptr) {
    SetError(error, "subscription data is null");
    return nullptr;
  }
  auto client = std::make_unique<IpcClient>();
  if (!client->ConnectAndRegister(MakeSubscriptionEndpoint(subscription), error)) {
    return nullptr;
  }
  client->StartReader(subscription);
  return client.release();
}

void * CreateClientBrokerClient(
  ClientData * client_data, BrokerDeliveryCallback callback, void * user_data, std::string * error)
{
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

void * CreateServiceBrokerClient(
  ServiceData * service, BrokerDeliveryCallback callback, void * user_data, std::string * error)
{
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

void DestroyBrokerClient(void * client)
{
  delete static_cast<IpcClient *>(client);
}

bool BrokerClientPublish(
  void * client, const std::vector<uint8_t> & payload, uint64_t sequence_number,
  std::string * error, bool mdds_payload)
{
  auto * ipc_client = static_cast<IpcClient *>(client);
  if (ipc_client == nullptr) {
    SetError(error, "broker client is null");
    return false;
  }
  return ipc_client->Publish(payload, sequence_number, mdds_payload, error);
}

}  // namespace rmw_mdds_cpp
