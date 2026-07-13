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
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <utility>

#include "bridge_backend.hpp"
#include "ipc_loan_pool.hpp"
#include "rmw/types.h"

namespace rmw_mdds_cpp {
namespace ipc {

struct IpcBroker::BridgeSubscriptionState {
  IpcBroker *broker = nullptr;
  EndpointDescriptor endpoint;
};

struct IpcBroker::Connection {
  explicit Connection(UniqueFd accepted_fd) : fd(std::move(accepted_fd)) {}

  struct BridgeEndpoint {
    EndpointDescriptor endpoint;
    void *bridge_publisher = nullptr;
    bool shared_bridge_publisher = false;
    std::string bridge_publisher_topic;
    std::string bridge_publisher_type;
    std::shared_ptr<std::mutex> bridge_publisher_mutex;
    void *bridge_subscription = nullptr;
    bool shared_bridge_subscription = false;
    std::string bridge_subscription_topic;
    std::string bridge_subscription_type;
    std::unique_ptr<BridgeSubscriptionState> subscription_state;
  };

  struct PendingLoanDelivery {
    SampleMessage sample;
    uint64_t request_id = 0u;
  };

  UniqueFd fd;
  std::mutex write_mutex;
  std::thread thread;
  std::vector<EndpointDescriptor> endpoints;
  std::vector<BridgeEndpoint> bridge_endpoints;
  std::mutex loan_pool_mutex;
  std::unique_ptr<LoanPoolOwner> loan_pool;
  uint64_t loan_pool_entity_id = 0u;
  bool loan_pool_reliable = false;
  bool loan_pool_keep_last = true;
  size_t loan_pool_pending_limit = 1u;
  std::deque<PendingLoanDelivery> pending_loan_deliveries;
  std::atomic<bool> active{true};

  void ResetLoanPoolLocked()
  {
    loan_pool.reset();
    loan_pool_entity_id = 0u;
    loan_pool_reliable = false;
    loan_pool_keep_last = true;
    loan_pool_pending_limit = 1u;
    pending_loan_deliveries.clear();
  }

  void ConfigureLoanPoolLocked(
    std::unique_ptr<LoanPoolOwner> pool, const EndpointDescriptor & endpoint)
  {
    ResetLoanPoolLocked();
    loan_pool = std::move(pool);
    if (loan_pool == nullptr) {
      return;
    }
    loan_pool_entity_id = endpoint.entity_id;
    loan_pool_reliable =
      endpoint.qos.reliability != RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
    loan_pool_keep_last = endpoint.qos.history != RMW_QOS_POLICY_HISTORY_KEEP_ALL;
    if (loan_pool_keep_last) {
      loan_pool_pending_limit = std::max<size_t>(
        1u, std::min<size_t>(endpoint.qos.depth, kMaxLoanPoolSlotCount));
    } else {
      loan_pool_pending_limit = kMaxLoanPoolSlotCount;
    }
  }
};

IpcBroker::IpcBroker() = default;

IpcBroker::~IpcBroker() { Stop(); }

namespace {
// --- cross-board graph-sync wire helpers ---------------------------------
// Payload = [broker_id:8 LE][epoch:8 LE] + EncodeEndpointList(local endpoints).
constexpr size_t kGraphSyncHeaderSize = 16u;
constexpr std::chrono::seconds kGraphPeerTtl{30};
constexpr std::chrono::seconds kBridgeDeliveryDedupeTtl{5};
constexpr std::chrono::seconds kGraphSyncUnchangedPublishInterval{10};
constexpr std::chrono::milliseconds kGraphSyncChangedPublishInterval{500};
constexpr std::chrono::milliseconds kGraphUpdateCoalesceInterval{100};
constexpr std::chrono::milliseconds kGraphReannounceInterval{1500};
static_assert(
    kGraphPeerTtl > kGraphSyncUnchangedPublishInterval,
    "remote graph TTL must outlive the unchanged graph-sync heartbeat");

bool GraphDebugEnabled() {
  const char *value = std::getenv("RMW_MDDS_GRAPH_DEBUG");
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

std::string NodeSyncTopicFromEnvironment() {
  const char *configured = std::getenv("RMW_MDDS_NODE_SYNC_TOPIC");
  return configured == nullptr || configured[0] == '\0' ? "mdds_node_sync"
                                                        : configured;
}

bool ParseDomainId(const char *begin, const char *end, uint32_t *domain_id) {
  if (begin == nullptr || end == nullptr || domain_id == nullptr ||
      begin >= end) {
    return false;
  }
  uint64_t value = 0u;
  for (const char *cursor = begin; cursor < end; ++cursor) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    value = value * 10u + static_cast<uint64_t>(*cursor - '0');
    if (value > std::numeric_limits<uint32_t>::max()) {
      return false;
    }
  }
  *domain_id = static_cast<uint32_t>(value);
  return true;
}

uint32_t NodeSyncDomainFromEnvironment(const std::string &topic) {
  const size_t slash = topic.find('/');
  uint32_t domain_id = 0u;
  if (topic.size() > 2u && topic.front() == 'd' && slash != std::string::npos &&
      ParseDomainId(topic.data() + 1u, topic.data() + slash, &domain_id)) {
    return domain_id;
  }

  const char *configured = std::getenv("ROS_DOMAIN_ID");
  if (configured != nullptr && configured[0] != '\0' &&
      ParseDomainId(configured, configured + std::strlen(configured),
                    &domain_id)) {
    return domain_id;
  }
  return 0u;
}

bool ProtectedTransportFlagEnabled(const char *name) {
  const char *value = std::getenv(name);
  return value != nullptr &&
         (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
          std::strcmp(value, "on") == 0 || std::strcmp(value, "yes") == 0);
}

void SetError(std::string *error, const std::string &message) {
  if (error != nullptr) {
    *error = message;
  }
}

std::string BrokerListenerLockPath(const std::string &socket_path) {
  return socket_path + ".listener.lockdir";
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

ScopedPathLock AcquireBrokerListenerLock(const std::string &socket_path,
                                         std::string *error) {
  const std::string lock_path = BrokerListenerLockPath(socket_path);
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
        SetError(error, "timed out waiting for broker listener lock");
        return ScopedPathLock();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    SetError(error, std::string("failed to acquire broker listener lock: ") +
                        std::strerror(saved_errno));
    return ScopedPathLock();
  }
}

void AppendU64Le(std::vector<uint8_t> &out, uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFFu));
  }
}

uint64_t ReadU64Le(const uint8_t *p) {
  uint64_t value = 0u;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<uint64_t>(p[i]) << (8 * i);
  }
  return value;
}

uint64_t MakeBrokerId() {
  std::random_device rd;
  uint64_t value =
      (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
  value ^= static_cast<uint64_t>(getpid()) << 16;
  value ^= static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return value == 0u ? 1u : value; // 0 is reserved (unset)
}

rmw_qos_profile_t GraphSyncSnapshotQos() {
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  qos.depth = 1u;
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  qos.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
  return qos;
}

uint64_t HashPayload(const std::vector<uint8_t> &payload) {
  uint64_t hash = 1469598103934665603ull;
  for (const uint8_t byte : payload) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

uint64_t
ClientEntityFromServiceWirePayload(const std::vector<uint8_t> &payload) {
  constexpr size_t kServiceSequenceSize = sizeof(int64_t);
  constexpr size_t kServiceSourceTimestampSize = sizeof(int64_t);
  constexpr size_t kServiceGuidOffset = kServiceSequenceSize;
  if (payload.size() < kServiceSequenceSize + RMW_GID_STORAGE_SIZE +
                           kServiceSourceTimestampSize) {
    return 0u;
  }

  uint64_t entity_id = 0u;
  std::memcpy(
      &entity_id, payload.data() + kServiceGuidOffset,
      std::min(sizeof(entity_id), static_cast<size_t>(RMW_GID_STORAGE_SIZE)));
  return entity_id;
}

void PruneExpiredBridgeDeliveriesLocked(
    std::vector<BridgeDeliveryDedupe> *deliveries,
    std::chrono::steady_clock::time_point now) {
  if (deliveries == nullptr) {
    return;
  }
  deliveries->erase(std::remove_if(deliveries->begin(), deliveries->end(),
                                   [now](const BridgeDeliveryDedupe &delivery) {
                                     return delivery.expires_at <= now;
                                   }),
                    deliveries->end());
}
} // namespace

bool IpcBroker::Start(const std::string &socket_path, std::string *error) {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    if (error != nullptr) {
      *error = "broker is already running";
    }
    return false;
  }

  // Default-on: enable the DSoftBus bridge data plane whenever the bridge
  // library is loadable. Broker-local IPC is the sole local data path, so a
  // loaded bridge must support remote-only publishers before any listener is
  // exposed to clients.
  auto &bridge_backend = BridgeBackend::Instance();
  const bool bridge_enabled = bridge_backend.Available();
  if (bridge_enabled && !bridge_backend.SupportsRemoteOnlyPublishers()) {
    if (error != nullptr) {
      *error = "MDDS bridge ABI mismatch: broker requires remote-only "
               "publisher capability";
    }
    bridge_backend.Shutdown();
    running_.store(false);
    return false;
  }
  const bool require_authenticated = ProtectedTransportFlagEnabled(
      "RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED");
  const bool require_encrypted =
      ProtectedTransportFlagEnabled("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED");
  if (require_authenticated != require_encrypted) {
    if (error != nullptr) {
      *error = "protected MDDS broker requires both authenticated and "
               "encrypted transport";
    }
    bridge_backend.Shutdown();
    running_.store(false);
    return false;
  }
  if (require_authenticated) {
    std::string protected_transport_error;
    if (!bridge_backend.ActivateProtectedTransport(
            true, true, &protected_transport_error)) {
      if (error != nullptr) {
        *error = protected_transport_error;
      }
      bridge_backend.Shutdown();
      running_.store(false);
      return false;
    }
  }

  ScopedPathLock listener_lock = AcquireBrokerListenerLock(socket_path, error);
  if (!listener_lock) {
    running_.store(false);
    return false;
  }

  UniqueFd listener = ListenUnixSocket(socket_path, error);
  if (!listener) {
    running_.store(false);
    return false;
  }
  listener_lock = ScopedPathLock();

  if (GraphDebugEnabled()) {
    std::fprintf(stderr, "[rmw_mdds_graph] broker start bridge_enabled=%d\n",
                 bridge_enabled ? 1 : 0);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    socket_path_ = socket_path;
    listener_ = std::move(listener);
    bridge_enabled_ = bridge_enabled;
    broker_id_ = MakeBrokerId();
    graph_epoch_.store(0u);
    graph_update_requested_ = false;
    graph_publish_requested_ = false;
  }
  if (bridge_enabled) {
    // Ingest the DDS-side node list a gateway publishes, so cross-board nodes
    // (and thus `ros2 param`/`ros2 lifecycle`/`ros2 node list`) resolve here.
    const std::string node_sync_topic = NodeSyncTopicFromEnvironment();
    node_sync_domain_id_ = NodeSyncDomainFromEnvironment(node_sync_topic);
    node_sync_subscription_ = BridgeBackend::Instance().Subscribe(
        node_sync_topic.c_str(), "mdds_graph_NodeList",
        &rmw_qos_profile_default, NodeSyncBridgeCallback, this);
    if (GraphDebugEnabled()) {
      std::fprintf(
          stderr, "[rmw_mdds_graph] node sync init topic=%s domain=%u sub=%p\n",
          node_sync_topic.c_str(), node_sync_domain_id_,
          node_sync_subscription_);
    }

    // Cross-board graph introspection (every rmw_mdds broker participates, no
    // gateway required): subscribe to peers' endpoint-graph announcements and
    // publish our own. broker_id_ tags our frames so we drop our own echo.
    const rmw_qos_profile_t graph_sync_qos = GraphSyncSnapshotQos();
    graph_sync_subscription_ = BridgeBackend::Instance().Subscribe(
        "mdds_graph_sync", "mdds_graph_EndpointList", &graph_sync_qos,
        GraphSyncBridgeCallback, this);
    graph_sync_publisher_ = BridgeBackend::Instance().CreatePublisher(
        "mdds_graph_sync", "mdds_graph_EndpointList", &graph_sync_qos,
        BridgePublisherMode::kRemoteOnly);
    if (GraphDebugEnabled()) {
      std::fprintf(
          stderr,
          "[rmw_mdds_graph] graph sync init broker_id=%llu sub=%p pub=%p\n",
          static_cast<unsigned long long>(broker_id_), graph_sync_subscription_,
          graph_sync_publisher_);
    }
  }
  // One worker coalesces endpoint-registration bursts before constructing and
  // fanning out a complete graph snapshot. It also provides the bridge
  // heartbeat because DSoftBus graph sync is not transient-local.
  graph_reannounce_thread_ = std::thread(&IpcBroker::GraphUpdateLoop, this);
  // Start accepting client registrations after the graph-sync bridge endpoints
  // exist. In no-prestart storms, accepting dozens of clients first can flood
  // MDDS bridge discovery before brokers have a chance to match their own
  // graph-sync endpoints, leaving remote services invisible for the whole
  // wait_for_service window.
  accept_thread_ = std::thread(&IpcBroker::AcceptLoop, this);
  return true;
}

