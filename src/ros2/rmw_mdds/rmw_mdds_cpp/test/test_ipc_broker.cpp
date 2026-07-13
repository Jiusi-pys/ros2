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

#include <gtest/gtest.h>

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "bridge_backend.hpp"
#include "ipc_broker.hpp"
#include "ipc_protocol.hpp"
#include "ipc_transport.hpp"

extern "C" {
void FakeMddsBridgeReset(void);
void FakeMddsBridgeFailNextPublisherCreate(void);
void FakeMddsBridgeFailNextSubscriberCreate(void);
int FakeMddsBridgeInitCount(void);
int FakeMddsBridgeShutdownCount(void);
int FakeMddsBridgeProtectedTransportActivateCount(void);
int FakeMddsBridgeProtectedTransportAuthenticated(void);
int FakeMddsBridgeProtectedTransportEncrypted(void);
int FakeMddsBridgePublisherCount(void);
int FakeMddsBridgeSubscriberCount(void);
int FakeMddsBridgeHasPublisher(const char *topicName, const char *typeName);
int FakeMddsBridgeHasSubscriber(const char *topicName, const char *typeName);
int FakeMddsBridgeSubscriberCountFor(const char *topicName,
                                     const char *typeName);
int FakeMddsBridgePublisherPublishCount(const char *topicName,
                                        const char *typeName);
void FakeMddsBridgeSetPublisherUnackedCount(const char *topicName,
                                            const char *typeName,
                                            uint32_t count);
int FakeMddsBridgePublisherHeartbeatNowCount(const char *topicName,
                                             const char *typeName);
uint32_t FakeMddsBridgePublisherHistoryDepth(const char *topicName,
                                             const char *typeName);
int FakeMddsBridgePublisherHistoryKind(const char *topicName,
                                       const char *typeName);
int FakeMddsBridgePublisherReliability(const char *topicName,
                                       const char *typeName);
const uint8_t *FakeMddsBridgePublisherLastPayloadData(const char *topicName,
                                                      const char *typeName);
uint32_t FakeMddsBridgePublisherLastPayloadLen(const char *topicName,
                                               const char *typeName);
uint32_t FakeMddsBridgeSubscriberHistoryDepth(const char *topicName,
                                              const char *typeName);
int FakeMddsBridgeSubscriberHistoryKind(const char *topicName,
                                        const char *typeName);
int FakeMddsBridgeSubscriberReliability(const char *topicName,
                                        const char *typeName);
void FakeMddsBridgeInject(const void *data, uint32_t len);
int FakeMddsBridgeInjectFor(const char *topicName, const char *typeName,
                            const void *data, uint32_t len,
                            uint64_t sequenceNumber);
MddsBridgeSubscriber *MddsBridgeSubscribeQos(const char *topicName,
                                             const char *typeName,
                                             const MddsBridgeQos *qos,
                                             MddsBridgeDataCallback cb,
                                             void *userData);
void MddsBridgeUnsubscribe(MddsBridgeSubscriber *sub);
MddsBridgePublisher *MddsBridgeCreatePublisherQos(const char *topicName,
                                                  const char *typeName,
                                                  const MddsBridgeQos *qos);
void MddsBridgeDestroyPublisher(MddsBridgePublisher *pub);
}

namespace {
class EnvVarGuard {
public:
  explicit EnvVarGuard(const char *name) : name_(name) {
    const char *value = std::getenv(name);
    if (value != nullptr) {
      had_value_ = true;
      value_ = value;
    }
  }

  ~EnvVarGuard() {
    if (had_value_) {
      setenv(name_.c_str(), value_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

private:
  std::string name_;
  bool had_value_ = false;
  std::string value_;
};

class TempSocketPath {
public:
  TempSocketPath() {
    char templ[] = "/tmp/rmw_mdds_ipc_broker_XXXXXX";
    char *dir = mkdtemp(templ);
    if (dir != nullptr) {
      dir_ = dir;
      path_ = dir_ + "/broker.sock";
    }
  }

  ~TempSocketPath() {
    if (!path_.empty()) {
      unlink(path_.c_str());
    }
    if (!dir_.empty()) {
      rmdir(dir_.c_str());
    }
  }

  const std::string &path() const { return path_; }

private:
  std::string dir_;
  std::string path_;
};

rmw_mdds_cpp::ipc::EndpointDescriptor
MakeEndpoint(uint64_t entity_id, rmw_mdds_cpp::ipc::EndpointKind kind,
             const char *topic_name, const char *type_name) {
  rmw_mdds_cpp::ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = entity_id;
  endpoint.kind = kind;
  endpoint.node_name = "broker_test_node";
  endpoint.node_namespace = "/broker";
  endpoint.topic_name = topic_name;
  endpoint.type_name = type_name;
  endpoint.mdds_type_name = type_name;
  endpoint.qos = rmw_qos_profile_default;
  return endpoint;
}

void AppendI64(std::vector<uint8_t> *out, int64_t value) {
  ASSERT_NE(nullptr, out);
  for (size_t i = 0; i < sizeof(value); ++i) {
    out->push_back(static_cast<uint8_t>(
        (static_cast<uint64_t>(value) >> (8u * i)) & 0xffu));
  }
}

std::vector<uint8_t> MakeServiceWirePayload(uint64_t client_entity_id,
                                            int64_t sequence_number,
                                            const std::vector<uint8_t> &body) {
  std::vector<uint8_t> payload;
  AppendI64(&payload, sequence_number);
  uint8_t guid[RMW_GID_STORAGE_SIZE] = {};
  const uintptr_t address = static_cast<uintptr_t>(client_entity_id);
  std::memcpy(
      guid, &address,
      std::min(sizeof(address), static_cast<size_t>(RMW_GID_STORAGE_SIZE)));
  payload.insert(payload.end(), guid, guid + RMW_GID_STORAGE_SIZE);
  AppendI64(&payload, 123456789);
  payload.insert(payload.end(), body.begin(), body.end());
  return payload;
}

void ExpectValidGraphUpdate(const rmw_mdds_cpp::ipc::Frame &frame) {
  std::string error;
  rmw_mdds_cpp::ipc::GraphUpdateMessage update;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeGraphUpdate(
      frame.payload.data(), frame.payload.size(), &update, &error))
      << error;
}

bool GraphUpdateContainsEndpoint(const rmw_mdds_cpp::ipc::Frame &frame,
                                 rmw_mdds_cpp::ipc::EndpointKind kind,
                                 const char *topic_name,
                                 const char *type_name) {
  std::string error;
  rmw_mdds_cpp::ipc::GraphUpdateMessage update;
  if (frame.kind != rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate ||
      !rmw_mdds_cpp::ipc::DecodeGraphUpdate(
          frame.payload.data(), frame.payload.size(), &update, &error)) {
    return false;
  }
  return std::any_of(
      update.endpoints.begin(), update.endpoints.end(),
      [kind, topic_name,
       type_name](const rmw_mdds_cpp::ipc::EndpointDescriptor &endpoint) {
        return endpoint.kind == kind && endpoint.topic_name == topic_name &&
               endpoint.type_name == type_name;
      });
}

bool FindGraphEndpoint(const rmw_mdds_cpp::ipc::Frame &frame,
                       rmw_mdds_cpp::ipc::EndpointKind kind,
                       const char *topic_name, const char *type_name,
                       rmw_mdds_cpp::ipc::EndpointDescriptor *out) {
  std::string error;
  rmw_mdds_cpp::ipc::GraphUpdateMessage update;
  if (frame.kind != rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate ||
      !rmw_mdds_cpp::ipc::DecodeGraphUpdate(
          frame.payload.data(), frame.payload.size(), &update, &error)) {
    return false;
  }
  auto it = std::find_if(
      update.endpoints.begin(), update.endpoints.end(),
      [kind, topic_name,
       type_name](const rmw_mdds_cpp::ipc::EndpointDescriptor &endpoint) {
        return endpoint.kind == kind && endpoint.topic_name == topic_name &&
               endpoint.type_name == type_name;
      });
  if (it == update.endpoints.end()) {
    return false;
  }
  if (out != nullptr) {
    *out = *it;
  }
  return true;
}

void RegisterEndpoint(int fd,
                      const rmw_mdds_cpp::ipc::EndpointDescriptor &endpoint,
                      uint64_t request_id,
                      rmw_mdds_cpp::ipc::Frame *registration_ack = nullptr) {
  rmw_mdds_cpp::ipc::MessageKind kind = rmw_mdds_cpp::ipc::MessageKind::kError;
  switch (endpoint.kind) {
  case rmw_mdds_cpp::ipc::EndpointKind::kPublisher:
    kind = rmw_mdds_cpp::ipc::MessageKind::kRegisterPublisher;
    break;
  case rmw_mdds_cpp::ipc::EndpointKind::kSubscription:
    kind = rmw_mdds_cpp::ipc::MessageKind::kRegisterSubscription;
    break;
  case rmw_mdds_cpp::ipc::EndpointKind::kClient:
    kind = rmw_mdds_cpp::ipc::MessageKind::kRegisterClient;
    break;
  case rmw_mdds_cpp::ipc::EndpointKind::kService:
    kind = rmw_mdds_cpp::ipc::MessageKind::kRegisterService;
    break;
  }
  std::string error;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      fd,
      rmw_mdds_cpp::ipc::Frame{
          kind, request_id,
          rmw_mdds_cpp::ipc::EncodeEndpointDescriptor(endpoint)},
      &error))
      << error;

  rmw_mdds_cpp::ipc::Frame ack;
  for (;;) {
    ASSERT_EQ(rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
              rmw_mdds_cpp::ipc::ReadFrame(fd, &ack, &error))
        << error;
    if (ack.kind == rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate) {
      ExpectValidGraphUpdate(ack);
      continue;
    }
    break;
  }
  EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kAck, ack.kind);
  EXPECT_EQ(request_id, ack.request_id);
  if (registration_ack != nullptr) {
    *registration_ack = std::move(ack);
  } else {
    EXPECT_TRUE(ack.payload.empty());
  }
}

bool HasReadableData(int fd) {
  pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  return poll(&pfd, 1u, 50) > 0 && (pfd.revents & POLLIN) != 0;
}

bool WaitForGraphEndpoint(int fd, rmw_mdds_cpp::ipc::EndpointKind kind,
                          const char *topic_name, const char *type_name,
                          std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::string error;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!HasReadableData(fd)) {
      usleep(10000);
      continue;
    }
    rmw_mdds_cpp::ipc::Frame frame;
    if (rmw_mdds_cpp::ipc::ReadFrame(fd, &frame, &error) !=
        rmw_mdds_cpp::ipc::ReadFrameStatus::kOk) {
      return false;
    }
    if (GraphUpdateContainsEndpoint(frame, kind, topic_name, type_name)) {
      return true;
    }
  }
  return false;
}

