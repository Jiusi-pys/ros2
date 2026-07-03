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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <string>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"

#include "context.hpp"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/publisher_options.h"
#include "rmw/rmw.h"
#include "rmw/subscription_options.h"
#include "rtps_protocol.hpp"
#include "rtps_transport.hpp"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "std_msgs/msg/string.hpp"

namespace
{
constexpr size_t TEST_DOMAIN_ID = 42;

class ScopedEnvVar
{
public:
  ScopedEnvVar(const char * name, const char * value) : name_(name)
  {
    const char * old_value = std::getenv(name);
    if (old_value != nullptr) {
      had_old_value_ = true;
      old_value_ = old_value;
    }
    setenv(name, value, 1);
  }

  ~ScopedEnvVar()
  {
    if (had_old_value_) {
      setenv(name_.c_str(), old_value_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

private:
  std::string name_;
  std::string old_value_;
  bool had_old_value_ = false;
};

void SetEnclave(rmw_init_options_t * options, const char * enclave)
{
  options->allocator.deallocate(options->enclave, options->allocator.state);
  options->enclave = rcutils_strdup(enclave, options->allocator);
  ASSERT_NE(nullptr, options->enclave);
}

rmw_ret_t AssertNodeLiveliness(const rmw_node_t * node)
{
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
  const rmw_ret_t ret = rmw_node_assert_liveliness(node);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  return ret;
}

rmw_mdds_cpp::rtps::DataSubmessage ReceiveSpdpPacket(
  const rmw_mdds_cpp::rtps::UdpSocket & receiver,
  rmw_mdds_cpp::rtps::UdpEndpoint * remote)
{
  std::vector<uint8_t> packet;
  std::string error;
  EXPECT_TRUE(receiver.Receive(&packet, remote, 1000, &error)) << error;

  rmw_mdds_cpp::rtps::RtpsHeader header;
  rmw_mdds_cpp::rtps::DataSubmessage data;
  EXPECT_TRUE(
    rmw_mdds_cpp::rtps::DecodeDataMessage(
      packet.data(), packet.size(), &header, &data, &error))
    << error;
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdUnknown, data.reader_id);
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdSpdpBuiltinParticipantWriter, data.writer_id);
  return data;
}

rmw_mdds_cpp::rtps::GuidPrefix TestGuidPrefix(uint8_t base)
{
  return rmw_mdds_cpp::rtps::GuidPrefix{
    base, static_cast<uint8_t>(base + 1u), static_cast<uint8_t>(base + 2u),
    static_cast<uint8_t>(base + 3u), static_cast<uint8_t>(base + 4u),
    static_cast<uint8_t>(base + 5u), static_cast<uint8_t>(base + 6u),
    static_cast<uint8_t>(base + 7u), static_cast<uint8_t>(base + 8u),
    static_cast<uint8_t>(base + 9u), static_cast<uint8_t>(base + 10u),
    static_cast<uint8_t>(base + 11u)};
}

rmw_mdds_cpp::rtps::ParticipantConfig MakeRemoteRtpsConfig(
  uint32_t domain_id, uint32_t participant_id, uint16_t port_base,
  const rmw_mdds_cpp::rtps::GuidPrefix & guid_prefix)
{
  rmw_mdds_cpp::rtps::ParticipantConfig config;
  config.domain_id = domain_id;
  config.participant_id = participant_id;
  config.guid_prefix = guid_prefix;
  config.bind_address = "127.0.0.1";
  config.advertised_address = "127.0.0.1";
  config.port_mapping.port_base = port_base;
  return config;
}

const rmw_mdds_cpp::rtps::Parameter * FindParameter(
  const rmw_mdds_cpp::rtps::ParameterList & parameters, uint16_t parameter_id)
{
  const auto it = std::find_if(
    parameters.begin(), parameters.end(),
    [parameter_id](const rmw_mdds_cpp::rtps::Parameter & parameter) {
      return parameter.parameter_id == parameter_id;
    });
  return it == parameters.end() ? nullptr : &*it;
}

uint32_t ReadU32Little(const std::vector<uint8_t> & bytes, size_t offset)
{
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1u]) << 8u) |
         (static_cast<uint32_t>(bytes[offset + 2u]) << 16u) |
         (static_cast<uint32_t>(bytes[offset + 3u]) << 24u);
}

