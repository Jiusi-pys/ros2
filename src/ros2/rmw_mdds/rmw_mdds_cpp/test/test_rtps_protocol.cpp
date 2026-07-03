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
#include <cstdint>
#include <string>
#include <vector>

#include "rtps_protocol.hpp"

namespace
{
rmw_mdds_cpp::rtps::RtpsHeader MakeHeader()
{
  rmw_mdds_cpp::rtps::RtpsHeader header;
  header.protocol_version = {2u, 3u};
  header.vendor_id = {0x00u, 0x00u};
  header.guid_prefix = {
    0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u,
    0x07u, 0x08u, 0x09u, 0x0au, 0x0bu, 0x0cu};
  return header;
}

rmw_mdds_cpp::rtps::DataSubmessage MakeDataSubmessage()
{
  rmw_mdds_cpp::rtps::DataSubmessage data;
  data.reader_id = {0x00u, 0x00u, 0x00u, 0x00u};
  data.writer_id = {0x00u, 0x00u, 0x10u, 0xc2u};
  data.writer_sequence_number = 42;
  data.serialized_payload = {0x00u, 0x01u, 0x00u, 0x00u, 'p', 'i', 'n', 'g'};
  return data;
}

rmw_mdds_cpp::rtps::ParticipantAnnouncement MakeParticipantAnnouncement()
{
  rmw_mdds_cpp::rtps::ParticipantAnnouncement announcement;
  announcement.participant_guid_prefix = MakeHeader().guid_prefix;
  announcement.vendor_id = {0x01u, 0x0fu};
  announcement.builtin_endpoint_set =
    rmw_mdds_cpp::rtps::kBuiltinEndpointParticipantAnnouncer |
    rmw_mdds_cpp::rtps::kBuiltinEndpointParticipantDetector |
    rmw_mdds_cpp::rtps::kBuiltinEndpointPublicationAnnouncer |
    rmw_mdds_cpp::rtps::kBuiltinEndpointPublicationDetector |
    rmw_mdds_cpp::rtps::kBuiltinEndpointSubscriptionAnnouncer |
    rmw_mdds_cpp::rtps::kBuiltinEndpointSubscriptionDetector;
  announcement.metatraffic_unicast_locator = rmw_mdds_cpp::rtps::Locator{
    rmw_mdds_cpp::rtps::kLocatorKindUdpV4,
    7412u,
    {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
     0x00u, 0x00u, 0x00u, 0x00u, 192u, 168u, 1u, 20u}};
  announcement.default_unicast_locator = rmw_mdds_cpp::rtps::Locator{
    rmw_mdds_cpp::rtps::kLocatorKindUdpV4,
    7413u,
    {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
     0x00u, 0x00u, 0x00u, 0x00u, 192u, 168u, 1u, 20u}};
  return announcement;
}

uint32_t ReadU32Little(const std::vector<uint8_t> & bytes, size_t offset)
{
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1u]) << 8u) |
         (static_cast<uint32_t>(bytes[offset + 2u]) << 16u) |
         (static_cast<uint32_t>(bytes[offset + 3u]) << 24u);
}

int32_t ReadI32Little(const std::vector<uint8_t> & bytes, size_t offset)
{
  return static_cast<int32_t>(ReadU32Little(bytes, offset));
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

std::string DecodeCdrStringValue(const std::vector<uint8_t> & value)
{
  if (value.size() < 5u) {
    return {};
  }
  const uint32_t length = ReadU32Little(value, 0u);
  if (length == 0u || value.size() < 4u + length || value[4u + length - 1u] != '\0') {
    return {};
  }
  return std::string(
    reinterpret_cast<const char *>(value.data() + 4u),
    reinterpret_cast<const char *>(value.data() + 4u + length - 1u));
}
}  // namespace

TEST(RmwMddsRtpsProtocol, EncodeDataMessageProducesRtpsHeaderAndDataSubmessage)
{
  const auto encoded =
    rmw_mdds_cpp::rtps::EncodeDataMessage(MakeHeader(), MakeDataSubmessage());

  const std::vector<uint8_t> expected = {
    'R', 'T', 'P', 'S',
    0x02u, 0x03u,
    0x00u, 0x00u,
    0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u,
    0x07u, 0x08u, 0x09u, 0x0au, 0x0bu, 0x0cu,
    0x15u, 0x05u,
    0x1cu, 0x00u,
    0x00u, 0x00u,
    0x10u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x10u, 0xc2u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x2au, 0x00u, 0x00u, 0x00u,
    0x00u, 0x01u, 0x00u, 0x00u, 'p', 'i', 'n', 'g'};
  EXPECT_EQ(expected, encoded);
}