void DrainReadableFrames(int fd, std::chrono::milliseconds duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  std::string error;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!HasReadableData(fd)) {
      usleep(10000);
      continue;
    }
    rmw_mdds_cpp::ipc::Frame frame;
    if (rmw_mdds_cpp::ipc::ReadFrame(fd, &frame, &error) !=
        rmw_mdds_cpp::ipc::ReadFrameStatus::kOk) {
      return;
    }
  }
}

bool WaitForFakePublisherPublishCountAtLeast(
    const char *topic_name, const char *type_name, int expected_count,
    std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (FakeMddsBridgePublisherPublishCount(topic_name, type_name) >=
        expected_count) {
      return true;
    }
    usleep(10000);
  }
  return FakeMddsBridgePublisherPublishCount(topic_name, type_name) >=
         expected_count;
}

void ReadNextNonGraphFrame(int fd, rmw_mdds_cpp::ipc::Frame *frame,
                           std::string *error) {
  ASSERT_NE(nullptr, frame);
  for (;;) {
    ASSERT_EQ(rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
              rmw_mdds_cpp::ipc::ReadFrame(fd, frame, error))
        << (error == nullptr ? "" : *error);
    if (frame->kind == rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate) {
      ExpectValidGraphUpdate(*frame);
      continue;
    }
    return;
  }
}

bool ReadUntilPayload(int fd, const std::vector<uint8_t> &expected_payload,
                      rmw_mdds_cpp::ipc::SampleMessage *sample,
                      std::string *error) {
  for (int i = 0; i < 8; ++i) {
    rmw_mdds_cpp::ipc::Frame delivery;
    ReadNextNonGraphFrame(fd, &delivery, error);
    EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kDeliverSample, delivery.kind);
    rmw_mdds_cpp::ipc::SampleMessage delivered_sample;
    if (!rmw_mdds_cpp::ipc::DecodeSampleMessage(delivery.payload.data(),
                                                delivery.payload.size(),
                                                &delivered_sample, error)) {
      return false;
    }
    if (delivered_sample.payload == expected_payload) {
      if (sample != nullptr) {
        *sample = std::move(delivered_sample);
      }
      return true;
    }
  }
  return false;
}

bool WaitForPayload(int fd, const std::vector<uint8_t> &expected_payload,
                    rmw_mdds_cpp::ipc::SampleMessage *sample,
                    std::string *error, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!HasReadableData(fd)) {
      usleep(10000);
      continue;
    }
    rmw_mdds_cpp::ipc::Frame delivery;
    if (rmw_mdds_cpp::ipc::ReadFrame(fd, &delivery, error) !=
        rmw_mdds_cpp::ipc::ReadFrameStatus::kOk) {
      return false;
    }
    if (delivery.kind == rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate) {
      ExpectValidGraphUpdate(delivery);
      continue;
    }
    if (delivery.kind != rmw_mdds_cpp::ipc::MessageKind::kDeliverSample) {
      continue;
    }
    rmw_mdds_cpp::ipc::SampleMessage delivered_sample;
    if (!rmw_mdds_cpp::ipc::DecodeSampleMessage(delivery.payload.data(),
                                                delivery.payload.size(),
                                                &delivered_sample, error)) {
      return false;
    }
    if (delivered_sample.payload == expected_payload) {
      if (sample != nullptr) {
        *sample = std::move(delivered_sample);
      }
      return true;
    }
  }
  return false;
}
} // namespace

TEST(RmwMddsIpcBroker, StopShutsDownConfiguredBridgeRuntime) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;
  ASSERT_EQ(1, FakeMddsBridgeInitCount());
  ASSERT_EQ(0, FakeMddsBridgeShutdownCount());

  broker.Stop();
  EXPECT_EQ(1, FakeMddsBridgeShutdownCount());
}

TEST(RmwMddsIpcBroker, UsesConfiguredNodeSyncTopic) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  EnvVarGuard node_sync_guard("RMW_MDDS_NODE_SYNC_TOPIC");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_NODE_SYNC_TOPIC", "d171/mdds_node_sync", 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  EXPECT_EQ(1, FakeMddsBridgeHasSubscriber("d171/mdds_node_sync",
                                           "mdds_graph_NodeList"));
  EXPECT_EQ(
      0, FakeMddsBridgeHasSubscriber("mdds_node_sync", "mdds_graph_NodeList"));
}

TEST(RmwMddsIpcBroker, AssignsConfiguredDomainToRemoteNodeSyncEndpoints) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  EnvVarGuard node_sync_guard("RMW_MDDS_NODE_SYNC_TOPIC");
  EnvVarGuard domain_guard("ROS_DOMAIN_ID");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_NODE_SYNC_TOPIC", "d171/mdds_node_sync", 1));
  ASSERT_EQ(0, setenv("ROS_DOMAIN_ID", "7", 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd observer =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(observer) << error;
  DrainReadableFrames(observer.get(), std::chrono::milliseconds(100));

  const std::string payload = "/parameter_blackboard\n";
  ASSERT_EQ(1, FakeMddsBridgeInjectFor(
                   "d171/mdds_node_sync", "mdds_graph_NodeList", payload.data(),
                   static_cast<uint32_t>(payload.size()), 1u));

  rmw_mdds_cpp::ipc::EndpointDescriptor remote_node;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
  bool found = false;
  while (std::chrono::steady_clock::now() < deadline && !found) {
    if (!HasReadableData(observer.get())) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    rmw_mdds_cpp::ipc::Frame frame;
    ASSERT_EQ(rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
              rmw_mdds_cpp::ipc::ReadFrame(observer.get(), &frame, &error))
        << error;
    found = FindGraphEndpoint(
        frame, rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
        "_mdds_remote_node", "mdds_graph/msg/RemoteNode", &remote_node);
  }

  ASSERT_TRUE(found);
  EXPECT_EQ("parameter_blackboard", remote_node.node_name);
  EXPECT_EQ("/", remote_node.node_namespace);
  EXPECT_EQ(171u, remote_node.domain_id);
}

TEST(RmwMddsIpcBroker, RoutesPublishedSamplesToMatchingSubscriptions) {
  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd publisher =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(publisher) << error;
  rmw_mdds_cpp::ipc::UniqueFd matching_subscription =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(matching_subscription) << error;
  rmw_mdds_cpp::ipc::UniqueFd other_subscription =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(other_subscription) << error;

  RegisterEndpoint(publisher.get(),
                   MakeEndpoint(100u,
                                rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                                "/broker/chatter", "std_msgs/msg/String"),
                   1u);
  RegisterEndpoint(matching_subscription.get(),
                   MakeEndpoint(200u,
                                rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
                                "/broker/chatter", "std_msgs/msg/String"),
                   2u);
  RegisterEndpoint(other_subscription.get(),
                   MakeEndpoint(300u,
                                rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
                                "/broker/other", "std_msgs/msg/String"),
                   3u);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 100u;
  sample.sequence_number = 44u;
  sample.payload = {'b', 'r', 'o', 'k', 'e', 'r'};
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{rmw_mdds_cpp::ipc::MessageKind::kPublishSample,
                               4u,
                               rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
      << error;

  rmw_mdds_cpp::ipc::Frame delivery;
  ReadNextNonGraphFrame(matching_subscription.get(), &delivery, &error);
  EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kDeliverSample, delivery.kind);
  EXPECT_EQ(4u, delivery.request_id);

  rmw_mdds_cpp::ipc::SampleMessage delivered_sample;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeSampleMessage(delivery.payload.data(),
                                                     delivery.payload.size(),
                                                     &delivered_sample, &error))
      << error;
  EXPECT_EQ(sample.entity_id, delivered_sample.entity_id);
  EXPECT_EQ(sample.sequence_number, delivered_sample.sequence_number);
  EXPECT_EQ(sample.payload, delivered_sample.payload);
  while (HasReadableData(other_subscription.get())) {
    rmw_mdds_cpp::ipc::Frame frame;
    ASSERT_EQ(
        rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
        rmw_mdds_cpp::ipc::ReadFrame(other_subscription.get(), &frame, &error))
        << error;
    EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate, frame.kind);
    if (frame.kind == rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate) {
      ExpectValidGraphUpdate(frame);
    }
  }
}

