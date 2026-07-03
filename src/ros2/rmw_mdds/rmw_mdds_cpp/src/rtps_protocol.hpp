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

#ifndef RMW_MDDS_CPP_SRC__RTPS_PROTOCOL_HPP_
#define RMW_MDDS_CPP_SRC__RTPS_PROTOCOL_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rmw_mdds_cpp
{
namespace rtps
{

constexpr size_t kRtpsHeaderSize = 20u;
constexpr uint8_t kAckNackSubmessageId = 0x06u;
constexpr uint8_t kHeartbeatSubmessageId = 0x07u;
constexpr uint8_t kDataSubmessageId = 0x15u;
constexpr uint8_t kSubmessageFlagLittleEndian = 0x01u;
constexpr uint8_t kDataSubmessageFlagInlineQos = 0x02u;
constexpr uint8_t kDataSubmessageFlagData = 0x04u;
constexpr uint8_t kDataSubmessageFlagKey = 0x08u;
constexpr uint16_t kHeartbeatSubmessageFixedContentSize = 28u;
constexpr uint16_t kAckNackSubmessageFixedContentSize = 24u;
constexpr uint16_t kDataSubmessageOctetsToInlineQos = 16u;
constexpr uint16_t kDataSubmessageFixedContentSize = 20u;
constexpr uint16_t kPidSentinel = 0x0001u;
constexpr uint16_t kPidParticipantLeaseDuration = 0x0002u;
constexpr uint16_t kPidTopicName = 0x0005u;
constexpr uint16_t kPidTypeName = 0x0007u;
constexpr uint16_t kPidReliability = 0x001au;
constexpr uint16_t kPidDurability = 0x001du;
constexpr uint16_t kPidProtocolVersion = 0x0015u;
constexpr uint16_t kPidVendorId = 0x0016u;
constexpr uint16_t kPidUnicastLocator = 0x002fu;
constexpr uint16_t kPidDefaultUnicastLocator = 0x0031u;
constexpr uint16_t kPidMetatrafficUnicastLocator = 0x0032u;
constexpr uint16_t kPidExpectsInlineQos = 0x0043u;
constexpr uint16_t kPidParticipantGuid = 0x0050u;
constexpr uint16_t kPidBuiltinEndpointSet = 0x0058u;
constexpr uint16_t kPidEndpointGuid = 0x005au;
constexpr uint16_t kPidTypeMaxSizeSerialized = 0x0060u;
constexpr uint16_t kPidKeyHash = 0x0070u;
constexpr uint16_t kPidDataRepresentation = 0x0073u;
constexpr int32_t kLocatorKindUdpV4 = 1;
constexpr uint32_t kBuiltinEndpointParticipantAnnouncer = 0x00000001u;
constexpr uint32_t kBuiltinEndpointParticipantDetector = 0x00000002u;
constexpr uint32_t kBuiltinEndpointPublicationAnnouncer = 0x00000004u;
constexpr uint32_t kBuiltinEndpointPublicationDetector = 0x00000008u;
constexpr uint32_t kBuiltinEndpointSubscriptionAnnouncer = 0x00000010u;
constexpr uint32_t kBuiltinEndpointSubscriptionDetector = 0x00000020u;
constexpr uint8_t kEntityKindUserWriterNoKey = 0x03u;
constexpr uint8_t kEntityKindUserReaderNoKey = 0x04u;

using GuidPrefix = std::array<uint8_t, 12>;
using EntityId = std::array<uint8_t, 4>;

constexpr EntityId kEntityIdUnknown = {0x00u, 0x00u, 0x00u, 0x00u};
constexpr EntityId kEntityIdParticipant = {0x00u, 0x00u, 0x01u, 0xc1u};
constexpr EntityId kEntityIdSedpBuiltinPublicationsReader = {0x00u, 0x00u, 0x03u, 0xc7u};
constexpr EntityId kEntityIdSedpBuiltinPublicationsWriter = {0x00u, 0x00u, 0x03u, 0xc2u};
constexpr EntityId kEntityIdSedpBuiltinSubscriptionsReader = {0x00u, 0x00u, 0x04u, 0xc7u};
constexpr EntityId kEntityIdSedpBuiltinSubscriptionsWriter = {0x00u, 0x00u, 0x04u, 0xc2u};
constexpr EntityId kEntityIdSpdpBuiltinParticipantWriter = {0x00u, 0x01u, 0x00u, 0xc2u};

struct RtpsHeader
{
  std::array<uint8_t, 2> protocol_version{2u, 3u};
  std::array<uint8_t, 2> vendor_id{0u, 0u};
  GuidPrefix guid_prefix{};
};

struct DataSubmessage
{
  EntityId reader_id{};
  EntityId writer_id{};
  int64_t writer_sequence_number = 0;
  std::vector<uint8_t> serialized_payload;
};

struct HeartbeatSubmessage
{
  EntityId reader_id{};
  EntityId writer_id{};
  int64_t first_sequence_number = 0;
  int64_t last_sequence_number = 0;
  int32_t count = 0;
};

struct AckNackSubmessage
{
  EntityId reader_id{};
  EntityId writer_id{};
  int64_t bitmap_base = 1;
  uint32_t num_bits = 0;
  std::vector<uint32_t> bitmap;
  int32_t count = 0;
};

struct Locator
{
  int32_t kind = 0;
  uint32_t port = 0;
  std::array<uint8_t, 16> address{};
};

struct Parameter
{
  uint16_t parameter_id = 0;
  std::vector<uint8_t> value;
};

using ParameterList = std::vector<Parameter>;

struct ParticipantAnnouncement
{
  GuidPrefix participant_guid_prefix{};
  std::array<uint8_t, 2> protocol_version{2u, 3u};
  std::array<uint8_t, 2> vendor_id{0u, 0u};
  int32_t participant_lease_duration_seconds = 20;
  uint32_t participant_lease_duration_nanoseconds = 0;
  uint32_t builtin_endpoint_set = 0;
  Locator metatraffic_unicast_locator;
  Locator default_unicast_locator;
};

enum class SedpEndpointKind
{
  kPublication,
  kSubscription,
};

struct SedpEndpointAnnouncement
{
  GuidPrefix participant_guid_prefix{};
  EntityId endpoint_entity_id{};
  std::array<uint8_t, 2> protocol_version{2u, 3u};
  std::array<uint8_t, 2> vendor_id{0u, 0u};
  uint32_t type_max_serialized_size = 0;
  Locator unicast_locator;
  std::string topic_name;
  std::string type_name;
  SedpEndpointKind endpoint_kind = SedpEndpointKind::kPublication;
};

std::vector<uint8_t> EncodeDataMessage(
  const RtpsHeader & header, const DataSubmessage & data);

std::vector<uint8_t> EncodeAckNackMessage(
  const RtpsHeader & header, const AckNackSubmessage & acknack);

std::vector<uint8_t> EncodeHeartbeatMessage(
  const RtpsHeader & header, const HeartbeatSubmessage & heartbeat);

bool DecodeDataMessage(
  const uint8_t * data, size_t size, RtpsHeader * header, DataSubmessage * submessage,
  std::string * error);

bool DecodeDataMessages(
  const uint8_t * data, size_t size, RtpsHeader * header,
  std::vector<DataSubmessage> * submessages, std::string * error);

bool DecodeHeartbeatMessages(
  const uint8_t * data, size_t size, RtpsHeader * header,
  std::vector<HeartbeatSubmessage> * submessages, std::string * error);

std::vector<uint8_t> EncodeParameterList(const ParameterList & parameters);

bool DecodeParameterList(
  const uint8_t * data, size_t size, ParameterList * parameters, std::string * error);

DataSubmessage EncodeSpdpParticipantData(
  const ParticipantAnnouncement & announcement, int64_t writer_sequence_number);

DataSubmessage EncodeSedpEndpointData(
  const SedpEndpointAnnouncement & announcement, int64_t writer_sequence_number);

}  // namespace rtps
}  // namespace rmw_mdds_cpp

#endif  // RMW_MDDS_CPP_SRC__RTPS_PROTOCOL_HPP_
