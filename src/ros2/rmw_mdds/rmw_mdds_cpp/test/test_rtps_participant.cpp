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
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "rtps_participant.hpp"
#include "rtps_protocol.hpp"

namespace
{
rmw_mdds_cpp::rtps::GuidPrefix Prefix(uint8_t base)
{
  return rmw_mdds_cpp::rtps::GuidPrefix{
    base, static_cast<uint8_t>(base + 1u), static_cast<uint8_t>(base + 2u),
    static_cast<uint8_t>(base + 3u), static_cast<uint8_t>(base + 4u),
    static_cast<uint8_t>(base + 5u), static_cast<uint8_t>(base + 6u),
    static_cast<uint8_t>(base + 7u), static_cast<uint8_t>(base + 8u),
    static_cast<uint8_t>(base + 9u), static_cast<uint8_t>(base + 10u),
    static_cast<uint8_t>(base + 11u)};
}

rmw_mdds_cpp::rtps::ParticipantConfig MakeConfig(
  uint32_t participant_id, uint16_t port_base, const rmw_mdds_cpp::rtps::GuidPrefix & prefix)
{
  rmw_mdds_cpp::rtps::ParticipantConfig config;
  config.domain_id = 0;
  config.participant_id = participant_id;
  config.guid_prefix = prefix;
  config.vendor_id = {0x13u, 0x37u};
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

void AppendU32Little(std::vector<uint8_t> * bytes, uint32_t value)
{
  bytes->push_back(static_cast<uint8_t>(value & 0xffu));
  bytes->push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  bytes->push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  bytes->push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

std::vector<uint8_t> EncodeCdrStringValue(const std::string & value)
{
  std::vector<uint8_t> encoded;
  AppendU32Little(&encoded, static_cast<uint32_t>(value.size() + 1u));
  encoded.insert(encoded.end(), value.begin(), value.end());
  encoded.push_back('\0');
  return encoded;
}

std::vector<uint8_t> EncodePlCdrParameterPayload(
  const rmw_mdds_cpp::rtps::ParameterList & parameters)
{
  std::vector<uint8_t> payload{0x00u, 0x03u, 0x00u, 0x00u};
  const auto encoded_parameters = rmw_mdds_cpp::rtps::EncodeParameterList(parameters);
  payload.insert(payload.end(), encoded_parameters.begin(), encoded_parameters.end());
  return payload;
}

std::vector<uint8_t> ParticipantGuidValue(
  const rmw_mdds_cpp::rtps::GuidPrefix & prefix)
{
  std::vector<uint8_t> value(prefix.begin(), prefix.end());
  value.insert(
    value.end(), rmw_mdds_cpp::rtps::kEntityIdParticipant.begin(),
    rmw_mdds_cpp::rtps::kEntityIdParticipant.end());
  return value;
}

void ExpectIpv4Locator(
  const rmw_mdds_cpp::rtps::Parameter & parameter, uint16_t expected_port)
{
  ASSERT_EQ(24u, parameter.value.size());
  EXPECT_EQ(
    static_cast<uint32_t>(rmw_mdds_cpp::rtps::kLocatorKindUdpV4),
    ReadU32Little(parameter.value, 0u));
  EXPECT_EQ(expected_port, ReadU32Little(parameter.value, 4u));
  EXPECT_EQ(127u, parameter.value[20]);
  EXPECT_EQ(0u, parameter.value[21]);
  EXPECT_EQ(0u, parameter.value[22]);
  EXPECT_EQ(1u, parameter.value[23]);
}

rmw_mdds_cpp::rtps::ParameterList DecodeSpdpParameters(
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

std::vector<uint8_t> EndpointGuidValue(
  const rmw_mdds_cpp::rtps::GuidPrefix & prefix,
  const rmw_mdds_cpp::rtps::EntityId & endpoint_id)
{
  std::vector<uint8_t> value(prefix.begin(), prefix.end());
  value.insert(value.end(), endpoint_id.begin(), endpoint_id.end());
  return value;
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

template<typename Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return predicate();
}
}  // namespace

TEST(RmwMddsRtpsParticipant, BindsMetatrafficSocketAndBuildsSpdpAnnouncement)
{
  std::string error;
  const auto config = MakeConfig(2, 45200u, Prefix(0x20u));
  auto participant = rmw_mdds_cpp::rtps::RtpsParticipant::Create(config, &error);
  ASSERT_NE(nullptr, participant) << error;

  rmw_mdds_cpp::rtps::RtpsPorts expected_ports;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::CalculateRtpsPorts(
      config.port_mapping, config.domain_id, config.participant_id,
      &expected_ports, &error))
    << error;
  EXPECT_EQ(expected_ports.metatraffic_unicast, participant->local_metatraffic_unicast_port());
  EXPECT_EQ(expected_ports.user_unicast, participant->local_user_unicast_port());
  EXPECT_EQ(expected_ports.metatraffic_unicast, participant->ports().metatraffic_unicast);
  EXPECT_EQ(expected_ports.user_unicast, participant->ports().user_unicast);

  const std::vector<uint8_t> packet = participant->BuildSpdpAnnouncement(11);
  rmw_mdds_cpp::rtps::RtpsHeader header;
  rmw_mdds_cpp::rtps::DataSubmessage data;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeDataMessage(
      packet.data(), packet.size(), &header, &data, &error))
    << error;
  EXPECT_EQ(config.guid_prefix, header.guid_prefix);
  EXPECT_EQ(config.vendor_id, header.vendor_id);
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdUnknown, data.reader_id);
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdSpdpBuiltinParticipantWriter, data.writer_id);
  EXPECT_EQ(11, data.writer_sequence_number);

  const auto parameters = DecodeSpdpParameters(data);
  const auto * participant_guid =
    FindParameter(parameters, rmw_mdds_cpp::rtps::kPidParticipantGuid);
  ASSERT_NE(nullptr, participant_guid);
  EXPECT_EQ(ParticipantGuidValue(config.guid_prefix), participant_guid->value);

  const auto * metatraffic_locator =
    FindParameter(parameters, rmw_mdds_cpp::rtps::kPidMetatrafficUnicastLocator);
  ASSERT_NE(nullptr, metatraffic_locator);
  ExpectIpv4Locator(*metatraffic_locator, expected_ports.metatraffic_unicast);

  const auto * default_locator =
    FindParameter(parameters, rmw_mdds_cpp::rtps::kPidDefaultUnicastLocator);
  ASSERT_NE(nullptr, default_locator);
  ExpectIpv4Locator(*default_locator, expected_ports.user_unicast);
}

