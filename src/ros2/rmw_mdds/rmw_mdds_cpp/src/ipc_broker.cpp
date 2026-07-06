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

#include "ipc_broker.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <utility>

#include "bridge_backend.hpp"

namespace rmw_mdds_cpp
{
namespace ipc
{

struct IpcBroker::BridgeSubscriptionState
{
  IpcBroker * broker = nullptr;
  EndpointDescriptor endpoint;
};

struct IpcBroker::Connection
{
  explicit Connection(UniqueFd accepted_fd) : fd(std::move(accepted_fd)) {}

  struct BridgeEndpoint
  {
    EndpointDescriptor endpoint;
    void * bridge_publisher = nullptr;
    void * bridge_subscription = nullptr;
    std::unique_ptr<BridgeSubscriptionState> subscription_state;
  };

  UniqueFd fd;
  std::mutex write_mutex;
  std::thread thread;
  std::vector<EndpointDescriptor> endpoints;
  std::vector<BridgeEndpoint> bridge_endpoints;
  std::atomic<bool> active{true};
};

IpcBroker::IpcBroker() = default;

IpcBroker::~IpcBroker()
{
  Stop();
}

namespace
{
// --- cross-board graph-sync wire helpers ---------------------------------
// Payload = [broker_id:8 LE][epoch:8 LE] + EncodeEndpointList(local endpoints).
constexpr size_t kGraphSyncHeaderSize = 16u;
constexpr std::chrono::seconds kGraphPeerTtl{5};
constexpr std::chrono::seconds kLocalBridgeEchoTtl{5};

bool GraphDebugEnabled()
{
  const char * value = std::getenv("RMW_MDDS_GRAPH_DEBUG");
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void AppendU64Le(std::vector<uint8_t> & out, uint64_t value)
{
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFFu));
  }
}

uint64_t ReadU64Le(const uint8_t * p)
{
  uint64_t value = 0u;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<uint64_t>(p[i]) << (8 * i);
  }
  return value;
}

uint64_t MakeBrokerId()
{
  std::random_device rd;
  uint64_t value = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
  value ^= static_cast<uint64_t>(getpid()) << 16;
  value ^= static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  return value == 0u ? 1u : value;  // 0 is reserved (unset)
}
}  // namespace

bool IpcBroker::Start(const std::string & socket_path, std::string * error)
{
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    if (error != nullptr) {
      *error = "broker is already running";
    }
    return false;
  }

  UniqueFd listener = ListenUnixSocket(socket_path, error);
  if (!listener) {
    running_.store(false);
    return false;
  }

  // Default-on: enable the DSoftBus bridge data plane whenever the bridge library
  // is actually loadable (dlopen + MddsBridgeInit ok), not only when
  // RMW_MDDS_BRIDGE_LIBRARY is explicitly set. Available() returns false on hosts
  // without the library / softbus, so local-loopback still works; set
  // RMW_MDDS_BRIDGE=0 to force the bridge off.
  const bool bridge_enabled = BridgeBackend::Instance().Available();
  if (GraphDebugEnabled()) {
    std::fprintf(stderr, "[rmw_mdds_graph] broker start bridge_enabled=%d\n", bridge_enabled ? 1 : 0);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    socket_path_ = socket_path;
    listener_ = std::move(listener);
    bridge_enabled_ = bridge_enabled;
  }
  accept_thread_ = std::thread(&IpcBroker::AcceptLoop, this);
  if (bridge_enabled) {
    // Ingest the DDS-side node list a gateway publishes, so cross-board nodes
    // (and thus `ros2 param`/`ros2 lifecycle`/`ros2 node list`) resolve here.
    node_sync_subscription_ = BridgeBackend::Instance().Subscribe(
      "mdds_node_sync", "mdds_graph_NodeList", &rmw_qos_profile_default, NodeSyncBridgeCallback, this);

    // Cross-board graph introspection (every rmw_mdds broker participates, no
    // gateway required): subscribe to peers' endpoint-graph announcements and
    // publish our own. broker_id_ tags our frames so we drop our own echo.
    broker_id_ = MakeBrokerId();
    graph_sync_subscription_ = BridgeBackend::Instance().Subscribe(
      "mdds_graph_sync", "mdds_graph_EndpointList", &rmw_qos_profile_default,
      GraphSyncBridgeCallback, this);
    graph_sync_publisher_ = BridgeBackend::Instance().CreatePublisher(
      "mdds_graph_sync", "mdds_graph_EndpointList", &rmw_qos_profile_default);
    if (GraphDebugEnabled()) {
      std::fprintf(
        stderr,
        "[rmw_mdds_graph] graph sync init broker_id=%llu sub=%p pub=%p\n",
        static_cast<unsigned long long>(broker_id_), graph_sync_subscription_,
        graph_sync_publisher_);
    }
    // Periodic re-announce: DSoftBus pub/sub is not transient-local, so a board
    // that joins later would miss earlier graph frames. A low-rate heartbeat
    // (plus the immediate publish on every endpoint change) keeps peers current.
    graph_reannounce_thread_ = std::thread(
      [this]() {
        while (running_.load()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1500));
          if (!running_.load()) {
            break;
          }
          // Refresh local clients too. Bridge matched callbacks are the fast
          // path, but a late or missed callback must not leave wait_for_service
          // stuck behind a stale broker graph cache.
          BroadcastGraphUpdate();
          PublishLocalGraph();
        }
      });
  }
  return true;
}