void IpcBroker::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  // Stop the graph worker first so no delayed broadcast or publish can race
  // endpoint and graph-sync teardown below.
  graph_update_cv_.notify_all();
  if (graph_reannounce_thread_.joinable()) {
    graph_reannounce_thread_.join();
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
    for (auto &connection : connections_) {
      connection->active.store(false);
      if (connection->fd) {
        shutdown(connection->fd.get(), SHUT_RDWR);
      }
      connection->fd.reset();
      DestroyBridgeEndpoints(connection.get());
      {
        std::lock_guard<std::mutex> loan_lock(connection->loan_pool_mutex);
        connection->ResetLoanPoolLocked();
      }
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
  for (auto &connection : connections) {
    if (connection->thread.joinable()) {
      connection->thread.join();
    }
  }

  // All broker threads are joined, so endpoint bridge teardown cannot race
  // node/graph-sync unsubscription or publisher destruction.
  if (node_sync_subscription_ != nullptr) {
    BridgeBackend::Instance().Unsubscribe(node_sync_subscription_);
    node_sync_subscription_ = nullptr;
  }
  if (graph_sync_subscription_ != nullptr) {
    BridgeBackend::Instance().Unsubscribe(graph_sync_subscription_);
    graph_sync_subscription_ = nullptr;
  }
  if (graph_sync_publisher_ != nullptr) {
    BridgeBackend::Instance().DestroyPublisher(graph_sync_publisher_);
    graph_sync_publisher_ = nullptr;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &shared : shared_bridge_subscriptions_) {
      if (shared.bridge_subscription != nullptr) {
        BridgeBackend::Instance().SubscriberSetOnMatched(
            shared.bridge_subscription, nullptr, nullptr);
        BridgeBackend::Instance().Unsubscribe(shared.bridge_subscription);
        shared.bridge_subscription = nullptr;
      }
    }
    shared_bridge_subscriptions_.clear();
    remote_graph_endpoints_.clear();
    recent_bridge_deliveries_.clear();
    last_graph_sync_body_hash_ = 0u;
    last_graph_sync_body_size_ = 0u;
    last_graph_sync_publish_ = std::chrono::steady_clock::time_point{};
    graph_sync_publish_dirty_ = false;
    graph_update_requested_ = false;
    graph_publish_requested_ = false;
    broker_id_ = 0u;
    graph_epoch_.store(0u);
  }

  // The broker owns the bridge runtime. Closing it here releases DSoftBus
  // listen and peer sockets before a replacement broker starts.
  BridgeBackend::Instance().Shutdown();

  if (!socket_path.empty()) {
    unlink(socket_path.c_str());
  }
}

bool IpcBroker::IsRunning() const { return running_.load(); }

size_t IpcBroker::ConnectionCountForTesting() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connections_.size();
}