TEST(RmwMddsIpcBroker, ValidatesLoanReturnsPerConnectionAndReclaimsOnDisconnect) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE");
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE", "0", 1));

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());
  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd publisher =
    rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(publisher) << error;
  rmw_mdds_cpp::ipc::UniqueFd subscription =
    rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(subscription) << error;
  rmw_mdds_cpp::ipc::UniqueFd foreign_subscription =
    rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(foreign_subscription) << error;

  auto publisher_endpoint = MakeEndpoint(
    100u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
    "/broker/loan_int32", "std_msgs/msg/Int32");
  auto subscription_endpoint = MakeEndpoint(
    200u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
    "/broker/loan_int32", "std_msgs/msg/Int32");
  subscription_endpoint.loaned_message_size = sizeof(int32_t);
  subscription_endpoint.qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  subscription_endpoint.qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  subscription_endpoint.qos.depth = 10u;
  auto foreign_endpoint = MakeEndpoint(
    300u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
    "/broker/other_loan_int32", "std_msgs/msg/Int32");
  foreign_endpoint.loaned_message_size = sizeof(int32_t);

  RegisterEndpoint(publisher.get(), publisher_endpoint, 1u);
  rmw_mdds_cpp::ipc::Frame subscription_ack;
  RegisterEndpoint(subscription.get(), subscription_endpoint, 2u, &subscription_ack);
  rmw_mdds_cpp::ipc::Frame foreign_ack;
  RegisterEndpoint(foreign_subscription.get(), foreign_endpoint, 3u, &foreign_ack);

  rmw_mdds_cpp::ipc::LoanPoolDescriptor pool;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeLoanPoolDescriptor(
    subscription_ack.payload.data(), subscription_ack.payload.size(), &pool, &error)) << error;
  EXPECT_EQ(0, access(pool.path.c_str(), F_OK));
  rmw_mdds_cpp::ipc::LoanPoolDescriptor foreign_pool;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeLoanPoolDescriptor(
    foreign_ack.payload.data(), foreign_ack.payload.size(), &foreign_pool, &error)) << error;
  EXPECT_NE(pool.path, foreign_pool.path);

  auto publish_value = [&](int32_t value, uint64_t sequence, uint64_t request_id) {
      rmw_mdds_cpp::ipc::SampleMessage sample;
      sample.entity_id = publisher_endpoint.entity_id;
      sample.sequence_number = sequence;
      sample.payload.resize(sizeof(value));
      std::memcpy(sample.payload.data(), &value, sizeof(value));
      ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
        publisher.get(),
        rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kPublishSample, request_id,
          rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
        &error)) << error;
    };

  publish_value(71, 11u, 4u);
  rmw_mdds_cpp::ipc::Frame delivery;
  ReadNextNonGraphFrame(subscription.get(), &delivery, &error);
  ASSERT_EQ(rmw_mdds_cpp::ipc::MessageKind::kDeliverLoanedSample, delivery.kind);
  rmw_mdds_cpp::ipc::LoanedSampleMessage loaned;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeLoanedSampleMessage(
    delivery.payload.data(), delivery.payload.size(), &loaned, &error)) << error;
  EXPECT_EQ(pool.generation, loaned.pool_generation);

  rmw_mdds_cpp::ipc::Frame foreign_return;
  foreign_return.kind = rmw_mdds_cpp::ipc::MessageKind::kReturnLoanedSample;
  foreign_return.request_id = 5u;
  foreign_return.payload = rmw_mdds_cpp::ipc::EncodeLoanReturn(loaned.loan_id);
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
    foreign_subscription.get(), foreign_return, &error)) << error;
  rmw_mdds_cpp::ipc::Frame response;
  ReadNextNonGraphFrame(foreign_subscription.get(), &response, &error);
  EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kError, response.kind);
  EXPECT_EQ(5u, response.request_id);

  rmw_mdds_cpp::ipc::Frame valid_return = foreign_return;
  valid_return.request_id = 6u;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(subscription.get(), valid_return, &error)) << error;
  ReadNextNonGraphFrame(subscription.get(), &response, &error);
  EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kAck, response.kind);
  EXPECT_EQ(6u, response.request_id);

  valid_return.request_id = 7u;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(subscription.get(), valid_return, &error)) << error;
  ReadNextNonGraphFrame(subscription.get(), &response, &error);
  EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kError, response.kind);
  EXPECT_EQ(7u, response.request_id);

  publish_value(72, 12u, 8u);
  ReadNextNonGraphFrame(subscription.get(), &delivery, &error);
  ASSERT_EQ(rmw_mdds_cpp::ipc::MessageKind::kDeliverLoanedSample, delivery.kind);
  ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeLoanedSampleMessage(
    delivery.payload.data(), delivery.payload.size(), &loaned, &error)) << error;
  EXPECT_EQ(12u, loaned.sequence_number);

  subscription.reset();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (access(pool.path.c_str(), F_OK) == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_NE(0, access(pool.path.c_str(), F_OK));
}

TEST(RmwMddsIpcBroker, DoesNotRouteSamplesAcrossDomains) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE", "0", 1));

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd publisher =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(publisher) << error;
  rmw_mdds_cpp::ipc::UniqueFd matching_subscription =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(matching_subscription) << error;
  rmw_mdds_cpp::ipc::UniqueFd other_domain_subscription =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(other_domain_subscription) << error;

  auto publisher_endpoint =
      MakeEndpoint(4100u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                   "/broker/domain_chatter", "std_msgs/msg/String");
  publisher_endpoint.domain_id = 93u;
  auto matching_endpoint =
      MakeEndpoint(4200u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
                   "/broker/domain_chatter", "std_msgs/msg/String");
  matching_endpoint.domain_id = 93u;
  auto other_domain_endpoint =
      MakeEndpoint(4300u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
                   "/broker/domain_chatter", "std_msgs/msg/String");
  other_domain_endpoint.domain_id = 94u;

  RegisterEndpoint(publisher.get(), publisher_endpoint, 1u);
  RegisterEndpoint(matching_subscription.get(), matching_endpoint, 2u);
  RegisterEndpoint(other_domain_subscription.get(), other_domain_endpoint, 3u);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 4100u;
  sample.sequence_number = 144u;
  sample.payload = {'d', 'o', 'm', 'a', 'i', 'n'};
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{rmw_mdds_cpp::ipc::MessageKind::kPublishSample,
                               4u,
                               rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
      << error;

  rmw_mdds_cpp::ipc::SampleMessage delivered_sample;
  EXPECT_TRUE(WaitForPayload(matching_subscription.get(), sample.payload,
                             &delivered_sample, &error,
                             std::chrono::milliseconds(500)))
      << error;
  EXPECT_FALSE(WaitForPayload(other_domain_subscription.get(), sample.payload,
                              nullptr, &error, std::chrono::milliseconds(200)))
      << "domain 94 subscription must not receive domain 93 sample";
}

TEST(RmwMddsIpcBroker, DoesNotRouteBestEffortSamplesToReliableSubscription) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE", "0", 1));

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd publisher =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(publisher) << error;
  rmw_mdds_cpp::ipc::UniqueFd best_effort_subscription =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(best_effort_subscription) << error;
  rmw_mdds_cpp::ipc::UniqueFd reliable_subscription =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(reliable_subscription) << error;

  auto publisher_endpoint =
      MakeEndpoint(5100u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                   "/broker/qos_chatter", "std_msgs/msg/String");
  publisher_endpoint.qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  auto best_effort_endpoint =
      MakeEndpoint(5200u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
                   "/broker/qos_chatter", "std_msgs/msg/String");
  best_effort_endpoint.qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  auto reliable_endpoint =
      MakeEndpoint(5300u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
                   "/broker/qos_chatter", "std_msgs/msg/String");
  reliable_endpoint.qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;

  RegisterEndpoint(publisher.get(), publisher_endpoint, 1u);
  RegisterEndpoint(best_effort_subscription.get(), best_effort_endpoint, 2u);
  RegisterEndpoint(reliable_subscription.get(), reliable_endpoint, 3u);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 5100u;
  sample.sequence_number = 244u;
  sample.payload = {'q', 'o', 's'};
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{rmw_mdds_cpp::ipc::MessageKind::kPublishSample,
                               4u,
                               rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
      << error;

  EXPECT_TRUE(WaitForPayload(best_effort_subscription.get(), sample.payload,
                             nullptr, &error, std::chrono::milliseconds(500)))
      << error;
  EXPECT_FALSE(WaitForPayload(reliable_subscription.get(), sample.payload,
                              nullptr, &error, std::chrono::milliseconds(200)))
      << "reliable subscription must reject a best-effort offer";
}

TEST(RmwMddsIpcBroker, SendsGraphSnapshotToPlainNewConnection) {
  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd publisher =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(publisher) << error;

  RegisterEndpoint(publisher.get(),
                   MakeEndpoint(900u,
                                rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                                "/broker/plain_graph", "std_msgs/msg/String"),
                   1u);

  rmw_mdds_cpp::ipc::UniqueFd observer =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(observer) << error;
  EXPECT_TRUE(WaitForGraphEndpoint(observer.get(),
                                   rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                                   "/broker/plain_graph", "std_msgs/msg/String",
                                   std::chrono::milliseconds(500)));
}

TEST(RmwMddsIpcBroker, CoalescesEndpointRegistrationGraphBroadcastBurst) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, unsetenv("RMW_MDDS_BRIDGE_LIBRARY"));

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  constexpr size_t client_count = 24u;
  std::vector<rmw_mdds_cpp::ipc::UniqueFd> clients;
  clients.reserve(client_count);
  for (size_t i = 0; i < client_count; ++i) {
    auto client =
        rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
    ASSERT_TRUE(client) << error;
    clients.push_back(std::move(client));
  }

  for (size_t i = 0; i < client_count; ++i) {
    const std::string topic = "/broker/graph_burst_" + std::to_string(i);
    const auto endpoint =
        MakeEndpoint(10000u + i, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                     topic.c_str(), "std_msgs/msg/String");
    ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
        clients[i].get(),
        rmw_mdds_cpp::ipc::Frame{
            rmw_mdds_cpp::ipc::MessageKind::kRegisterPublisher, 20000u + i,
            rmw_mdds_cpp::ipc::EncodeEndpointDescriptor(endpoint)},
        &error))
        << error;
  }

  size_t graph_update_count = 0u;
  std::vector<bool> saw_complete_graph(client_count, false);
  for (size_t i = 0; i < client_count; ++i) {
    for (;;) {
      rmw_mdds_cpp::ipc::Frame frame;
      ASSERT_EQ(rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
                rmw_mdds_cpp::ipc::ReadFrame(clients[i].get(), &frame, &error))
          << error;
      if (frame.kind == rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate) {
        ++graph_update_count;
        rmw_mdds_cpp::ipc::GraphUpdateMessage update;
        ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeGraphUpdate(
            frame.payload.data(), frame.payload.size(), &update, &error))
            << error;
        saw_complete_graph[i] = update.endpoints.size() >= client_count;
        continue;
      }
      ASSERT_EQ(rmw_mdds_cpp::ipc::MessageKind::kAck, frame.kind);
      EXPECT_EQ(20000u + i, frame.request_id);
      break;
    }
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::find(saw_complete_graph.begin(), saw_complete_graph.end(),
                   false) != saw_complete_graph.end() &&
         std::chrono::steady_clock::now() < deadline) {
    for (size_t i = 0; i < client_count; ++i) {
      if (saw_complete_graph[i] || !HasReadableData(clients[i].get())) {
        continue;
      }
      rmw_mdds_cpp::ipc::Frame frame;
      ASSERT_EQ(rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
                rmw_mdds_cpp::ipc::ReadFrame(clients[i].get(), &frame, &error))
          << error;
      if (frame.kind != rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate) {
        continue;
      }
      ++graph_update_count;
      rmw_mdds_cpp::ipc::GraphUpdateMessage update;
      ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeGraphUpdate(
          frame.payload.data(), frame.payload.size(), &update, &error))
          << error;
      saw_complete_graph[i] = update.endpoints.size() >= client_count;
    }
  }

  EXPECT_EQ(
      std::count(saw_complete_graph.begin(), saw_complete_graph.end(), true),
      client_count)
      << "every connection must converge on the complete endpoint graph";
  EXPECT_LE(graph_update_count, client_count * 4u)
      << "registration bursts must not broadcast a full graph per endpoint";
}

TEST(RmwMddsIpcBroker, ReapsInactiveClientConnections) {
  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  size_t connection_count = 0u;
  for (size_t i = 0; i < 32u; ++i) {
    {
      rmw_mdds_cpp::ipc::UniqueFd client =
          rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
      ASSERT_TRUE(client) << error;
    }
    for (size_t poll = 0; poll < 100u; ++poll) {
      usleep(10000);
      connection_count = broker.ConnectionCountForTesting();
      if (connection_count <= 2u) {
        break;
      }
    }
    ASSERT_LE(connection_count, 2u)
        << "inactive broker client threads must be joined during churn";
  }
}