TEST(RmwMddsRtpsParticipant, SendsAndReceivesSpdpAnnouncementsOverUdp)
{
  std::string error;
  auto participant_a =
    rmw_mdds_cpp::rtps::RtpsParticipant::Create(MakeConfig(0, 45300u, Prefix(0x40u)), &error);
  ASSERT_NE(nullptr, participant_a) << error;
  auto participant_b =
    rmw_mdds_cpp::rtps::RtpsParticipant::Create(MakeConfig(1, 45300u, Prefix(0x60u)), &error);
  ASSERT_NE(nullptr, participant_b) << error;

  ASSERT_TRUE(
    participant_a->SendSpdpAnnouncement(
      rmw_mdds_cpp::rtps::UdpEndpoint{
        "127.0.0.1", participant_b->local_metatraffic_unicast_port()},
      &error))
    << error;

  rmw_mdds_cpp::rtps::ReceivedSpdpAnnouncement received;
  rmw_mdds_cpp::rtps::UdpEndpoint remote;
  ASSERT_TRUE(
    participant_b->ReceiveSpdpAnnouncement(&received, &remote, 1000, &error))
    << error;

  EXPECT_EQ("127.0.0.1", remote.address);
  EXPECT_EQ(participant_a->local_metatraffic_unicast_port(), remote.port);
  EXPECT_EQ(participant_a->guid_prefix(), received.header.guid_prefix);
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdSpdpBuiltinParticipantWriter, received.data.writer_id);
  EXPECT_EQ(1, received.data.writer_sequence_number);

  const auto * participant_guid =
    FindParameter(received.parameters, rmw_mdds_cpp::rtps::kPidParticipantGuid);
  ASSERT_NE(nullptr, participant_guid);
  EXPECT_EQ(ParticipantGuidValue(participant_a->guid_prefix()), participant_guid->value);
}