namespace {
bool CanPublishFrom(EndpointKind kind) {
  return kind == EndpointKind::kPublisher || kind == EndpointKind::kClient ||
         kind == EndpointKind::kService;
}

bool ReliabilityCompatible(rmw_qos_reliability_policy_t offered,
                           rmw_qos_reliability_policy_t requested) {
  return !(offered == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT &&
           requested == RMW_QOS_POLICY_RELIABILITY_RELIABLE);
}

bool DurabilityCompatible(rmw_qos_durability_policy_t offered,
                          rmw_qos_durability_policy_t requested) {
  return !(offered == RMW_QOS_POLICY_DURABILITY_VOLATILE &&
           requested == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);
}

bool LivelinessCompatible(rmw_qos_liveliness_policy_t offered,
                          rmw_qos_liveliness_policy_t requested) {
  return !(offered == RMW_QOS_POLICY_LIVELINESS_AUTOMATIC &&
           requested == RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC);
}

bool QosProfilesCompatible(const rmw_qos_profile_t &offered,
                           const rmw_qos_profile_t &requested) {
  return ReliabilityCompatible(offered.reliability, requested.reliability) &&
         DurabilityCompatible(offered.durability, requested.durability) &&
         LivelinessCompatible(offered.liveliness, requested.liveliness);
}

bool IsMatchingDeliveryTarget(const EndpointDescriptor &source,
                              const EndpointDescriptor &target) {
  if (source.domain_id != target.domain_id) {
    return false;
  }
  if (source.topic_name != target.topic_name ||
      source.type_name != target.type_name) {
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

std::string BridgeTopicName(const EndpointDescriptor &endpoint) {
  std::string topic;
  if (endpoint.domain_id != 0u) {
    topic = "d" + std::to_string(endpoint.domain_id) + "/";
  }
  topic += ToMddsTopicName(endpoint.topic_name.c_str());
  return topic;
}

std::string BridgeTypeName(const EndpointDescriptor &endpoint) {
  return endpoint.mdds_type_name.empty() ? endpoint.type_name
                                         : endpoint.mdds_type_name;
}

std::string ServiceBridgeTopicName(const char *prefix,
                                   const EndpointDescriptor &endpoint) {
  std::string topic(prefix == nullptr ? "" : prefix);
  if (endpoint.domain_id != 0u) {
    topic += "d" + std::to_string(endpoint.domain_id) + "/";
  }
  topic += ToMddsTopicName(endpoint.topic_name.c_str());
  return topic;
}

std::string ServiceBridgeTypeName(const EndpointDescriptor &endpoint,
                                  const char *suffix) {
  std::string type = BridgeTypeName(endpoint);
  if (!type.empty()) {
    type += suffix == nullptr ? "" : suffix;
  }
  return type;
}

bool BridgePublisherTopicAndType(const EndpointDescriptor &endpoint,
                                 std::string *topic, std::string *type) {
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

bool BridgeSubscriptionTopicAndType(const EndpointDescriptor &endpoint,
                                    std::string *topic, std::string *type) {
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

bool ShouldPublishBridgePayload(const EndpointDescriptor &source,
                                const SampleMessage &sample) {
  if (source.kind == EndpointKind::kClient ||
      source.kind == EndpointKind::kService) {
    return true;
  }
  return sample.mdds_payload;
}

bool IsServiceLikeEndpoint(EndpointKind kind) {
  return kind == EndpointKind::kClient || kind == EndpointKind::kService;
}

bool ShouldShareBridgeSubscription(const EndpointDescriptor &endpoint) {
  return endpoint.kind == EndpointKind::kClient;
}

bool ShouldShareBridgePublisher(const EndpointDescriptor &endpoint) {
  return endpoint.kind == EndpointKind::kClient;
}

constexpr size_t kServiceBridgeHistoryDepth = 16u * 1024u;
constexpr uint32_t kDefaultServiceBridgeMaxUnacked = 1u;
constexpr uint32_t kDefaultServiceBridgeBackpressureTimeoutMs = 30000u;
constexpr uint32_t kDefaultTopicBridgeMaxUnacked = 32u;
constexpr uint32_t kDefaultTopicBridgeBackpressureTimeoutMs = 30000u;
constexpr uint32_t kBridgeBackpressurePollMs = 5u;

uint32_t ReadEnvUint32(const char *name, uint32_t default_value,
                       uint32_t max_value) {
  const char *text = std::getenv(name);
  if (text == nullptr || text[0] == '\0') {
    return default_value;
  }
  errno = 0;
  char *end = nullptr;
  const unsigned long value = std::strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value > max_value) {
    return default_value;
  }
  return static_cast<uint32_t>(value);
}

uint32_t ServiceBridgeMaxUnacked() {
  return ReadEnvUint32("RMW_MDDS_SERVICE_BRIDGE_MAX_UNACKED",
                       kDefaultServiceBridgeMaxUnacked, 1024u);
}

std::chrono::milliseconds ServiceBridgeBackpressureTimeout() {
  return std::chrono::milliseconds(
      ReadEnvUint32("RMW_MDDS_SERVICE_BRIDGE_BACKPRESSURE_TIMEOUT_MS",
                    kDefaultServiceBridgeBackpressureTimeoutMs, 300000u));
}

uint32_t TopicBridgeMaxUnacked(const EndpointDescriptor &source) {
  const uint32_t configured = ReadEnvUint32(
      "RMW_MDDS_TOPIC_BRIDGE_MAX_UNACKED", kDefaultTopicBridgeMaxUnacked,
      1024u);
  if (configured != 0u &&
      source.qos.history == RMW_QOS_POLICY_HISTORY_KEEP_LAST &&
      source.qos.depth != 0u && source.qos.depth < configured) {
    return static_cast<uint32_t>(source.qos.depth);
  }
  return configured;
}

std::chrono::milliseconds TopicBridgeBackpressureTimeout() {
  return std::chrono::milliseconds(
      ReadEnvUint32("RMW_MDDS_TOPIC_BRIDGE_BACKPRESSURE_TIMEOUT_MS",
                    kDefaultTopicBridgeBackpressureTimeoutMs, 300000u));
}

bool UsesReliableBridgeBackpressure(const EndpointDescriptor &source) {
  return IsServiceLikeEndpoint(source.kind) ||
         (source.kind == EndpointKind::kPublisher &&
          source.qos.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE);
}

void WaitForReliableBridgeBackpressure(const EndpointDescriptor &source,
                                       const SampleMessage &sample,
                                       void *bridge_publisher) {
  if (!UsesReliableBridgeBackpressure(source) || bridge_publisher == nullptr) {
    return;
  }

  const bool service_like = IsServiceLikeEndpoint(source.kind);
  const uint32_t max_unacked =
      service_like ? ServiceBridgeMaxUnacked()
                   : TopicBridgeMaxUnacked(source);
  if (max_unacked == 0u) {
    return;
  }

  auto &backend = BridgeBackend::Instance();
  uint32_t unacked = 0u;
  if (!backend.PublisherUnackedCount(bridge_publisher, &unacked) ||
      unacked < max_unacked) {
    return;
  }

  const bool heartbeat_sent =
      backend.PublisherSendHeartbeatNow(bridge_publisher);
  if (GraphDebugEnabled()) {
    std::fprintf(stderr,
                 "[rmw_mdds_graph] broker bridge_backpressure_heartbeat "
                 "entity=%llu kind=%u seq=%llu unacked=%u max=%u sent=%d\n",
                 static_cast<unsigned long long>(sample.entity_id),
                 static_cast<unsigned>(source.kind),
                 static_cast<unsigned long long>(sample.sequence_number),
                 unacked, max_unacked, heartbeat_sent ? 1 : 0);
  }

  const auto timeout = service_like ? ServiceBridgeBackpressureTimeout()
                                    : TopicBridgeBackpressureTimeout();
  const auto started = std::chrono::steady_clock::now();
  auto next_timeout_log = started + timeout;
  if (GraphDebugEnabled()) {
    std::fprintf(stderr,
                 "[rmw_mdds_graph] broker bridge_backpressure_wait "
                 "entity=%llu kind=%u seq=%llu bytes=%zu unacked=%u max=%u "
                 "timeout_ms=%lld\n",
                 static_cast<unsigned long long>(sample.entity_id),
                 static_cast<unsigned>(source.kind),
                 static_cast<unsigned long long>(sample.sequence_number),
                 sample.payload.size(), unacked, max_unacked,
                 static_cast<long long>(timeout.count()));
  }

  for (;;) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kBridgeBackpressurePollMs));
    if (!backend.PublisherUnackedCount(bridge_publisher, &unacked) ||
        unacked < max_unacked) {
      break;
    }
    const auto now = std::chrono::steady_clock::now();
    if (timeout.count() > 0 && now >= next_timeout_log) {
      if (GraphDebugEnabled()) {
        std::fprintf(stderr,
                     "[rmw_mdds_graph] broker bridge_backpressure_timeout "
                     "entity=%llu kind=%u seq=%llu bytes=%zu unacked=%u "
                     "max=%u\n",
                     static_cast<unsigned long long>(sample.entity_id),
                     static_cast<unsigned>(source.kind),
                     static_cast<unsigned long long>(sample.sequence_number),
                     sample.payload.size(), unacked, max_unacked);
      }
      next_timeout_log = now + timeout;
    }
  }

  if (GraphDebugEnabled()) {
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count();
    std::fprintf(stderr,
                 "[rmw_mdds_graph] broker bridge_backpressure_resume "
                 "entity=%llu kind=%u seq=%llu bytes=%zu unacked=%u max=%u "
                 "elapsed_ms=%lld\n",
                 static_cast<unsigned long long>(sample.entity_id),
                 static_cast<unsigned>(source.kind),
                 static_cast<unsigned long long>(sample.sequence_number),
                 sample.payload.size(), unacked, max_unacked,
                 static_cast<long long>(elapsed_ms));
  }
}

rmw_qos_profile_t
BridgeTransportQosForEndpoint(const EndpointDescriptor &endpoint) {
  rmw_qos_profile_t qos = endpoint.qos;
  if (IsServiceLikeEndpoint(endpoint.kind)) {
    qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
    if (qos.depth < kServiceBridgeHistoryDepth) {
      qos.depth = kServiceBridgeHistoryDepth;
    }
  }
  return qos;
}

bool SameQosDuration(const rmw_time_t &lhs, const rmw_time_t &rhs) {
  return lhs.sec == rhs.sec && lhs.nsec == rhs.nsec;
}

bool SameBridgeTransportQos(const rmw_qos_profile_t &lhs,
                            const rmw_qos_profile_t &rhs) {
  return lhs.history == rhs.history && lhs.depth == rhs.depth &&
         lhs.reliability == rhs.reliability &&
         lhs.durability == rhs.durability &&
         SameQosDuration(lhs.deadline, rhs.deadline) &&
         SameQosDuration(lhs.lifespan, rhs.lifespan) &&
         lhs.liveliness == rhs.liveliness &&
         SameQosDuration(lhs.liveliness_lease_duration,
                         rhs.liveliness_lease_duration) &&
         lhs.avoid_ros_namespace_conventions ==
             rhs.avoid_ros_namespace_conventions;
}

bool OffersTransientLocalDurability(const EndpointDescriptor &endpoint) {
  return endpoint.kind == EndpointKind::kPublisher &&
         endpoint.qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
}

bool RequestsTransientLocalDurability(const EndpointDescriptor &endpoint) {
  return endpoint.kind == EndpointKind::kSubscription &&
         endpoint.qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
}

size_t TransientLocalHistoryDepth(const rmw_qos_profile_t &qos) {
  if (qos.history == RMW_QOS_POLICY_HISTORY_KEEP_ALL) {
    return qos.depth == 0u ? std::numeric_limits<size_t>::max() : qos.depth;
  }
  return qos.depth == 0u ? 1u : qos.depth;
}

bool SameRetainedSource(const RetainedSample &retained, const void *owner,
                        uint64_t entity_id) {
  return retained.owner == owner && retained.source.entity_id == entity_id;
}

void RemoveRetainedSamplesForEndpointLocked(
    std::vector<RetainedSample> *retained_samples, const void *owner,
    uint64_t entity_id) {
  if (retained_samples == nullptr) {
    return;
  }
  retained_samples->erase(
      std::remove_if(retained_samples->begin(), retained_samples->end(),
                     [owner, entity_id](const RetainedSample &retained) {
                       return SameRetainedSource(retained, owner, entity_id);
                     }),
      retained_samples->end());
}

void RemoveRetainedSamplesForConnectionLocked(
    std::vector<RetainedSample> *retained_samples, const void *owner) {
  if (retained_samples == nullptr) {
    return;
  }
  retained_samples->erase(
      std::remove_if(retained_samples->begin(), retained_samples->end(),
                     [owner](const RetainedSample &retained) {
                       return retained.owner == owner;
                     }),
      retained_samples->end());
}

size_t CountRetainedSamplesForSource(
    const std::vector<RetainedSample> &retained_samples, const void *owner,
    uint64_t entity_id) {
  return static_cast<size_t>(
      std::count_if(retained_samples.begin(), retained_samples.end(),
                    [owner, entity_id](const RetainedSample &retained) {
                      return SameRetainedSource(retained, owner, entity_id);
                    }));
}

void StoreRetainedTransientLocalSampleLocked(
    std::vector<RetainedSample> *retained_samples, void *owner,
    const EndpointDescriptor &source, const SampleMessage &sample) {
  if (retained_samples == nullptr || owner == nullptr ||
      !OffersTransientLocalDurability(source)) {
    return;
  }
  const size_t depth = TransientLocalHistoryDepth(source.qos);
  while (CountRetainedSamplesForSource(*retained_samples, owner,
                                       source.entity_id) >= depth) {
    auto it = std::find_if(retained_samples->begin(), retained_samples->end(),
                           [owner, &source](const RetainedSample &retained) {
                             return SameRetainedSource(retained, owner,
                                                       source.entity_id);
                           });
    if (it == retained_samples->end()) {
      break;
    }
    retained_samples->erase(it);
  }
  retained_samples->push_back(RetainedSample{owner, source, sample});
}

bool ShouldReplayRetainedSample(const RetainedSample &retained,
                                const EndpointDescriptor &subscription) {
  if (!RequestsTransientLocalDurability(subscription)) {
    return false;
  }
  if (subscription.ignore_local_publications &&
      subscription.local_context_id != 0u &&
      subscription.local_context_id == retained.source.local_context_id) {
    return false;
  }
  return IsMatchingDeliveryTarget(retained.source, subscription);
}
} // namespace

void IpcBroker::AcceptLoop() {
  while (running_.load()) {
    ReapInactiveConnections();
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
    Connection *connection_ptr = connection.get();
    try {
      connection->thread =
          std::thread(&IpcBroker::ClientLoop, this, connection_ptr);
    } catch (const std::exception &e) {
      if (GraphDebugEnabled()) {
        std::fprintf(
            stderr,
            "[rmw_mdds_graph] broker connection thread start failed: %s\n",
            e.what());
      }
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      connections_.push_back(std::move(connection));
    }
    RequestGraphUpdate(false);
  }
  ReapInactiveConnections();
}

void IpcBroker::ClientLoop(Connection *connection) {
  if (connection == nullptr) {
    return;
  }

  while (running_.load() && connection->fd.get() >= 0) {
    Frame frame;
    std::string error;
    const ReadFrameStatus status =
        ReadFrame(connection->fd.get(), &frame, &error);
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
    {
      std::lock_guard<std::mutex> loan_lock(connection->loan_pool_mutex);
      connection->ResetLoanPoolLocked();
    }
  }
  RequestGraphUpdate(true);
  // This client's endpoints are gone; re-announce the smaller local set so
  // peers drop the corresponding entries from their cross-board graph.
}

void IpcBroker::ReapInactiveConnections() {
  std::vector<std::unique_ptr<Connection>> inactive;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = connections_.begin(); it != connections_.end();) {
      if (*it == nullptr || (*it)->active.load()) {
        ++it;
        continue;
      }
      inactive.push_back(std::move(*it));
      it = connections_.erase(it);
    }
  }

  for (auto &connection : inactive) {
    if (connection != nullptr && connection->thread.joinable()) {
      connection->thread.join();
    }
  }
}