TEST(RmwMddsIpcBroker, RelaysPeerBrokerGraphOverBridgeSync) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd observer =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(observer) << error;
  DrainReadableFrames(observer.get(), std::chrono::milliseconds(100));

  auto peer_publisher =
      MakeEndpoint(1200u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                   "/broker/peer_graph", "std_msgs/msg/String");
  const std::vector<rmw_mdds_cpp::ipc::EndpointDescriptor> peer_endpoints{
      peer_publisher};
  const std::vector<uint8_t> peer_graph = rmw_mdds_cpp::ipc::EncodeGraphUpdate(
      0x1122334455667788u, 1u, peer_endpoints);
  ASSERT_EQ(
      1, FakeMddsBridgeInjectFor("mdds_graph_sync", "mdds_graph_EndpointList",
                                 peer_graph.data(),
                                 static_cast<uint32_t>(peer_graph.size()), 1u));

  EXPECT_TRUE(WaitForGraphEndpoint(
      observer.get(), rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
      "/broker/peer_graph", "std_msgs/msg/String", std::chrono::seconds(3)))
      << "broker must rebroadcast a remote graph-sync publisher endpoint";
}

TEST(RmwMddsIpcBroker, ReplaysTransientLocalSampleToLateSubscription) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE", "0", 1));

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd publisher =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(publisher) << error;
  auto publisher_endpoint =
      MakeEndpoint(1100u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                   "/broker/transient_local", "std_msgs/msg/String");
  publisher_endpoint.qos.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  publisher_endpoint.qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  publisher_endpoint.qos.depth = 1u;
  RegisterEndpoint(publisher.get(), publisher_endpoint, 1u);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 1100u;
  sample.sequence_number = 88u;
  sample.payload = {'r', 'e', 't', 'a', 'i', 'n', 'e', 'd'};
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{rmw_mdds_cpp::ipc::MessageKind::kPublishSample,
                               3u,
                               rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
      << error;

  rmw_mdds_cpp::ipc::UniqueFd subscription =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(subscription) << error;
  auto subscription_endpoint =
      MakeEndpoint(2200u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
                   "/broker/transient_local", "std_msgs/msg/String");
  subscription_endpoint.qos.durability =
      RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  subscription_endpoint.qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  subscription_endpoint.qos.depth = 1u;
  RegisterEndpoint(subscription.get(), subscription_endpoint, 4u);

  rmw_mdds_cpp::ipc::SampleMessage delivered_sample;
  ASSERT_TRUE(WaitForPayload(subscription.get(), sample.payload,
                             &delivered_sample, &error,
                             std::chrono::milliseconds(500)))
      << error;
  EXPECT_EQ(sample.entity_id, delivered_sample.entity_id);
  EXPECT_EQ(sample.sequence_number, delivered_sample.sequence_number);
  EXPECT_EQ(sample.payload, delivered_sample.payload);
}

TEST(RmwMddsIpcBroker, OwnsBridgeTransportForRegisteredTopicEndpoints) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;
  ASSERT_EQ(1, FakeMddsBridgeInitCount());

  rmw_mdds_cpp::ipc::UniqueFd publisher =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(publisher) << error;
  rmw_mdds_cpp::ipc::UniqueFd subscription =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(subscription) << error;

  auto publisher_endpoint =
      MakeEndpoint(101u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                   "/rt/broker_bridge_chatter", "std_msgs/msg/String");
  publisher_endpoint.mdds_type_name = "std_msgs::msg::dds_::String_";
  auto subscription_endpoint =
      MakeEndpoint(202u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
                   "/rt/broker_bridge_chatter", "std_msgs/msg/String");
  subscription_endpoint.mdds_type_name = "std_msgs::msg::dds_::String_";

  RegisterEndpoint(publisher.get(), publisher_endpoint, 1u);
  RegisterEndpoint(subscription.get(), subscription_endpoint, 2u);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 101u;
  sample.sequence_number = 55u;
  sample.mdds_payload = true;
  sample.payload = {'b', 'r', 'i', 'd', 'g', 'e'};
  const char *topic_bridge_name = "rt/broker_bridge_chatter";
  const char *topic_bridge_type = "std_msgs::msg::dds_::String_";
  const int topic_publish_count_before =
      FakeMddsBridgePublisherPublishCount(topic_bridge_name, topic_bridge_type);
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{rmw_mdds_cpp::ipc::MessageKind::kPublishSample,
                               3u,
                               rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
      << error;

  ASSERT_TRUE(WaitForFakePublisherPublishCountAtLeast(
      topic_bridge_name, topic_bridge_type, topic_publish_count_before + 1,
      std::chrono::seconds(1)));
  ASSERT_EQ(sample.payload.size(), FakeMddsBridgePublisherLastPayloadLen(
                                       topic_bridge_name, topic_bridge_type));
  EXPECT_EQ(0, std::memcmp(sample.payload.data(),
                           FakeMddsBridgePublisherLastPayloadData(
                               topic_bridge_name, topic_bridge_type),
                           sample.payload.size()));

  rmw_mdds_cpp::ipc::SampleMessage local_sample;
  ASSERT_TRUE(ReadUntilPayload(subscription.get(), sample.payload,
                               &local_sample, &error))
      << error;
  EXPECT_EQ(sample.sequence_number, local_sample.sequence_number);
  EXPECT_FALSE(WaitForPayload(subscription.get(), sample.payload, nullptr,
                              &error, std::chrono::milliseconds(200)))
      << "same-broker bridge echo must not duplicate the local topic delivery";

  const char injected[] = "from_bridge";
  FakeMddsBridgeInject(injected, sizeof(injected) - 1u);

  const std::vector<uint8_t> expected_injected(
      injected, injected + sizeof(injected) - 1u);
  bool saw_injected_payload = false;
  for (int i = 0; i < 4 && !saw_injected_payload; ++i) {
    rmw_mdds_cpp::ipc::Frame delivery;
    ReadNextNonGraphFrame(subscription.get(), &delivery, &error);
    EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kDeliverSample, delivery.kind);
    rmw_mdds_cpp::ipc::SampleMessage delivered_sample;
    ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeSampleMessage(
        delivery.payload.data(), delivery.payload.size(), &delivered_sample,
        &error))
        << error;
    EXPECT_TRUE(delivered_sample.mdds_payload);
    saw_injected_payload = delivered_sample.payload == expected_injected;
  }
  EXPECT_TRUE(saw_injected_payload);
}

TEST(RmwMddsIpcBroker, IgnoreLocalBridgeUsesDirectIpcOnly) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;
  ASSERT_EQ(1, FakeMddsBridgeInitCount());

  rmw_mdds_cpp::ipc::UniqueFd publisher =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(publisher) << error;
  rmw_mdds_cpp::ipc::UniqueFd normal_subscription =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(normal_subscription) << error;
  rmw_mdds_cpp::ipc::UniqueFd ignored_subscription =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(ignored_subscription) << error;

  constexpr uint64_t kLocalContextId = 0x12345678u;
  constexpr const char *kTopic = "rt/broker_ignore_local";
  constexpr const char *kType = "std_msgs::msg::dds_::String_";
  auto publisher_endpoint =
      MakeEndpoint(111u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                   "/rt/broker_ignore_local", "std_msgs/msg/String");
  publisher_endpoint.mdds_type_name = kType;
  publisher_endpoint.local_context_id = kLocalContextId;
  auto normal_endpoint =
      MakeEndpoint(222u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
                   "/rt/broker_ignore_local", "std_msgs/msg/String");
  normal_endpoint.mdds_type_name = kType;
  normal_endpoint.local_context_id = kLocalContextId;
  auto ignored_endpoint =
      MakeEndpoint(333u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription,
                   "/rt/broker_ignore_local", "std_msgs/msg/String");
  ignored_endpoint.mdds_type_name = kType;
  ignored_endpoint.local_context_id = kLocalContextId;
  ignored_endpoint.ignore_local_publications = true;

  RegisterEndpoint(publisher.get(), publisher_endpoint, 1u);
  RegisterEndpoint(normal_subscription.get(), normal_endpoint, 2u);
  RegisterEndpoint(ignored_subscription.get(), ignored_endpoint, 3u);

  rmw_mdds_cpp::ipc::SampleMessage local_sample;
  local_sample.entity_id = publisher_endpoint.entity_id;
  local_sample.sequence_number = 10u;
  local_sample.mdds_payload = true;
  local_sample.payload = {'l', 'o', 'c', 'a', 'l'};
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 4u,
          rmw_mdds_cpp::ipc::EncodeSampleMessage(local_sample)},
      &error))
      << error;

  ASSERT_TRUE(WaitForPayload(normal_subscription.get(), local_sample.payload,
                             nullptr, &error, std::chrono::milliseconds(500)))
      << error;
  EXPECT_FALSE(WaitForPayload(normal_subscription.get(), local_sample.payload,
                              nullptr, &error, std::chrono::milliseconds(200)))
      << "normal subscription must receive one direct IPC copy";
  EXPECT_FALSE(WaitForPayload(ignored_subscription.get(), local_sample.payload,
                              nullptr, &error, std::chrono::milliseconds(200)))
      << "same-context ignore-local subscription must not receive the bridge "
         "loopback";

  const std::vector<uint8_t> remote_payload = {'r', 'e', 'm', 'o', 't', 'e'};
  ASSERT_EQ(2, FakeMddsBridgeInjectFor(
                   kTopic, kType, remote_payload.data(),
                   static_cast<uint32_t>(remote_payload.size()), 11u));
  EXPECT_TRUE(WaitForPayload(normal_subscription.get(), remote_payload, nullptr,
                             &error, std::chrono::milliseconds(500)))
      << error;
  EXPECT_TRUE(WaitForPayload(ignored_subscription.get(), remote_payload,
                             nullptr, &error, std::chrono::milliseconds(500)))
      << error;
}

TEST(RmwMddsIpcBroker, RejectsBridgeWithoutRemoteOnlyPublisherCapability) {
  EnvVarGuard enabled_guard("RMW_MDDS_BRIDGE");
  EnvVarGuard library_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  unsetenv("RMW_MDDS_BRIDGE");
  ASSERT_EQ(0,
            setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_LEGACY_PATH, 1));

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  const bool started = broker.Start(socket_path.path(), &error);
  EXPECT_FALSE(started);
  EXPECT_FALSE(broker.IsRunning());
  EXPECT_NE(std::string::npos, error.find("remote-only")) << error;
  if (started) {
    broker.Stop();
  }
}