TEST(RmwMddsRtpsProtocol, DecodeDataMessagePreservesHeaderIdsSequenceAndPayload)
{
  const auto header = MakeHeader();
  const auto data = MakeDataSubmessage();
  const auto encoded = rmw_mdds_cpp::rtps::EncodeDataMessage(header, data);

  rmw_mdds_cpp::rtps::RtpsHeader decoded_header;
  rmw_mdds_cpp::rtps::DataSubmessage decoded_data;
  std::string error;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeDataMessage(
      encoded.data(), encoded.size(), &decoded_header, &decoded_data, &error))
    << error;

  EXPECT_EQ(header.protocol_version, decoded_header.protocol_version);
  EXPECT_EQ(header.vendor_id, decoded_header.vendor_id);
  EXPECT_EQ(header.guid_prefix, decoded_header.guid_prefix);
  EXPECT_EQ(data.reader_id, decoded_data.reader_id);
  EXPECT_EQ(data.writer_id, decoded_data.writer_id);
  EXPECT_EQ(data.writer_sequence_number, decoded_data.writer_sequence_number);
  EXPECT_EQ(data.serialized_payload, decoded_data.serialized_payload);
}

TEST(RmwMddsRtpsProtocol, DecodeDataMessageSkipsInfoTimestampBeforeDataSubmessage)
{
  const auto header = MakeHeader();
  const auto data = MakeDataSubmessage();
  std::vector<uint8_t> encoded = rmw_mdds_cpp::rtps::EncodeDataMessage(header, data);
  const std::vector<uint8_t> info_timestamp = {
    0x09u, 0x01u,
    0x08u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u};
  encoded.insert(
    encoded.begin() + static_cast<std::ptrdiff_t>(rmw_mdds_cpp::rtps::kRtpsHeaderSize),
    info_timestamp.begin(), info_timestamp.end());

  rmw_mdds_cpp::rtps::RtpsHeader decoded_header;
  rmw_mdds_cpp::rtps::DataSubmessage decoded_data;
  std::string error;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeDataMessage(
      encoded.data(), encoded.size(), &decoded_header, &decoded_data, &error))
    << error;

  EXPECT_EQ(header.guid_prefix, decoded_header.guid_prefix);
  EXPECT_EQ(data.reader_id, decoded_data.reader_id);
  EXPECT_EQ(data.writer_id, decoded_data.writer_id);
  EXPECT_EQ(data.writer_sequence_number, decoded_data.writer_sequence_number);
  EXPECT_EQ(data.serialized_payload, decoded_data.serialized_payload);
}

TEST(RmwMddsRtpsProtocol, DecodeDataMessagesReturnsEveryDataSubmessage)
{
  const auto header = MakeHeader();
  const auto first = MakeDataSubmessage();
  auto second = MakeDataSubmessage();
  second.writer_id = {0x00u, 0x00u, 0x11u, 0xc2u};
  second.writer_sequence_number = 43;
  second.serialized_payload = {0x00u, 0x01u, 0x00u, 0x00u, 'p', 'o', 'n', 'g'};

  std::vector<uint8_t> encoded = rmw_mdds_cpp::rtps::EncodeDataMessage(header, first);
  const std::vector<uint8_t> second_packet =
    rmw_mdds_cpp::rtps::EncodeDataMessage(header, second);
  encoded.insert(
    encoded.end(),
    second_packet.begin() + static_cast<std::ptrdiff_t>(rmw_mdds_cpp::rtps::kRtpsHeaderSize),
    second_packet.end());

  rmw_mdds_cpp::rtps::RtpsHeader decoded_header;
  std::vector<rmw_mdds_cpp::rtps::DataSubmessage> decoded_messages;
  std::string error;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeDataMessages(
      encoded.data(), encoded.size(), &decoded_header, &decoded_messages, &error))
    << error;

  ASSERT_EQ(2u, decoded_messages.size());
  EXPECT_EQ(header.guid_prefix, decoded_header.guid_prefix);
  EXPECT_EQ(first.writer_id, decoded_messages[0].writer_id);
  EXPECT_EQ(first.serialized_payload, decoded_messages[0].serialized_payload);
  EXPECT_EQ(second.writer_id, decoded_messages[1].writer_id);
  EXPECT_EQ(second.writer_sequence_number, decoded_messages[1].writer_sequence_number);
  EXPECT_EQ(second.serialized_payload, decoded_messages[1].serialized_payload);
}