void IpcBroker::HandleFrame(Connection *connection, const Frame &frame) {
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
  case MessageKind::kUnregisterEntity:
    UnregisterEntity(connection, frame);
    break;
  case MessageKind::kPublishSample:
    PublishSample(connection, frame);
    break;
  case MessageKind::kReturnLoanedSample:
    ReturnLoanedSample(connection, frame);
    break;
  default:
    SendError(connection, frame.request_id,
              "broker frame kind is not supported");
    break;
  }
}

void IpcBroker::RegisterEndpoint(Connection *connection, const Frame &frame,
                                 EndpointKind expected_kind) {
  EndpointDescriptor endpoint;
  std::string error;
  void *client_match_publisher = nullptr;
  void *sub_match_subscription = nullptr;
  std::vector<SampleMessage> retained_deliveries;
  std::unique_ptr<LoanPoolOwner> requested_loan_pool;
  bool bridge_creation_failed = false;
  if (!DecodeEndpointDescriptor(frame.payload.data(), frame.payload.size(),
                                &endpoint, &error)) {
    SendError(connection, frame.request_id, error);
    return;
  }
  if (endpoint.kind != expected_kind) {
    SendError(connection, frame.request_id,
              "endpoint kind does not match registration frame");
    return;
  }
  const bool fixed_loan_requested = endpoint.loaned_message_size > 0u;
  const bool dynamic_loan_requested =
    endpoint.loan_pool_version == kDynamicLoanPoolVersion;
  if (fixed_loan_requested || dynamic_loan_requested) {
    if (
      endpoint.kind != EndpointKind::kSubscription ||
      (fixed_loan_requested && endpoint.loaned_message_size > kMaxSampleUserPayloadSize) ||
      (dynamic_loan_requested &&
      (endpoint.loan_pool_flags != kLoanPoolFlagTypedArena ||
      endpoint.loaned_payload_capacity == 0u ||
      endpoint.loaned_payload_capacity > kDefaultDynamicLoanPayloadCapacity ||
      endpoint.loaned_arena_capacity == 0u ||
      endpoint.loaned_arena_capacity > kDefaultDynamicLoanArenaCapacity ||
      endpoint.loaned_slot_count == 0u ||
      endpoint.loaned_slot_count > kMaxDynamicLoanPoolSlotCount)))
    {
      SendError(connection, frame.request_id, "invalid broker loaned-message size request");
      return;
    }
    std::string socket_path;
    uint64_t broker_id = 0u;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      socket_path = socket_path_;
      broker_id = broker_id_;
    }
    requested_loan_pool = std::make_unique<LoanPoolOwner>();
    const bool created = fixed_loan_requested ?
      requested_loan_pool->Create(
      socket_path, broker_id, endpoint.entity_id, endpoint.loaned_message_size,
      kDefaultLoanPoolSlotCount, &error) :
      requested_loan_pool->CreateDynamic(
      socket_path, broker_id, endpoint.entity_id, endpoint.loaned_payload_capacity,
      endpoint.loaned_arena_capacity, endpoint.loaned_slot_count, &error);
    if (!created)
    {
      SendError(connection, frame.request_id, "broker loan pool creation failed: " + error);
      return;
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    {
      std::lock_guard<std::mutex> loan_lock(connection->loan_pool_mutex);
      connection->ConfigureLoanPoolLocked(std::move(requested_loan_pool), endpoint);
    }
    auto it =
        std::find_if(connection->endpoints.begin(), connection->endpoints.end(),
                     [&endpoint](const EndpointDescriptor &current) {
                       return current.entity_id == endpoint.entity_id;
                     });
    if (it == connection->endpoints.end()) {
      connection->endpoints.push_back(endpoint);
    } else {
      *it = endpoint;
    }
    RemoveRetainedSamplesForEndpointLocked(&retained_samples_, connection,
                                           endpoint.entity_id);
    auto bridge_it =
        std::find_if(connection->bridge_endpoints.begin(),
                     connection->bridge_endpoints.end(),
                     [&endpoint](const Connection::BridgeEndpoint &current) {
                       return current.endpoint.entity_id == endpoint.entity_id;
                     });
    if (bridge_it != connection->bridge_endpoints.end()) {
      if (bridge_it->bridge_publisher != nullptr) {
        ReleaseBridgePublisher(bridge_it->bridge_publisher,
                               bridge_it->shared_bridge_publisher,
                               bridge_it->bridge_publisher_topic,
                               bridge_it->bridge_publisher_type);
        bridge_it->bridge_publisher = nullptr;
        bridge_it->bridge_publisher_mutex.reset();
      }
      if (bridge_it->bridge_subscription != nullptr) {
        if (bridge_it->shared_bridge_subscription) {
          auto shared_it = std::find_if(
              shared_bridge_subscriptions_.begin(),
              shared_bridge_subscriptions_.end(),
              [&bridge_it](const SharedBridgeSubscription &shared) {
                return shared.bridge_subscription ==
                           bridge_it->bridge_subscription &&
                       shared.bridge_topic ==
                           bridge_it->bridge_subscription_topic &&
                       shared.bridge_type ==
                           bridge_it->bridge_subscription_type;
              });
          if (shared_it != shared_bridge_subscriptions_.end()) {
            if (shared_it->ref_count > 0u) {
              --shared_it->ref_count;
            }
            if (shared_it->ref_count == 0u) {
              BridgeBackend::Instance().SubscriberSetOnMatched(
                  shared_it->bridge_subscription, nullptr, nullptr);
              BridgeBackend::Instance().Unsubscribe(
                  shared_it->bridge_subscription);
              shared_bridge_subscriptions_.erase(shared_it);
            }
          }
        } else {
          BridgeBackend::Instance().Unsubscribe(bridge_it->bridge_subscription);
        }
      }
      connection->bridge_endpoints.erase(bridge_it);
    }

    if (bridge_enabled_) {
      Connection::BridgeEndpoint bridge_endpoint;
      bridge_endpoint.endpoint = endpoint;
      const rmw_qos_profile_t bridge_transport_qos =
          BridgeTransportQosForEndpoint(endpoint);
      std::string bridge_topic;
      std::string bridge_type;
      const bool needs_bridge_publisher =
          BridgePublisherTopicAndType(endpoint, &bridge_topic, &bridge_type);
      if (needs_bridge_publisher) {
        if (ShouldShareBridgePublisher(endpoint)) {
          bridge_endpoint.shared_bridge_publisher = true;
          bridge_endpoint.bridge_publisher_topic = bridge_topic;
          bridge_endpoint.bridge_publisher_type = bridge_type;
          auto shared_it = std::find_if(
              shared_bridge_publishers_.begin(),
              shared_bridge_publishers_.end(),
              [&endpoint, &bridge_topic, &bridge_type,
               &bridge_transport_qos](const SharedBridgePublisher &shared) {
                return shared.endpoint.kind == endpoint.kind &&
                       shared.endpoint.domain_id == endpoint.domain_id &&
                       shared.endpoint.topic_name == endpoint.topic_name &&
                       shared.endpoint.type_name == endpoint.type_name &&
                       shared.bridge_topic == bridge_topic &&
                       shared.bridge_type == bridge_type &&
                       SameBridgeTransportQos(
                           BridgeTransportQosForEndpoint(shared.endpoint),
                           bridge_transport_qos);
              });
          if (shared_it != shared_bridge_publishers_.end()) {
            ++shared_it->ref_count;
            bridge_endpoint.bridge_publisher = shared_it->bridge_publisher;
            bridge_endpoint.bridge_publisher_mutex = shared_it->publish_mutex;
          } else {
            void *publisher = BridgeBackend::Instance().CreatePublisher(
                bridge_topic.c_str(), bridge_type.c_str(),
                &bridge_transport_qos, BridgePublisherMode::kRemoteOnly);
            if (publisher != nullptr) {
              SharedBridgePublisher shared;
              shared.endpoint = endpoint;
              shared.bridge_topic = bridge_topic;
              shared.bridge_type = bridge_type;
              shared.bridge_publisher = publisher;
              shared.publish_mutex = std::make_shared<std::mutex>();
              shared.ref_count = 1u;
              shared_bridge_publishers_.push_back(std::move(shared));
              bridge_endpoint.bridge_publisher = publisher;
              bridge_endpoint.bridge_publisher_mutex =
                  shared_bridge_publishers_.back().publish_mutex;
            }
          }
        } else {
          bridge_endpoint.bridge_publisher =
              BridgeBackend::Instance().CreatePublisher(
                  bridge_topic.c_str(), bridge_type.c_str(),
                  &bridge_transport_qos, BridgePublisherMode::kRemoteOnly);
        }
      }
      const bool needs_bridge_subscription =
          BridgeSubscriptionTopicAndType(endpoint, &bridge_topic, &bridge_type);
      if (needs_bridge_subscription) {
        if (ShouldShareBridgeSubscription(endpoint)) {
          bridge_endpoint.shared_bridge_subscription = true;
          bridge_endpoint.bridge_subscription_topic = bridge_topic;
          bridge_endpoint.bridge_subscription_type = bridge_type;
          auto shared_it = std::find_if(
              shared_bridge_subscriptions_.begin(),
              shared_bridge_subscriptions_.end(),
              [&endpoint, &bridge_topic,
               &bridge_type](const SharedBridgeSubscription &shared) {
                return shared.endpoint.kind == endpoint.kind &&
                       shared.endpoint.domain_id == endpoint.domain_id &&
                       shared.endpoint.topic_name == endpoint.topic_name &&
                       shared.endpoint.type_name == endpoint.type_name &&
                       shared.bridge_topic == bridge_topic &&
                       shared.bridge_type == bridge_type;
              });
          if (shared_it != shared_bridge_subscriptions_.end()) {
            ++shared_it->ref_count;
            bridge_endpoint.bridge_subscription =
                shared_it->bridge_subscription;
          } else {
            auto shared_state = std::make_unique<BridgeSubscriptionState>();
            shared_state->broker = this;
            shared_state->endpoint = endpoint;
            // A shared rr/ subscriber is not owned by any one client. Keep
            // entity_id unset so non-wire fallback samples broadcast to all
            // matching local clients, while wire-format service responses still
            // route to the writer_guid target.
            shared_state->endpoint.entity_id = 0u;
            void *subscription = BridgeBackend::Instance().Subscribe(
                bridge_topic.c_str(), bridge_type.c_str(),
                &bridge_transport_qos, BridgeSampleCallback,
                shared_state.get());
            if (subscription != nullptr) {
              SharedBridgeSubscription shared;
              shared.endpoint = endpoint;
              shared.bridge_topic = bridge_topic;
              shared.bridge_type = bridge_type;
              shared.bridge_subscription = subscription;
              shared.subscription_state = std::move(shared_state);
              shared.ref_count = 1u;
              shared_bridge_subscriptions_.push_back(std::move(shared));
              bridge_endpoint.bridge_subscription = subscription;
            }
          }
        } else {
          bridge_endpoint.subscription_state =
              std::make_unique<BridgeSubscriptionState>();
          bridge_endpoint.subscription_state->broker = this;
          bridge_endpoint.subscription_state->endpoint = endpoint;
          bridge_endpoint.bridge_subscription =
              BridgeBackend::Instance().Subscribe(
                  bridge_topic.c_str(), bridge_type.c_str(),
                  &bridge_transport_qos, BridgeSampleCallback,
                  bridge_endpoint.subscription_state.get());
        }
      }
      const bool bridge_ready =
          (!needs_bridge_publisher ||
           bridge_endpoint.bridge_publisher != nullptr) &&
          (!needs_bridge_subscription ||
           bridge_endpoint.bridge_subscription != nullptr);
      if (!bridge_ready) {
        bridge_creation_failed = true;
        if (bridge_endpoint.bridge_publisher != nullptr ||
            bridge_endpoint.bridge_subscription != nullptr) {
          connection->bridge_endpoints.push_back(std::move(bridge_endpoint));
          RemoveBridgeEndpointForEntity(connection, endpoint.entity_id);
        }
        connection->endpoints.erase(
            std::remove_if(connection->endpoints.begin(),
                           connection->endpoints.end(),
                           [&endpoint](const EndpointDescriptor &current) {
                             return current.entity_id == endpoint.entity_id;
                           }),
            connection->endpoints.end());
      } else if (bridge_endpoint.bridge_publisher != nullptr ||
                 bridge_endpoint.bridge_subscription != nullptr) {
        // Service availability depends on both directions: rq/ must match the
        // remote server's request subscriber and rr/ must match its response
        // publisher. Capture both local bridge handles so either match change
        // refreshes the synthesized-service graph.
        if (endpoint.kind == EndpointKind::kClient) {
          client_match_publisher = bridge_endpoint.bridge_publisher;
          sub_match_subscription = bridge_endpoint.bridge_subscription;
        } else if (endpoint.kind == EndpointKind::kSubscription) {
          sub_match_subscription = bridge_endpoint.bridge_subscription;
        }
        connection->bridge_endpoints.push_back(std::move(bridge_endpoint));
      }
    }
    if (!bridge_creation_failed && RequestsTransientLocalDurability(endpoint)) {
      for (const auto &retained : retained_samples_) {
        if (!ShouldReplayRetainedSample(retained, endpoint)) {
          continue;
        }
        retained_deliveries.push_back(retained.sample);
      }
    }
  }
  if (bridge_creation_failed) {
    {
      std::lock_guard<std::mutex> loan_lock(connection->loan_pool_mutex);
      connection->ResetLoanPoolLocked();
    }
    SendError(connection, frame.request_id,
              "MDDS bridge endpoint creation failed: resource limit");
    RequestGraphUpdate(true);
    return;
  }
  std::vector<uint8_t> ack_payload;
  {
    std::lock_guard<std::mutex> loan_lock(connection->loan_pool_mutex);
    if (connection->loan_pool != nullptr) {
      ack_payload = EncodeLoanPoolDescriptor(connection->loan_pool->descriptor());
    }
  }
  SendAck(connection, frame.request_id, ack_payload);
  // Push our updated local endpoint set to peer brokers so their cross-board
  // graph reflects this new node/topic/service promptly (the timer also covers
  // it).
  RequestGraphUpdate(true);
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
  for (const auto &delivery : retained_deliveries) {
    SendSampleDelivery(connection, delivery, 0u);
  }
}