TEST(RmwMddsIpcBroker, AllowsBridgeWithoutImmediateHeartbeatCapability) {
  EnvVarGuard enabled_guard("RMW_MDDS_BRIDGE");
  EnvVarGuard library_guard("RMW_MDDS_BRIDGE_LIBRARY");
  auto &backend = rmw_mdds_cpp::BridgeBackend::Instance();
  backend.ResetForTesting();
  unsetenv("RMW_MDDS_BRIDGE");
  ASSERT_EQ(0,
            setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_LEGACY_PATH, 1));
  ASSERT_TRUE(backend.Available());

  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  void *publisher = backend.CreatePublisher("rt/legacy_heartbeat",
                                            "std_msgs/msg/String", &qos);
  ASSERT_NE(nullptr, publisher);
  EXPECT_FALSE(backend.PublisherSendHeartbeatNow(publisher));
  backend.DestroyPublisher(publisher);
  backend.ResetForTesting();
}

TEST(RmwMddsIpcBroker, ActivatesProtectedTransportBeforeListening) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  EnvVarGuard authenticated_guard("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED");
  EnvVarGuard encrypted_guard("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED", "1", 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;
  EXPECT_EQ(1, FakeMddsBridgeProtectedTransportActivateCount());
  EXPECT_EQ(1, FakeMddsBridgeProtectedTransportAuthenticated());
  EXPECT_EQ(1, FakeMddsBridgeProtectedTransportEncrypted());
}

TEST(RmwMddsIpcBroker, OwnsBridgeTransportForRegisteredServiceEndpoints) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;
  ASSERT_EQ(1, FakeMddsBridgeInitCount());

  rmw_mdds_cpp::ipc::UniqueFd client =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client) << error;
  rmw_mdds_cpp::ipc::UniqueFd service =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(service) << error;

  auto client_endpoint =
      MakeEndpoint(303u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_bridge_trigger", "std_srvs::srv::Trigger");
  auto service_endpoint =
      MakeEndpoint(404u, rmw_mdds_cpp::ipc::EndpointKind::kService,
                   "/broker_bridge_trigger", "std_srvs::srv::Trigger");

  RegisterEndpoint(client.get(), client_endpoint, 1u);
  RegisterEndpoint(service.get(), service_endpoint, 2u);

  EXPECT_GE(FakeMddsBridgePublisherCount(), 2);
  EXPECT_GE(FakeMddsBridgeSubscriberCount(), 2);
  EXPECT_EQ(1, FakeMddsBridgeHasPublisher("rq/broker_bridge_trigger",
                                          "std_srvs::srv::Trigger_Request"));
  EXPECT_EQ(1, FakeMddsBridgeHasSubscriber("rr/broker_bridge_trigger",
                                           "std_srvs::srv::Trigger_Response"));
  EXPECT_EQ(1, FakeMddsBridgeHasSubscriber("rq/broker_bridge_trigger",
                                           "std_srvs::srv::Trigger_Request"));
  EXPECT_EQ(1, FakeMddsBridgeHasPublisher("rr/broker_bridge_trigger",
                                          "std_srvs::srv::Trigger_Response"));

  rmw_mdds_cpp::ipc::SampleMessage request;
  request.entity_id = 303u;
  request.sequence_number = 11u;
  request.payload = {'r', 'e', 'q'};
  const char *request_bridge_name = "rq/broker_bridge_trigger";
  const char *request_bridge_type = "std_srvs::srv::Trigger_Request";
  const int request_publish_count_before = FakeMddsBridgePublisherPublishCount(
      request_bridge_name, request_bridge_type);
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      client.get(),
      rmw_mdds_cpp::ipc::Frame{rmw_mdds_cpp::ipc::MessageKind::kPublishSample,
                               3u,
                               rmw_mdds_cpp::ipc::EncodeSampleMessage(request)},
      &error))
      << error;
  DrainReadableFrames(client.get(), std::chrono::milliseconds(100));
  EXPECT_EQ(request_publish_count_before,
            FakeMddsBridgePublisherPublishCount(request_bridge_name,
                                                request_bridge_type));
  rmw_mdds_cpp::ipc::SampleMessage local_request;
  EXPECT_TRUE(
      ReadUntilPayload(service.get(), request.payload, &local_request, &error))
      << error;
  EXPECT_FALSE(local_request.mdds_payload);

  rmw_mdds_cpp::ipc::SampleMessage response;
  response.entity_id = 404u;
  response.sequence_number = 12u;
  response.payload = {'r', 'e', 's'};
  const char *response_bridge_name = "rr/broker_bridge_trigger";
  const char *response_bridge_type = "std_srvs::srv::Trigger_Response";
  const int response_publish_count_before = FakeMddsBridgePublisherPublishCount(
      response_bridge_name, response_bridge_type);
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      service.get(),
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 4u,
          rmw_mdds_cpp::ipc::EncodeSampleMessage(response)},
      &error))
      << error;
  DrainReadableFrames(service.get(), std::chrono::milliseconds(100));
  EXPECT_EQ(response_publish_count_before,
            FakeMddsBridgePublisherPublishCount(response_bridge_name,
                                                response_bridge_type));
  rmw_mdds_cpp::ipc::SampleMessage local_response;
  EXPECT_TRUE(
      ReadUntilPayload(client.get(), response.payload, &local_response, &error))
      << error;
  EXPECT_FALSE(local_response.mdds_payload);

  const std::vector<uint8_t> external_request = {'r', 'e', 'm', 'o', 't',
                                                 'e', '_', 'r', 'e', 'q'};
  ASSERT_EQ(1, FakeMddsBridgeInjectFor(
                   "rq/broker_bridge_trigger", "std_srvs::srv::Trigger_Request",
                   external_request.data(),
                   static_cast<uint32_t>(external_request.size()), 21u));
  rmw_mdds_cpp::ipc::SampleMessage delivered_request;
  EXPECT_TRUE(ReadUntilPayload(service.get(), external_request,
                               &delivered_request, &error))
      << error;
  EXPECT_TRUE(delivered_request.mdds_payload);
  EXPECT_EQ(21u, delivered_request.sequence_number);

  const std::vector<uint8_t> external_response = {'r', 'e', 'm', 'o', 't',
                                                  'e', '_', 'r', 'e', 's'};
  ASSERT_EQ(1, FakeMddsBridgeInjectFor(
                   "rr/broker_bridge_trigger",
                   "std_srvs::srv::Trigger_Response", external_response.data(),
                   static_cast<uint32_t>(external_response.size()), 22u));
  rmw_mdds_cpp::ipc::SampleMessage delivered_response;
  EXPECT_TRUE(ReadUntilPayload(client.get(), external_response,
                               &delivered_response, &error))
      << error;
  EXPECT_TRUE(delivered_response.mdds_payload);
  EXPECT_EQ(22u, delivered_response.sequence_number);
}

TEST(RmwMddsIpcBroker, RoutesBridgeResponseToOwningClientEndpointOnly) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd client_a =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client_a) << error;
  rmw_mdds_cpp::ipc::UniqueFd client_b =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client_b) << error;

  auto endpoint_a =
      MakeEndpoint(3303u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_bridge_fanout", "std_srvs::srv::Trigger");
  auto endpoint_b =
      MakeEndpoint(3304u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_bridge_fanout", "std_srvs::srv::Trigger");

  RegisterEndpoint(client_a.get(), endpoint_a, 1u);
  RegisterEndpoint(client_b.get(), endpoint_b, 2u);

  const std::vector<uint8_t> external_response = {'f', 'a', 'n', 'o', 'u', 't'};
  ASSERT_EQ(1, FakeMddsBridgeInjectFor(
                   "rr/broker_bridge_fanout", "std_srvs::srv::Trigger_Response",
                   external_response.data(),
                   static_cast<uint32_t>(external_response.size()), 77u));

  rmw_mdds_cpp::ipc::SampleMessage delivered_a;
  ASSERT_TRUE(
      ReadUntilPayload(client_a.get(), external_response, &delivered_a, &error))
      << error;
  EXPECT_TRUE(delivered_a.mdds_payload);
  EXPECT_EQ(77u, delivered_a.sequence_number);

  rmw_mdds_cpp::ipc::SampleMessage delivered_b;
  ASSERT_TRUE(
      ReadUntilPayload(client_b.get(), external_response, &delivered_b, &error))
      << error;
  EXPECT_TRUE(delivered_b.mdds_payload);
  EXPECT_EQ(77u, delivered_b.sequence_number);

  EXPECT_FALSE(WaitForPayload(client_a.get(), external_response, nullptr,
                              &error, std::chrono::milliseconds(200)))
      << "bridge response callback for client B must not be replayed to client "
         "A";
  EXPECT_FALSE(WaitForPayload(client_b.get(), external_response, nullptr,
                              &error, std::chrono::milliseconds(200)))
      << "bridge response callback for client A must not be replayed to client "
         "B";
}

TEST(RmwMddsIpcBroker, RoutesBridgeResponseByWriterGuidWithSharedSubscriber) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd client_a =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client_a) << error;
  rmw_mdds_cpp::ipc::UniqueFd client_b =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client_b) << error;

  auto endpoint_a =
      MakeEndpoint(4403u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_bridge_guid_route", "std_srvs::srv::Trigger");
  auto endpoint_b =
      MakeEndpoint(4404u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_bridge_guid_route", "std_srvs::srv::Trigger");

  RegisterEndpoint(client_a.get(), endpoint_a, 1u);
  RegisterEndpoint(client_b.get(), endpoint_b, 2u);

  const std::vector<uint8_t> response_body = {'g', 'u', 'i', 'd'};
  const std::vector<uint8_t> response_payload =
      MakeServiceWirePayload(endpoint_a.entity_id, 2, response_body);
  ASSERT_EQ(1, FakeMddsBridgeInjectFor(
                   "rr/broker_bridge_guid_route",
                   "std_srvs::srv::Trigger_Response", response_payload.data(),
                   static_cast<uint32_t>(response_payload.size()), 88u));

  rmw_mdds_cpp::ipc::SampleMessage delivered_a;
  ASSERT_TRUE(
      ReadUntilPayload(client_a.get(), response_payload, &delivered_a, &error))
      << error;
  EXPECT_TRUE(delivered_a.mdds_payload);
  EXPECT_EQ(88u, delivered_a.sequence_number);

  EXPECT_FALSE(WaitForPayload(client_a.get(), response_payload, nullptr, &error,
                              std::chrono::milliseconds(200)))
      << "duplicate bridge callbacks must not enqueue duplicate responses";
  EXPECT_FALSE(WaitForPayload(client_b.get(), response_payload, nullptr, &error,
                              std::chrono::milliseconds(200)))
      << "response payload writer_guid targets client A, not client B";
}

TEST(RmwMddsIpcBroker, SharesServiceResponseBridgeSubscriberAcrossClients) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd client_a =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client_a) << error;
  rmw_mdds_cpp::ipc::UniqueFd client_b =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client_b) << error;

  auto endpoint_a =
      MakeEndpoint(6603u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_bridge_shared_rr", "std_srvs::srv::Trigger");
  auto endpoint_b =
      MakeEndpoint(6604u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_bridge_shared_rr", "std_srvs::srv::Trigger");

  RegisterEndpoint(client_a.get(), endpoint_a, 1u);
  RegisterEndpoint(client_b.get(), endpoint_b, 2u);

  const char *response_topic = "rr/broker_bridge_shared_rr";
  const char *response_type = "std_srvs::srv::Trigger_Response";
  ASSERT_EQ(1, FakeMddsBridgeSubscriberCountFor(response_topic, response_type))
      << "same-broker service clients must share one response bridge "
         "subscriber instead of multiplying remote response callbacks";

  const std::vector<uint8_t> response_body = {'s', 'h', 'a', 'r', 'e', 'd'};
  const std::vector<uint8_t> response_payload =
      MakeServiceWirePayload(endpoint_b.entity_id, 42, response_body);
  ASSERT_EQ(1, FakeMddsBridgeInjectFor(
                   response_topic, response_type, response_payload.data(),
                   static_cast<uint32_t>(response_payload.size()), 101u));

  rmw_mdds_cpp::ipc::SampleMessage delivered_b;
  ASSERT_TRUE(
      ReadUntilPayload(client_b.get(), response_payload, &delivered_b, &error))
      << error;
  EXPECT_TRUE(delivered_b.mdds_payload);
  EXPECT_EQ(101u, delivered_b.sequence_number);

  EXPECT_FALSE(WaitForPayload(client_a.get(), response_payload, nullptr, &error,
                              std::chrono::milliseconds(200)))
      << "shared response bridge subscriber must still route by writer_guid";
}

TEST(RmwMddsIpcBroker,
     RejectsClientRegistrationWhenResponseBridgeCreationFails) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  const int publisher_count_before = FakeMddsBridgePublisherCount();
  const int subscriber_count_before = FakeMddsBridgeSubscriberCount();
  FakeMddsBridgeFailNextSubscriberCreate();

  rmw_mdds_cpp::ipc::UniqueFd client =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client) << error;
  auto endpoint =
      MakeEndpoint(6650u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_bridge_resource_limit", "std_srvs::srv::Trigger");

  constexpr uint64_t request_id = 41u;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      client.get(),
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kRegisterClient, request_id,
          rmw_mdds_cpp::ipc::EncodeEndpointDescriptor(endpoint)},
      &error))
      << error;

  rmw_mdds_cpp::ipc::Frame response;
  for (;;) {
    ASSERT_EQ(rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
              rmw_mdds_cpp::ipc::ReadFrame(client.get(), &response, &error))
        << error;
    if (response.kind == rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate) {
      ExpectValidGraphUpdate(response);
      continue;
    }
    break;
  }

  EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kError, response.kind);
  EXPECT_EQ(request_id, response.request_id);
  EXPECT_NE(std::string(response.payload.begin(), response.payload.end())
                .find("bridge"),
            std::string::npos);
  EXPECT_EQ(publisher_count_before, FakeMddsBridgePublisherCount())
      << "failed client registration must roll back its request publisher";
  EXPECT_EQ(subscriber_count_before, FakeMddsBridgeSubscriberCount());
}