void IpcBroker::Stop()
{
  if (!running_.exchange(false)) {
    return;
  }

  // Stop the graph re-announce heartbeat first; it is the only thread that calls
  // PublishLocalGraph() on a timer, so joining it here means no timer publish can
  // race the graph_sync_publisher_ teardown below.
  if (graph_reannounce_thread_.joinable()) {
    graph_reannounce_thread_.join();
  }
  if (node_sync_subscription_ != nullptr) {
    BridgeBackend::Instance().Unsubscribe(node_sync_subscription_);
    node_sync_subscription_ = nullptr;
  }
  if (graph_sync_subscription_ != nullptr) {
    BridgeBackend::Instance().Unsubscribe(graph_sync_subscription_);
    graph_sync_subscription_ = nullptr;
  }

  std::string socket_path;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    socket_path = socket_path_;
  }

  if (!socket_path.empty()) {
    std::string ignored_error;
    UniqueFd wake_fd = ConnectUnixSocket(socket_path, &ignored_error);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    listener_.reset();
    for (auto & connection : connections_) {
      if (connection->fd) {
        shutdown(connection->fd.get(), SHUT_RDWR);
      }
      connection->fd.reset();
      DestroyBridgeEndpoints(connection.get());
    }
  }

  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }

  std::vector<std::unique_ptr<Connection>> connections;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    connections.swap(connections_);
    socket_path_.clear();
    bridge_enabled_ = false;
  }
  for (auto & connection : connections) {
    if (connection->thread.joinable()) {
      connection->thread.join();
    }
  }

  // All broker threads (accept / connections / re-announce) are joined now, so no
  // one can touch the graph_sync publisher; safe to tear it down.
  if (graph_sync_publisher_ != nullptr) {
    BridgeBackend::Instance().DestroyPublisher(graph_sync_publisher_);
    graph_sync_publisher_ = nullptr;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    remote_graph_endpoints_.clear();
    broker_id_ = 0u;
  }

  if (!socket_path.empty()) {
    unlink(socket_path.c_str());
  }
}

bool IpcBroker::IsRunning() const
{
  return running_.load();
}