void IpcBroker::UnregisterEntity(Connection *connection, const Frame &frame) {
  uint64_t entity_id = 0u;
  std::string error;
  if (!DecodeEntityId(frame.payload.data(), frame.payload.size(), &entity_id,
                      &error)) {
    SendError(connection, frame.request_id, error);
    return;
  }

  bool removed = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &current_connection : connections_) {
      if (current_connection == nullptr) {
        continue;
      }
      const auto old_size = current_connection->endpoints.size();
      current_connection->endpoints.erase(
          std::remove_if(current_connection->endpoints.begin(),
                         current_connection->endpoints.end(),
                         [entity_id](const EndpointDescriptor &endpoint) {
                           return endpoint.entity_id == entity_id;
                         }),
          current_connection->endpoints.end());
      if (current_connection->endpoints.size() != old_size) {
        removed = true;
        RemoveRetainedSamplesForEndpointLocked(
            &retained_samples_, current_connection.get(), entity_id);
        RemoveBridgeEndpointForEntity(current_connection.get(), entity_id);
        std::lock_guard<std::mutex> loan_lock(current_connection->loan_pool_mutex);
        if (current_connection->loan_pool_entity_id == entity_id) {
          current_connection->ResetLoanPoolLocked();
        }
      }
    }
  }
  if (GraphDebugEnabled()) {
    std::fprintf(stderr,
                 "[rmw_mdds_graph] broker unregister entity=%llu removed=%d\n",
                 static_cast<unsigned long long>(entity_id), removed ? 1 : 0);
  }

  SendAck(connection, frame.request_id);
  if (removed) {
    RequestGraphUpdate(true);
  }
}

void IpcBroker::OnBridgePublisherMatched(uint32_t /*matched_count*/,
                                         void *user_data) {
  auto *broker = static_cast<IpcBroker *>(user_data);
  if (broker != nullptr) {
    broker->RequestGraphUpdate(false);
  }
}