TEST(RmwMddsRtpsProtocol, DecodeDataMessagesSkipsDataSubmessagesWithoutPayload)
{
  const auto header = MakeHeader();
  const auto data = MakeDataSubmessage();
  std::vector<uint8_t> encoded = rmw_mdds_cpp::rtps::EncodeDataMessage(header, data);
  const std::vector<uint8_t> inline_qos_only_data = {
    0x15u, 0x03u,
    0x18u, 0x00u,
    0x00u, 0x00u,
    0x10u, 0x00u,
    0x00u, 0x00u, 0x10u, 0x04u,
    0x00u, 0x00u, 0x20u, 0x03u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x01u, 0x00u, 0x00u, 0x00u,
    0x01u, 0x00u, 0x00u, 0x00u};
  encoded.insert(
    encoded.begin() + static_cast<std::ptrdiff_t>(rmw_mdds_cpp::rtps::kRtpsHeaderSize),
    inline_qos_only_data.begin(), inline_qos_only_data.end());

  rmw_mdds_cpp::rtps::RtpsHeader decoded_header;
  std::vector<rmw_mdds_cpp::rtps::DataSubmessage> decoded_messages;
  std::string error;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeDataMessages(
      encoded.data(), encoded.size(), &decoded_header, &decoded_messages, &error))
    << error;

  ASSERT_EQ(1u, decoded_messages.size());
  EXPECT_EQ(header.guid_prefix, decoded_header.guid_prefix);
  EXPECT_EQ(data.writer_id, decoded_messages[0].writer_id);
  EXPECT_EQ(data.writer_sequence_number, decoded_messages[0].writer_sequence_number);
  EXPECT_EQ(data.serialized_payload, decoded_messages[0].serialized_payload);
}

TEST(RmwMddsRtpsProtocol, EncodeAckNackMessageRequestsMissingSedpSequences)
{
  rmw_mdds_cpp::rtps::AckNackSubmessage acknack;
  acknack.reader_id = rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinPublicationsReader;
  acknack.writer_id = rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinPublicationsWriter;
  acknack.bitmap_base = 1;
  acknack.num_bits = 3;
  acknack.bitmap = {0xe0000000u};
  acknack.count = 7;

  const auto encoded = rmw_mdds_cpp::rtps::EncodeAckNackMessage(MakeHeader(), acknack);

  const std::vector<uint8_t> expected = {
    'R', 'T', 'P', 'S',
    0x02u, 0x03u,
    0x00u, 0x00u,
    0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u,
    0x07u, 0x08u, 0x09u, 0x0au, 0x0bu, 0x0cu,
    0x06u, 0x01u,
    0x1cu, 0x00u,
    0x00u, 0x00u, 0x03u, 0xc7u,
    0x00u, 0x00u, 0x03u, 0xc2u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x01u, 0x00u, 0x00u, 0x00u,
    0x03u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0xe0u,
    0x07u, 0x00u, 0x00u, 0x00u};
  EXPECT_EQ(expected, encoded);
}

TEST(RmwMddsRtpsProtocol, EncodeHeartbeatMessageAnnouncesSedpWriterSequence)
{
  rmw_mdds_cpp::rtps::HeartbeatSubmessage heartbeat;
  heartbeat.reader_id = rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsReader;
  heartbeat.writer_id = rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsWriter;
  heartbeat.first_sequence_number = 1;
  heartbeat.last_sequence_number = 3;
  heartbeat.count = 11;

  const auto encoded = rmw_mdds_cpp::rtps::EncodeHeartbeatMessage(MakeHeader(), heartbeat);

  const std::vector<uint8_t> expected = {
    'R', 'T', 'P', 'S',
    0x02u, 0x03u,
    0x00u, 0x00u,
    0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u,
    0x07u, 0x08u, 0x09u, 0x0au, 0x0bu, 0x0cu,
    0x07u, 0x01u,
    0x1cu, 0x00u,
    0x00u, 0x00u, 0x04u, 0xc7u,
    0x00u, 0x00u, 0x04u, 0xc2u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x01u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x03u, 0x00u, 0x00u, 0x00u,
    0x0bu, 0x00u, 0x00u, 0x00u};
  EXPECT_EQ(expected, encoded);
}