TEST(RmwMddsIpcBroker,
     RejectsClientRegistrationWhenRequestBridgeCreationFails) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  const int publisher_count_before = FakeMddsBridgePublisherCount();
  const int subscriber_count_before = FakeMddsBridgeSubscriberCount();
  FakeMddsBridgeFailNextPublisherCreate();

  rmw_mdds_cpp::ipc::UniqueFd client =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client) << error;
  auto endpoint = MakeEndpoint(6651u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                               "/broker_bridge_request_resource_limit",
                               "std_srvs::srv::Trigger");

  constexpr uint64_t request_id = 42u;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      client.get(),
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kRegisterClient, request_id,
          rmw_mdds_cpp::ipc::EncodeEndpointDescriptor(endpoint)},
      &error))
      << error;

  rmw_mdds_cpp::ipc::Frame response;
  for (;;) {
    ASSERT_EQ(rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
              rmw_mdds_cpp::ipc::ReadFrame(client.get(), &response, &error))
        << error;
    if (response.kind == rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate) {
      ExpectValidGraphUpdate(response);
      continue;
    }
    break;
  }

  EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kError, response.kind);
  EXPECT_EQ(request_id, response.request_id);
  EXPECT_NE(std::string(response.payload.begin(), response.payload.end())
                .find("bridge"),
            std::string::npos);
  EXPECT_EQ(publisher_count_before, FakeMddsBridgePublisherCount());
  EXPECT_EQ(subscriber_count_before, FakeMddsBridgeSubscriberCount())
      << "failed client registration must roll back its response subscriber";
}

TEST(RmwMddsIpcBroker, SharesServiceRequestBridgePublisherAcrossClients) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  EnvVarGuard max_unacked_guard("RMW_MDDS_SERVICE_BRIDGE_MAX_UNACKED");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_SERVICE_BRIDGE_MAX_UNACKED", "0", 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd client_a =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client_a) << error;
  rmw_mdds_cpp::ipc::UniqueFd client_b =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client_b) << error;

  auto endpoint_a =
      MakeEndpoint(6703u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_bridge_shared_rq", "std_srvs::srv::Trigger");
  auto endpoint_b =
      MakeEndpoint(6704u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_bridge_shared_rq", "std_srvs::srv::Trigger");

  RegisterEndpoint(client_a.get(), endpoint_a, 1u);
  const int publisher_count_after_first = FakeMddsBridgePublisherCount();
  RegisterEndpoint(client_b.get(), endpoint_b, 2u);

  const char *request_topic = "rq/broker_bridge_shared_rq";
  const char *request_type = "std_srvs::srv::Trigger_Request";
  ASSERT_EQ(1, FakeMddsBridgeHasPublisher(request_topic, request_type));
  EXPECT_EQ(publisher_count_after_first, FakeMddsBridgePublisherCount())
      << "same-broker service clients must share one request bridge publisher";

  rmw_mdds_cpp::ipc::SampleMessage request_a;
  request_a.entity_id = endpoint_a.entity_id;
  request_a.sequence_number = 1u;
  request_a.payload = MakeServiceWirePayload(endpoint_a.entity_id, 1, {'a'});
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      client_a.get(),
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 3u,
          rmw_mdds_cpp::ipc::EncodeSampleMessage(request_a)},
      &error))
      << error;

  rmw_mdds_cpp::ipc::SampleMessage request_b;
  request_b.entity_id = endpoint_b.entity_id;
  request_b.sequence_number = 1u;
  request_b.payload = MakeServiceWirePayload(endpoint_b.entity_id, 1, {'b'});
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      client_b.get(),
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 4u,
          rmw_mdds_cpp::ipc::EncodeSampleMessage(request_b)},
      &error))
      << error;

  EXPECT_TRUE(WaitForFakePublisherPublishCountAtLeast(
      request_topic, request_type, 2, std::chrono::milliseconds(500)))
      << "both clients must publish requests through the shared bridge writer";
}

TEST(RmwMddsIpcBroker, RoutesLocalServiceResponseToOwningClientOnly) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE", "0", 1));

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd client_a =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client_a) << error;
  rmw_mdds_cpp::ipc::UniqueFd client_b =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client_b) << error;
  rmw_mdds_cpp::ipc::UniqueFd service =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(service) << error;

  auto endpoint_a =
      MakeEndpoint(5503u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_local_guid_route", "std_srvs::srv::Trigger");
  auto endpoint_b =
      MakeEndpoint(5504u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_local_guid_route", "std_srvs::srv::Trigger");
  auto service_endpoint =
      MakeEndpoint(5505u, rmw_mdds_cpp::ipc::EndpointKind::kService,
                   "/broker_local_guid_route", "std_srvs::srv::Trigger");

  RegisterEndpoint(client_a.get(), endpoint_a, 1u);
  RegisterEndpoint(client_b.get(), endpoint_b, 2u);
  RegisterEndpoint(service.get(), service_endpoint, 3u);

  const std::vector<uint8_t> response_body = {'l', 'o', 'c', 'a', 'l'};
  const std::vector<uint8_t> response_payload =
      MakeServiceWirePayload(endpoint_a.entity_id, 9, response_body);
  rmw_mdds_cpp::ipc::SampleMessage response;
  response.entity_id = service_endpoint.entity_id;
  response.sequence_number = 1u;
  response.payload = response_payload;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      service.get(),
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 4u,
          rmw_mdds_cpp::ipc::EncodeSampleMessage(response)},
      &error))
      << error;

  rmw_mdds_cpp::ipc::SampleMessage delivered_a;
  ASSERT_TRUE(
      ReadUntilPayload(client_a.get(), response_payload, &delivered_a, &error))
      << error;
  EXPECT_EQ(response.sequence_number, delivered_a.sequence_number);

  EXPECT_FALSE(WaitForPayload(client_b.get(), response_payload, nullptr, &error,
                              std::chrono::milliseconds(200)))
      << "local service response writer_guid targets client A, not client B";
}

TEST(RmwMddsIpcBroker, ServiceBridgeTransportUsesExpandedInternalHistory) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;
  ASSERT_EQ(1, FakeMddsBridgeInitCount());

  rmw_mdds_cpp::ipc::UniqueFd client =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client) << error;
  rmw_mdds_cpp::ipc::UniqueFd service =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(service) << error;

  auto client_endpoint =
      MakeEndpoint(1303u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/history_bridge_trigger", "std_srvs::srv::Trigger");
  auto service_endpoint =
      MakeEndpoint(1404u, rmw_mdds_cpp::ipc::EndpointKind::kService,
                   "/history_bridge_trigger", "std_srvs::srv::Trigger");
  client_endpoint.qos = rmw_qos_profile_services_default;
  service_endpoint.qos = rmw_qos_profile_services_default;

  ASSERT_EQ(10u, client_endpoint.qos.depth);
  ASSERT_EQ(10u, service_endpoint.qos.depth);

  RegisterEndpoint(client.get(), client_endpoint, 1u);
  RegisterEndpoint(service.get(), service_endpoint, 2u);

  const char *request_topic = "rq/history_bridge_trigger";
  const char *request_type = "std_srvs::srv::Trigger_Request";
  const char *response_topic = "rr/history_bridge_trigger";
  const char *response_type = "std_srvs::srv::Trigger_Response";

  ASSERT_EQ(1, FakeMddsBridgeHasPublisher(request_topic, request_type));
  ASSERT_EQ(1, FakeMddsBridgeHasSubscriber(request_topic, request_type));
  ASSERT_EQ(1, FakeMddsBridgeHasPublisher(response_topic, response_type));
  ASSERT_EQ(1, FakeMddsBridgeHasSubscriber(response_topic, response_type));

  EXPECT_EQ(0, FakeMddsBridgePublisherHistoryKind(request_topic, request_type));
  EXPECT_EQ(0,
            FakeMddsBridgeSubscriberHistoryKind(request_topic, request_type));
  EXPECT_EQ(0,
            FakeMddsBridgePublisherHistoryKind(response_topic, response_type));
  EXPECT_EQ(0,
            FakeMddsBridgeSubscriberHistoryKind(response_topic, response_type));
  EXPECT_GT(FakeMddsBridgePublisherHistoryDepth(request_topic, request_type),
            client_endpoint.qos.depth);
  EXPECT_GT(FakeMddsBridgeSubscriberHistoryDepth(request_topic, request_type),
            service_endpoint.qos.depth);
  EXPECT_GT(FakeMddsBridgePublisherHistoryDepth(response_topic, response_type),
            service_endpoint.qos.depth);
  EXPECT_GT(FakeMddsBridgeSubscriberHistoryDepth(response_topic, response_type),
            client_endpoint.qos.depth);
}