void IpcBroker::PublishSample(Connection *connection, const Frame &frame) {
  SampleMessage sample;
  std::string error;
  if (!DecodeSampleMessage(frame.payload.data(), frame.payload.size(), &sample,
                           &error)) {
    SendError(connection, frame.request_id, error);
    return;
  }

  EndpointDescriptor source;
  bool source_found = false;
  void *bridge_publisher = nullptr;
  std::shared_ptr<std::mutex> bridge_publisher_mutex;
  uint64_t target_client_entity = 0u;
  std::vector<Connection *> targets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto source_it =
        std::find_if(connection->endpoints.begin(), connection->endpoints.end(),
                     [&sample](const EndpointDescriptor &endpoint) {
                       return endpoint.entity_id == sample.entity_id &&
                              CanPublishFrom(endpoint.kind);
                     });
    if (source_it != connection->endpoints.end()) {
      source = *source_it;
      source_found = true;
    }
    if (source_found) {
      if (source.kind == EndpointKind::kService) {
        target_client_entity =
            ClientEntityFromServiceWirePayload(sample.payload);
      }
      StoreRetainedTransientLocalSampleLocked(&retained_samples_, connection,
                                              source, sample);
      if (bridge_enabled_ && ShouldPublishBridgePayload(source, sample)) {
        auto bridge_it = std::find_if(
            connection->bridge_endpoints.begin(),
            connection->bridge_endpoints.end(),
            [&sample](const Connection::BridgeEndpoint &endpoint) {
              return endpoint.endpoint.entity_id == sample.entity_id;
            });
        if (bridge_it != connection->bridge_endpoints.end()) {
          bridge_publisher = bridge_it->bridge_publisher;
          bridge_publisher_mutex = bridge_it->bridge_publisher_mutex;
        }
      }
      for (const auto &current_connection : connections_) {
        if (!current_connection->active.load()) {
          continue;
        }
        const bool matches = std::any_of(
            current_connection->endpoints.begin(),
            current_connection->endpoints.end(),
            [&source,
             target_client_entity](const EndpointDescriptor &endpoint) {
              if (source.kind == EndpointKind::kService &&
                  target_client_entity != 0u &&
                  endpoint.entity_id != target_client_entity) {
                return false;
              }
              if (endpoint.ignore_local_publications &&
                  endpoint.local_context_id != 0u &&
                  endpoint.local_context_id == source.local_context_id &&
                  source.kind == EndpointKind::kPublisher &&
                  endpoint.kind == EndpointKind::kSubscription) {
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
    SendError(connection, frame.request_id,
              "publishing endpoint is not registered");
    return;
  }

  if (GraphDebugEnabled()) {
    std::fprintf(
        stderr,
        "[rmw_mdds_graph] broker publish_sample entity=%llu kind=%u seq=%llu "
        "bytes=%zu targets=%zu bridge=%p service_like=%d\n",
        static_cast<unsigned long long>(sample.entity_id),
        static_cast<unsigned>(source.kind),
        static_cast<unsigned long long>(sample.sequence_number),
        sample.payload.size(), targets.size(), bridge_publisher,
        IsServiceLikeEndpoint(source.kind) ? 1 : 0);
  }

  for (auto *target : targets) {
    SendSampleDelivery(target, sample, frame.request_id);
  }
  if (bridge_publisher != nullptr &&
      !(IsServiceLikeEndpoint(source.kind) && !targets.empty())) {
    std::unique_lock<std::mutex> publish_lock;
    if (bridge_publisher_mutex != nullptr) {
      publish_lock = std::unique_lock<std::mutex>(*bridge_publisher_mutex);
    }
    WaitForReliableBridgeBackpressure(source, sample, bridge_publisher);
    const int32_t rc = BridgeBackend::Instance().Publish(
        bridge_publisher, sample.payload.data(),
        static_cast<uint32_t>(sample.payload.size()));
    if (GraphDebugEnabled()) {
      std::fprintf(
          stderr,
          "[rmw_mdds_graph] broker bridge_publish entity=%llu seq=%llu "
          "bytes=%zu rc=%d\n",
          static_cast<unsigned long long>(sample.entity_id),
          static_cast<unsigned long long>(sample.sequence_number),
          sample.payload.size(), rc);
    }
  } else if (GraphDebugEnabled()) {
    std::fprintf(
        stderr,
        "[rmw_mdds_graph] broker bridge_publish_skip entity=%llu seq=%llu "
        "bridge=%p targets=%zu service_like=%d\n",
        static_cast<unsigned long long>(sample.entity_id),
        static_cast<unsigned long long>(sample.sequence_number),
        bridge_publisher, targets.size(),
        IsServiceLikeEndpoint(source.kind) ? 1 : 0);
  }
}

bool IpcBroker::SendSampleDelivery(
    Connection *connection, const SampleMessage &sample, uint64_t request_id) {
  if (connection == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> loan_lock(connection->loan_pool_mutex);
  if (connection->loan_pool == nullptr) {
    Frame delivery;
    delivery.kind = MessageKind::kDeliverSample;
    delivery.request_id = request_id;
    delivery.payload = EncodeSampleMessage(sample);
    return SendFrame(connection, delivery);
  }

  std::string error;
  if (!connection->pending_loan_deliveries.empty()) {
    const bool queued =
      QueueLoanedDeliveryLocked(connection, sample, request_id, &error);
    if (!queued && !error.empty()) {
      SendError(connection, request_id, error);
    }
    return queued;
  }

  bool pool_full = false;
  if (SendLoanedDeliveryLocked(
      connection, sample, request_id, &pool_full, &error))
  {
    return true;
  }
  if (pool_full) {
    error.clear();
    const bool queued =
      QueueLoanedDeliveryLocked(connection, sample, request_id, &error);
    if (queued) {
      return true;
    }
    if (!connection->loan_pool_reliable) {
      return false;
    }
  }
  if (!error.empty()) {
    SendError(connection, request_id, error);
  }
  return false;
}

bool IpcBroker::SendLoanedDeliveryLocked(
  Connection * connection, const SampleMessage & sample, uint64_t request_id,
  bool * pool_full, std::string * error)
{
  if (pool_full != nullptr) {
    *pool_full = false;
  }
  if (connection == nullptr || connection->loan_pool == nullptr) {
    if (error != nullptr) {
      *error = "broker loan pool is not available";
    }
    return false;
  }
  const LoanPoolDescriptor & pool = connection->loan_pool->descriptor();
  if (
    (pool.version == kFixedLoanPoolVersion && sample.payload.size() != pool.slot_size) ||
    (pool.version == kDynamicLoanPoolVersion && sample.payload.size() > pool.slot_size))
  {
    if (error != nullptr) {
      *error = pool.version == kFixedLoanPoolVersion ?
        "sample is not the fixed-size raw layout registered for broker loaning" :
        "sample exceeds the negotiated dynamic broker loan payload capacity";
    }
    return false;
  }

  uint64_t loan_id = 0u;
  uint32_t slot_index = 0u;
  if (!connection->loan_pool->Store(
      sample.payload.data(), sample.payload.size(), &loan_id, &slot_index,
      error, pool_full))
  {
    return false;
  }

  LoanedSampleMessage loaned;
  loaned.entity_id = connection->loan_pool_entity_id;
  loaned.loan_id = loan_id;
  loaned.pool_generation = pool.generation;
  loaned.sequence_number = sample.sequence_number;
  loaned.slot_index = slot_index;
  loaned.payload_size = static_cast<uint32_t>(sample.payload.size());
  loaned.mdds_payload = sample.mdds_payload;
  Frame delivery;
  delivery.kind = MessageKind::kDeliverLoanedSample;
  delivery.request_id = request_id;
  delivery.payload = EncodeLoanedSampleMessage(loaned);
  if (!SendFrame(connection, delivery)) {
    (void)connection->loan_pool->Release(loan_id);
    if (error != nullptr) {
      *error = "failed to send broker loan descriptor";
    }
    return false;
  }
  return true;
}

bool IpcBroker::QueueLoanedDeliveryLocked(
  Connection * connection, const SampleMessage & sample, uint64_t request_id,
  std::string * error)
{
  if (connection == nullptr || !connection->loan_pool_reliable) {
    return false;
  }
  if (connection->pending_loan_deliveries.size() >= connection->loan_pool_pending_limit) {
    if (!connection->loan_pool_keep_last) {
      if (error != nullptr) {
        *error = "reliable broker loan pending queue reached its resource limit";
      }
      return false;
    }
    connection->pending_loan_deliveries.pop_front();
  }
  Connection::PendingLoanDelivery pending;
  pending.sample = sample;
  pending.request_id = request_id;
  connection->pending_loan_deliveries.push_back(std::move(pending));
  return true;
}

void IpcBroker::ReturnLoanedSample(Connection *connection, const Frame &frame) {
  uint64_t loan_id = 0u;
  std::string error;
  if (!DecodeLoanReturn(frame.payload.data(), frame.payload.size(), &loan_id,
                        &error) ||
      loan_id == 0u) {
    SendError(connection, frame.request_id,
              error.empty() ? "invalid broker loan return" : error);
    return;
  }
  bool released = false;
  bool pending_delivery_failed = false;
  {
    std::lock_guard<std::mutex> loan_lock(connection->loan_pool_mutex);
    released = connection->loan_pool != nullptr &&
               connection->loan_pool->Release(loan_id);
    if (released && !connection->pending_loan_deliveries.empty()) {
      Connection::PendingLoanDelivery pending =
        std::move(connection->pending_loan_deliveries.front());
      connection->pending_loan_deliveries.pop_front();
      bool pool_full = false;
      pending_delivery_failed = !SendLoanedDeliveryLocked(
        connection, pending.sample, pending.request_id, &pool_full, &error);
      if (pool_full) {
        connection->pending_loan_deliveries.push_front(std::move(pending));
      }
    }
  }
  if (!released) {
    SendError(connection, frame.request_id,
              "broker loan does not belong to this connection or was already returned");
    return;
  }
  if (pending_delivery_failed && !error.empty()) {
    SendError(connection, frame.request_id, error);
  }
  SendAck(connection, frame.request_id);
}

void IpcBroker::DeliverBridgeSample(
    const EndpointDescriptor &subscription_endpoint,
    const std::vector<uint8_t> &payload, uint64_t sequence_number) {
  const uint64_t target_client_entity =
      subscription_endpoint.kind == EndpointKind::kClient
          ? ClientEntityFromServiceWirePayload(payload)
          : 0u;
  SampleMessage sample;
  sample.entity_id = 0u;
  sample.sequence_number = sequence_number;
  sample.mdds_payload = true;
  sample.payload = payload;

  std::vector<Connection *> targets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (target_client_entity != 0u) {
      const auto now = std::chrono::steady_clock::now();
      PruneExpiredBridgeDeliveriesLocked(&recent_bridge_deliveries_, now);
      const uint64_t payload_hash = HashPayload(payload);
      const bool duplicate = std::any_of(
          recent_bridge_deliveries_.begin(), recent_bridge_deliveries_.end(),
          [&subscription_endpoint, target_client_entity, sequence_number,
           payload_hash, &payload](const BridgeDeliveryDedupe &delivery) {
            return delivery.target_entity_id == target_client_entity &&
                   delivery.domain_id == subscription_endpoint.domain_id &&
                   delivery.kind == subscription_endpoint.kind &&
                   delivery.topic_name == subscription_endpoint.topic_name &&
                   delivery.type_name == subscription_endpoint.type_name &&
                   delivery.sequence_number == sequence_number &&
                   delivery.payload_hash == payload_hash &&
                   delivery.payload_size == payload.size();
          });
      if (duplicate) {
        return;
      }
      recent_bridge_deliveries_.push_back(BridgeDeliveryDedupe{
          target_client_entity, subscription_endpoint.domain_id,
          subscription_endpoint.kind, subscription_endpoint.topic_name,
          subscription_endpoint.type_name, sequence_number, payload_hash,
          payload.size(), now + kBridgeDeliveryDedupeTtl});
    }
    const bool has_owning_endpoint =
        subscription_endpoint.entity_id != 0u && target_client_entity == 0u;
    for (const auto &connection : connections_) {
      if (!connection->active.load()) {
        continue;
      }
      const bool matches = std::any_of(
          connection->endpoints.begin(), connection->endpoints.end(),
          [&subscription_endpoint, has_owning_endpoint,
           target_client_entity](const EndpointDescriptor &endpoint) {
            return endpoint.kind == subscription_endpoint.kind &&
                   endpoint.domain_id == subscription_endpoint.domain_id &&
                   (target_client_entity == 0u ||
                    endpoint.entity_id == target_client_entity) &&
                   (!has_owning_endpoint ||
                    endpoint.entity_id == subscription_endpoint.entity_id) &&
                   endpoint.topic_name == subscription_endpoint.topic_name &&
                   endpoint.type_name == subscription_endpoint.type_name;
          });
      if (matches) {
        targets.push_back(connection.get());
      }
    }
  }
  for (auto *target : targets) {
    SendSampleDelivery(target, sample, 0u);
  }
}

void IpcBroker::RequestGraphUpdate(bool publish_local_graph) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.load()) {
      return;
    }
    graph_update_requested_ = true;
    graph_publish_requested_ = graph_publish_requested_ || publish_local_graph;
  }
  graph_update_cv_.notify_one();
}

void IpcBroker::GraphUpdateLoop() {
  auto next_reannounce =
      std::chrono::steady_clock::now() + kGraphReannounceInterval;
  while (true) {
    bool broadcast = false;
    bool publish = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      graph_update_cv_.wait_until(lock, next_reannounce, [this]() {
        return !running_.load() || graph_update_requested_;
      });
      if (!running_.load()) {
        break;
      }

      auto now = std::chrono::steady_clock::now();
      bool heartbeat = now >= next_reannounce;
      if (graph_update_requested_ && !heartbeat) {
        const auto coalesce_deadline = now + kGraphUpdateCoalesceInterval;
        graph_update_cv_.wait_until(lock, coalesce_deadline,
                                    [this]() { return !running_.load(); });
        if (!running_.load()) {
          break;
        }
        now = std::chrono::steady_clock::now();
        heartbeat = now >= next_reannounce;
      }

      broadcast = graph_update_requested_ || (heartbeat && bridge_enabled_);
      publish = bridge_enabled_ && (graph_publish_requested_ || heartbeat);
      graph_update_requested_ = false;
      graph_publish_requested_ = false;
      if (heartbeat) {
        next_reannounce = now + kGraphReannounceInterval;
      }
    }

    if (broadcast) {
      BroadcastGraphUpdate();
    }
    if (publish) {
      PublishLocalGraph();
    }
  }
}