void AppendU32Little(uint32_t value, std::vector<uint8_t> * bytes)
{
  ASSERT_NE(nullptr, bytes);
  bytes->push_back(static_cast<uint8_t>(value & 0xffu));
  bytes->push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  bytes->push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  bytes->push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

std::string DecodeCdrStringValue(const std::vector<uint8_t> & value)
{
  if (value.size() < 4u) {
    return {};
  }
  const uint32_t string_size = ReadU32Little(value, 0u);
  if (string_size == 0u || value.size() < 4u + string_size) {
    return {};
  }
  return std::string(value.begin() + 4u, value.begin() + 4u + string_size - 1u);
}

bool DecodeCdrStringPayload(const std::vector<uint8_t> & payload, std::string * value)
{
  if (value == nullptr || payload.size() < 8u) {
    return false;
  }
  if (payload[0] != 0x00u || payload[1] != 0x01u || payload[2] != 0x00u || payload[3] != 0x00u) {
    return false;
  }
  const uint32_t string_size = ReadU32Little(payload, 4u);
  if (string_size == 0u || payload.size() < 8u + string_size || payload[8u + string_size - 1u] != 0u) {
    return false;
  }
  value->assign(payload.begin() + 8u, payload.begin() + 8u + string_size - 1u);
  return true;
}

std::vector<uint8_t> EncodeCdrStringPayload(const std::string & value)
{
  std::vector<uint8_t> payload{0x00u, 0x01u, 0x00u, 0x00u};
  AppendU32Little(static_cast<uint32_t>(value.size() + 1u), &payload);
  payload.insert(payload.end(), value.begin(), value.end());
  payload.push_back(0u);
  return payload;
}

rmw_mdds_cpp::rtps::ParameterList DecodeSedpParameters(
  const rmw_mdds_cpp::rtps::DataSubmessage & data)
{
  EXPECT_GE(data.serialized_payload.size(), 4u);
  EXPECT_EQ(0x00u, data.serialized_payload[0]);
  EXPECT_EQ(0x03u, data.serialized_payload[1]);
  EXPECT_EQ(0x00u, data.serialized_payload[2]);
  EXPECT_EQ(0x00u, data.serialized_payload[3]);

  rmw_mdds_cpp::rtps::ParameterList parameters;
  std::string error;
  EXPECT_TRUE(
    rmw_mdds_cpp::rtps::DecodeParameterList(
      data.serialized_payload.data() + 4u, data.serialized_payload.size() - 4u,
      &parameters, &error))
    << error;
  return parameters;
}

rmw_mdds_cpp::rtps::EntityId ExtractEndpointEntityId(
  const rmw_mdds_cpp::rtps::ReceivedSedpEndpointAnnouncement & announcement)
{
  const auto parameters = DecodeSedpParameters(announcement.data);
  const auto * endpoint_guid =
    FindParameter(parameters, rmw_mdds_cpp::rtps::kPidEndpointGuid);
  EXPECT_NE(nullptr, endpoint_guid);
  if (endpoint_guid == nullptr || endpoint_guid->value.size() != 16u) {
    return rmw_mdds_cpp::rtps::kEntityIdUnknown;
  }
  return rmw_mdds_cpp::rtps::EntityId{
    endpoint_guid->value[12], endpoint_guid->value[13],
    endpoint_guid->value[14], endpoint_guid->value[15]};
}

std::vector<rmw_mdds_cpp::rtps::DiscoveredParticipant> WaitForDiscoveredParticipants(
  const rmw_context_t & context)
{
  std::vector<rmw_mdds_cpp::rtps::DiscoveredParticipant> discovered;
  if (context.impl == nullptr || context.impl->rtps_participant == nullptr) {
    return discovered;
  }
  for (int attempt = 0; attempt < 100; ++attempt) {
    discovered = context.impl->rtps_participant->GetDiscoveredParticipants();
    if (!discovered.empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return discovered;
}

std::vector<rmw_mdds_cpp::rtps::DiscoveredSedpEndpoint> WaitForDiscoveredSedpEndpoints(
  const rmw_context_t & context, size_t expected_count)
{
  std::vector<rmw_mdds_cpp::rtps::DiscoveredSedpEndpoint> discovered;
  if (context.impl == nullptr || context.impl->rtps_participant == nullptr) {
    return discovered;
  }
  for (int attempt = 0; attempt < 100; ++attempt) {
    discovered = context.impl->rtps_participant->GetDiscoveredSedpEndpoints();
    if (discovered.size() >= expected_count) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return discovered;
}
}  // namespace

TEST(RmwMddsLifecycle, ContextRetainsRemoteSpdpParticipants)
{
  constexpr uint16_t port_base = 45700u;
  constexpr uint32_t domain_id = 3u;
  const auto remote_prefix = TestGuidPrefix(0x80u);

  ScopedEnvVar broker_mode("RMW_MDDS_BROKER", "0");
  ScopedEnvVar bind_address("RMW_MDDS_RTPS_BIND_ADDRESS", "127.0.0.1");
  ScopedEnvVar advertised_address("RMW_MDDS_RTPS_ADVERTISED_ADDRESS", "127.0.0.1");
  ScopedEnvVar port_base_env("RMW_MDDS_RTPS_PORT_BASE", "45700");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  options.instance_id = 5;
  options.domain_id = domain_id;
  SetEnclave(&options, "/rmw_mdds_rtps_receiver_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  ASSERT_NE(nullptr, context.impl);
  ASSERT_NE(nullptr, context.impl->rtps_participant);

  std::string error;
  auto remote_participant = rmw_mdds_cpp::rtps::RtpsParticipant::Create(
    MakeRemoteRtpsConfig(domain_id, 6u, port_base, remote_prefix), &error);
  ASSERT_NE(nullptr, remote_participant) << error;
  ASSERT_TRUE(
    remote_participant->SendSpdpAnnouncement(
      rmw_mdds_cpp::rtps::UdpEndpoint{
        "127.0.0.1", context.impl->rtps_participant->local_metatraffic_unicast_port()},
      &error))
    << error;

  std::vector<rmw_mdds_cpp::rtps::DiscoveredParticipant> discovered;
  for (int attempt = 0; attempt < 100; ++attempt) {
    discovered = context.impl->rtps_participant->GetDiscoveredParticipants();
    if (!discovered.empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  ASSERT_EQ(1u, discovered.size());
  EXPECT_EQ(remote_prefix, discovered[0].guid_prefix);
  EXPECT_EQ("127.0.0.1", discovered[0].remote_endpoint.address);
  EXPECT_EQ(remote_participant->local_metatraffic_unicast_port(), discovered[0].remote_endpoint.port);
  EXPECT_EQ(1, discovered[0].last_spdp_sequence_number);

  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsLifecycle, ContextAnnouncesPubSubSedpToDiscoveredParticipant)
{
  constexpr uint16_t port_base = 45800u;
  constexpr uint32_t domain_id = 4u;
  const auto remote_prefix = TestGuidPrefix(0xb0u);

  ScopedEnvVar broker_mode("RMW_MDDS_BROKER", "0");
  ScopedEnvVar bind_address("RMW_MDDS_RTPS_BIND_ADDRESS", "127.0.0.1");
  ScopedEnvVar advertised_address("RMW_MDDS_RTPS_ADVERTISED_ADDRESS", "127.0.0.1");
  ScopedEnvVar port_base_env("RMW_MDDS_RTPS_PORT_BASE", "45800");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  options.instance_id = 5;
  options.domain_id = domain_id;
  SetEnclave(&options, "/rmw_mdds_rtps_sedp_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  ASSERT_NE(nullptr, context.impl);
  ASSERT_NE(nullptr, context.impl->rtps_participant);

  std::string error;
  auto remote_participant = rmw_mdds_cpp::rtps::RtpsParticipant::Create(
    MakeRemoteRtpsConfig(domain_id, 6u, port_base, remote_prefix), &error);
  ASSERT_NE(nullptr, remote_participant) << error;
  ASSERT_TRUE(
    remote_participant->SendSpdpAnnouncement(
      rmw_mdds_cpp::rtps::UdpEndpoint{
        "127.0.0.1", context.impl->rtps_participant->local_metatraffic_unicast_port()},
      &error))
    << error;
  ASSERT_EQ(1u, WaitForDiscoveredParticipants(context).size());

  rmw_node_t * node = rmw_create_node(&context, "mdds_sedp_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const auto * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_sedp_chatter", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);

  rmw_mdds_cpp::rtps::ReceivedSedpEndpointAnnouncement publisher_sedp;
  rmw_mdds_cpp::rtps::UdpEndpoint remote;
  ASSERT_TRUE(
    remote_participant->ReceiveSedpEndpointAnnouncement(&publisher_sedp, &remote, 1000, &error))
    << error;
  EXPECT_EQ(context.impl->rtps_participant->local_metatraffic_unicast_port(), remote.port);
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinPublicationsWriter, publisher_sedp.data.writer_id);

  const auto publisher_parameters = DecodeSedpParameters(publisher_sedp.data);
  const auto * publisher_endpoint_guid =
    FindParameter(publisher_parameters, rmw_mdds_cpp::rtps::kPidEndpointGuid);
  ASSERT_NE(nullptr, publisher_endpoint_guid);
  ASSERT_EQ(16u, publisher_endpoint_guid->value.size());
  EXPECT_TRUE(std::equal(
    context.impl->rtps_participant->guid_prefix().begin(),
    context.impl->rtps_participant->guid_prefix().end(),
    publisher_endpoint_guid->value.begin()));
  EXPECT_EQ(0x03u, publisher_endpoint_guid->value[15]);
  const auto * publisher_topic =
    FindParameter(publisher_parameters, rmw_mdds_cpp::rtps::kPidTopicName);
  ASSERT_NE(nullptr, publisher_topic);
  EXPECT_EQ("rt/mdds_sedp_chatter", DecodeCdrStringValue(publisher_topic->value));
  const auto * publisher_type =
    FindParameter(publisher_parameters, rmw_mdds_cpp::rtps::kPidTypeName);
  ASSERT_NE(nullptr, publisher_type);
  EXPECT_EQ("std_msgs::msg::dds_::String_", DecodeCdrStringValue(publisher_type->value));

  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_sedp_chatter", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_mdds_cpp::rtps::ReceivedSedpEndpointAnnouncement subscription_sedp;
  ASSERT_TRUE(
    remote_participant->ReceiveSedpEndpointAnnouncement(
      &subscription_sedp, &remote, 1000, &error))
    << error;
  EXPECT_EQ(
    rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsWriter,
    subscription_sedp.data.writer_id);

  const auto subscription_parameters = DecodeSedpParameters(subscription_sedp.data);
  const auto * subscription_endpoint_guid =
    FindParameter(subscription_parameters, rmw_mdds_cpp::rtps::kPidEndpointGuid);
  ASSERT_NE(nullptr, subscription_endpoint_guid);
  ASSERT_EQ(16u, subscription_endpoint_guid->value.size());
  EXPECT_TRUE(std::equal(
    context.impl->rtps_participant->guid_prefix().begin(),
    context.impl->rtps_participant->guid_prefix().end(),
    subscription_endpoint_guid->value.begin()));
  EXPECT_EQ(0x04u, subscription_endpoint_guid->value[15]);
  const auto * subscription_topic =
    FindParameter(subscription_parameters, rmw_mdds_cpp::rtps::kPidTopicName);
  ASSERT_NE(nullptr, subscription_topic);
  EXPECT_EQ("rt/mdds_sedp_chatter", DecodeCdrStringValue(subscription_topic->value));
  const auto * subscription_type =
    FindParameter(subscription_parameters, rmw_mdds_cpp::rtps::kPidTypeName);
  ASSERT_NE(nullptr, subscription_type);
  EXPECT_EQ("std_msgs::msg::dds_::String_", DecodeCdrStringValue(subscription_type->value));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsLifecycle, ContextReplaysExistingPubSubSedpToNewlyDiscoveredParticipant)
{
  constexpr uint16_t port_base = 45900u;
  constexpr uint32_t domain_id = 5u;
  const auto remote_prefix = TestGuidPrefix(0xc0u);

  ScopedEnvVar broker_mode("RMW_MDDS_BROKER", "0");
  ScopedEnvVar bind_address("RMW_MDDS_RTPS_BIND_ADDRESS", "127.0.0.1");
  ScopedEnvVar advertised_address("RMW_MDDS_RTPS_ADVERTISED_ADDRESS", "127.0.0.1");
  ScopedEnvVar port_base_env("RMW_MDDS_RTPS_PORT_BASE", "45900");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  options.instance_id = 5;
  options.domain_id = domain_id;
  SetEnclave(&options, "/rmw_mdds_rtps_sedp_replay_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  ASSERT_NE(nullptr, context.impl);
  ASSERT_NE(nullptr, context.impl->rtps_participant);

  rmw_node_t * node = rmw_create_node(&context, "mdds_sedp_replay_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const auto * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_sedp_replay", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_sedp_replay", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std::string error;
  auto remote_participant = rmw_mdds_cpp::rtps::RtpsParticipant::Create(
    MakeRemoteRtpsConfig(domain_id, 6u, port_base, remote_prefix), &error);
  ASSERT_NE(nullptr, remote_participant) << error;
  ASSERT_TRUE(
    remote_participant->SendSpdpAnnouncement(
      rmw_mdds_cpp::rtps::UdpEndpoint{
        "127.0.0.1", context.impl->rtps_participant->local_metatraffic_unicast_port()},
      &error))
    << error;
  ASSERT_EQ(1u, WaitForDiscoveredParticipants(context).size());

  rmw_mdds_cpp::rtps::UdpEndpoint remote;
  rmw_mdds_cpp::rtps::ReceivedSedpEndpointAnnouncement publisher_sedp;
  ASSERT_TRUE(
    remote_participant->ReceiveSedpEndpointAnnouncement(&publisher_sedp, &remote, 1000, &error))
    << error;
  EXPECT_EQ(
    rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinPublicationsWriter,
    publisher_sedp.data.writer_id);
  const auto publisher_parameters = DecodeSedpParameters(publisher_sedp.data);
  const auto * publisher_topic =
    FindParameter(publisher_parameters, rmw_mdds_cpp::rtps::kPidTopicName);
  ASSERT_NE(nullptr, publisher_topic);
  EXPECT_EQ("rt/mdds_sedp_replay", DecodeCdrStringValue(publisher_topic->value));
  const auto * publisher_type =
    FindParameter(publisher_parameters, rmw_mdds_cpp::rtps::kPidTypeName);
  ASSERT_NE(nullptr, publisher_type);
  EXPECT_EQ("std_msgs::msg::dds_::String_", DecodeCdrStringValue(publisher_type->value));

  rmw_mdds_cpp::rtps::ReceivedSedpEndpointAnnouncement subscription_sedp;
  ASSERT_TRUE(
    remote_participant->ReceiveSedpEndpointAnnouncement(
      &subscription_sedp, &remote, 1000, &error))
    << error;
  EXPECT_EQ(
    rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsWriter,
    subscription_sedp.data.writer_id);
  const auto subscription_parameters = DecodeSedpParameters(subscription_sedp.data);
  const auto * subscription_topic =
    FindParameter(subscription_parameters, rmw_mdds_cpp::rtps::kPidTopicName);
  ASSERT_NE(nullptr, subscription_topic);
  EXPECT_EQ("rt/mdds_sedp_replay", DecodeCdrStringValue(subscription_topic->value));
  const auto * subscription_type =
    FindParameter(subscription_parameters, rmw_mdds_cpp::rtps::kPidTypeName);
  ASSERT_NE(nullptr, subscription_type);
  EXPECT_EQ("std_msgs::msg::dds_::String_", DecodeCdrStringValue(subscription_type->value));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsLifecycle, ContextRetainsRemoteSedpEndpointAnnouncements)
{
  constexpr uint16_t port_base = 46000u;
  constexpr uint32_t domain_id = 6u;
  const auto remote_prefix = TestGuidPrefix(0xd0u);
  const rmw_mdds_cpp::rtps::EntityId remote_publication_id{
    0x00u, 0x00u, 0x20u, rmw_mdds_cpp::rtps::kEntityKindUserWriterNoKey};
  const rmw_mdds_cpp::rtps::EntityId remote_subscription_id{
    0x00u, 0x00u, 0x21u, rmw_mdds_cpp::rtps::kEntityKindUserReaderNoKey};

  ScopedEnvVar broker_mode("RMW_MDDS_BROKER", "0");
  ScopedEnvVar bind_address("RMW_MDDS_RTPS_BIND_ADDRESS", "127.0.0.1");
  ScopedEnvVar advertised_address("RMW_MDDS_RTPS_ADVERTISED_ADDRESS", "127.0.0.1");
  ScopedEnvVar port_base_env("RMW_MDDS_RTPS_PORT_BASE", "46000");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  options.instance_id = 5;
  options.domain_id = domain_id;
  SetEnclave(&options, "/rmw_mdds_rtps_remote_sedp_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  ASSERT_NE(nullptr, context.impl);
  ASSERT_NE(nullptr, context.impl->rtps_participant);

  std::string error;
  auto remote_participant = rmw_mdds_cpp::rtps::RtpsParticipant::Create(
    MakeRemoteRtpsConfig(domain_id, 6u, port_base, remote_prefix), &error);
  ASSERT_NE(nullptr, remote_participant) << error;

  const rmw_mdds_cpp::rtps::UdpEndpoint context_metatraffic{
    "127.0.0.1", context.impl->rtps_participant->local_metatraffic_unicast_port()};
  ASSERT_TRUE(remote_participant->SendSpdpAnnouncement(context_metatraffic, &error)) << error;
  ASSERT_EQ(1u, WaitForDiscoveredParticipants(context).size());

  rmw_mdds_cpp::rtps::SedpEndpointAnnouncement remote_publication;
  remote_publication.participant_guid_prefix = remote_prefix;
  remote_publication.endpoint_entity_id = remote_publication_id;
  remote_publication.topic_name = "rt/remote_chatter";
  remote_publication.type_name = "std_msgs::msg::dds_::String_";
  remote_publication.endpoint_kind = rmw_mdds_cpp::rtps::SedpEndpointKind::kPublication;
  ASSERT_TRUE(
    remote_participant->SendSedpEndpointAnnouncement(
      context_metatraffic, remote_publication, &error))
    << error;

  rmw_mdds_cpp::rtps::SedpEndpointAnnouncement remote_subscription;
  remote_subscription.participant_guid_prefix = remote_prefix;
  remote_subscription.endpoint_entity_id = remote_subscription_id;
  remote_subscription.topic_name = "rt/remote_chatter";
  remote_subscription.type_name = "std_msgs::msg::dds_::String_";
  remote_subscription.endpoint_kind = rmw_mdds_cpp::rtps::SedpEndpointKind::kSubscription;
  ASSERT_TRUE(
    remote_participant->SendSedpEndpointAnnouncement(
      context_metatraffic, remote_subscription, &error))
    << error;

  const auto endpoints = WaitForDiscoveredSedpEndpoints(context, 2u);
  ASSERT_EQ(2u, endpoints.size());
  const auto has_publication = std::any_of(
    endpoints.begin(), endpoints.end(),
    [&](const rmw_mdds_cpp::rtps::DiscoveredSedpEndpoint & endpoint) {
      return endpoint.participant_guid_prefix == remote_prefix &&
             endpoint.endpoint_entity_id == remote_publication_id &&
             endpoint.endpoint_kind == rmw_mdds_cpp::rtps::SedpEndpointKind::kPublication &&
             endpoint.topic_name == "rt/remote_chatter" &&
             endpoint.type_name == "std_msgs::msg::dds_::String_" &&
             endpoint.remote_endpoint.address == "127.0.0.1" &&
             endpoint.remote_endpoint.port == remote_participant->local_metatraffic_unicast_port() &&
             endpoint.last_sedp_sequence_number == 1;
    });
  EXPECT_TRUE(has_publication);
  const auto has_subscription = std::any_of(
    endpoints.begin(), endpoints.end(),
    [&](const rmw_mdds_cpp::rtps::DiscoveredSedpEndpoint & endpoint) {
      return endpoint.participant_guid_prefix == remote_prefix &&
             endpoint.endpoint_entity_id == remote_subscription_id &&
             endpoint.endpoint_kind == rmw_mdds_cpp::rtps::SedpEndpointKind::kSubscription &&
             endpoint.topic_name == "rt/remote_chatter" &&
             endpoint.type_name == "std_msgs::msg::dds_::String_" &&
             endpoint.remote_endpoint.address == "127.0.0.1" &&
             endpoint.remote_endpoint.port == remote_participant->local_metatraffic_unicast_port() &&
             endpoint.last_sedp_sequence_number == 1;
    });
  EXPECT_TRUE(has_subscription);

  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsLifecycle, ContextPublishesUserDataToRemoteSedpSubscription)
{
  constexpr uint16_t port_base = 46200u;
  constexpr uint32_t domain_id = 7u;
  const auto remote_prefix = TestGuidPrefix(0xe0u);
  const rmw_mdds_cpp::rtps::EntityId remote_subscription_id{
    0x00u, 0x00u, 0x41u, rmw_mdds_cpp::rtps::kEntityKindUserReaderNoKey};

  ScopedEnvVar broker_mode("RMW_MDDS_BROKER", "0");
  ScopedEnvVar bind_address("RMW_MDDS_RTPS_BIND_ADDRESS", "127.0.0.1");
  ScopedEnvVar advertised_address("RMW_MDDS_RTPS_ADVERTISED_ADDRESS", "127.0.0.1");
  ScopedEnvVar port_base_env("RMW_MDDS_RTPS_PORT_BASE", "46200");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  options.instance_id = 5;
  options.domain_id = domain_id;
  SetEnclave(&options, "/rmw_mdds_rtps_user_data_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  ASSERT_NE(nullptr, context.impl);
  ASSERT_NE(nullptr, context.impl->rtps_participant);

  std::string error;
  auto remote_participant = rmw_mdds_cpp::rtps::RtpsParticipant::Create(
    MakeRemoteRtpsConfig(domain_id, 6u, port_base, remote_prefix), &error);
  ASSERT_NE(nullptr, remote_participant) << error;

  const rmw_mdds_cpp::rtps::UdpEndpoint context_metatraffic{
    "127.0.0.1", context.impl->rtps_participant->local_metatraffic_unicast_port()};
  ASSERT_TRUE(remote_participant->SendSpdpAnnouncement(context_metatraffic, &error)) << error;
  ASSERT_EQ(1u, WaitForDiscoveredParticipants(context).size());

  rmw_node_t * node = rmw_create_node(&context, "mdds_rtps_user_data_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const auto * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_rtps_direct_chatter", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);

  rmw_mdds_cpp::rtps::ReceivedSedpEndpointAnnouncement publisher_sedp;
  rmw_mdds_cpp::rtps::UdpEndpoint sedp_remote;
  ASSERT_TRUE(
    remote_participant->ReceiveSedpEndpointAnnouncement(
      &publisher_sedp, &sedp_remote, 1000, &error))
    << error;
  const auto publisher_parameters = DecodeSedpParameters(publisher_sedp.data);
  const auto * publisher_endpoint_guid =
    FindParameter(publisher_parameters, rmw_mdds_cpp::rtps::kPidEndpointGuid);
  ASSERT_NE(nullptr, publisher_endpoint_guid);
  ASSERT_EQ(16u, publisher_endpoint_guid->value.size());
  rmw_mdds_cpp::rtps::EntityId local_publisher_entity_id{
    publisher_endpoint_guid->value[12], publisher_endpoint_guid->value[13],
    publisher_endpoint_guid->value[14], publisher_endpoint_guid->value[15]};

  rmw_mdds_cpp::rtps::SedpEndpointAnnouncement remote_subscription;
  remote_subscription.participant_guid_prefix = remote_prefix;
  remote_subscription.endpoint_entity_id = remote_subscription_id;
  remote_subscription.topic_name = "rt/mdds_rtps_direct_chatter";
  remote_subscription.type_name = "std_msgs::msg::dds_::String_";
  remote_subscription.endpoint_kind = rmw_mdds_cpp::rtps::SedpEndpointKind::kSubscription;
  ASSERT_TRUE(
    remote_participant->SendSedpEndpointAnnouncement(
      context_metatraffic, remote_subscription, &error))
    << error;

  const auto endpoints = WaitForDiscoveredSedpEndpoints(context, 1u);
  ASSERT_EQ(1u, endpoints.size());
  EXPECT_EQ(remote_prefix, endpoints[0].participant_guid_prefix);
  EXPECT_EQ(remote_subscription_id, endpoints[0].endpoint_entity_id);
  EXPECT_EQ(rmw_mdds_cpp::rtps::SedpEndpointKind::kSubscription, endpoints[0].endpoint_kind);
  EXPECT_EQ("rt/mdds_rtps_direct_chatter", endpoints[0].topic_name);
  EXPECT_EQ("std_msgs::msg::dds_::String_", endpoints[0].type_name);

  std_msgs::msg::String message;
  message.data = "direct rtps user payload";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &message, nullptr));

  rmw_mdds_cpp::rtps::ReceivedUserDataMessage received;
  rmw_mdds_cpp::rtps::UdpEndpoint remote;
  ASSERT_TRUE(remote_participant->ReceiveUserDataMessage(&received, &remote, 1000, &error))
    << error;
  EXPECT_EQ("127.0.0.1", remote.address);
  EXPECT_EQ(context.impl->rtps_participant->local_user_unicast_port(), remote.port);
  EXPECT_EQ(context.impl->rtps_participant->guid_prefix(), received.header.guid_prefix);
  EXPECT_EQ(remote_subscription_id, received.data.reader_id);
  EXPECT_EQ(local_publisher_entity_id, received.data.writer_id);
  EXPECT_GT(received.data.writer_sequence_number, 0);

  std::string decoded_payload;
  ASSERT_TRUE(DecodeCdrStringPayload(received.data.serialized_payload, &decoded_payload));
  EXPECT_EQ(message.data, decoded_payload);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsLifecycle, ContextTakesUserDataFromRemoteSedpPublication)
{
  constexpr uint16_t port_base = 46300u;
  constexpr uint32_t domain_id = 8u;
  const auto remote_prefix = TestGuidPrefix(0xf0u);
  const rmw_mdds_cpp::rtps::EntityId remote_publication_id{
    0x00u, 0x00u, 0x51u, rmw_mdds_cpp::rtps::kEntityKindUserWriterNoKey};

  ScopedEnvVar broker_mode("RMW_MDDS_BROKER", "0");
  ScopedEnvVar bind_address("RMW_MDDS_RTPS_BIND_ADDRESS", "127.0.0.1");
  ScopedEnvVar advertised_address("RMW_MDDS_RTPS_ADVERTISED_ADDRESS", "127.0.0.1");
  ScopedEnvVar port_base_env("RMW_MDDS_RTPS_PORT_BASE", "46300");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  options.instance_id = 5;
  options.domain_id = domain_id;
  SetEnclave(&options, "/rmw_mdds_rtps_take_user_data_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  ASSERT_NE(nullptr, context.impl);
  ASSERT_NE(nullptr, context.impl->rtps_participant);

  std::string error;
  auto remote_participant = rmw_mdds_cpp::rtps::RtpsParticipant::Create(
    MakeRemoteRtpsConfig(domain_id, 6u, port_base, remote_prefix), &error);
  ASSERT_NE(nullptr, remote_participant) << error;

  const rmw_mdds_cpp::rtps::UdpEndpoint context_metatraffic{
    "127.0.0.1", context.impl->rtps_participant->local_metatraffic_unicast_port()};
  ASSERT_TRUE(remote_participant->SendSpdpAnnouncement(context_metatraffic, &error)) << error;
  ASSERT_EQ(1u, WaitForDiscoveredParticipants(context).size());

  rmw_node_t * node = rmw_create_node(&context, "mdds_rtps_take_user_data_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const auto * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_rtps_remote_chatter", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_mdds_cpp::rtps::ReceivedSedpEndpointAnnouncement subscription_sedp;
  rmw_mdds_cpp::rtps::UdpEndpoint sedp_remote;
  ASSERT_TRUE(
    remote_participant->ReceiveSedpEndpointAnnouncement(
      &subscription_sedp, &sedp_remote, 1000, &error))
    << error;
  EXPECT_EQ(
    rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsWriter,
    subscription_sedp.data.writer_id);
  const rmw_mdds_cpp::rtps::EntityId local_subscription_id =
    ExtractEndpointEntityId(subscription_sedp);
  ASSERT_NE(rmw_mdds_cpp::rtps::kEntityIdUnknown, local_subscription_id);

  rmw_mdds_cpp::rtps::SedpEndpointAnnouncement remote_publication;
  remote_publication.participant_guid_prefix = remote_prefix;
  remote_publication.endpoint_entity_id = remote_publication_id;
  remote_publication.topic_name = "rt/mdds_rtps_remote_chatter";
  remote_publication.type_name = "std_msgs::msg::dds_::String_";
  remote_publication.endpoint_kind = rmw_mdds_cpp::rtps::SedpEndpointKind::kPublication;
  ASSERT_TRUE(
    remote_participant->SendSedpEndpointAnnouncement(
      context_metatraffic, remote_publication, &error))
    << error;

  const auto endpoints = WaitForDiscoveredSedpEndpoints(context, 1u);
  ASSERT_EQ(1u, endpoints.size());
  EXPECT_EQ(remote_prefix, endpoints[0].participant_guid_prefix);
  EXPECT_EQ(remote_publication_id, endpoints[0].endpoint_entity_id);
  EXPECT_EQ(rmw_mdds_cpp::rtps::SedpEndpointKind::kPublication, endpoints[0].endpoint_kind);
  EXPECT_EQ("rt/mdds_rtps_remote_chatter", endpoints[0].topic_name);
  EXPECT_EQ("std_msgs::msg::dds_::String_", endpoints[0].type_name);

  const std::string sent_text = "remote rtps user payload";
  const auto payload = EncodeCdrStringPayload(sent_text);
  ASSERT_TRUE(
    remote_participant->SendUserDataMessage(
      rmw_mdds_cpp::rtps::UdpEndpoint{
        "127.0.0.1", context.impl->rtps_participant->local_user_unicast_port()},
      local_subscription_id, remote_publication_id, payload, 7, &error))
    << error;

  std_msgs::msg::String received;
  rmw_message_info_t info = rmw_get_zero_initialized_message_info();
  bool taken = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    ASSERT_EQ(RMW_RET_OK, rmw_take_with_info(subscription, &received, &taken, &info, nullptr));
    if (taken) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(taken);
  EXPECT_EQ(sent_text, received.data);
  EXPECT_EQ(7, info.publication_sequence_number);
  EXPECT_TRUE(std::equal(remote_prefix.begin(), remote_prefix.end(), info.publisher_gid.data));
  EXPECT_TRUE(std::equal(
    remote_publication_id.begin(), remote_publication_id.end(),
    info.publisher_gid.data + remote_prefix.size()));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsLifecycle, ContextSpdpAnnouncerSendsConfiguredPeerAnnouncements)
{
  std::string error;
  rmw_mdds_cpp::rtps::UdpSocket receiver =
    rmw_mdds_cpp::rtps::UdpSocket::Bind("127.0.0.1", 0, &error);
  ASSERT_TRUE(receiver) << error;

  ScopedEnvVar broker_mode("RMW_MDDS_BROKER", "0");
  ScopedEnvVar bind_address("RMW_MDDS_RTPS_BIND_ADDRESS", "127.0.0.1");
  ScopedEnvVar advertised_address("RMW_MDDS_RTPS_ADVERTISED_ADDRESS", "127.0.0.1");
  ScopedEnvVar port_base("RMW_MDDS_RTPS_PORT_BASE", "45600");
  const std::string peer = "127.0.0.1:" + std::to_string(receiver.local_port());
  ScopedEnvVar peers("RMW_MDDS_RTPS_SPDP_PEERS", peer.c_str());
  ScopedEnvVar period("RMW_MDDS_RTPS_SPDP_PERIOD_MS", "20");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  options.instance_id = 4;
  options.domain_id = 2;
  SetEnclave(&options, "/rmw_mdds_rtps_announcer_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  ASSERT_NE(nullptr, context.impl);
  ASSERT_EQ(1u, context.impl->rtps_participant_config.spdp_peer_endpoints.size());
  EXPECT_EQ("127.0.0.1", context.impl->rtps_participant_config.spdp_peer_endpoints[0].address);
  EXPECT_EQ(
    receiver.local_port(), context.impl->rtps_participant_config.spdp_peer_endpoints[0].port);
  EXPECT_EQ(20u, context.impl->rtps_participant_config.spdp_announcement_period_ms);

  rmw_mdds_cpp::rtps::UdpEndpoint first_remote;
  const auto first = ReceiveSpdpPacket(receiver, &first_remote);
  rmw_mdds_cpp::rtps::UdpEndpoint second_remote;
  const auto second = ReceiveSpdpPacket(receiver, &second_remote);

  ASSERT_NE(nullptr, context.impl->rtps_participant);
  EXPECT_EQ(context.impl->rtps_participant->local_metatraffic_unicast_port(), first_remote.port);
  EXPECT_EQ(first_remote.port, second_remote.port);
  EXPECT_EQ("127.0.0.1", first_remote.address);
  EXPECT_EQ(first.writer_sequence_number + 1, second.writer_sequence_number);

  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsLifecycle, ContextOwnsRtpsParticipantFromInit)
{
  ScopedEnvVar broker_mode("RMW_MDDS_BROKER", "0");
  ScopedEnvVar bind_address("RMW_MDDS_RTPS_BIND_ADDRESS", "127.0.0.1");
  ScopedEnvVar advertised_address("RMW_MDDS_RTPS_ADVERTISED_ADDRESS", "127.0.0.1");
  ScopedEnvVar port_base("RMW_MDDS_RTPS_PORT_BASE", "45400");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  options.instance_id = 3;
  options.domain_id = 1;
  SetEnclave(&options, "/rmw_mdds_rtps_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  ASSERT_NE(nullptr, context.impl);
  ASSERT_NE(nullptr, context.impl->rtps_participant);

  const auto & config = context.impl->rtps_participant_config;
  EXPECT_EQ(1u, config.domain_id);
  EXPECT_EQ(3u, config.participant_id);
  EXPECT_EQ(45400u, config.port_mapping.port_base);
  EXPECT_EQ("127.0.0.1", config.bind_address);
  EXPECT_EQ("127.0.0.1", config.advertised_address);

  rmw_mdds_cpp::rtps::RtpsPorts expected_ports;
  std::string error;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::CalculateRtpsPorts(
      config.port_mapping, config.domain_id, config.participant_id,
      &expected_ports, &error))
    << error;
  EXPECT_EQ(
    expected_ports.metatraffic_unicast,
    context.impl->rtps_participant->local_metatraffic_unicast_port());
  EXPECT_EQ(expected_ports.user_unicast, context.impl->rtps_participant->ports().user_unicast);

  const auto & prefix = context.impl->rtps_participant->guid_prefix();
  EXPECT_EQ('M', prefix[0]);
  EXPECT_EQ('D', prefix[1]);
  EXPECT_EQ('D', prefix[2]);
  EXPECT_EQ('S', prefix[3]);
  EXPECT_EQ(static_cast<uint8_t>(getpid() >> 24u), prefix[6]);
  EXPECT_EQ(static_cast<uint8_t>(getpid() >> 16u), prefix[7]);
  EXPECT_EQ(static_cast<uint8_t>(getpid() >> 8u), prefix[8]);
  EXPECT_EQ(static_cast<uint8_t>(getpid()), prefix[9]);
  EXPECT_EQ(0u, prefix[10]);
  EXPECT_EQ(3u, prefix[11]);

  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsLifecycle, InitOptionsCopyAndFini)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();

  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  EXPECT_STREQ("rmw_mdds_cpp", options.implementation_identifier);
  EXPECT_EQ(RMW_DEFAULT_DOMAIN_ID, options.domain_id);

  SetEnclave(&options, "/rmw_mdds_test");

  rmw_init_options_t copy = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_copy(&options, &copy));
  EXPECT_STREQ("rmw_mdds_cpp", copy.implementation_identifier);
  ASSERT_NE(nullptr, copy.enclave);
  EXPECT_STREQ("/rmw_mdds_test", copy.enclave);

  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&copy));
  EXPECT_EQ(nullptr, copy.implementation_identifier);
  EXPECT_EQ(nullptr, copy.enclave);

  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  EXPECT_EQ(nullptr, options.implementation_identifier);
}

TEST(RmwMddsLifecycle, ZeroInitializedContextShutdownAndFiniReturnInvalidArgument)
{
  rmw_context_t context = rmw_get_zero_initialized_context();

  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_shutdown(&context)) << rmw_get_error_string().str;
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_context_fini(&context))
    << rmw_get_error_string().str;
  rmw_reset_error();

  context.implementation_identifier = "not_rmw_mdds_cpp";
  EXPECT_EQ(RMW_RET_INCORRECT_RMW_IMPLEMENTATION, rmw_shutdown(&context))
    << rmw_get_error_string().str;
  rmw_reset_error();
}

TEST(RmwMddsLifecycle, CreateNodeRejectsInvalidNameAndNamespace)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_node_validation_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));

  EXPECT_EQ(nullptr, rmw_create_node(&context, "foo bar", "/mdds"))
    << rmw_get_error_string().str;
  rmw_reset_error();

  EXPECT_EQ(nullptr, rmw_create_node(&context, "mdds_valid_node", "foo bar"))
    << rmw_get_error_string().str;
  rmw_reset_error();

  rmw_node_t * node = rmw_create_node(&context, "mdds_valid_node", "/mdds");
  ASSERT_NE(nullptr, node) << rmw_get_error_string().str;
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));

  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsLifecycle, ContextNodeGuardAndWaitSetLifecycle)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  options.instance_id = 7;
  options.domain_id = TEST_DOMAIN_ID;
  SetEnclave(&options, "/rmw_mdds_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  EXPECT_STREQ("rmw_mdds_cpp", context.implementation_identifier);
  EXPECT_EQ(options.instance_id, context.instance_id);
  EXPECT_EQ(TEST_DOMAIN_ID, context.actual_domain_id);
  ASSERT_NE(nullptr, context.impl);

  rmw_node_t * node = rmw_create_node(&context, "mdds_lifecycle_node", "/mdds");
  ASSERT_NE(nullptr, node);
  EXPECT_STREQ("rmw_mdds_cpp", node->implementation_identifier);
  EXPECT_STREQ("mdds_lifecycle_node", node->name);
  EXPECT_STREQ("/mdds", node->namespace_);
  EXPECT_EQ(&context, node->context);
  EXPECT_NE(nullptr, rmw_node_get_graph_guard_condition(node));
  EXPECT_EQ(RMW_RET_OK, AssertNodeLiveliness(node));

  rmw_guard_condition_t * guard = rmw_create_guard_condition(&context);
  ASSERT_NE(nullptr, guard);
  EXPECT_STREQ("rmw_mdds_cpp", guard->implementation_identifier);
  EXPECT_EQ(&context, guard->context);
  EXPECT_EQ(RMW_RET_OK, rmw_trigger_guard_condition(guard));

  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 4);
  ASSERT_NE(nullptr, wait_set);
  EXPECT_STREQ("rmw_mdds_cpp", wait_set->implementation_identifier);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_guard_condition(guard));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(nullptr, context.implementation_identifier);

  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsLifecycle, DestroyWaitSetRejectsNullLikeUpstreamRmw)
{
  EXPECT_EQ(RMW_RET_ERROR, rmw_destroy_wait_set(nullptr)) << rmw_get_error_string().str;
  rmw_reset_error();
}