TEST(RmwMddsIpcBroker, PublishesServiceSamplesToBridgeWhenNoLocalTarget) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;
  ASSERT_EQ(1, FakeMddsBridgeInitCount());

  rmw_mdds_cpp::ipc::UniqueFd client =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client) << error;
  auto client_endpoint =
      MakeEndpoint(4403u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/remote_only_trigger", "std_srvs::srv::Trigger");
  RegisterEndpoint(client.get(), client_endpoint, 1u);

  rmw_mdds_cpp::ipc::SampleMessage request;
  request.entity_id = 4403u;
  request.sequence_number = 101u;
  request.payload = {'r', 'e', 'm', 'o', 't', 'e'};
  const char *request_bridge_name = "rq/remote_only_trigger";
  const char *request_bridge_type = "std_srvs::srv::Trigger_Request";
  const int request_publish_count_before = FakeMddsBridgePublisherPublishCount(
      request_bridge_name, request_bridge_type);
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      client.get(),
      rmw_mdds_cpp::ipc::Frame{rmw_mdds_cpp::ipc::MessageKind::kPublishSample,
                               2u,
                               rmw_mdds_cpp::ipc::EncodeSampleMessage(request)},
      &error))
      << error;
  ASSERT_TRUE(WaitForFakePublisherPublishCountAtLeast(
      request_bridge_name, request_bridge_type,
      request_publish_count_before + 1, std::chrono::seconds(1)));
  ASSERT_EQ(request.payload.size(),
            FakeMddsBridgePublisherLastPayloadLen(request_bridge_name,
                                                  request_bridge_type));
  EXPECT_EQ(0, std::memcmp(request.payload.data(),
                           FakeMddsBridgePublisherLastPayloadData(
                               request_bridge_name, request_bridge_type),
                           request.payload.size()));
}

TEST(RmwMddsIpcBroker, ServiceBridgePublishWaitsForReliableAckBackpressure) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  EnvVarGuard max_unacked_guard("RMW_MDDS_SERVICE_BRIDGE_MAX_UNACKED");
  EnvVarGuard timeout_guard("RMW_MDDS_SERVICE_BRIDGE_BACKPRESSURE_TIMEOUT_MS");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  unsetenv("RMW_MDDS_SERVICE_BRIDGE_MAX_UNACKED");
  ASSERT_EQ(0,
            setenv("RMW_MDDS_SERVICE_BRIDGE_BACKPRESSURE_TIMEOUT_MS", "50", 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;
  ASSERT_EQ(1, FakeMddsBridgeInitCount());

  rmw_mdds_cpp::ipc::UniqueFd service =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(service) << error;
  auto service_endpoint =
      MakeEndpoint(7705u, rmw_mdds_cpp::ipc::EndpointKind::kService,
                   "/backpressure_trigger", "std_srvs::srv::Trigger");
  RegisterEndpoint(service.get(), service_endpoint, 1u);

  const char *response_topic = "rr/backpressure_trigger";
  const char *response_type = "std_srvs::srv::Trigger_Response";
  ASSERT_EQ(1, FakeMddsBridgeHasPublisher(response_topic, response_type));
  const int publish_count_before =
      FakeMddsBridgePublisherPublishCount(response_topic, response_type);
  FakeMddsBridgeSetPublisherUnackedCount(response_topic, response_type, 1u);

  const std::vector<uint8_t> response_payload =
      MakeServiceWirePayload(0x1234u, 7, {'b', 'a', 'c', 'k'});
  rmw_mdds_cpp::ipc::SampleMessage response;
  response.entity_id = service_endpoint.entity_id;
  response.sequence_number = 1u;
  response.payload = response_payload;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      service.get(),
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 2u,
          rmw_mdds_cpp::ipc::EncodeSampleMessage(response)},
      &error))
      << error;

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_EQ(1, FakeMddsBridgePublisherHeartbeatNowCount(response_topic,
                                                        response_type));
  EXPECT_EQ(publish_count_before,
            FakeMddsBridgePublisherPublishCount(response_topic, response_type))
      << "service bridge publish must not bypass reliable backpressure when "
         "the diagnostic timeout fires";

  FakeMddsBridgeSetPublisherUnackedCount(response_topic, response_type, 0u);
  EXPECT_TRUE(WaitForFakePublisherPublishCountAtLeast(
      response_topic, response_type, publish_count_before + 1,
      std::chrono::seconds(1)));
  EXPECT_EQ(response_payload.size(), FakeMddsBridgePublisherLastPayloadLen(
                                         response_topic, response_type));
}

