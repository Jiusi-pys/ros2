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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "bridge_backend.hpp"
#include "ipc_broker.hpp"
#include "ipc_protocol.hpp"
#include "ipc_transport.hpp"

extern "C" {
struct MddsBridgePublisher;
struct MddsBridgeSubscriber;
typedef struct {
  const void * data;
  uint32_t len;
  uint64_t sequenceNumber;
  uint8_t senderGuid[16];
} MddsBridgeSample;
typedef struct {
  int reliability;
  int durability;
  int historyKind;
  uint32_t historyDepth;
  uint32_t deadlineMs;
  uint32_t lifespanMs;
} MddsBridgeQos;
typedef void (*MddsBridgeDataCallback)(const MddsBridgeSample * sample, void * userData);
void FakeMddsBridgeReset(void);
int FakeMddsBridgeInitCount(void);
int FakeMddsBridgePublisherCount(void);
int FakeMddsBridgeSubscriberCount(void);
int FakeMddsBridgeHasPublisher(const char * topicName, const char * typeName);
int FakeMddsBridgeHasSubscriber(const char * topicName, const char * typeName);
int FakeMddsBridgePublisherPublishCount(const char * topicName, const char * typeName);
const uint8_t * FakeMddsBridgePublisherLastPayloadData(
  const char * topicName, const char * typeName);
uint32_t FakeMddsBridgePublisherLastPayloadLen(const char * topicName, const char * typeName);
void FakeMddsBridgeInject(const void * data, uint32_t len);
int FakeMddsBridgeInjectFor(
  const char * topicName, const char * typeName, const void * data, uint32_t len,
  uint64_t sequenceNumber);
MddsBridgeSubscriber * MddsBridgeSubscribeQos(
  const char * topicName, const char * typeName, const MddsBridgeQos * qos,
  MddsBridgeDataCallback cb, void * userData);
void MddsBridgeUnsubscribe(MddsBridgeSubscriber * sub);
MddsBridgePublisher * MddsBridgeCreatePublisherQos(
  const char * topicName, const char * typeName, const MddsBridgeQos * qos);
void MddsBridgeDestroyPublisher(MddsBridgePublisher * pub);
}

namespace
{
class EnvVarGuard
{
public:
  explicit EnvVarGuard(const char * name) : name_(name)
  {
    const char * value = std::getenv(name);
    if (value != nullptr) {
      had_value_ = true;
      value_ = value;
    }
  }

  ~EnvVarGuard()
  {
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

class TempSocketPath
{
public:
  TempSocketPath()
  {
    char templ[] = "/tmp/rmw_mdds_ipc_broker_XXXXXX";
    char * dir = mkdtemp(templ);
    if (dir != nullptr) {
      dir_ = dir;
      path_ = dir_ + "/broker.sock";
    }
  }

  ~TempSocketPath()
  {
    if (!path_.empty()) {
      unlink(path_.c_str());
    }
    if (!dir_.empty()) {
      rmdir(dir_.c_str());
    }
  }

  const std::string & path() const
  {
    return path_;
  }

private:
  std::string dir_;
  std::string path_;
};

rmw_mdds_cpp::ipc::EndpointDescriptor MakeEndpoint(
  uint64_t entity_id, rmw_mdds_cpp::ipc::EndpointKind kind, const char * topic_name,
  const char * type_name)
{
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

void ExpectValidGraphUpdate(const rmw_mdds_cpp::ipc::Frame & frame)
{
  std::string error;
  std::vector<rmw_mdds_cpp::ipc::EndpointDescriptor> endpoints;
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::DecodeEndpointList(
      frame.payload.data(), frame.payload.size(), &endpoints, &error))
    << error;
}

bool GraphUpdateContainsEndpoint(
  const rmw_mdds_cpp::ipc::Frame & frame, rmw_mdds_cpp::ipc::EndpointKind kind,
  const char * topic_name, const char * type_name)
{
  std::string error;
  std::vector<rmw_mdds_cpp::ipc::EndpointDescriptor> endpoints;
  if (
    frame.kind != rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate ||
    !rmw_mdds_cpp::ipc::DecodeEndpointList(
      frame.payload.data(), frame.payload.size(), &endpoints, &error)) {
    return false;
  }
  return std::any_of(
    endpoints.begin(), endpoints.end(),
    [kind, topic_name, type_name](const rmw_mdds_cpp::ipc::EndpointDescriptor & endpoint) {
      return endpoint.kind == kind && endpoint.topic_name == topic_name &&
             endpoint.type_name == type_name;
    });
}

void RegisterEndpoint(
  int fd, const rmw_mdds_cpp::ipc::EndpointDescriptor & endpoint, uint64_t request_id)
{
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
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::WriteFrame(
      fd,
      rmw_mdds_cpp::ipc::Frame{
        kind, request_id, rmw_mdds_cpp::ipc::EncodeEndpointDescriptor(endpoint)},
      &error))
    << error;

  rmw_mdds_cpp::ipc::Frame ack;
  for (;;) {
    ASSERT_EQ(
      rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
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
  EXPECT_TRUE(ack.payload.empty());
}

bool HasReadableData(int fd)
{
  pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  return poll(&pfd, 1u, 50) > 0 && (pfd.revents & POLLIN) != 0;
}

bool WaitForGraphEndpoint(
  int fd, rmw_mdds_cpp::ipc::EndpointKind kind, const char * topic_name,
  const char * type_name, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::string error;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!HasReadableData(fd)) {
      usleep(10000);
      continue;
    }
    rmw_mdds_cpp::ipc::Frame frame;
    if (
      rmw_mdds_cpp::ipc::ReadFrame(fd, &frame, &error) !=
      rmw_mdds_cpp::ipc::ReadFrameStatus::kOk) {
      return false;
    }
    if (GraphUpdateContainsEndpoint(frame, kind, topic_name, type_name)) {
      return true;
    }
  }
  return false;
}

void DrainReadableFrames(int fd, std::chrono::milliseconds duration)
{
  const auto deadline = std::chrono::steady_clock::now() + duration;
  std::string error;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!HasReadableData(fd)) {
      usleep(10000);
      continue;
    }
    rmw_mdds_cpp::ipc::Frame frame;
    if (
      rmw_mdds_cpp::ipc::ReadFrame(fd, &frame, &error) !=
      rmw_mdds_cpp::ipc::ReadFrameStatus::kOk) {
      return;
    }
  }
}