TEST(RmwMddsLifecycle, GuardConditionTriggerIsConsumedByWait)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_guard_wait_consume_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));

  rmw_guard_condition_t * guard = rmw_create_guard_condition(&context);
  ASSERT_NE(nullptr, guard);
  ASSERT_EQ(RMW_RET_OK, rmw_trigger_guard_condition(guard));

  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);

  void * guard_handle = guard->data;
  rmw_guard_conditions_t guard_conditions;
  guard_conditions.guard_condition_count = 1;
  guard_conditions.guard_conditions = &guard_handle;
  rmw_time_t zero_timeout;
  zero_timeout.sec = 0;
  zero_timeout.nsec = 0;

  ASSERT_EQ(
    RMW_RET_OK,
    rmw_wait(nullptr, &guard_conditions, nullptr, nullptr, nullptr, wait_set, &zero_timeout));
  EXPECT_NE(nullptr, guard_conditions.guard_conditions[0]);

  guard_handle = guard->data;
  guard_conditions.guard_conditions = &guard_handle;
  EXPECT_EQ(
    RMW_RET_TIMEOUT,
    rmw_wait(nullptr, &guard_conditions, nullptr, nullptr, nullptr, wait_set, &zero_timeout));
  EXPECT_EQ(nullptr, guard_conditions.guard_conditions[0]);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_guard_condition(guard));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}