namespace
{
bool CanPublishFrom(EndpointKind kind)
{
  return kind == EndpointKind::kPublisher || kind == EndpointKind::kClient ||
         kind == EndpointKind::kService;
}

bool ReliabilityCompatible(
  rmw_qos_reliability_policy_t offered, rmw_qos_reliability_policy_t requested)
{
  return !(offered == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT &&
           requested == RMW_QOS_POLICY_RELIABILITY_RELIABLE);
}

bool DurabilityCompatible(
  rmw_qos_durability_policy_t offered, rmw_qos_durability_policy_t requested)
{
  return !(offered == RMW_QOS_POLICY_DURABILITY_VOLATILE &&
           requested == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);
}

bool LivelinessCompatible(
  rmw_qos_liveliness_policy_t offered, rmw_qos_liveliness_policy_t requested)
{
  return !(offered == RMW_QOS_POLICY_LIVELINESS_AUTOMATIC &&
           requested == RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC);
}

bool QosProfilesCompatible(
  const rmw_qos_profile_t & offered, const rmw_qos_profile_t & requested)
{
  return ReliabilityCompatible(offered.reliability, requested.reliability) &&
         DurabilityCompatible(offered.durability, requested.durability) &&
         LivelinessCompatible(offered.liveliness, requested.liveliness);
}

bool IsMatchingDeliveryTarget(
  const EndpointDescriptor & source, const EndpointDescriptor & target)
{
  if (source.domain_id != target.domain_id) {
    return false;
  }
  if (source.topic_name != target.topic_name || source.type_name != target.type_name) {
    return false;
  }
  if (!QosProfilesCompatible(source.qos, target.qos)) {
    return false;
  }
  switch (source.kind) {
    case EndpointKind::kPublisher:
      return target.kind == EndpointKind::kSubscription;
    case EndpointKind::kClient:
      return target.kind == EndpointKind::kService;
    case EndpointKind::kService:
      return target.kind == EndpointKind::kClient;
    case EndpointKind::kSubscription:
      return false;
  }
  return false;
}

std::string BridgeTopicName(const EndpointDescriptor & endpoint)
{
  std::string topic;
  if (endpoint.domain_id != 0u) {
    topic = "d" + std::to_string(endpoint.domain_id) + "/";
  }
  topic += ToMddsTopicName(endpoint.topic_name.c_str());
  return topic;
}

std::string BridgeTypeName(const EndpointDescriptor & endpoint)
{
  return endpoint.mdds_type_name.empty() ? endpoint.type_name : endpoint.mdds_type_name;
}

std::string ServiceBridgeTopicName(const char * prefix, const EndpointDescriptor & endpoint)
{
  std::string topic(prefix == nullptr ? "" : prefix);
  if (endpoint.domain_id != 0u) {
    topic += "d" + std::to_string(endpoint.domain_id) + "/";
  }
  topic += ToMddsTopicName(endpoint.topic_name.c_str());
  return topic;
}

std::string ServiceBridgeTypeName(const EndpointDescriptor & endpoint, const char * suffix)
{
  std::string type = BridgeTypeName(endpoint);
  if (!type.empty()) {
    type += suffix == nullptr ? "" : suffix;
  }
  return type;
}

bool BridgePublisherTopicAndType(
  const EndpointDescriptor & endpoint, std::string * topic, std::string * type)
{
  if (topic == nullptr || type == nullptr) {
    return false;
  }
  switch (endpoint.kind) {
    case EndpointKind::kPublisher:
      *topic = BridgeTopicName(endpoint);
      *type = BridgeTypeName(endpoint);
      return !topic->empty() && !type->empty();
    case EndpointKind::kClient:
      *topic = ServiceBridgeTopicName("rq/", endpoint);
      *type = ServiceBridgeTypeName(endpoint, "_Request");
      return !topic->empty() && !type->empty();
    case EndpointKind::kService:
      *topic = ServiceBridgeTopicName("rr/", endpoint);
      *type = ServiceBridgeTypeName(endpoint, "_Response");
      return !topic->empty() && !type->empty();
    case EndpointKind::kSubscription:
      return false;
  }
  return false;
}

bool BridgeSubscriptionTopicAndType(
  const EndpointDescriptor & endpoint, std::string * topic, std::string * type)
{
  if (topic == nullptr || type == nullptr) {
    return false;
  }
  switch (endpoint.kind) {
    case EndpointKind::kSubscription:
      *topic = BridgeTopicName(endpoint);
      *type = BridgeTypeName(endpoint);
      return !topic->empty() && !type->empty();
    case EndpointKind::kClient:
      *topic = ServiceBridgeTopicName("rr/", endpoint);
      *type = ServiceBridgeTypeName(endpoint, "_Response");
      return !topic->empty() && !type->empty();
    case EndpointKind::kService:
      *topic = ServiceBridgeTopicName("rq/", endpoint);
      *type = ServiceBridgeTypeName(endpoint, "_Request");
      return !topic->empty() && !type->empty();
    case EndpointKind::kPublisher:
      return false;
  }
  return false;
}

bool ShouldPublishBridgePayload(const EndpointDescriptor & source, const SampleMessage & sample)
{
  if (source.kind == EndpointKind::kClient || source.kind == EndpointKind::kService) {
    return true;
  }
  return sample.mdds_payload;
}

bool IsServiceLikeEndpoint(EndpointKind kind)
{
  return kind == EndpointKind::kClient || kind == EndpointKind::kService;
}

bool IsLocalBridgeEchoMatch(
  const LocalBridgeEcho & echo, const EndpointDescriptor & target,
  const std::vector<uint8_t> & payload)
{
  return echo.payload == payload && IsMatchingDeliveryTarget(echo.source, target);
}

void PruneExpiredLocalBridgeEchoesLocked(
  std::vector<LocalBridgeEcho> * echoes, std::chrono::steady_clock::time_point now)
{
  if (echoes == nullptr) {
    return;
  }
  echoes->erase(
    std::remove_if(
      echoes->begin(), echoes->end(),
      [now](const LocalBridgeEcho & echo) {
        return echo.expires_at <= now;
      }),
    echoes->end());
}

bool OffersTransientLocalDurability(const EndpointDescriptor & endpoint)
{
  return endpoint.kind == EndpointKind::kPublisher &&
         endpoint.qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
}

bool RequestsTransientLocalDurability(const EndpointDescriptor & endpoint)
{
  return endpoint.kind == EndpointKind::kSubscription &&
         endpoint.qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
}

size_t TransientLocalHistoryDepth(const rmw_qos_profile_t & qos)
{
  if (qos.history == RMW_QOS_POLICY_HISTORY_KEEP_ALL) {
    return qos.depth == 0u ? std::numeric_limits<size_t>::max() : qos.depth;
  }
  return qos.depth == 0u ? 1u : qos.depth;
}

bool SameRetainedSource(
  const RetainedSample & retained, const void * owner, uint64_t entity_id)
{
  return retained.owner == owner && retained.source.entity_id == entity_id;
}

void RemoveRetainedSamplesForEndpointLocked(
  std::vector<RetainedSample> * retained_samples, const void * owner, uint64_t entity_id)
{
  if (retained_samples == nullptr) {
    return;
  }
  retained_samples->erase(
    std::remove_if(
      retained_samples->begin(), retained_samples->end(),
      [owner, entity_id](const RetainedSample & retained) {
        return SameRetainedSource(retained, owner, entity_id);
      }),
    retained_samples->end());
}

void RemoveRetainedSamplesForConnectionLocked(
  std::vector<RetainedSample> * retained_samples, const void * owner)
{
  if (retained_samples == nullptr) {
    return;
  }
  retained_samples->erase(
    std::remove_if(
      retained_samples->begin(), retained_samples->end(),
      [owner](const RetainedSample & retained) {
        return retained.owner == owner;
      }),
    retained_samples->end());
}

size_t CountRetainedSamplesForSource(
  const std::vector<RetainedSample> & retained_samples, const void * owner, uint64_t entity_id)
{
  return static_cast<size_t>(std::count_if(
    retained_samples.begin(), retained_samples.end(),
    [owner, entity_id](const RetainedSample & retained) {
      return SameRetainedSource(retained, owner, entity_id);
    }));
}

void StoreRetainedTransientLocalSampleLocked(
  std::vector<RetainedSample> * retained_samples, void * owner,
  const EndpointDescriptor & source, const SampleMessage & sample)
{
  if (retained_samples == nullptr || owner == nullptr || !OffersTransientLocalDurability(source)) {
    return;
  }
  const size_t depth = TransientLocalHistoryDepth(source.qos);
  while (CountRetainedSamplesForSource(*retained_samples, owner, source.entity_id) >= depth) {
    auto it = std::find_if(
      retained_samples->begin(), retained_samples->end(),
      [owner, &source](const RetainedSample & retained) {
        return SameRetainedSource(retained, owner, source.entity_id);
      });
    if (it == retained_samples->end()) {
      break;
    }
    retained_samples->erase(it);
  }
  retained_samples->push_back(RetainedSample{owner, source, sample});
}

bool ShouldReplayRetainedSample(
  const RetainedSample & retained, const EndpointDescriptor & subscription)
{
  if (!RequestsTransientLocalDurability(subscription)) {
    return false;
  }
  if (
    subscription.ignore_local_publications &&
    subscription.local_context_id != 0u &&
    subscription.local_context_id == retained.source.local_context_id) {
    return false;
  }
  return IsMatchingDeliveryTarget(retained.source, subscription);
}
}  // namespace