TEST(RmwMddsIpcBroker, ReliableTopicBridgePublishWaitsForAckBackpressure) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  EnvVarGuard max_unacked_guard("RMW_MDDS_TOPIC_BRIDGE_MAX_UNACKED");
  EnvVarGuard timeout_guard("RMW_MDDS_TOPIC_BRIDGE_BACKPRESSURE_TIMEOUT_MS");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_TOPIC_BRIDGE_MAX_UNACKED", "1", 1));
  ASSERT_EQ(
      0,
      setenv("RMW_MDDS_TOPIC_BRIDGE_BACKPRESSURE_TIMEOUT_MS", "50", 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;
  ASSERT_EQ(1, FakeMddsBridgeInitCount());

  rmw_mdds_cpp::ipc::UniqueFd publisher =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(publisher) << error;
  auto publisher_endpoint =
      MakeEndpoint(7805u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                   "/rt/backpressure_topic", "std_msgs/msg/String");
  publisher_endpoint.mdds_type_name = "std_msgs::msg::dds_::String_";
  publisher_endpoint.qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  publisher_endpoint.qos.depth = 1024u;
  RegisterEndpoint(publisher.get(), publisher_endpoint, 1u);

  const char *topic_name = "rt/backpressure_topic";
  const char *type_name = "std_msgs::msg::dds_::String_";
  ASSERT_EQ(1, FakeMddsBridgeHasPublisher(topic_name, type_name));
  const int publish_count_before =
      FakeMddsBridgePublisherPublishCount(topic_name, type_name);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = publisher_endpoint.entity_id;
  sample.sequence_number = 1u;
  sample.mdds_payload = true;
  sample.payload = {'t', 'o', 'p', 'i', 'c'};
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 2u,
          rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
      << error;

  EXPECT_TRUE(WaitForFakePublisherPublishCountAtLeast(topic_name, type_name,
                                                      publish_count_before + 1,
                                                      std::chrono::seconds(1)));
  EXPECT_EQ(0, FakeMddsBridgePublisherHeartbeatNowCount(topic_name, type_name));

  const int blocked_publish_count_before =
      FakeMddsBridgePublisherPublishCount(topic_name, type_name);
  FakeMddsBridgeSetPublisherUnackedCount(topic_name, type_name, 1u);
  sample.sequence_number = 2u;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{rmw_mdds_cpp::ipc::MessageKind::kPublishSample,
                               3u,
                               rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
      << error;

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_EQ(1, FakeMddsBridgePublisherHeartbeatNowCount(topic_name, type_name));
  EXPECT_EQ(blocked_publish_count_before,
            FakeMddsBridgePublisherPublishCount(topic_name, type_name))
      << "reliable topic bridge publish must wait for MDDS acknowledgements";

  FakeMddsBridgeSetPublisherUnackedCount(topic_name, type_name, 0u);
  EXPECT_TRUE(WaitForFakePublisherPublishCountAtLeast(
      topic_name, type_name, blocked_publish_count_before + 1,
      std::chrono::seconds(1)));
  EXPECT_EQ(sample.payload.size(),
            FakeMddsBridgePublisherLastPayloadLen(topic_name, type_name));
}

TEST(RmwMddsIpcBroker, ReliableTopicBridgeBackpressureHonorsKeepLastDepth) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  EnvVarGuard max_unacked_guard("RMW_MDDS_TOPIC_BRIDGE_MAX_UNACKED");
  EnvVarGuard timeout_guard("RMW_MDDS_TOPIC_BRIDGE_BACKPRESSURE_TIMEOUT_MS");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  unsetenv("RMW_MDDS_TOPIC_BRIDGE_MAX_UNACKED");
  ASSERT_EQ(
      0,
      setenv("RMW_MDDS_TOPIC_BRIDGE_BACKPRESSURE_TIMEOUT_MS", "50", 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd publisher =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(publisher) << error;
  auto publisher_endpoint =
      MakeEndpoint(7855u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                   "/rt/depth_backpressure_topic", "std_msgs/msg/String");
  publisher_endpoint.mdds_type_name = "std_msgs::msg::dds_::String_";
  publisher_endpoint.qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  publisher_endpoint.qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  publisher_endpoint.qos.depth = 1u;
  RegisterEndpoint(publisher.get(), publisher_endpoint, 1u);

  const char *topic_name = "rt/depth_backpressure_topic";
  const char *type_name = "std_msgs::msg::dds_::String_";
  ASSERT_EQ(1, FakeMddsBridgeHasPublisher(topic_name, type_name));
  const int publish_count_before =
      FakeMddsBridgePublisherPublishCount(topic_name, type_name);
  FakeMddsBridgeSetPublisherUnackedCount(topic_name, type_name, 1u);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = publisher_endpoint.entity_id;
  sample.sequence_number = 1u;
  sample.mdds_payload = true;
  sample.payload = {'d', 'e', 'p', 't', 'h'};
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 2u,
          rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
      << error;

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_EQ(publish_count_before,
            FakeMddsBridgePublisherPublishCount(topic_name, type_name))
      << "reliable topic backpressure must not exceed KEEP_LAST depth";

  FakeMddsBridgeSetPublisherUnackedCount(topic_name, type_name, 0u);
  EXPECT_TRUE(WaitForFakePublisherPublishCountAtLeast(
      topic_name, type_name, publish_count_before + 1,
      std::chrono::seconds(1)));
}

TEST(RmwMddsIpcBroker, BestEffortTopicBridgePublishBypassesAckBackpressure) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  EnvVarGuard max_unacked_guard("RMW_MDDS_TOPIC_BRIDGE_MAX_UNACKED");
  EnvVarGuard timeout_guard("RMW_MDDS_TOPIC_BRIDGE_BACKPRESSURE_TIMEOUT_MS");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_TOPIC_BRIDGE_MAX_UNACKED", "1", 1));
  ASSERT_EQ(
      0,
      setenv("RMW_MDDS_TOPIC_BRIDGE_BACKPRESSURE_TIMEOUT_MS", "50", 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd publisher =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(publisher) << error;
  auto publisher_endpoint =
      MakeEndpoint(7905u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                   "/rt/best_effort_topic", "std_msgs/msg/String");
  publisher_endpoint.mdds_type_name = "std_msgs::msg::dds_::String_";
  publisher_endpoint.qos.reliability =
      RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  RegisterEndpoint(publisher.get(), publisher_endpoint, 1u);

  const char *topic_name = "rt/best_effort_topic";
  const char *type_name = "std_msgs::msg::dds_::String_";
  ASSERT_EQ(1, FakeMddsBridgeHasPublisher(topic_name, type_name));
  const int publish_count_before =
      FakeMddsBridgePublisherPublishCount(topic_name, type_name);
  FakeMddsBridgeSetPublisherUnackedCount(topic_name, type_name, 1u);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = publisher_endpoint.entity_id;
  sample.sequence_number = 1u;
  sample.mdds_payload = true;
  sample.payload = {'b', 'e', 's', 't'};
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 2u,
          rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
      << error;

  EXPECT_TRUE(WaitForFakePublisherPublishCountAtLeast(
      topic_name, type_name, publish_count_before + 1,
      std::chrono::milliseconds(250)))
      << "best-effort topic bridge publish must not wait for acknowledgements";
  EXPECT_EQ(0, FakeMddsBridgePublisherHeartbeatNowCount(topic_name, type_name));
  FakeMddsBridgeSetPublisherUnackedCount(topic_name, type_name, 0u);
}

TEST(RmwMddsIpcBroker, SynthesizesRemoteServiceGraphFromBridgeMatchedClient) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  MddsBridgeQos qos = {};
  MddsBridgeSubscriber *remote_service_request_subscription =
      MddsBridgeSubscribeQos("rq/broker_bridge_trigger",
                             "std_srvs/srv/Trigger_Request", &qos, nullptr,
                             nullptr);
  ASSERT_NE(nullptr, remote_service_request_subscription);
  MddsBridgePublisher *remote_service_response_publisher =
      MddsBridgeCreatePublisherQos("rr/broker_bridge_trigger",
                                   "std_srvs/srv/Trigger_Response", &qos);
  ASSERT_NE(nullptr, remote_service_response_publisher);

  rmw_mdds_cpp::ipc::UniqueFd client =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client) << error;

  auto client_endpoint =
      MakeEndpoint(505u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/broker_bridge_trigger", "std_srvs/srv/Trigger");
  RegisterEndpoint(client.get(), client_endpoint, 1u);

  EXPECT_TRUE(WaitForGraphEndpoint(
      client.get(), rmw_mdds_cpp::ipc::EndpointKind::kService,
      "/broker_bridge_trigger", "std_srvs/srv/Trigger",
      std::chrono::seconds(1)))
      << "a fresh client matched to a remote request subscriber must see a "
         "synthetic service";

  MddsBridgeDestroyPublisher(remote_service_response_publisher);
  MddsBridgeUnsubscribe(remote_service_request_subscription);
}

TEST(RmwMddsIpcBroker,
     RebroadcastsRemoteServiceGraphWhenBridgeMatchArrivesAfterClient) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  rmw_mdds_cpp::ipc::UniqueFd client =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client) << error;

  MddsBridgeQos qos = {};
  MddsBridgePublisher *remote_service_response_publisher =
      MddsBridgeCreatePublisherQos("rr/late_broker_bridge_trigger",
                                   "std_srvs/srv/Trigger_Response", &qos);
  ASSERT_NE(nullptr, remote_service_response_publisher);

  auto client_endpoint =
      MakeEndpoint(606u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/late_broker_bridge_trigger", "std_srvs/srv/Trigger");
  RegisterEndpoint(client.get(), client_endpoint, 1u);
  DrainReadableFrames(client.get(), std::chrono::milliseconds(400));

  MddsBridgeSubscriber *remote_service_request_subscription =
      MddsBridgeSubscribeQos("rq/late_broker_bridge_trigger",
                             "std_srvs/srv/Trigger_Request", &qos, nullptr,
                             nullptr);
  ASSERT_NE(nullptr, remote_service_request_subscription);

  EXPECT_TRUE(WaitForGraphEndpoint(
      client.get(), rmw_mdds_cpp::ipc::EndpointKind::kService,
      "/late_broker_bridge_trigger", "std_srvs/srv/Trigger",
      std::chrono::seconds(3)))
      << "a bridge match that arrives after client registration must still "
         "refresh availability";

  MddsBridgeDestroyPublisher(remote_service_response_publisher);
  MddsBridgeUnsubscribe(remote_service_request_subscription);
}

TEST(RmwMddsIpcBroker,
     DoesNotSynthesizeRemoteServiceFromResponsePublisherOnly) {
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  MddsBridgeQos qos = {};
  MddsBridgePublisher *remote_service_response_publisher =
      MddsBridgeCreatePublisherQos("rr/response_matched_trigger",
                                   "std_srvs/srv/Trigger_Response", &qos);
  ASSERT_NE(nullptr, remote_service_response_publisher);

  rmw_mdds_cpp::ipc::UniqueFd client =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client) << error;

  auto client_endpoint =
      MakeEndpoint(707u, rmw_mdds_cpp::ipc::EndpointKind::kClient,
                   "/response_matched_trigger", "std_srvs/srv/Trigger");
  RegisterEndpoint(client.get(), client_endpoint, 1u);

  EXPECT_FALSE(WaitForGraphEndpoint(
      client.get(), rmw_mdds_cpp::ipc::EndpointKind::kService,
      "/response_matched_trigger", "std_srvs/srv/Trigger",
      std::chrono::milliseconds(300)))
      << "a response publisher alone can make response delivery possible, but "
         "does not prove that request delivery to the server is ready";

  MddsBridgeSubscriber *remote_service_request_subscription =
      MddsBridgeSubscribeQos("rq/response_matched_trigger",
                             "std_srvs/srv/Trigger_Request", &qos, nullptr,
                             nullptr);
  ASSERT_NE(nullptr, remote_service_request_subscription);
  EXPECT_TRUE(WaitForGraphEndpoint(
      client.get(), rmw_mdds_cpp::ipc::EndpointKind::kService,
      "/response_matched_trigger", "std_srvs/srv/Trigger",
      std::chrono::seconds(3)))
      << "service availability should appear after both request and response "
         "bridge directions have matched";

  MddsBridgeUnsubscribe(remote_service_request_subscription);
  MddsBridgeDestroyPublisher(remote_service_response_publisher);
}

TEST(RmwMddsIpcBroker, MarksRemoteGraphServicesAsNonLocal) {
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  auto remote_service =
      MakeEndpoint(8080u, rmw_mdds_cpp::ipc::EndpointKind::kService,
                   "/graph_only_trigger", "std_srvs/srv/Trigger");
  remote_service.domain_id = 42424u;
  remote_service.local_context_id = 0x12345678u;
  const std::vector<rmw_mdds_cpp::ipc::EndpointDescriptor> remote_endpoints{
      remote_service};
  const std::vector<uint8_t> graph_sync = rmw_mdds_cpp::ipc::EncodeGraphUpdate(
      0x5152535455565758u, 1u, remote_endpoints);

  ASSERT_EQ(
      1, FakeMddsBridgeInjectFor("mdds_graph_sync", "mdds_graph_EndpointList",
                                 graph_sync.data(),
                                 static_cast<uint32_t>(graph_sync.size()), 1u));

  rmw_mdds_cpp::ipc::UniqueFd observer =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(observer) << error;
  rmw_mdds_cpp::ipc::EndpointDescriptor observed_service;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
  bool found = false;
  while (std::chrono::steady_clock::now() < deadline && !found) {
    if (!HasReadableData(observer.get())) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    rmw_mdds_cpp::ipc::Frame frame;
    ASSERT_EQ(rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
              rmw_mdds_cpp::ipc::ReadFrame(observer.get(), &frame, &error))
        << error;
    found = FindGraphEndpoint(frame, rmw_mdds_cpp::ipc::EndpointKind::kService,
                              "/graph_only_trigger", "std_srvs/srv/Trigger",
                              &observed_service);
  }

  ASSERT_TRUE(found)
      << "remote graph service should still be visible for graph "
         "introspection";
  EXPECT_EQ(0u, observed_service.local_context_id)
      << "remote graph-sync endpoints must not carry a peer process context id "
         "into this broker's local graph; service availability uses non-zero "
         "local_context_id to distinguish local/synthetic matched services "
         "from "
         "remote graph-only services";
}

TEST(RmwMddsIpcBroker, GraphSyncUsesBestEffortSnapshotQos) {
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");
  rmw_mdds_cpp::BridgeBackend::Instance().ResetForTesting();
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  ASSERT_TRUE(broker.Start(socket_path.path(), &error)) << error;

  constexpr const char *kGraphTopic = "mdds_graph_sync";
  constexpr const char *kGraphType = "mdds_graph_EndpointList";
  ASSERT_EQ(1, FakeMddsBridgeHasPublisher(kGraphTopic, kGraphType));
  ASSERT_EQ(1, FakeMddsBridgeHasSubscriber(kGraphTopic, kGraphType));
  EXPECT_EQ(1, FakeMddsBridgePublisherReliability(kGraphTopic, kGraphType));
  EXPECT_EQ(1, FakeMddsBridgeSubscriberReliability(kGraphTopic, kGraphType));
  EXPECT_EQ(0, FakeMddsBridgePublisherHistoryKind(kGraphTopic, kGraphType));
  EXPECT_EQ(0, FakeMddsBridgeSubscriberHistoryKind(kGraphTopic, kGraphType));
  EXPECT_EQ(1u, FakeMddsBridgePublisherHistoryDepth(kGraphTopic, kGraphType));
  EXPECT_EQ(1u, FakeMddsBridgeSubscriberHistoryDepth(kGraphTopic, kGraphType));
}