void IpcBroker::BroadcastGraphUpdate() {
  std::vector<Connection *> targets;
  std::vector<EndpointDescriptor> endpoints;
  uint64_t broker_id = 0u;
  uint64_t epoch = 0u;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    broker_id = broker_id_;
    epoch = graph_epoch_.fetch_add(1u) + 1u;
    for (const auto &connection : connections_) {
      if (!connection->active.load()) {
        continue;
      }
      targets.push_back(connection.get());
      endpoints.insert(endpoints.end(), connection->endpoints.begin(),
                       connection->endpoints.end());
      // Cross-board / cross-RMW service availability: the broker graph is built
      // only from LOCAL IPC registrations and has no remote service discovery.
      // Synthesize a kService for each local client only when both service
      // directions have matched over the bridge. A response publisher match
      // alone can make wait_for_service return before rq/ request delivery is
      // ready, which lets rmw_send_request report success while the server
      // never observes the request.
      if (!bridge_enabled_) {
        continue;
      }
      for (const auto &bridge_endpoint : connection->bridge_endpoints) {
        const EndpointKind kind = bridge_endpoint.endpoint.kind;
        const uint32_t request_match_count =
            bridge_endpoint.bridge_publisher == nullptr
                ? 0u
                : BridgeBackend::Instance().PublisherSubCount(
                      bridge_endpoint.bridge_publisher);
        const uint32_t response_match_count =
            bridge_endpoint.bridge_subscription == nullptr
                ? 0u
                : BridgeBackend::Instance().SubscriberPubCount(
                      bridge_endpoint.bridge_subscription);
        const bool remote_service_request_matched = request_match_count != 0u;
        const bool remote_service_response_matched = response_match_count != 0u;
        if (GraphDebugEnabled() &&
            (kind == EndpointKind::kClient || kind == EndpointKind::kService)) {
          std::fprintf(stderr,
                       "[rmw_mdds_graph] bridge match kind=%u name=%s type=%s "
                       "rq=%u rr=%u\n",
                       static_cast<unsigned>(kind),
                       bridge_endpoint.endpoint.topic_name.c_str(),
                       bridge_endpoint.endpoint.type_name.c_str(),
                       request_match_count, response_match_count);
        }
        if (kind == EndpointKind::kClient && remote_service_request_matched &&
            remote_service_response_matched) {
          EndpointDescriptor service = bridge_endpoint.endpoint;
          service.kind = EndpointKind::kService;
          endpoints.push_back(std::move(service));
        } else if (kind == EndpointKind::kSubscription &&
                   bridge_endpoint.bridge_subscription != nullptr &&
                   BridgeBackend::Instance().SubscriberPubCount(
                       bridge_endpoint.bridge_subscription) != 0u) {
          // Symmetric to kService: a local subscription whose bridge subscriber
          // has matched remote publishers means a remote publisher exists (e.g.
          // an action feedback/status topic, or any wait-for-publisher).
          // Surface it as a kPublisher so cross-board availability checks see
          // it.
          EndpointDescriptor publisher = bridge_endpoint.endpoint;
          publisher.kind = EndpointKind::kPublisher;
          endpoints.push_back(std::move(publisher));
        }
      }
    }
    // Cross-board node discovery: include nodes a gateway announced over MDDS.
    endpoints.insert(endpoints.end(), remote_node_endpoints_.begin(),
                     remote_node_endpoints_.end());
    // Cross-board graph: merge every peer broker's full endpoint list (carrying
    // their real ROS node/topic/service identities). Each source bucket is
    // aged out after kGraphPeerTtl so a board that drops off (no re-announce)
    // stops appearing in this broker's graph.
    const auto now = std::chrono::steady_clock::now();
    for (auto it = remote_graph_endpoints_.begin();
         it != remote_graph_endpoints_.end();) {
      if (now - it->second.last_seen > kGraphPeerTtl) {
        it = remote_graph_endpoints_.erase(it);
        continue;
      }
      for (const auto &remote_endpoint : it->second.endpoints) {
        EndpointDescriptor endpoint = remote_endpoint;
        // Peer graph entries are visible for ROS graph introspection, but they
        // are not proof that this broker's local bridge request/response path
        // has matched. Keep them out of local service availability decisions.
        endpoint.local_context_id = 0u;
        endpoints.push_back(std::move(endpoint));
      }
      ++it;
    }
  }

  Frame update;
  update.kind = MessageKind::kGraphUpdate;
  update.request_id = 0u;
  update.payload = EncodeGraphUpdate(broker_id, epoch, endpoints);
  if (GraphDebugEnabled()) {
    std::fprintf(stderr,
                 "[rmw_mdds_graph] broadcast broker_id=%llu epoch=%llu "
                 "endpoints=%zu targets=%zu\n",
                 static_cast<unsigned long long>(broker_id),
                 static_cast<unsigned long long>(epoch), endpoints.size(),
                 targets.size());
  }
  for (auto *target : targets) {
    SendFrame(target, update);
  }
}

void IpcBroker::ReleaseBridgePublisher(void *bridge_publisher, bool shared,
                                       const std::string &bridge_topic,
                                       const std::string &bridge_type) {
  if (bridge_publisher == nullptr) {
    return;
  }
  if (shared) {
    auto shared_it = std::find_if(
        shared_bridge_publishers_.begin(), shared_bridge_publishers_.end(),
        [bridge_publisher, &bridge_topic,
         &bridge_type](const SharedBridgePublisher &candidate) {
          return candidate.bridge_publisher == bridge_publisher &&
                 candidate.bridge_topic == bridge_topic &&
                 candidate.bridge_type == bridge_type;
        });
    if (shared_it != shared_bridge_publishers_.end()) {
      if (shared_it->ref_count > 0u) {
        --shared_it->ref_count;
      }
      if (shared_it->ref_count != 0u) {
        return;
      }
      std::lock_guard<std::mutex> publish_lock(*shared_it->publish_mutex);
      BridgeBackend::Instance().PublisherSetOnMatched(
          shared_it->bridge_publisher, nullptr, nullptr);
      BridgeBackend::Instance().DestroyPublisher(shared_it->bridge_publisher);
      shared_bridge_publishers_.erase(shared_it);
      return;
    }
  }
  BridgeBackend::Instance().PublisherSetOnMatched(bridge_publisher, nullptr,
                                                  nullptr);
  BridgeBackend::Instance().DestroyPublisher(bridge_publisher);
}

void IpcBroker::DestroyBridgeEndpoints(Connection *connection) {
  if (connection == nullptr) {
    return;
  }
  for (auto &endpoint : connection->bridge_endpoints) {
    if (endpoint.bridge_publisher != nullptr) {
      ReleaseBridgePublisher(
          endpoint.bridge_publisher, endpoint.shared_bridge_publisher,
          endpoint.bridge_publisher_topic, endpoint.bridge_publisher_type);
      endpoint.bridge_publisher = nullptr;
      endpoint.bridge_publisher_mutex.reset();
    }
    if (endpoint.bridge_subscription != nullptr) {
      if (endpoint.shared_bridge_subscription) {
        auto shared_it = std::find_if(
            shared_bridge_subscriptions_.begin(),
            shared_bridge_subscriptions_.end(),
            [&endpoint](const SharedBridgeSubscription &shared) {
              return shared.bridge_subscription ==
                         endpoint.bridge_subscription &&
                     shared.bridge_topic ==
                         endpoint.bridge_subscription_topic &&
                     shared.bridge_type == endpoint.bridge_subscription_type;
            });
        if (shared_it != shared_bridge_subscriptions_.end()) {
          if (shared_it->ref_count > 0u) {
            --shared_it->ref_count;
          }
          if (shared_it->ref_count == 0u) {
            BridgeBackend::Instance().SubscriberSetOnMatched(
                shared_it->bridge_subscription, nullptr, nullptr);
            BridgeBackend::Instance().Unsubscribe(
                shared_it->bridge_subscription);
            shared_bridge_subscriptions_.erase(shared_it);
          }
        }
      } else {
        BridgeBackend::Instance().SubscriberSetOnMatched(
            endpoint.bridge_subscription, nullptr, nullptr);
        BridgeBackend::Instance().Unsubscribe(endpoint.bridge_subscription);
      }
      endpoint.bridge_subscription = nullptr;
    }
  }
  connection->bridge_endpoints.clear();
}

void IpcBroker::RemoveBridgeEndpointForEntity(Connection *connection,
                                              uint64_t entity_id) {
  if (connection == nullptr) {
    return;
  }
  auto it = connection->bridge_endpoints.begin();
  while (it != connection->bridge_endpoints.end()) {
    if (it->endpoint.entity_id != entity_id) {
      ++it;
      continue;
    }
    if (it->bridge_publisher != nullptr) {
      ReleaseBridgePublisher(it->bridge_publisher, it->shared_bridge_publisher,
                             it->bridge_publisher_topic,
                             it->bridge_publisher_type);
      it->bridge_publisher = nullptr;
      it->bridge_publisher_mutex.reset();
    }
    if (it->bridge_subscription != nullptr) {
      if (it->shared_bridge_subscription) {
        auto shared_it = std::find_if(
            shared_bridge_subscriptions_.begin(),
            shared_bridge_subscriptions_.end(),
            [&it](const SharedBridgeSubscription &shared) {
              return shared.bridge_subscription == it->bridge_subscription &&
                     shared.bridge_topic == it->bridge_subscription_topic &&
                     shared.bridge_type == it->bridge_subscription_type;
            });
        if (shared_it != shared_bridge_subscriptions_.end()) {
          if (shared_it->ref_count > 0u) {
            --shared_it->ref_count;
          }
          if (shared_it->ref_count == 0u) {
            BridgeBackend::Instance().SubscriberSetOnMatched(
                shared_it->bridge_subscription, nullptr, nullptr);
            BridgeBackend::Instance().Unsubscribe(
                shared_it->bridge_subscription);
            shared_bridge_subscriptions_.erase(shared_it);
          }
        }
      } else {
        BridgeBackend::Instance().SubscriberSetOnMatched(
            it->bridge_subscription, nullptr, nullptr);
        BridgeBackend::Instance().Unsubscribe(it->bridge_subscription);
      }
      it->bridge_subscription = nullptr;
    }
    it = connection->bridge_endpoints.erase(it);
  }
}

void IpcBroker::BridgeSampleCallback(const BridgeSample *sample,
                                     void *user_data) {
  auto *state = static_cast<BridgeSubscriptionState *>(user_data);
  if (state == nullptr || state->broker == nullptr || sample == nullptr ||
      (sample->data == nullptr && sample->len != 0u)) {
    return;
  }
  const auto *data = static_cast<const uint8_t *>(sample->data);
  std::vector<uint8_t> payload;
  if (data != nullptr && sample->len != 0u) {
    payload.assign(data, data + sample->len);
  }
  state->broker->DeliverBridgeSample(state->endpoint, payload,
                                     sample->sequenceNumber);
}

void IpcBroker::NodeSyncBridgeCallback(const BridgeSample *sample,
                                       void *user_data) {
  auto *broker = static_cast<IpcBroker *>(user_data);
  if (broker == nullptr || sample == nullptr ||
      (sample->data == nullptr && sample->len != 0u)) {
    return;
  }
  const auto *data = static_cast<const uint8_t *>(sample->data);
  std::vector<uint8_t> payload;
  if (data != nullptr && sample->len != 0u) {
    payload.assign(data, data + sample->len);
  }
  if (GraphDebugEnabled()) {
    std::fprintf(
        stderr, "[rmw_mdds_graph] node sync receive bytes=%u seq=%llu\n",
        sample->len, static_cast<unsigned long long>(sample->sequenceNumber));
  }
  broker->OnNodeSync(payload);
}