void IpcBroker::AcceptLoop()
{
  while (running_.load()) {
    int listener_fd = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      listener_fd = listener_.get();
    }
    if (listener_fd < 0) {
      break;
    }

    std::string error;
    UniqueFd accepted_fd = AcceptUnixSocket(listener_fd, &error);
    if (!accepted_fd) {
      if (!running_.load()) {
        break;
      }
      continue;
    }
    if (!running_.load()) {
      break;
    }

    auto connection = std::make_unique<Connection>(std::move(accepted_fd));
    Connection * connection_ptr = connection.get();
    connection->thread = std::thread(&IpcBroker::ClientLoop, this, connection_ptr);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      connections_.push_back(std::move(connection));
    }
    BroadcastGraphUpdate();
  }
}

void IpcBroker::ClientLoop(Connection * connection)
{
  if (connection == nullptr) {
    return;
  }

  while (running_.load() && connection->fd.get() >= 0) {
    Frame frame;
    std::string error;
    const ReadFrameStatus status = ReadFrame(connection->fd.get(), &frame, &error);
    if (status != ReadFrameStatus::kOk) {
      break;
    }
    HandleFrame(connection, frame);
  }

  connection->active.store(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    DestroyBridgeEndpoints(connection);
    RemoveRetainedSamplesForConnectionLocked(&retained_samples_, connection);
    connection->endpoints.clear();
  }
  BroadcastGraphUpdate();
  // This client's endpoints are gone; re-announce the smaller local set so peers
  // drop the corresponding entries from their cross-board graph.
  PublishLocalGraph();
}

void IpcBroker::HandleFrame(Connection * connection, const Frame & frame)
{
  switch (frame.kind) {
    case MessageKind::kRegisterPublisher:
      RegisterEndpoint(connection, frame, EndpointKind::kPublisher);
      break;
    case MessageKind::kRegisterSubscription:
      RegisterEndpoint(connection, frame, EndpointKind::kSubscription);
      break;
    case MessageKind::kRegisterClient:
      RegisterEndpoint(connection, frame, EndpointKind::kClient);
      break;
    case MessageKind::kRegisterService:
      RegisterEndpoint(connection, frame, EndpointKind::kService);
      break;
    case MessageKind::kPublishSample:
      PublishSample(connection, frame);
      break;
    default:
      SendError(connection, frame.request_id, "broker frame kind is not supported");
      break;
  }
}

void IpcBroker::RegisterEndpoint(
  Connection * connection, const Frame & frame, EndpointKind expected_kind)
{
  EndpointDescriptor endpoint;
  std::string error;
  void * client_match_publisher = nullptr;
  void * sub_match_subscription = nullptr;
  std::vector<Frame> retained_deliveries;
  if (!DecodeEndpointDescriptor(frame.payload.data(), frame.payload.size(), &endpoint, &error)) {
    SendError(connection, frame.request_id, error);
    return;
  }
  if (endpoint.kind != expected_kind) {
    SendError(connection, frame.request_id, "endpoint kind does not match registration frame");
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(
      connection->endpoints.begin(), connection->endpoints.end(),
      [&endpoint](const EndpointDescriptor & current) {
        return current.entity_id == endpoint.entity_id;
      });
    if (it == connection->endpoints.end()) {
      connection->endpoints.push_back(endpoint);
    } else {
      *it = endpoint;
    }
    RemoveRetainedSamplesForEndpointLocked(&retained_samples_, connection, endpoint.entity_id);
    auto bridge_it = std::find_if(
      connection->bridge_endpoints.begin(), connection->bridge_endpoints.end(),
      [&endpoint](const Connection::BridgeEndpoint & current) {
        return current.endpoint.entity_id == endpoint.entity_id;
      });
    if (bridge_it != connection->bridge_endpoints.end()) {
      if (bridge_it->bridge_publisher != nullptr) {
        BridgeBackend::Instance().DestroyPublisher(bridge_it->bridge_publisher);
      }
      if (bridge_it->bridge_subscription != nullptr) {
        BridgeBackend::Instance().Unsubscribe(bridge_it->bridge_subscription);
      }
      connection->bridge_endpoints.erase(bridge_it);
    }

    if (bridge_enabled_) {
      Connection::BridgeEndpoint bridge_endpoint;
      bridge_endpoint.endpoint = endpoint;
      std::string bridge_topic;
      std::string bridge_type;
      if (BridgePublisherTopicAndType(endpoint, &bridge_topic, &bridge_type)) {
        bridge_endpoint.bridge_publisher = BridgeBackend::Instance().CreatePublisher(
          bridge_topic.c_str(), bridge_type.c_str(), &endpoint.qos);
      }
      if (BridgeSubscriptionTopicAndType(endpoint, &bridge_topic, &bridge_type)) {
        bridge_endpoint.subscription_state = std::make_unique<BridgeSubscriptionState>();
        bridge_endpoint.subscription_state->broker = this;
        bridge_endpoint.subscription_state->endpoint = endpoint;
        bridge_endpoint.bridge_subscription = BridgeBackend::Instance().Subscribe(
          bridge_topic.c_str(), bridge_type.c_str(), &endpoint.qos, BridgeSampleCallback,
          bridge_endpoint.subscription_state.get());
      }
      if (
        bridge_endpoint.bridge_publisher != nullptr ||
        bridge_endpoint.bridge_subscription != nullptr) {
        // A client's rq/ publisher matching a remote subscriber means a remote
        // service provider exists; capture it to install a matched listener
        // (outside the lock) that refreshes the synthesized-service graph.
        if (endpoint.kind == EndpointKind::kClient) {
          client_match_publisher = bridge_endpoint.bridge_publisher;
        } else if (endpoint.kind == EndpointKind::kSubscription) {
          sub_match_subscription = bridge_endpoint.bridge_subscription;
        }
        connection->bridge_endpoints.push_back(std::move(bridge_endpoint));
      }
    }
    if (RequestsTransientLocalDurability(endpoint)) {
      for (const auto & retained : retained_samples_) {
        if (!ShouldReplayRetainedSample(retained, endpoint)) {
          continue;
        }
        Frame delivery;
        delivery.kind = MessageKind::kDeliverSample;
        delivery.request_id = 0u;
        delivery.payload = EncodeSampleMessage(retained.sample);
        retained_deliveries.push_back(std::move(delivery));
      }
    }
  }
  SendAck(connection, frame.request_id);
  BroadcastGraphUpdate();
  // Push our updated local endpoint set to peer brokers so their cross-board
  // graph reflects this new node/topic/service promptly (the timer also covers it).
  PublishLocalGraph();
  // Install the matched listener LAST: SetOnMatched may invoke the callback
  // synchronously with the current count, and the callback rebroadcasts the
  // graph. Doing this before SendAck would push a kGraphUpdate ahead of the
  // registration ACK, which the client reads as an unexpected response. After
  // the ACK + initial broadcast it is just another (idempotent) graph update.
  if (client_match_publisher != nullptr) {
    BridgeBackend::Instance().PublisherSetOnMatched(
      client_match_publisher, &IpcBroker::OnBridgePublisherMatched, this);
  }
  if (sub_match_subscription != nullptr) {
    BridgeBackend::Instance().SubscriberSetOnMatched(
      sub_match_subscription, &IpcBroker::OnBridgePublisherMatched, this);
  }
  for (const auto & delivery : retained_deliveries) {
    SendFrame(connection, delivery);
  }
}