TEST(RmwMddsRtpsParticipant, SendsAndReceivesSedpEndpointAnnouncementsOverUdp)
{
  std::string error;
  auto participant_a =
    rmw_mdds_cpp::rtps::RtpsParticipant::Create(MakeConfig(0, 45900u, Prefix(0x90u)), &error);
  ASSERT_NE(nullptr, participant_a) << error;
  auto participant_b =
    rmw_mdds_cpp::rtps::RtpsParticipant::Create(MakeConfig(1, 45900u, Prefix(0xa0u)), &error);
  ASSERT_NE(nullptr, participant_b) << error;

  rmw_mdds_cpp::rtps::SedpEndpointAnnouncement publication;
  publication.participant_guid_prefix = participant_a->guid_prefix();
  publication.endpoint_entity_id = {0x00u, 0x00u, 0x10u, 0x03u};
  publication.topic_name = "rt/chatter";
  publication.type_name = "std_msgs::msg::dds_::String_";
  publication.endpoint_kind = rmw_mdds_cpp::rtps::SedpEndpointKind::kPublication;

  ASSERT_TRUE(
    participant_a->SendSedpEndpointAnnouncement(
      rmw_mdds_cpp::rtps::UdpEndpoint{
        "127.0.0.1", participant_b->local_metatraffic_unicast_port()},
      publication, &error))
    << error;

  rmw_mdds_cpp::rtps::ReceivedSedpEndpointAnnouncement received;
  rmw_mdds_cpp::rtps::UdpEndpoint remote;
  ASSERT_TRUE(
    participant_b->ReceiveSedpEndpointAnnouncement(&received, &remote, 1000, &error))
    << error;

  EXPECT_EQ("127.0.0.1", remote.address);
  EXPECT_EQ(participant_a->local_metatraffic_unicast_port(), remote.port);
  EXPECT_EQ(participant_a->guid_prefix(), received.header.guid_prefix);
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinPublicationsWriter, received.data.writer_id);
  EXPECT_EQ(1, received.data.writer_sequence_number);

  const auto parameters = DecodeSedpParameters(received.data);
  const auto * endpoint_guid =
    FindParameter(parameters, rmw_mdds_cpp::rtps::kPidEndpointGuid);
  ASSERT_NE(nullptr, endpoint_guid);
  EXPECT_EQ(
    EndpointGuidValue(participant_a->guid_prefix(), publication.endpoint_entity_id),
    endpoint_guid->value);

  const auto * topic_name = FindParameter(parameters, rmw_mdds_cpp::rtps::kPidTopicName);
  ASSERT_NE(nullptr, topic_name);
  EXPECT_EQ("rt/chatter", DecodeCdrStringValue(topic_name->value));

  const auto * type_name = FindParameter(parameters, rmw_mdds_cpp::rtps::kPidTypeName);
  ASSERT_NE(nullptr, type_name);
  EXPECT_EQ("std_msgs::msg::dds_::String_", DecodeCdrStringValue(type_name->value));

  const auto * participant_guid =
    FindParameter(parameters, rmw_mdds_cpp::rtps::kPidParticipantGuid);
  ASSERT_NE(nullptr, participant_guid);
  EXPECT_EQ(ParticipantGuidValue(participant_a->guid_prefix()), participant_guid->value);

  const auto * key_hash = FindParameter(parameters, rmw_mdds_cpp::rtps::kPidKeyHash);
  ASSERT_NE(nullptr, key_hash);
  EXPECT_EQ(
    EndpointGuidValue(participant_a->guid_prefix(), publication.endpoint_entity_id),
    key_hash->value);

  const auto * unicast_locator =
    FindParameter(parameters, rmw_mdds_cpp::rtps::kPidUnicastLocator);
  ASSERT_NE(nullptr, unicast_locator);
  ExpectIpv4Locator(*unicast_locator, participant_a->local_user_unicast_port());

  const auto * protocol_version =
    FindParameter(parameters, rmw_mdds_cpp::rtps::kPidProtocolVersion);
  ASSERT_NE(nullptr, protocol_version);
  EXPECT_EQ(std::vector<uint8_t>({2u, 3u, 0u, 0u}), protocol_version->value);

  const auto * vendor_id = FindParameter(parameters, rmw_mdds_cpp::rtps::kPidVendorId);
  ASSERT_NE(nullptr, vendor_id);
  EXPECT_EQ(std::vector<uint8_t>({0x13u, 0x37u, 0u, 0u}), vendor_id->value);

  const auto * type_max_size =
    FindParameter(parameters, rmw_mdds_cpp::rtps::kPidTypeMaxSizeSerialized);
  ASSERT_NE(nullptr, type_max_size);
  ASSERT_EQ(4u, type_max_size->value.size());
  EXPECT_EQ(0u, ReadU32Little(type_max_size->value, 0u));

  rmw_mdds_cpp::rtps::SedpEndpointAnnouncement subscription = publication;
  subscription.endpoint_entity_id = {0x00u, 0x00u, 0x11u, 0x04u};
  subscription.endpoint_kind = rmw_mdds_cpp::rtps::SedpEndpointKind::kSubscription;
  ASSERT_TRUE(
    participant_a->SendSedpEndpointAnnouncement(
      rmw_mdds_cpp::rtps::UdpEndpoint{
        "127.0.0.1", participant_b->local_metatraffic_unicast_port()},
      subscription, &error))
    << error;

  rmw_mdds_cpp::rtps::ReceivedSedpEndpointAnnouncement received_subscription;
  ASSERT_TRUE(
    participant_b->ReceiveSedpEndpointAnnouncement(
      &received_subscription, &remote, 1000, &error))
    << error;
  EXPECT_EQ(
    rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsWriter,
    received_subscription.data.writer_id);
  EXPECT_EQ(1, received_subscription.data.writer_sequence_number);
}