TEST(RmwMddsRtpsProtocol, DecodeHeartbeatMessagesSkipsInfoTimestampBeforeHeartbeat)
{
  std::vector<uint8_t> packet = {
    'R', 'T', 'P', 'S',
    0x02u, 0x03u,
    0x01u, 0x0fu,
    0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u,
    0x07u, 0x08u, 0x09u, 0x0au, 0x0bu, 0x0cu,
    0x09u, 0x01u,
    0x08u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x07u, 0x01u,
    0x1cu, 0x00u,
    0x00u, 0x00u, 0x04u, 0xc7u,
    0x00u, 0x00u, 0x04u, 0xc2u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x01u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x05u, 0x00u, 0x00u, 0x00u,
    0x09u, 0x00u, 0x00u, 0x00u};

  rmw_mdds_cpp::rtps::RtpsHeader decoded_header;
  std::vector<rmw_mdds_cpp::rtps::HeartbeatSubmessage> heartbeats;
  std::string error;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeHeartbeatMessages(
      packet.data(), packet.size(), &decoded_header, &heartbeats, &error))
    << error;

  ASSERT_EQ(1u, heartbeats.size());
  EXPECT_EQ(MakeHeader().guid_prefix, decoded_header.guid_prefix);
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsReader, heartbeats[0].reader_id);
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsWriter, heartbeats[0].writer_id);
  EXPECT_EQ(1, heartbeats[0].first_sequence_number);
  EXPECT_EQ(5, heartbeats[0].last_sequence_number);
  EXPECT_EQ(9, heartbeats[0].count);
}

TEST(RmwMddsRtpsProtocol, DecodeDataMessageRejectsCorruptOrTruncatedPackets)
{
  const auto encoded =
    rmw_mdds_cpp::rtps::EncodeDataMessage(MakeHeader(), MakeDataSubmessage());
  std::string error;
  rmw_mdds_cpp::rtps::RtpsHeader decoded_header;
  rmw_mdds_cpp::rtps::DataSubmessage decoded_data;

  std::vector<uint8_t> corrupt_magic = encoded;
  corrupt_magic[0] = 'X';
  EXPECT_FALSE(
    rmw_mdds_cpp::rtps::DecodeDataMessage(
      corrupt_magic.data(), corrupt_magic.size(), &decoded_header, &decoded_data, &error));

  EXPECT_FALSE(
    rmw_mdds_cpp::rtps::DecodeDataMessage(
      encoded.data(), rmw_mdds_cpp::rtps::kRtpsHeaderSize - 1u, &decoded_header,
      &decoded_data, &error));

  std::vector<uint8_t> truncated_submessage = encoded;
  truncated_submessage.pop_back();
  EXPECT_FALSE(
    rmw_mdds_cpp::rtps::DecodeDataMessage(
      truncated_submessage.data(), truncated_submessage.size(), &decoded_header,
      &decoded_data, &error));
}

TEST(RmwMddsRtpsProtocol, EncodeParameterListPadsParametersAndAppendsSentinel)
{
  rmw_mdds_cpp::rtps::ParameterList parameters;
  parameters.push_back(rmw_mdds_cpp::rtps::Parameter{
    rmw_mdds_cpp::rtps::kPidVendorId,
    {0x01u, 0x0fu}});
  parameters.push_back(rmw_mdds_cpp::rtps::Parameter{
    rmw_mdds_cpp::rtps::kPidBuiltinEndpointSet,
    {0x3fu, 0x00u, 0x00u, 0x00u}});

  const std::vector<uint8_t> encoded =
    rmw_mdds_cpp::rtps::EncodeParameterList(parameters);

  const std::vector<uint8_t> expected = {
    0x16u, 0x00u, 0x02u, 0x00u,
    0x01u, 0x0fu, 0x00u, 0x00u,
    0x58u, 0x00u, 0x04u, 0x00u,
    0x3fu, 0x00u, 0x00u, 0x00u,
    0x01u, 0x00u, 0x00u, 0x00u};
  EXPECT_EQ(expected, encoded);

  rmw_mdds_cpp::rtps::ParameterList decoded;
  std::string error;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeParameterList(encoded.data(), encoded.size(), &decoded, &error))
    << error;
  ASSERT_EQ(2u, decoded.size());
  EXPECT_EQ(parameters[0].parameter_id, decoded[0].parameter_id);
  EXPECT_EQ(parameters[0].value, decoded[0].value);
  EXPECT_EQ(parameters[1].parameter_id, decoded[1].parameter_id);
  EXPECT_EQ(parameters[1].value, decoded[1].value);
}