void IpcBroker::OnBridgePublisherMatched(uint32_t /*matched_count*/, void * user_data)
{
  auto * broker = static_cast<IpcBroker *>(user_data);
  if (broker != nullptr) {
    broker->BroadcastGraphUpdate();
  }
}

void IpcBroker::PublishSample(Connection * connection, const Frame & frame)
{
  SampleMessage sample;
  std::string error;
  if (!DecodeSampleMessage(frame.payload.data(), frame.payload.size(), &sample, &error)) {
    SendError(connection, frame.request_id, error);
    return;
  }

  EndpointDescriptor source;
  bool source_found = false;
  void * bridge_publisher = nullptr;
  std::vector<Connection *> targets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto source_it = std::find_if(
      connection->endpoints.begin(), connection->endpoints.end(),
      [&sample](const EndpointDescriptor & endpoint) {
        return endpoint.entity_id == sample.entity_id && CanPublishFrom(endpoint.kind);
      });
    if (source_it != connection->endpoints.end()) {
      source = *source_it;
      source_found = true;
    }
    if (source_found) {
      StoreRetainedTransientLocalSampleLocked(&retained_samples_, connection, source, sample);
      if (bridge_enabled_ && ShouldPublishBridgePayload(source, sample)) {
        auto bridge_it = std::find_if(
          connection->bridge_endpoints.begin(), connection->bridge_endpoints.end(),
          [&sample](const Connection::BridgeEndpoint & endpoint) {
            return endpoint.endpoint.entity_id == sample.entity_id;
          });
        if (bridge_it != connection->bridge_endpoints.end()) {
          bridge_publisher = bridge_it->bridge_publisher;
        }
      }
      for (const auto & current_connection : connections_) {
        if (!current_connection->active.load()) {
          continue;
        }
        const bool matches = std::any_of(
          current_connection->endpoints.begin(), current_connection->endpoints.end(),
          [&source](const EndpointDescriptor & endpoint) {
            if (
              endpoint.ignore_local_publications &&
              endpoint.local_context_id != 0u &&
              endpoint.local_context_id == source.local_context_id &&
              source.kind == EndpointKind::kPublisher && endpoint.kind == EndpointKind::kSubscription) {
              return false;
            }
            return IsMatchingDeliveryTarget(source, endpoint);
          });
        if (matches) {
          targets.push_back(current_connection.get());
        }
      }
    }
  }

  if (!source_found) {
    SendError(connection, frame.request_id, "publishing endpoint is not registered");
    return;
  }

  Frame delivery;
  delivery.kind = MessageKind::kDeliverSample;
  delivery.request_id = frame.request_id;
  delivery.payload = frame.payload;
  for (auto * target : targets) {
    SendFrame(target, delivery);
  }
  if (bridge_publisher != nullptr && !(IsServiceLikeEndpoint(source.kind) && !targets.empty())) {
    RememberLocalBridgeEcho(source, sample.payload);
    if (BridgeBackend::Instance().Publish(
          bridge_publisher, sample.payload.data(), static_cast<uint32_t>(sample.payload.size())) != 0) {
      ForgetLocalBridgeEcho(source, sample.payload);
    }
  }
}

void IpcBroker::RememberLocalBridgeEcho(
  const EndpointDescriptor & source, const std::vector<uint8_t> & payload)
{
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  PruneExpiredLocalBridgeEchoesLocked(&pending_bridge_echoes_, now);
  pending_bridge_echoes_.push_back(
    LocalBridgeEcho{source, payload, now + kLocalBridgeEchoTtl});
}