TEST(RmwMddsRtpsParticipant, SendSedpEndpointAnnouncementFollowsDataWithHeartbeatOverUdp)
{
  std::string error;
  auto participant =
    rmw_mdds_cpp::rtps::RtpsParticipant::Create(MakeConfig(0, 46200u, Prefix(0xb0u)), &error);
  ASSERT_NE(nullptr, participant) << error;

  auto receiver = rmw_mdds_cpp::rtps::UdpSocket::Bind("127.0.0.1", 0u, &error);
  ASSERT_TRUE(receiver) << error;

  rmw_mdds_cpp::rtps::SedpEndpointAnnouncement subscription;
  subscription.participant_guid_prefix = participant->guid_prefix();
  subscription.endpoint_entity_id = {0x00u, 0x00u, 0x11u, 0x04u};
  subscription.topic_name = "rt/chatter";
  subscription.type_name = "std_msgs::msg::dds_::String_";
  subscription.endpoint_kind = rmw_mdds_cpp::rtps::SedpEndpointKind::kSubscription;

  ASSERT_TRUE(
    participant->SendSedpEndpointAnnouncement(
      rmw_mdds_cpp::rtps::UdpEndpoint{"127.0.0.1", receiver.local_port()},
      subscription, &error))
    << error;

  std::vector<uint8_t> data_packet;
  rmw_mdds_cpp::rtps::UdpEndpoint remote;
  ASSERT_TRUE(receiver.Receive(&data_packet, &remote, 1000, &error)) << error;

  rmw_mdds_cpp::rtps::RtpsHeader data_header;
  std::vector<rmw_mdds_cpp::rtps::DataSubmessage> data_messages;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeDataMessages(
      data_packet.data(), data_packet.size(), &data_header, &data_messages, &error))
    << error;
  ASSERT_EQ(1u, data_messages.size());
  EXPECT_EQ(participant->guid_prefix(), data_header.guid_prefix);
  EXPECT_EQ(
    rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsWriter,
    data_messages[0].writer_id);
  EXPECT_EQ(1, data_messages[0].writer_sequence_number);

  std::vector<uint8_t> heartbeat_packet;
  ASSERT_TRUE(receiver.Receive(&heartbeat_packet, &remote, 1000, &error)) << error;

  rmw_mdds_cpp::rtps::RtpsHeader heartbeat_header;
  std::vector<rmw_mdds_cpp::rtps::HeartbeatSubmessage> heartbeats;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeHeartbeatMessages(
      heartbeat_packet.data(), heartbeat_packet.size(), &heartbeat_header, &heartbeats, &error))
    << error;
  ASSERT_EQ(1u, heartbeats.size());
  EXPECT_EQ(participant->guid_prefix(), heartbeat_header.guid_prefix);
  EXPECT_EQ(
    rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsReader,
    heartbeats[0].reader_id);
  EXPECT_EQ(
    rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsWriter,
    heartbeats[0].writer_id);
  EXPECT_EQ(1, heartbeats[0].first_sequence_number);
  EXPECT_EQ(1, heartbeats[0].last_sequence_number);
  EXPECT_EQ(1, heartbeats[0].count);
}

