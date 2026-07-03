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

#ifndef RMW_MDDS_CPP_SRC__IPC_BROKER_HPP_
#define RMW_MDDS_CPP_SRC__IPC_BROKER_HPP_

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ipc_protocol.hpp"
#include "ipc_transport.hpp"

namespace rmw_mdds_cpp
{
struct BridgeSample;

namespace ipc
{

struct RetainedSample
{
  void * owner = nullptr;
  EndpointDescriptor source;
  SampleMessage sample;
};

class IpcBroker
{
public:
  IpcBroker();
  ~IpcBroker();

  IpcBroker(const IpcBroker &) = delete;
  IpcBroker & operator=(const IpcBroker &) = delete;

  bool Start(const std::string & socket_path, std::string * error);
  void Stop();
  bool IsRunning() const;

private:
  struct Connection;
  struct BridgeSubscriptionState;

  void AcceptLoop();
  void ClientLoop(Connection * connection);
  void HandleFrame(Connection * connection, const Frame & frame);
  void RegisterEndpoint(Connection * connection, const Frame & frame, EndpointKind expected_kind);
  void PublishSample(Connection * connection, const Frame & frame);
  void DeliverBridgeSample(
    const EndpointDescriptor & subscription_endpoint, const std::vector<uint8_t> & payload,
    uint64_t sequence_number);
  void BroadcastGraphUpdate();
  void DestroyBridgeEndpoints(Connection * connection);
  static void BridgeSampleCallback(const BridgeSample * sample, void * user_data);
  /* Matched-count listener on a local client's rq/ bridge publisher; rebroadcasts
   * the graph so a remote service provider becomes visible as a kService. */
  static void OnBridgePublisherMatched(uint32_t matched_count, void * user_data);
  /* Receives the DDS-side node list a gateway publishes over MDDS, so remote
   * nodes can be surfaced in this broker's graph (cross-board node discovery). */
  static void NodeSyncBridgeCallback(const BridgeSample * sample, void * user_data);
  void OnNodeSync(const std::vector<uint8_t> & payload);
  /* Cross-board graph introspection: every broker publishes its full local
   * endpoint list (real ROS node_name/topic/service identities) over the bridge;
   * peers merge it so get_node_names / get_*_names_and_types resolve cross-board.
   * Generalises the gateway-only node-sync channel to every rmw_mdds broker. */
  static void GraphSyncBridgeCallback(const BridgeSample * sample, void * user_data);
  void OnGraphSync(const std::vector<uint8_t> & payload);
  void PublishLocalGraph();
  bool SendFrame(Connection * connection, const Frame & frame);
  void SendAck(Connection * connection, uint64_t request_id);
  void SendError(Connection * connection, uint64_t request_id, const std::string & message);

  mutable std::mutex mutex_;
  std::atomic<bool> running_{false};
  std::string socket_path_;
  UniqueFd listener_;
  std::thread accept_thread_;
  std::vector<std::unique_ptr<Connection>> connections_;
  std::vector<RetainedSample> retained_samples_;
  bool bridge_enabled_ = false;
  void * node_sync_subscription_ = nullptr;
  std::vector<EndpointDescriptor> remote_node_endpoints_;

  // Cross-board graph sync state. Each peer broker is keyed by a random
  // broker_id_; its latest full endpoint list is kept in a per-source bucket
  // (whole-list replace + epoch ordering so a peer's removals propagate, plus a
  // last-seen TTL so a vanished board's entries age out).
  struct RemoteGraphBucket
  {
    std::chrono::steady_clock::time_point last_seen{};
    uint64_t epoch = 0u;
    std::vector<EndpointDescriptor> endpoints;
  };
  uint64_t broker_id_ = 0u;
  std::atomic<uint64_t> graph_epoch_{0u};
  void * graph_sync_publisher_ = nullptr;
  void * graph_sync_subscription_ = nullptr;
  std::map<uint64_t, RemoteGraphBucket> remote_graph_endpoints_;
  std::thread graph_reannounce_thread_;
};

}  // namespace ipc
}  // namespace rmw_mdds_cpp

#endif  // RMW_MDDS_CPP_SRC__IPC_BROKER_HPP_