void IpcBroker::ForgetLocalBridgeEcho(
  const EndpointDescriptor & source, const std::vector<uint8_t> & payload)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(
    pending_bridge_echoes_.begin(), pending_bridge_echoes_.end(),
    [&source, &payload](const LocalBridgeEcho & echo) {
      return echo.source.entity_id == source.entity_id &&
             echo.source.domain_id == source.domain_id &&
             echo.source.topic_name == source.topic_name &&
             echo.source.type_name == source.type_name &&
             echo.payload == payload;
    });
  if (it != pending_bridge_echoes_.end()) {
    pending_bridge_echoes_.erase(it);
  }
}

bool IpcBroker::ConsumeLocalBridgeEcho(
  const EndpointDescriptor & target, const std::vector<uint8_t> & payload)
{
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  PruneExpiredLocalBridgeEchoesLocked(&pending_bridge_echoes_, now);
  auto it = std::find_if(
    pending_bridge_echoes_.begin(), pending_bridge_echoes_.end(),
    [&target, &payload](const LocalBridgeEcho & echo) {
      return IsLocalBridgeEchoMatch(echo, target, payload);
    });
  if (it == pending_bridge_echoes_.end()) {
    return false;
  }
  pending_bridge_echoes_.erase(it);
  return true;
}

void IpcBroker::DeliverBridgeSample(
  const EndpointDescriptor & subscription_endpoint, const std::vector<uint8_t> & payload,
  uint64_t sequence_number)
{
  if (ConsumeLocalBridgeEcho(subscription_endpoint, payload)) {
    return;
  }

  SampleMessage sample;
  sample.entity_id = 0u;
  sample.sequence_number = sequence_number;
  sample.mdds_payload = true;
  sample.payload = payload;

  Frame delivery;
  delivery.kind = MessageKind::kDeliverSample;
  delivery.request_id = 0u;
  delivery.payload = EncodeSampleMessage(sample);

  std::vector<Connection *> targets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & connection : connections_) {
      if (!connection->active.load()) {
        continue;
      }
      const bool matches = std::any_of(
        connection->endpoints.begin(), connection->endpoints.end(),
        [&subscription_endpoint](const EndpointDescriptor & endpoint) {
          return endpoint.kind == subscription_endpoint.kind &&
                 endpoint.domain_id == subscription_endpoint.domain_id &&
                 endpoint.topic_name == subscription_endpoint.topic_name &&
                 endpoint.type_name == subscription_endpoint.type_name;
        });
      if (matches) {
        targets.push_back(connection.get());
      }
    }
  }
  for (auto * target : targets) {
    SendFrame(target, delivery);
  }
}

void IpcBroker::BroadcastGraphUpdate()
{
  std::vector<Connection *> targets;
  std::vector<EndpointDescriptor> endpoints;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & connection : connections_) {
      if (!connection->active.load()) {
        continue;
      }
      targets.push_back(connection.get());
      endpoints.insert(
        endpoints.end(), connection->endpoints.begin(), connection->endpoints.end());
      // Cross-board / cross-RMW service availability: the broker graph is built
      // only from LOCAL IPC registrations and has no remote service discovery.
      // Synthesize a kService for each local client whose rq/ bridge publisher
      // has matched remote subscribers — i.e. a remote service provider (a peer
      // rmw_mdds server or the MDDS<->DDS gateway) is present over DSoftBus.
      // Without this a client on one board never sees a server on another board.
      if (!bridge_enabled_) {
        continue;
      }
      for (const auto & bridge_endpoint : connection->bridge_endpoints) {
        const EndpointKind kind = bridge_endpoint.endpoint.kind;
        const bool remote_service_request_matched =
          bridge_endpoint.bridge_publisher != nullptr &&
          BridgeBackend::Instance().PublisherSubCount(bridge_endpoint.bridge_publisher) != 0u;
        const bool remote_service_response_matched =
          bridge_endpoint.bridge_subscription != nullptr &&
          BridgeBackend::Instance().SubscriberPubCount(bridge_endpoint.bridge_subscription) != 0u;
        if (
          kind == EndpointKind::kClient &&
          (remote_service_request_matched || remote_service_response_matched)) {
          EndpointDescriptor service = bridge_endpoint.endpoint;
          service.kind = EndpointKind::kService;
          endpoints.push_back(std::move(service));
        } else if (kind == EndpointKind::kSubscription && bridge_endpoint.bridge_subscription != nullptr &&
          BridgeBackend::Instance().SubscriberPubCount(bridge_endpoint.bridge_subscription) != 0u)
        {
          // Symmetric to kService: a local subscription whose bridge subscriber
          // has matched remote publishers means a remote publisher exists (e.g.
          // an action feedback/status topic, or any wait-for-publisher). Surface
          // it as a kPublisher so cross-board availability checks see it.
          EndpointDescriptor publisher = bridge_endpoint.endpoint;
          publisher.kind = EndpointKind::kPublisher;
          endpoints.push_back(std::move(publisher));
        }
      }
    }
    // Cross-board node discovery: include nodes a gateway announced over MDDS.
    endpoints.insert(endpoints.end(), remote_node_endpoints_.begin(), remote_node_endpoints_.end());
    // Cross-board graph: merge every peer broker's full endpoint list (carrying
    // their real ROS node/topic/service identities). Each source bucket is
    // aged out after kGraphPeerTtl so a board that drops off (no re-announce)
    // stops appearing in this broker's graph.
    const auto now = std::chrono::steady_clock::now();
    for (auto it = remote_graph_endpoints_.begin(); it != remote_graph_endpoints_.end();) {
      if (now - it->second.last_seen > kGraphPeerTtl) {
        it = remote_graph_endpoints_.erase(it);
        continue;
      }
      endpoints.insert(endpoints.end(), it->second.endpoints.begin(), it->second.endpoints.end());
      ++it;
    }
  }

  Frame update;
  update.kind = MessageKind::kGraphUpdate;
  update.request_id = 0u;
  update.payload = EncodeEndpointList(endpoints);
  for (auto * target : targets) {
    SendFrame(target, update);
  }
}