bool WaitForFakePublisherPublishCountAtLeast(
  const char * topic_name, const char * type_name, int expected_count,
  std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (FakeMddsBridgePublisherPublishCount(topic_name, type_name) >= expected_count) {
      return true;
    }
    usleep(10000);
  }
  return FakeMddsBridgePublisherPublishCount(topic_name, type_name) >= expected_count;
}

void ReadNextNonGraphFrame(int fd, rmw_mdds_cpp::ipc::Frame * frame, std::string * error)
{
  ASSERT_NE(nullptr, frame);
  for (;;) {
    ASSERT_EQ(
      rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
      rmw_mdds_cpp::ipc::ReadFrame(fd, frame, error))
      << (error == nullptr ? "" : *error);
    if (frame->kind == rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate) {
      ExpectValidGraphUpdate(*frame);
      continue;
    }
    return;
  }
}

bool ReadUntilPayload(
  int fd, const std::vector<uint8_t> & expected_payload, rmw_mdds_cpp::ipc::SampleMessage * sample,
  std::string * error)
{
  for (int i = 0; i < 8; ++i) {
    rmw_mdds_cpp::ipc::Frame delivery;
    ReadNextNonGraphFrame(fd, &delivery, error);
    EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kDeliverSample, delivery.kind);
    rmw_mdds_cpp::ipc::SampleMessage delivered_sample;
    if (!rmw_mdds_cpp::ipc::DecodeSampleMessage(
        delivery.payload.data(), delivery.payload.size(), &delivered_sample, error)) {
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

bool WaitForPayload(
  int fd, const std::vector<uint8_t> & expected_payload,
  rmw_mdds_cpp::ipc::SampleMessage * sample, std::string * error,
  std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!HasReadableData(fd)) {
      usleep(10000);
      continue;
    }
    rmw_mdds_cpp::ipc::Frame delivery;
    if (
      rmw_mdds_cpp::ipc::ReadFrame(fd, &delivery, error) !=
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
    if (!rmw_mdds_cpp::ipc::DecodeSampleMessage(
        delivery.payload.data(), delivery.payload.size(), &delivered_sample, error)) {
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
}  // namespace

TEST(RmwMddsIpcBroker, RoutesPublishedSamplesToMatchingSubscriptions)
{
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

  RegisterEndpoint(
    publisher.get(),
    MakeEndpoint(
      100u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher, "/broker/chatter",
      "std_msgs/msg/String"),
    1u);
  RegisterEndpoint(
    matching_subscription.get(),
    MakeEndpoint(
      200u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription, "/broker/chatter",
      "std_msgs/msg/String"),
    2u);
  RegisterEndpoint(
    other_subscription.get(),
    MakeEndpoint(
      300u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription, "/broker/other",
      "std_msgs/msg/String"),
    3u);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 100u;
  sample.sequence_number = 44u;
  sample.payload = {'b', 'r', 'o', 'k', 'e', 'r'};
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{
        rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 4u,
        rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
    << error;

  rmw_mdds_cpp::ipc::Frame delivery;
  ReadNextNonGraphFrame(matching_subscription.get(), &delivery, &error);
  EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kDeliverSample, delivery.kind);
  EXPECT_EQ(4u, delivery.request_id);

  rmw_mdds_cpp::ipc::SampleMessage delivered_sample;
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::DecodeSampleMessage(
      delivery.payload.data(), delivery.payload.size(), &delivered_sample, &error))
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

TEST(RmwMddsIpcBroker, ReplaysTransientLocalSampleToLateSubscription)
{
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
  auto publisher_endpoint = MakeEndpoint(
    1100u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher, "/broker/transient_local",
    "std_msgs/msg/String");
  publisher_endpoint.qos.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  publisher_endpoint.qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  publisher_endpoint.qos.depth = 1u;
  RegisterEndpoint(publisher.get(), publisher_endpoint, 1u);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 1100u;
  sample.sequence_number = 88u;
  sample.payload = {'r', 'e', 't', 'a', 'i', 'n', 'e', 'd'};
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{
        rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 3u,
        rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
    << error;

  rmw_mdds_cpp::ipc::UniqueFd subscription =
    rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(subscription) << error;
  auto subscription_endpoint = MakeEndpoint(
    2200u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription, "/broker/transient_local",
    "std_msgs/msg/String");
  subscription_endpoint.qos.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  subscription_endpoint.qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  subscription_endpoint.qos.depth = 1u;
  RegisterEndpoint(subscription.get(), subscription_endpoint, 4u);

  rmw_mdds_cpp::ipc::SampleMessage delivered_sample;
  ASSERT_TRUE(
    WaitForPayload(
      subscription.get(), sample.payload, &delivered_sample, &error,
      std::chrono::milliseconds(500)))
    << error;
  EXPECT_EQ(sample.entity_id, delivered_sample.entity_id);
  EXPECT_EQ(sample.sequence_number, delivered_sample.sequence_number);
  EXPECT_EQ(sample.payload, delivered_sample.payload);
}

TEST(RmwMddsIpcBroker, OwnsBridgeTransportForRegisteredTopicEndpoints)
{
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

  auto publisher_endpoint = MakeEndpoint(
    101u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher, "/rt/broker_bridge_chatter",
    "std_msgs/msg/String");
  publisher_endpoint.mdds_type_name = "std_msgs::msg::dds_::String_";
  auto subscription_endpoint = MakeEndpoint(
    202u, rmw_mdds_cpp::ipc::EndpointKind::kSubscription, "/rt/broker_bridge_chatter",
    "std_msgs/msg/String");
  subscription_endpoint.mdds_type_name = "std_msgs::msg::dds_::String_";

  RegisterEndpoint(publisher.get(), publisher_endpoint, 1u);
  RegisterEndpoint(subscription.get(), subscription_endpoint, 2u);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 101u;
  sample.sequence_number = 55u;
  sample.mdds_payload = true;
  sample.payload = {'b', 'r', 'i', 'd', 'g', 'e'};
  const char * topic_bridge_name = "rt/broker_bridge_chatter";
  const char * topic_bridge_type = "std_msgs::msg::dds_::String_";
  const int topic_publish_count_before =
    FakeMddsBridgePublisherPublishCount(topic_bridge_name, topic_bridge_type);
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::WriteFrame(
      publisher.get(),
      rmw_mdds_cpp::ipc::Frame{
        rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 3u,
        rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
    << error;

  ASSERT_TRUE(
    WaitForFakePublisherPublishCountAtLeast(
      topic_bridge_name, topic_bridge_type, topic_publish_count_before + 1,
      std::chrono::seconds(1)));
  ASSERT_EQ(
    sample.payload.size(),
    FakeMddsBridgePublisherLastPayloadLen(topic_bridge_name, topic_bridge_type));
  EXPECT_EQ(
    0,
    std::memcmp(
      sample.payload.data(),
      FakeMddsBridgePublisherLastPayloadData(topic_bridge_name, topic_bridge_type),
      sample.payload.size()));

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
    ASSERT_TRUE(
      rmw_mdds_cpp::ipc::DecodeSampleMessage(
        delivery.payload.data(), delivery.payload.size(), &delivered_sample, &error))
      << error;
    EXPECT_TRUE(delivered_sample.mdds_payload);
    saw_injected_payload = delivered_sample.payload == expected_injected;
  }
  EXPECT_TRUE(saw_injected_payload);
}

TEST(RmwMddsIpcBroker, OwnsBridgeTransportForRegisteredServiceEndpoints)
{
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

  auto client_endpoint = MakeEndpoint(
    303u, rmw_mdds_cpp::ipc::EndpointKind::kClient, "/broker_bridge_trigger",
    "std_srvs::srv::Trigger");
  auto service_endpoint = MakeEndpoint(
    404u, rmw_mdds_cpp::ipc::EndpointKind::kService, "/broker_bridge_trigger",
    "std_srvs::srv::Trigger");

  RegisterEndpoint(client.get(), client_endpoint, 1u);
  RegisterEndpoint(service.get(), service_endpoint, 2u);

  EXPECT_GE(FakeMddsBridgePublisherCount(), 2);
  EXPECT_GE(FakeMddsBridgeSubscriberCount(), 2);
  EXPECT_EQ(
    1, FakeMddsBridgeHasPublisher("rq/broker_bridge_trigger", "std_srvs::srv::Trigger_Request"));
  EXPECT_EQ(
    1, FakeMddsBridgeHasSubscriber(
         "rr/broker_bridge_trigger", "std_srvs::srv::Trigger_Response"));
  EXPECT_EQ(
    1, FakeMddsBridgeHasSubscriber("rq/broker_bridge_trigger", "std_srvs::srv::Trigger_Request"));
  EXPECT_EQ(
    1, FakeMddsBridgeHasPublisher(
         "rr/broker_bridge_trigger", "std_srvs::srv::Trigger_Response"));

  rmw_mdds_cpp::ipc::SampleMessage request;
  request.entity_id = 303u;
  request.sequence_number = 11u;
  request.payload = {'r', 'e', 'q'};
  const char * request_bridge_name = "rq/broker_bridge_trigger";
  const char * request_bridge_type = "std_srvs::srv::Trigger_Request";
  const int request_publish_count_before =
    FakeMddsBridgePublisherPublishCount(request_bridge_name, request_bridge_type);
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::WriteFrame(
      client.get(),
      rmw_mdds_cpp::ipc::Frame{
        rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 3u,
        rmw_mdds_cpp::ipc::EncodeSampleMessage(request)},
      &error))
    << error;
  ASSERT_TRUE(
    WaitForFakePublisherPublishCountAtLeast(
      request_bridge_name, request_bridge_type, request_publish_count_before + 1,
      std::chrono::seconds(1)));
  ASSERT_EQ(
    request.payload.size(),
    FakeMddsBridgePublisherLastPayloadLen(request_bridge_name, request_bridge_type));
  EXPECT_EQ(
    0,
    std::memcmp(
      request.payload.data(),
      FakeMddsBridgePublisherLastPayloadData(request_bridge_name, request_bridge_type),
      request.payload.size()));

  rmw_mdds_cpp::ipc::SampleMessage response;
  response.entity_id = 404u;
  response.sequence_number = 12u;
  response.payload = {'r', 'e', 's'};
  const char * response_bridge_name = "rr/broker_bridge_trigger";
  const char * response_bridge_type = "std_srvs::srv::Trigger_Response";
  const int response_publish_count_before =
    FakeMddsBridgePublisherPublishCount(response_bridge_name, response_bridge_type);
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::WriteFrame(
      service.get(),
      rmw_mdds_cpp::ipc::Frame{
        rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 4u,
        rmw_mdds_cpp::ipc::EncodeSampleMessage(response)},
      &error))
    << error;
  ASSERT_TRUE(
    WaitForFakePublisherPublishCountAtLeast(
      response_bridge_name, response_bridge_type, response_publish_count_before + 1,
      std::chrono::seconds(1)));
  ASSERT_EQ(
    response.payload.size(),
    FakeMddsBridgePublisherLastPayloadLen(response_bridge_name, response_bridge_type));
  EXPECT_EQ(
    0,
    std::memcmp(
      response.payload.data(),
      FakeMddsBridgePublisherLastPayloadData(response_bridge_name, response_bridge_type),
      response.payload.size()));

  const std::vector<uint8_t> external_request = {'r', 'e', 'm', 'o', 't', 'e', '_', 'r', 'e', 'q'};
  ASSERT_EQ(
    1,
    FakeMddsBridgeInjectFor(
      "rq/broker_bridge_trigger", "std_srvs::srv::Trigger_Request",
      external_request.data(), static_cast<uint32_t>(external_request.size()), 21u));
  rmw_mdds_cpp::ipc::SampleMessage delivered_request;
  EXPECT_TRUE(ReadUntilPayload(service.get(), external_request, &delivered_request, &error))
    << error;
  EXPECT_TRUE(delivered_request.mdds_payload);
  EXPECT_EQ(21u, delivered_request.sequence_number);

  const std::vector<uint8_t> external_response =
    {'r', 'e', 'm', 'o', 't', 'e', '_', 'r', 'e', 's'};
  ASSERT_EQ(
    1,
    FakeMddsBridgeInjectFor(
      "rr/broker_bridge_trigger", "std_srvs::srv::Trigger_Response",
      external_response.data(), static_cast<uint32_t>(external_response.size()), 22u));
  rmw_mdds_cpp::ipc::SampleMessage delivered_response;
  EXPECT_TRUE(ReadUntilPayload(client.get(), external_response, &delivered_response, &error))
    << error;
  EXPECT_TRUE(delivered_response.mdds_payload);
  EXPECT_EQ(22u, delivered_response.sequence_number);
}

TEST(RmwMddsIpcBroker, SynthesizesRemoteServiceGraphFromBridgeMatchedClient)
{
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
  MddsBridgeSubscriber * remote_service_request_subscription = MddsBridgeSubscribeQos(
    "rq/broker_bridge_trigger", "std_srvs/srv/Trigger_Request", &qos, nullptr, nullptr);
  ASSERT_NE(nullptr, remote_service_request_subscription);

  rmw_mdds_cpp::ipc::UniqueFd client =
    rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client) << error;

  auto client_endpoint = MakeEndpoint(
    505u, rmw_mdds_cpp::ipc::EndpointKind::kClient, "/broker_bridge_trigger",
    "std_srvs/srv/Trigger");
  RegisterEndpoint(client.get(), client_endpoint, 1u);

  EXPECT_TRUE(
    WaitForGraphEndpoint(
      client.get(), rmw_mdds_cpp::ipc::EndpointKind::kService, "/broker_bridge_trigger",
      "std_srvs/srv/Trigger", std::chrono::seconds(1)))
    << "a fresh client matched to a remote request subscriber must see a synthetic service";

  MddsBridgeUnsubscribe(remote_service_request_subscription);
}

TEST(RmwMddsIpcBroker, RebroadcastsRemoteServiceGraphWhenBridgeMatchArrivesAfterClient)
{
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

  auto client_endpoint = MakeEndpoint(
    606u, rmw_mdds_cpp::ipc::EndpointKind::kClient, "/late_broker_bridge_trigger",
    "std_srvs/srv/Trigger");
  RegisterEndpoint(client.get(), client_endpoint, 1u);
  DrainReadableFrames(client.get(), std::chrono::milliseconds(400));

  MddsBridgeQos qos = {};
  MddsBridgeSubscriber * remote_service_request_subscription = MddsBridgeSubscribeQos(
    "rq/late_broker_bridge_trigger", "std_srvs/srv/Trigger_Request", &qos, nullptr, nullptr);
  ASSERT_NE(nullptr, remote_service_request_subscription);

  EXPECT_TRUE(
    WaitForGraphEndpoint(
      client.get(), rmw_mdds_cpp::ipc::EndpointKind::kService,
      "/late_broker_bridge_trigger", "std_srvs/srv/Trigger", std::chrono::seconds(3)))
    << "a bridge match that arrives after client registration must still refresh availability";

  MddsBridgeUnsubscribe(remote_service_request_subscription);
}

TEST(RmwMddsIpcBroker, SynthesizesRemoteServiceGraphFromResponsePublisherMatch)
{
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
  MddsBridgePublisher * remote_service_response_publisher = MddsBridgeCreatePublisherQos(
    "rr/response_matched_trigger", "std_srvs/srv/Trigger_Response", &qos);
  ASSERT_NE(nullptr, remote_service_response_publisher);

  rmw_mdds_cpp::ipc::UniqueFd client =
    rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client) << error;

  auto client_endpoint = MakeEndpoint(
    707u, rmw_mdds_cpp::ipc::EndpointKind::kClient, "/response_matched_trigger",
    "std_srvs/srv/Trigger");
  RegisterEndpoint(client.get(), client_endpoint, 1u);

  EXPECT_TRUE(
    WaitForGraphEndpoint(
      client.get(), rmw_mdds_cpp::ipc::EndpointKind::kService,
      "/response_matched_trigger", "std_srvs/srv/Trigger", std::chrono::seconds(1)))
    << "a matched remote response publisher also proves a remote service server";

  MddsBridgeDestroyPublisher(remote_service_response_publisher);
}