TEST(RmwMddsRtpsProtocol, EncodeSpdpParticipantDataUsesBuiltinEndpointIdsAndCdrPayload)
{
  const auto announcement = MakeParticipantAnnouncement();
  const auto spdp = rmw_mdds_cpp::rtps::EncodeSpdpParticipantData(announcement, 7);

  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdUnknown, spdp.reader_id);
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdSpdpBuiltinParticipantWriter, spdp.writer_id);
  EXPECT_EQ(7, spdp.writer_sequence_number);
  ASSERT_GE(spdp.serialized_payload.size(), 8u);
  EXPECT_EQ(0x00u, spdp.serialized_payload[0]);
  EXPECT_EQ(0x03u, spdp.serialized_payload[1]);
  EXPECT_EQ(0x00u, spdp.serialized_payload[2]);
  EXPECT_EQ(0x00u, spdp.serialized_payload[3]);

  const std::vector<uint8_t> expected_prefix = {
    0x50u, 0x00u, 0x10u, 0x00u,
    0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u,
    0x07u, 0x08u, 0x09u, 0x0au, 0x0bu, 0x0cu,
    0x00u, 0x00u, 0x01u, 0xc1u};
  ASSERT_GE(spdp.serialized_payload.size(), 4u + expected_prefix.size());
  EXPECT_EQ(
    expected_prefix,
    std::vector<uint8_t>(
      spdp.serialized_payload.begin() + 4,
      spdp.serialized_payload.begin() + 4 + expected_prefix.size()));

  const std::vector<uint8_t> expected_tail = {
    0x31u, 0x00u, 0x18u, 0x00u,
    0x01u, 0x00u, 0x00u, 0x00u,
    0xf5u, 0x1cu, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 192u, 168u, 1u, 20u,
    0x01u, 0x00u, 0x00u, 0x00u};
  ASSERT_GE(spdp.serialized_payload.size(), expected_tail.size());
  EXPECT_EQ(
    expected_tail,
    std::vector<uint8_t>(
      spdp.serialized_payload.end() - expected_tail.size(),
      spdp.serialized_payload.end()));

  rmw_mdds_cpp::rtps::ParameterList parameters;
  std::string error;
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeParameterList(
      spdp.serialized_payload.data() + 4u, spdp.serialized_payload.size() - 4u,
      &parameters, &error))
    << error;

  const auto * protocol_version =
    FindParameter(parameters, rmw_mdds_cpp::rtps::kPidProtocolVersion);
  ASSERT_NE(nullptr, protocol_version);
  ASSERT_EQ(4u, protocol_version->value.size());
  EXPECT_EQ(announcement.protocol_version[0], protocol_version->value[0]);
  EXPECT_EQ(announcement.protocol_version[1], protocol_version->value[1]);
  EXPECT_EQ(0u, protocol_version->value[2]);
  EXPECT_EQ(0u, protocol_version->value[3]);

  const auto * vendor_id = FindParameter(parameters, rmw_mdds_cpp::rtps::kPidVendorId);
  ASSERT_NE(nullptr, vendor_id);
  ASSERT_EQ(4u, vendor_id->value.size());
  EXPECT_EQ(announcement.vendor_id[0], vendor_id->value[0]);
  EXPECT_EQ(announcement.vendor_id[1], vendor_id->value[1]);
  EXPECT_EQ(0u, vendor_id->value[2]);
  EXPECT_EQ(0u, vendor_id->value[3]);

  const auto * lease_duration =
    FindParameter(parameters, rmw_mdds_cpp::rtps::kPidParticipantLeaseDuration);
  ASSERT_NE(nullptr, lease_duration);
  ASSERT_EQ(8u, lease_duration->value.size());
  EXPECT_EQ(20, ReadI32Little(lease_duration->value, 0u));
  EXPECT_EQ(0u, ReadU32Little(lease_duration->value, 4u));
}