void IpcBroker::DestroyBridgeEndpoints(Connection * connection)
{
  if (connection == nullptr) {
    return;
  }
  for (auto & endpoint : connection->bridge_endpoints) {
    if (endpoint.bridge_publisher != nullptr) {
      // Clear the matched listener before destroying the publisher so a final
      // unmatch notification can never re-enter BroadcastGraphUpdate against a
      // publisher that is being torn down (callback is a no-op if never set).
      BridgeBackend::Instance().PublisherSetOnMatched(endpoint.bridge_publisher, nullptr, nullptr);
      BridgeBackend::Instance().DestroyPublisher(endpoint.bridge_publisher);
      endpoint.bridge_publisher = nullptr;
    }
    if (endpoint.bridge_subscription != nullptr) {
      BridgeBackend::Instance().SubscriberSetOnMatched(endpoint.bridge_subscription, nullptr, nullptr);
      BridgeBackend::Instance().Unsubscribe(endpoint.bridge_subscription);
      endpoint.bridge_subscription = nullptr;
    }
  }
  connection->bridge_endpoints.clear();
}

void IpcBroker::BridgeSampleCallback(const BridgeSample * sample, void * user_data)
{
  auto * state = static_cast<BridgeSubscriptionState *>(user_data);
  if (
    state == nullptr || state->broker == nullptr || sample == nullptr ||
    (sample->data == nullptr && sample->len != 0u)) {
    return;
  }
  const auto * data = static_cast<const uint8_t *>(sample->data);
  std::vector<uint8_t> payload;
  if (data != nullptr && sample->len != 0u) {
    payload.assign(data, data + sample->len);
  }
  state->broker->DeliverBridgeSample(state->endpoint, payload, sample->sequenceNumber);
}

void IpcBroker::NodeSyncBridgeCallback(const BridgeSample * sample, void * user_data)
{
  auto * broker = static_cast<IpcBroker *>(user_data);
  if (broker == nullptr || sample == nullptr || (sample->data == nullptr && sample->len != 0u)) {
    return;
  }
  const auto * data = static_cast<const uint8_t *>(sample->data);
  std::vector<uint8_t> payload;
  if (data != nullptr && sample->len != 0u) {
    payload.assign(data, data + sample->len);
  }
  broker->OnNodeSync(payload);
}

void IpcBroker::OnNodeSync(const std::vector<uint8_t> & payload)
{
  // Payload is the gateway's DDS-side node list: one fully-qualified name per
  // line (e.g. "/parameter_blackboard"). Surface each as a hidden, node-bearing
  // synthetic endpoint so this broker's get_node_names() lists it.
  const std::string text(payload.begin(), payload.end());
  std::vector<EndpointDescriptor> endpoints;
  size_t start = 0;
  while (start < text.size()) {
    const size_t nl = text.find('\n', start);
    const std::string fqn = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    start = (nl == std::string::npos) ? text.size() : nl + 1;
    if (fqn.empty() || fqn.front() != '/') {
      continue;
    }
    const size_t slash = fqn.find_last_of('/');
    const std::string ns = (slash == 0u) ? "/" : fqn.substr(0, slash);
    const std::string name = fqn.substr(slash + 1);
    if (name.empty()) {
      continue;
    }
    EndpointDescriptor ep;
    ep.kind = EndpointKind::kSubscription;
    ep.node_name = name;
    ep.node_namespace = ns;
    ep.topic_name = "_mdds_remote_node";  // hidden topic, filtered from `ros2 topic list`
    ep.type_name = "mdds_graph/msg/RemoteNode";
    endpoints.push_back(std::move(ep));
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (remote_node_endpoints_.size() == endpoints.size()) {
      bool same = true;
      for (size_t i = 0; i < endpoints.size(); ++i) {
        if (remote_node_endpoints_[i].node_name != endpoints[i].node_name ||
          remote_node_endpoints_[i].node_namespace != endpoints[i].node_namespace)
        {
          same = false;
          break;
        }
      }
      if (same) {
        return;  // unchanged node set; avoid a needless rebroadcast
      }
    }
    remote_node_endpoints_ = std::move(endpoints);
  }
  BroadcastGraphUpdate();
}

void IpcBroker::PublishLocalGraph()
{
  void * publisher = nullptr;
  uint64_t broker_id = 0u;
  std::vector<EndpointDescriptor> local;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    publisher = graph_sync_publisher_;
    broker_id = broker_id_;
    if (publisher == nullptr || broker_id == 0u) {
      if (GraphDebugEnabled()) {
        std::fprintf(
          stderr,
          "[rmw_mdds_graph] publish skip publisher=%p broker_id=%llu\n", publisher,
          static_cast<unsigned long long>(broker_id));
      }
      return;
    }
    for (const auto & connection : connections_) {
      if (!connection->active.load()) {
        continue;
      }
      local.insert(local.end(), connection->endpoints.begin(), connection->endpoints.end());
    }
  }
  std::vector<uint8_t> payload;
  const uint64_t epoch = graph_epoch_.fetch_add(1u) + 1u;
  AppendU64Le(payload, broker_id);
  AppendU64Le(payload, epoch);
  const std::vector<uint8_t> body = EncodeEndpointList(local);
  payload.insert(payload.end(), body.begin(), body.end());
  // Publish outside the broker lock. Stop() joins every thread that can call this
  // (the re-announce timer and the connection threads) before destroying the
  // publisher, so the captured pointer is valid for the duration of this call.
  const int32_t rc = BridgeBackend::Instance().Publish(
    publisher, payload.data(), static_cast<uint32_t>(payload.size()));
  if (GraphDebugEnabled()) {
    std::fprintf(
      stderr,
      "[rmw_mdds_graph] publish broker_id=%llu epoch=%llu endpoints=%zu bytes=%zu rc=%d\n",
      static_cast<unsigned long long>(broker_id), static_cast<unsigned long long>(epoch),
      local.size(), payload.size(), rc);
  }
}