void IpcBroker::OnNodeSync(const std::vector<uint8_t> &payload) {
  // Payload is the gateway's DDS-side node list: one fully-qualified name per
  // line (e.g. "/parameter_blackboard"). Surface each as a hidden, node-bearing
  // synthetic endpoint so this broker's get_node_names() lists it.
  const std::string text(payload.begin(), payload.end());
  std::vector<EndpointDescriptor> endpoints;
  size_t start = 0;
  while (start < text.size()) {
    const size_t nl = text.find('\n', start);
    const std::string fqn = text.substr(
        start, nl == std::string::npos ? std::string::npos : nl - start);
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
    ep.domain_id = node_sync_domain_id_;
    ep.kind = EndpointKind::kSubscription;
    ep.node_name = name;
    ep.node_namespace = ns;
    ep.topic_name =
        "_mdds_remote_node"; // hidden topic, filtered from `ros2 topic list`
    ep.type_name = "mdds_graph/msg/RemoteNode";
    endpoints.push_back(std::move(ep));
  }
  if (GraphDebugEnabled()) {
    std::fprintf(stderr,
                 "[rmw_mdds_graph] node sync parsed domain=%u bytes=%zu "
                 "nodes=%zu\n",
                 node_sync_domain_id_, payload.size(), endpoints.size());
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (remote_node_endpoints_.size() == endpoints.size()) {
      bool same = true;
      for (size_t i = 0; i < endpoints.size(); ++i) {
        if (remote_node_endpoints_[i].node_name != endpoints[i].node_name ||
            remote_node_endpoints_[i].node_namespace !=
                endpoints[i].node_namespace) {
          same = false;
          break;
        }
      }
      if (same) {
        return; // unchanged node set; avoid a needless rebroadcast
      }
    }
    remote_node_endpoints_ = std::move(endpoints);
  }
  RequestGraphUpdate(false);
}

void IpcBroker::PublishLocalGraph() {
  void *publisher = nullptr;
  void *subscription = nullptr;
  uint64_t broker_id = 0u;
  uint64_t epoch = 0u;
  std::vector<EndpointDescriptor> local;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    publisher = graph_sync_publisher_;
    subscription = graph_sync_subscription_;
    broker_id = broker_id_;
    if (publisher == nullptr || broker_id == 0u) {
      if (GraphDebugEnabled()) {
        std::fprintf(
            stderr,
            "[rmw_mdds_graph] publish skip publisher=%p broker_id=%llu\n",
            publisher, static_cast<unsigned long long>(broker_id));
      }
      return;
    }
    for (const auto &connection : connections_) {
      if (!connection->active.load()) {
        continue;
      }
      local.insert(local.end(), connection->endpoints.begin(),
                   connection->endpoints.end());
    }
  }

  uint32_t graph_pub_matches = 0u;
  uint32_t graph_sub_matches = 0u;
  if (GraphDebugEnabled()) {
    graph_pub_matches =
        publisher == nullptr
            ? 0u
            : BridgeBackend::Instance().PublisherSubCount(publisher);
    graph_sub_matches =
        subscription == nullptr
            ? 0u
            : BridgeBackend::Instance().SubscriberPubCount(subscription);
  }

  const std::vector<uint8_t> body = EncodeEndpointList(local);
  const uint64_t body_hash = HashPayload(body);
  const auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (publisher != graph_sync_publisher_ || broker_id != broker_id_) {
      return;
    }
    const bool unchanged =
        last_graph_sync_body_hash_ == body_hash &&
        last_graph_sync_body_size_ == body.size() &&
        last_graph_sync_publish_ != std::chrono::steady_clock::time_point{};
    if (unchanged &&
        now - last_graph_sync_publish_ < kGraphSyncUnchangedPublishInterval) {
      if (GraphDebugEnabled()) {
        std::fprintf(stderr,
                     "[rmw_mdds_graph] publish skip unchanged broker_id=%llu "
                     "endpoints=%zu bytes=%zu graph_pub_matches=%u "
                     "graph_sub_matches=%u\n",
                     static_cast<unsigned long long>(broker_id), local.size(),
                     body.size() + kGraphSyncHeaderSize, graph_pub_matches,
                     graph_sub_matches);
      }
      return;
    }
    if (!unchanged &&
        last_graph_sync_publish_ != std::chrono::steady_clock::time_point{} &&
        now - last_graph_sync_publish_ < kGraphSyncChangedPublishInterval) {
      graph_sync_publish_dirty_ = true;
      if (GraphDebugEnabled()) {
        std::fprintf(stderr,
                     "[rmw_mdds_graph] publish coalesce broker_id=%llu "
                     "endpoints=%zu bytes=%zu graph_pub_matches=%u "
                     "graph_sub_matches=%u\n",
                     static_cast<unsigned long long>(broker_id), local.size(),
                     body.size() + kGraphSyncHeaderSize, graph_pub_matches,
                     graph_sub_matches);
      }
      return;
    }
    epoch = graph_epoch_.fetch_add(1u) + 1u;
    last_graph_sync_body_hash_ = body_hash;
    last_graph_sync_body_size_ = body.size();
    last_graph_sync_publish_ = now;
    graph_sync_publish_dirty_ = false;
  }

  std::vector<uint8_t> payload;
  AppendU64Le(payload, broker_id);
  AppendU64Le(payload, epoch);
  payload.insert(payload.end(), body.begin(), body.end());
  // Publish outside the broker lock. Stop() joins every thread that can call
  // this (the re-announce timer and the connection threads) before destroying
  // the publisher, so the captured pointer is valid for the duration of this
  // call.
  const int32_t rc = BridgeBackend::Instance().Publish(
      publisher, payload.data(), static_cast<uint32_t>(payload.size()));
  if (GraphDebugEnabled()) {
    std::fprintf(stderr,
                 "[rmw_mdds_graph] publish broker_id=%llu epoch=%llu "
                 "endpoints=%zu bytes=%zu rc=%d graph_pub_matches=%u "
                 "graph_sub_matches=%u\n",
                 static_cast<unsigned long long>(broker_id),
                 static_cast<unsigned long long>(epoch), local.size(),
                 payload.size(), rc, graph_pub_matches, graph_sub_matches);
  }
}

void IpcBroker::GraphSyncBridgeCallback(const BridgeSample *sample,
                                        void *user_data) {
  auto *broker = static_cast<IpcBroker *>(user_data);
  if (broker == nullptr || sample == nullptr ||
      (sample->data == nullptr && sample->len != 0u)) {
    if (GraphDebugEnabled()) {
      std::fprintf(
          stderr,
          "[rmw_mdds_graph] callback drop invalid sample=%p broker=%p\n",
          static_cast<const void *>(sample), static_cast<void *>(broker));
    }
    return;
  }
  if (GraphDebugEnabled()) {
    std::fprintf(stderr, "[rmw_mdds_graph] callback len=%u seq=%llu data=%p\n",
                 sample->len,
                 static_cast<unsigned long long>(sample->sequenceNumber),
                 sample->data);
  }
  const auto *data = static_cast<const uint8_t *>(sample->data);
  std::vector<uint8_t> payload;
  if (data != nullptr && sample->len != 0u) {
    payload.assign(data, data + sample->len);
  }
  broker->OnGraphSync(payload);
}

void IpcBroker::OnGraphSync(const std::vector<uint8_t> &payload) {
  if (payload.size() < kGraphSyncHeaderSize) {
    if (GraphDebugEnabled()) {
      std::fprintf(stderr, "[rmw_mdds_graph] graph sync drop short bytes=%zu\n",
                   payload.size());
    }
    return;
  }
  const uint64_t src = ReadU64Le(payload.data());
  const uint64_t epoch = ReadU64Le(payload.data() + 8u);
  if (src == 0u || src == broker_id_) {
    if (GraphDebugEnabled()) {
      std::fprintf(stderr,
                   "[rmw_mdds_graph] graph sync drop self_or_empty src=%llu "
                   "self=%llu epoch=%llu bytes=%zu\n",
                   static_cast<unsigned long long>(src),
                   static_cast<unsigned long long>(broker_id_),
                   static_cast<unsigned long long>(epoch), payload.size());
    }
    return; // unset source id, or our own echo over the N:N bridge topic
  }
  std::vector<EndpointDescriptor> endpoints;
  std::string error;
  if (!DecodeEndpointList(payload.data() + kGraphSyncHeaderSize,
                          payload.size() - kGraphSyncHeaderSize, &endpoints,
                          &error)) {
    if (GraphDebugEnabled()) {
      std::fprintf(stderr,
                   "[rmw_mdds_graph] graph sync decode fail src=%llu "
                   "epoch=%llu bytes=%zu error=%s\n",
                   static_cast<unsigned long long>(src),
                   static_cast<unsigned long long>(epoch), payload.size(),
                   error.c_str());
    }
    return;
  }
  const size_t endpoint_count = endpoints.size();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto &bucket = remote_graph_endpoints_[src];
    // Per-source epoch is monotonic; ignore an out-of-order/duplicate frame but
    // still refresh last_seen so the peer is not aged out. A fresh bucket has
    // epoch 0, so the peer's first frame (epoch >= 1) always lands.
    if (bucket.epoch != 0u && epoch != 0u && epoch <= bucket.epoch) {
      bucket.last_seen = std::chrono::steady_clock::now();
      if (GraphDebugEnabled()) {
        std::fprintf(stderr,
                     "[rmw_mdds_graph] graph sync stale src=%llu epoch=%llu "
                     "current=%llu endpoints=%zu\n",
                     static_cast<unsigned long long>(src),
                     static_cast<unsigned long long>(epoch),
                     static_cast<unsigned long long>(bucket.epoch),
                     endpoint_count);
      }
      return;
    }
    bucket.epoch = epoch;
    bucket.last_seen = std::chrono::steady_clock::now();
    bucket.endpoints = std::move(endpoints);
  }
  if (GraphDebugEnabled()) {
    std::fprintf(stderr,
                 "[rmw_mdds_graph] graph sync accept src=%llu epoch=%llu "
                 "endpoints=%zu\n",
                 static_cast<unsigned long long>(src),
                 static_cast<unsigned long long>(epoch), endpoint_count);
  }
  RequestGraphUpdate(false);
}

bool IpcBroker::SendFrame(Connection *connection, const Frame &frame) {
  if (connection == nullptr || connection->fd.get() < 0) {
    return false;
  }
  std::string error;
  std::lock_guard<std::mutex> lock(connection->write_mutex);
  return WriteFrame(connection->fd.get(), frame, &error);
}

void IpcBroker::SendAck(Connection *connection, uint64_t request_id,
                        const std::vector<uint8_t> &payload) {
  SendFrame(connection, Frame{MessageKind::kAck, request_id, payload});
}

void IpcBroker::SendError(Connection *connection, uint64_t request_id,
                          const std::string &message) {
  SendFrame(connection,
            Frame{MessageKind::kError, request_id,
                  std::vector<uint8_t>(message.begin(), message.end())});
}

} // namespace ipc
} // namespace rmw_mdds_cpp