TEST(RmwMddsRtpsParticipant, MatchesSedpEndpointWhenOnlyKeyHashCarriesEndpointGuid)
{
  std::string error;
  auto participant_a =
    rmw_mdds_cpp::rtps::RtpsParticipant::Create(MakeConfig(0, 46000u, Prefix(0x91u)), &error);
  ASSERT_NE(nullptr, participant_a) << error;
  auto participant_b =
    rmw_mdds_cpp::rtps::RtpsParticipant::Create(MakeConfig(1, 46000u, Prefix(0xa1u)), &error);
  ASSERT_NE(nullptr, participant_b) << error;

  ASSERT_TRUE(participant_b->StartSpdpReceiver(20, &error)) << error;
  ASSERT_TRUE(
    participant_a->SendSpdpAnnouncement(
      rmw_mdds_cpp::rtps::UdpEndpoint{
        "127.0.0.1", participant_b->local_metatraffic_unicast_port()},
      &error))
    << error;
  ASSERT_TRUE(
    WaitUntil(
      [&participant_b]() {
        return participant_b->GetDiscoveredParticipants().size() == 1u;
      },
      std::chrono::milliseconds(1000)));

  const rmw_mdds_cpp::rtps::EntityId reader_id{
    0x00u, 0x00u, 0x11u, rmw_mdds_cpp::rtps::kEntityKindUserReaderNoKey};
  const std::string topic_name = "rt/chatter";
  const std::string type_name = "std_msgs::msg::dds_::String_";

  rmw_mdds_cpp::rtps::ParameterList parameters;
  parameters.push_back(rmw_mdds_cpp::rtps::Parameter{
    rmw_mdds_cpp::rtps::kPidKeyHash,
    EndpointGuidValue(participant_a->guid_prefix(), reader_id)});
  parameters.push_back(rmw_mdds_cpp::rtps::Parameter{
    rmw_mdds_cpp::rtps::kPidTopicName,
    EncodeCdrStringValue(topic_name)});
  parameters.push_back(rmw_mdds_cpp::rtps::Parameter{
    rmw_mdds_cpp::rtps::kPidTypeName,
    EncodeCdrStringValue(type_name)});

  rmw_mdds_cpp::rtps::RtpsHeader header;
  header.protocol_version = {2u, 3u};
  header.vendor_id = {0x01u, 0x0fu};
  header.guid_prefix = participant_a->guid_prefix();

  rmw_mdds_cpp::rtps::DataSubmessage data;
  data.reader_id = rmw_mdds_cpp::rtps::kEntityIdUnknown;
  data.writer_id = rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsWriter;
  data.writer_sequence_number = 2;
  data.serialized_payload = EncodePlCdrParameterPayload(parameters);

  auto sender = rmw_mdds_cpp::rtps::UdpSocket::Bind("127.0.0.1", 0u, &error);
  ASSERT_TRUE(sender) << error;
  const std::vector<uint8_t> packet = rmw_mdds_cpp::rtps::EncodeDataMessage(header, data);
  ASSERT_TRUE(
    sender.SendTo(
      packet.data(), packet.size(),
      rmw_mdds_cpp::rtps::UdpEndpoint{
        "127.0.0.1", participant_b->local_metatraffic_unicast_port()},
      &error))
    << error;

  ASSERT_TRUE(
    WaitUntil(
      [&participant_b, &topic_name, &type_name]() {
        return participant_b->GetMatchedRemoteSedpEndpoints(
          topic_name, type_name,
          rmw_mdds_cpp::rtps::SedpEndpointKind::kSubscription).size() == 1u;
      },
      std::chrono::milliseconds(1000)));
  const auto matches = participant_b->GetMatchedRemoteSedpEndpoints(
    topic_name, type_name, rmw_mdds_cpp::rtps::SedpEndpointKind::kSubscription);
  ASSERT_EQ(1u, matches.size());
  EXPECT_EQ(participant_a->guid_prefix(), matches[0].participant_guid_prefix);
  EXPECT_EQ(reader_id, matches[0].endpoint_entity_id);
  EXPECT_EQ(participant_a->local_user_unicast_port(), matches[0].user_data_endpoint.port);

  participant_b->StopSpdpReceiver();
}