void IpcBroker::GraphSyncBridgeCallback(const BridgeSample * sample, void * user_data)
{
  auto * broker = static_cast<IpcBroker *>(user_data);
  if (broker == nullptr || sample == nullptr || (sample->data == nullptr && sample->len != 0u)) {
    if (GraphDebugEnabled()) {
      std::fprintf(
        stderr, "[rmw_mdds_graph] callback drop invalid sample=%p broker=%p\n",
        static_cast<const void *>(sample), static_cast<void *>(broker));
    }
    return;
  }
  if (GraphDebugEnabled()) {
    std::fprintf(
      stderr, "[rmw_mdds_graph] callback len=%u seq=%llu data=%p\n", sample->len,
      static_cast<unsigned long long>(sample->sequenceNumber), sample->data);
  }
  const auto * data = static_cast<const uint8_t *>(sample->data);
  std::vector<uint8_t> payload;
  if (data != nullptr && sample->len != 0u) {
    payload.assign(data, data + sample->len);
  }
  broker->OnGraphSync(payload);
}

void IpcBroker::OnGraphSync(const std::vector<uint8_t> & payload)
{
  if (payload.size() < kGraphSyncHeaderSize) {
    if (GraphDebugEnabled()) {
      std::fprintf(stderr, "[rmw_mdds_graph] graph sync drop short bytes=%zu\n", payload.size());
    }
    return;
  }
  const uint64_t src = ReadU64Le(payload.data());
  const uint64_t epoch = ReadU64Le(payload.data() + 8u);
  if (src == 0u || src == broker_id_) {
    if (GraphDebugEnabled()) {
      std::fprintf(
        stderr,
        "[rmw_mdds_graph] graph sync drop self_or_empty src=%llu self=%llu epoch=%llu bytes=%zu\n",
        static_cast<unsigned long long>(src), static_cast<unsigned long long>(broker_id_),
        static_cast<unsigned long long>(epoch), payload.size());
    }
    return;  // unset source id, or our own echo over the N:N bridge topic
  }
  std::vector<EndpointDescriptor> endpoints;
  std::string error;
  if (!DecodeEndpointList(
        payload.data() + kGraphSyncHeaderSize, payload.size() - kGraphSyncHeaderSize, &endpoints,
        &error)) {
    if (GraphDebugEnabled()) {
      std::fprintf(
        stderr,
        "[rmw_mdds_graph] graph sync decode fail src=%llu epoch=%llu bytes=%zu error=%s\n",
        static_cast<unsigned long long>(src), static_cast<unsigned long long>(epoch),
        payload.size(), error.c_str());
    }
    return;
  }
  const size_t endpoint_count = endpoints.size();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto & bucket = remote_graph_endpoints_[src];
    // Per-source epoch is monotonic; ignore an out-of-order/duplicate frame but
    // still refresh last_seen so the peer is not aged out. A fresh bucket has
    // epoch 0, so the peer's first frame (epoch >= 1) always lands.
    if (bucket.epoch != 0u && epoch != 0u && epoch <= bucket.epoch) {
      bucket.last_seen = std::chrono::steady_clock::now();
      if (GraphDebugEnabled()) {
        std::fprintf(
          stderr,
          "[rmw_mdds_graph] graph sync stale src=%llu epoch=%llu current=%llu endpoints=%zu\n",
          static_cast<unsigned long long>(src), static_cast<unsigned long long>(epoch),
          static_cast<unsigned long long>(bucket.epoch), endpoint_count);
      }
      return;
    }
    bucket.epoch = epoch;
    bucket.last_seen = std::chrono::steady_clock::now();
    bucket.endpoints = std::move(endpoints);
  }
  if (GraphDebugEnabled()) {
    std::fprintf(
      stderr, "[rmw_mdds_graph] graph sync accept src=%llu epoch=%llu endpoints=%zu\n",
      static_cast<unsigned long long>(src), static_cast<unsigned long long>(epoch), endpoint_count);
  }
  BroadcastGraphUpdate();
}

bool IpcBroker::SendFrame(Connection * connection, const Frame & frame)
{
  if (connection == nullptr || connection->fd.get() < 0) {
    return false;
  }
  std::string error;
  std::lock_guard<std::mutex> lock(connection->write_mutex);
  return WriteFrame(connection->fd.get(), frame, &error);
}

void IpcBroker::SendAck(Connection * connection, uint64_t request_id)
{
  SendFrame(connection, Frame{MessageKind::kAck, request_id, {}});
}

void IpcBroker::SendError(Connection * connection, uint64_t request_id, const std::string & message)
{
  SendFrame(
    connection,
    Frame{
      MessageKind::kError, request_id,
      std::vector<uint8_t>(message.begin(), message.end())});
}

}  // namespace ipc
}  // namespace rmw_mdds_cpp