TEST(RmwMddsRtpsProtocol, EncodeSedpEndpointDataUsesBuiltinWritersAndEndpointParameters)
{
  rmw_mdds_cpp::rtps::SedpEndpointAnnouncement publication;
  publication.participant_guid_prefix = MakeHeader().guid_prefix;
  publication.endpoint_entity_id = {0x00u, 0x00u, 0x10u, 0x03u};
  publication.topic_name = "rt/chatter";
  publication.type_name = "std_msgs::msg::dds_::String_";
  publication.endpoint_kind = rmw_mdds_cpp::rtps::SedpEndpointKind::kPublication;

  const auto publication_data = rmw_mdds_cpp::rtps::EncodeSedpEndpointData(publication, 12);
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdUnknown, publication_data.reader_id);
  EXPECT_EQ(
    rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinPublicationsWriter,
    publication_data.writer_id);
  EXPECT_EQ(12, publication_data.writer_sequence_number);

  rmw_mdds_cpp::rtps::ParameterList publication_parameters;
  std::string error;
  ASSERT_GE(publication_data.serialized_payload.size(), 4u);
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeParameterList(
      publication_data.serialized_payload.data() + 4u,
      publication_data.serialized_payload.size() - 4u, &publication_parameters, &error))
    << error;
  const auto * publication_guid =
    FindParameter(publication_parameters, rmw_mdds_cpp::rtps::kPidEndpointGuid);
  ASSERT_NE(nullptr, publication_guid);
  std::vector<uint8_t> expected_guid(
    publication.participant_guid_prefix.begin(), publication.participant_guid_prefix.end());
  expected_guid.insert(
    expected_guid.end(), publication.endpoint_entity_id.begin(),
    publication.endpoint_entity_id.end());
  EXPECT_EQ(expected_guid, publication_guid->value);
  const auto * publication_topic =
    FindParameter(publication_parameters, rmw_mdds_cpp::rtps::kPidTopicName);
  ASSERT_NE(nullptr, publication_topic);
  EXPECT_EQ("rt/chatter", DecodeCdrStringValue(publication_topic->value));
  const auto * publication_type =
    FindParameter(publication_parameters, rmw_mdds_cpp::rtps::kPidTypeName);
  ASSERT_NE(nullptr, publication_type);
  EXPECT_EQ("std_msgs::msg::dds_::String_", DecodeCdrStringValue(publication_type->value));
  const auto * reliability =
    FindParameter(publication_parameters, rmw_mdds_cpp::rtps::kPidReliability);
  ASSERT_NE(nullptr, reliability);
  const std::vector<uint8_t> expected_best_effort_reliability = {
    0x01u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u};
  EXPECT_EQ(expected_best_effort_reliability, reliability->value);
  const auto * durability =
    FindParameter(publication_parameters, rmw_mdds_cpp::rtps::kPidDurability);
  ASSERT_NE(nullptr, durability);
  EXPECT_EQ(std::vector<uint8_t>({0x00u, 0x00u, 0x00u, 0x00u}), durability->value);
  const auto * data_representation =
    FindParameter(publication_parameters, rmw_mdds_cpp::rtps::kPidDataRepresentation);
  ASSERT_NE(nullptr, data_representation);
  const std::vector<uint8_t> expected_xcdr_representation = {
    0x01u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u};
  EXPECT_EQ(expected_xcdr_representation, data_representation->value);

  rmw_mdds_cpp::rtps::SedpEndpointAnnouncement subscription = publication;
  subscription.endpoint_entity_id = {0x00u, 0x00u, 0x11u, 0x04u};
  subscription.endpoint_kind = rmw_mdds_cpp::rtps::SedpEndpointKind::kSubscription;
  const auto subscription_data = rmw_mdds_cpp::rtps::EncodeSedpEndpointData(subscription, 13);
  EXPECT_EQ(rmw_mdds_cpp::rtps::kEntityIdUnknown, subscription_data.reader_id);
  EXPECT_EQ(
    rmw_mdds_cpp::rtps::kEntityIdSedpBuiltinSubscriptionsWriter,
    subscription_data.writer_id);
  EXPECT_EQ(13, subscription_data.writer_sequence_number);

  rmw_mdds_cpp::rtps::ParameterList subscription_parameters;
  ASSERT_GE(subscription_data.serialized_payload.size(), 4u);
  ASSERT_TRUE(
    rmw_mdds_cpp::rtps::DecodeParameterList(
      subscription_data.serialized_payload.data() + 4u,
      subscription_data.serialized_payload.size() - 4u, &subscription_parameters, &error))
    << error;
  const auto * expects_inline_qos =
    FindParameter(subscription_parameters, rmw_mdds_cpp::rtps::kPidExpectsInlineQos);
  ASSERT_NE(nullptr, expects_inline_qos);
  EXPECT_EQ(std::vector<uint8_t>({0x00u, 0x00u, 0x00u, 0x00u}), expects_inline_qos->value);
  const auto * subscription_representation =
    FindParameter(subscription_parameters, rmw_mdds_cpp::rtps::kPidDataRepresentation);
  ASSERT_NE(nullptr, subscription_representation);
  EXPECT_EQ(expected_xcdr_representation, subscription_representation->value);
}