TEST(RmwMddsRtpsParticipant, SendsAndReceivesUserDataOverUserUnicastUdp)
{
  std::string error;
  auto participant_a =
    rmw_mdds_cpp::rtps::RtpsParticipant::Create(MakeConfig(0, 46100u, Prefix(0xb0u)), &error);
  ASSERT_NE(nullptr, participant_a) << error;
  auto participant_b =
    rmw_mdds_cpp::rtps::RtpsParticipant::Create(MakeConfig(1, 46100u, Prefix(0xc0u)), &error);
  ASSERT_NE(nullptr, participant_b) << error;

  const rmw_mdds_cpp::rtps::EntityId reader_id{
    0x00u, 0x00u, 0x31u, rmw_mdds_cpp::rtps::kEntityKindUserReaderNoKey};
  const rmw_mdds_cpp::rtps::EntityId writer_id{
    0x00u, 0x00u, 0x30u, rmw_mdds_cpp::rtps::kEntityKindUserWriterNoKey};
  const std::vector<uint8_t> payload{'m', 'd', 'd', 's', 0x00u, 0x01u, 0x02u};

  ASSERT_TRUE(
    participant_a->SendUserDataMessage(
      rmw_mdds_cpp::rtps::UdpEndpoint{
        "127.0.0.1", participant_b->local_user_unicast_port()},
      reader_id, writer_id, payload, 42, &error))
    << error;

  rmw_mdds_cpp::rtps::ReceivedUserDataMessage received;
  rmw_mdds_cpp::rtps::UdpEndpoint remote;
  ASSERT_TRUE(participant_b->ReceiveUserDataMessage(&received, &remote, 1000, &error))
    << error;

  EXPECT_EQ("127.0.0.1", remote.address);
  EXPECT_EQ(participant_a->local_user_unicast_port(), remote.port);
  EXPECT_EQ(participant_a->guid_prefix(), received.header.guid_prefix);
  EXPECT_EQ(reader_id, received.data.reader_id);
  EXPECT_EQ(writer_id, received.data.writer_id);
  EXPECT_EQ(42, received.data.writer_sequence_number);
  EXPECT_EQ(payload, received.data.serialized_payload);
}
