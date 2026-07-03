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

#include "rtps_protocol.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace rmw_mdds_cpp
{
namespace rtps
{
namespace
{
constexpr uint8_t kRtpsMagic[] = {'R', 'T', 'P', 'S'};
constexpr size_t kSubmessageHeaderSize = 4u;
constexpr uint8_t kPlCdrLittleEndianEncapsulation[] = {0x00u, 0x03u, 0x00u, 0x00u};

void SetError(std::string * error, const char * message)
{
  if (error != nullptr) {
    *error = message;
  }
}

void AppendU16Little(std::vector<uint8_t> * out, uint16_t value)
{
  out->push_back(static_cast<uint8_t>(value & 0xffu));
  out->push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
}

void AppendU32Little(std::vector<uint8_t> * out, uint32_t value)
{
  out->push_back(static_cast<uint8_t>(value & 0xffu));
  out->push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  out->push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  out->push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

void AppendI32Little(std::vector<uint8_t> * out, int32_t value)
{
  AppendU32Little(out, static_cast<uint32_t>(value));
}

void AppendSequenceNumberLittle(std::vector<uint8_t> * out, int64_t sequence_number)
{
  const uint64_t encoded = static_cast<uint64_t>(sequence_number);
  AppendU32Little(out, static_cast<uint32_t>((encoded >> 32u) & 0xffffffffu));
  AppendU32Little(out, static_cast<uint32_t>(encoded & 0xffffffffu));
}

bool ReadU16(
  const uint8_t * data, size_t size, size_t * offset, bool little_endian, uint16_t * value)
{
  if (data == nullptr || offset == nullptr || value == nullptr || *offset + 2u > size) {
    return false;
  }
  if (little_endian) {
    *value = static_cast<uint16_t>(data[*offset]) |
             static_cast<uint16_t>(static_cast<uint16_t>(data[*offset + 1u]) << 8u);
  } else {
    *value = static_cast<uint16_t>(static_cast<uint16_t>(data[*offset]) << 8u) |
             static_cast<uint16_t>(data[*offset + 1u]);
  }
  *offset += 2u;
  return true;
}

bool ReadU32(
  const uint8_t * data, size_t size, size_t * offset, bool little_endian, uint32_t * value)
{
  if (data == nullptr || offset == nullptr || value == nullptr || *offset + 4u > size) {
    return false;
  }
  if (little_endian) {
    *value = static_cast<uint32_t>(data[*offset]) |
             (static_cast<uint32_t>(data[*offset + 1u]) << 8u) |
             (static_cast<uint32_t>(data[*offset + 2u]) << 16u) |
             (static_cast<uint32_t>(data[*offset + 3u]) << 24u);
  } else {
    *value = (static_cast<uint32_t>(data[*offset]) << 24u) |
             (static_cast<uint32_t>(data[*offset + 1u]) << 16u) |
             (static_cast<uint32_t>(data[*offset + 2u]) << 8u) |
             static_cast<uint32_t>(data[*offset + 3u]);
  }
  *offset += 4u;
  return true;
}

bool ReadEntityId(const uint8_t * data, size_t size, size_t * offset, EntityId * entity_id)
{
  if (data == nullptr || offset == nullptr || entity_id == nullptr || *offset + entity_id->size() > size) {
    return false;
  }
  std::copy(data + *offset, data + *offset + entity_id->size(), entity_id->begin());
  *offset += entity_id->size();
  return true;
}

size_t Align4(size_t size)
{
  return (size + 3u) & ~static_cast<size_t>(3u);
}

bool AppendParameter(std::vector<uint8_t> * out, const Parameter & parameter)
{
  if (
    out == nullptr ||
    parameter.value.size() > static_cast<size_t>(std::numeric_limits<uint16_t>::max())) {
    return false;
  }
  AppendU16Little(out, parameter.parameter_id);
  AppendU16Little(out, static_cast<uint16_t>(parameter.value.size()));
  out->insert(out->end(), parameter.value.begin(), parameter.value.end());
  const size_t padded_size = Align4(parameter.value.size());
  out->insert(out->end(), padded_size - parameter.value.size(), 0u);
  return true;
}

Parameter MakeParameter(uint16_t parameter_id, const std::vector<uint8_t> & value)
{
  Parameter parameter;
  parameter.parameter_id = parameter_id;
  parameter.value = value;
  return parameter;
}

std::vector<uint8_t> EncodeU32ParameterValue(uint32_t value)
{
  std::vector<uint8_t> out;
  AppendU32Little(&out, value);
  return out;
}

std::vector<uint8_t> EncodeBoolParameterValue(bool value)
{
  return {static_cast<uint8_t>(value ? 1u : 0u), 0u, 0u, 0u};
}

std::vector<uint8_t> EncodeProtocolVersionParameterValue(const std::array<uint8_t, 2> & version)
{
  return {version[0], version[1], 0u, 0u};
}

std::vector<uint8_t> EncodeVendorIdParameterValue(const std::array<uint8_t, 2> & vendor_id)
{
  return {vendor_id[0], vendor_id[1], 0u, 0u};
}

std::vector<uint8_t> EncodeDurationParameterValue(int32_t seconds, uint32_t nanoseconds)
{
  std::vector<uint8_t> out;
  AppendI32Little(&out, seconds);
  AppendU32Little(&out, nanoseconds);
  return out;
}

std::vector<uint8_t> EncodeDurabilityParameterValue(uint8_t kind)
{
  return {kind, 0u, 0u, 0u};
}

std::vector<uint8_t> EncodeReliabilityParameterValue(
  uint8_t kind, int32_t max_blocking_seconds, uint32_t max_blocking_nanoseconds)
{
  std::vector<uint8_t> out{kind, 0u, 0u, 0u};
  AppendI32Little(&out, max_blocking_seconds);
  AppendU32Little(&out, max_blocking_nanoseconds);
  return out;
}

std::vector<uint8_t> EncodeDataRepresentationParameterValue(uint16_t representation)
{
  std::vector<uint8_t> out;
  AppendU32Little(&out, 1u);
  AppendU16Little(&out, representation);
  AppendU16Little(&out, 0u);
  return out;
}

std::vector<uint8_t> EncodeGuidParameterValue(const GuidPrefix & prefix, const EntityId & entity_id)
{
  std::vector<uint8_t> out;
  out.reserve(prefix.size() + entity_id.size());
  out.insert(out.end(), prefix.begin(), prefix.end());
  out.insert(out.end(), entity_id.begin(), entity_id.end());
  return out;
}

std::vector<uint8_t> EncodeStringParameterValue(const std::string & value)
{
  std::vector<uint8_t> out;
  if (value.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max() - 1u)) {
    return {};
  }
  AppendU32Little(&out, static_cast<uint32_t>(value.size() + 1u));
  out.insert(out.end(), value.begin(), value.end());
  out.push_back('\0');
  return out;
}

std::vector<uint8_t> EncodeLocatorParameterValue(const Locator & locator)
{
  std::vector<uint8_t> out;
  out.reserve(24u);
  AppendI32Little(&out, locator.kind);
  AppendU32Little(&out, locator.port);
  out.insert(out.end(), locator.address.begin(), locator.address.end());
  return out;
}

bool ValidateOutputPointers(
  RtpsHeader * header, DataSubmessage * submessage, std::string * error)
{
  if (header == nullptr || submessage == nullptr) {
    SetError(error, "RTPS decode output is null");
    return false;
  }
  return true;
}

bool DecodeRtpsHeader(const uint8_t * data, size_t size, RtpsHeader * header, std::string * error)
{
  if (header == nullptr) {
    SetError(error, "RTPS header output is null");
    return false;
  }
  if (data == nullptr && size != 0u) {
    SetError(error, "RTPS data is null");
    return false;
  }
  if (size < kRtpsHeaderSize + kSubmessageHeaderSize) {
    SetError(error, "RTPS packet is truncated");
    return false;
  }
  if (!std::equal(std::begin(kRtpsMagic), std::end(kRtpsMagic), data)) {
    SetError(error, "RTPS packet magic is invalid");
    return false;
  }

  header->protocol_version = {data[4], data[5]};
  header->vendor_id = {data[6], data[7]};
  std::copy(data + 8u, data + kRtpsHeaderSize, header->guid_prefix.begin());
  return true;
}

bool SkipParameterList(
  const uint8_t * data, size_t size, size_t * offset, bool little_endian, std::string * error)
{
  if (data == nullptr || offset == nullptr) {
    SetError(error, "RTPS inline QoS input is null");
    return false;
  }

  while (*offset < size) {
    uint16_t parameter_id = 0u;
    uint16_t parameter_size = 0u;
    if (
      !ReadU16(data, size, offset, little_endian, &parameter_id) ||
      !ReadU16(data, size, offset, little_endian, &parameter_size)) {
      SetError(error, "RTPS inline QoS header is truncated");
      return false;
    }
    if (parameter_id == kPidSentinel) {
      if (parameter_size != 0u) {
        SetError(error, "RTPS inline QoS sentinel size is invalid");
        return false;
      }
      return true;
    }

    const size_t padded_size = Align4(parameter_size);
    if (*offset + padded_size > size) {
      SetError(error, "RTPS inline QoS parameter is truncated");
      return false;
    }
    *offset += padded_size;
  }

  SetError(error, "RTPS inline QoS sentinel is missing");
  return false;
}
}  // namespace

std::vector<uint8_t> EncodeParameterList(const ParameterList & parameters)
{
  std::vector<uint8_t> out;
  for (const Parameter & parameter : parameters) {
    if (!AppendParameter(&out, parameter)) {
      return {};
    }
  }
  AppendU16Little(&out, kPidSentinel);
  AppendU16Little(&out, 0u);
  return out;
}

bool DecodeParameterList(
  const uint8_t * data, size_t size, ParameterList * parameters, std::string * error)
{
  if (parameters == nullptr) {
    SetError(error, "RTPS ParameterList output is null");
    return false;
  }
  if (data == nullptr && size != 0u) {
    SetError(error, "RTPS ParameterList data is null");
    return false;
  }

  ParameterList decoded;
  size_t offset = 0u;
  while (offset < size) {
    uint16_t parameter_id = 0u;
    uint16_t parameter_size = 0u;
    if (
      !ReadU16(data, size, &offset, true, &parameter_id) ||
      !ReadU16(data, size, &offset, true, &parameter_size)) {
      SetError(error, "RTPS ParameterList header is truncated");
      return false;
    }
    if (parameter_id == kPidSentinel) {
      if (parameter_size != 0u) {
        SetError(error, "RTPS ParameterList sentinel size is invalid");
        return false;
      }
      *parameters = std::move(decoded);
      return true;
    }
    if (offset + parameter_size > size) {
      SetError(error, "RTPS ParameterList value is truncated");
      return false;
    }

    Parameter parameter;
    parameter.parameter_id = parameter_id;
    parameter.value.assign(data + offset, data + offset + parameter_size);
    decoded.push_back(std::move(parameter));

    const size_t padded_size = Align4(parameter_size);
    if (offset + padded_size > size) {
      SetError(error, "RTPS ParameterList padding is truncated");
      return false;
    }
    offset += padded_size;
  }

  SetError(error, "RTPS ParameterList sentinel is missing");
  return false;
}

DataSubmessage EncodeSpdpParticipantData(
  const ParticipantAnnouncement & announcement, int64_t writer_sequence_number)
{
  ParameterList parameters;
  parameters.push_back(MakeParameter(
    kPidParticipantGuid,
    EncodeGuidParameterValue(announcement.participant_guid_prefix, kEntityIdParticipant)));
  parameters.push_back(MakeParameter(
    kPidProtocolVersion,
    EncodeProtocolVersionParameterValue(announcement.protocol_version)));
  parameters.push_back(MakeParameter(kPidVendorId, EncodeVendorIdParameterValue(announcement.vendor_id)));
  parameters.push_back(MakeParameter(
    kPidParticipantLeaseDuration,
    EncodeDurationParameterValue(
      announcement.participant_lease_duration_seconds,
      announcement.participant_lease_duration_nanoseconds)));
  parameters.push_back(MakeParameter(
    kPidBuiltinEndpointSet,
    EncodeU32ParameterValue(announcement.builtin_endpoint_set)));
  parameters.push_back(MakeParameter(
    kPidMetatrafficUnicastLocator,
    EncodeLocatorParameterValue(announcement.metatraffic_unicast_locator)));
  parameters.push_back(MakeParameter(
    kPidDefaultUnicastLocator,
    EncodeLocatorParameterValue(announcement.default_unicast_locator)));

  DataSubmessage data;
  data.reader_id = kEntityIdUnknown;
  data.writer_id = kEntityIdSpdpBuiltinParticipantWriter;
  data.writer_sequence_number = writer_sequence_number;
  data.serialized_payload.insert(
    data.serialized_payload.end(), std::begin(kPlCdrLittleEndianEncapsulation),
    std::end(kPlCdrLittleEndianEncapsulation));
  const std::vector<uint8_t> encoded_parameters = EncodeParameterList(parameters);
  data.serialized_payload.insert(
    data.serialized_payload.end(), encoded_parameters.begin(), encoded_parameters.end());
  return data;
}

DataSubmessage EncodeSedpEndpointData(
  const SedpEndpointAnnouncement & announcement, int64_t writer_sequence_number)
{
  const std::vector<uint8_t> endpoint_guid =
    EncodeGuidParameterValue(announcement.participant_guid_prefix, announcement.endpoint_entity_id);
  ParameterList parameters;
  parameters.push_back(MakeParameter(kPidEndpointGuid, endpoint_guid));
  if (announcement.unicast_locator.kind == kLocatorKindUdpV4 && announcement.unicast_locator.port != 0u) {
    parameters.push_back(MakeParameter(
      kPidUnicastLocator,
      EncodeLocatorParameterValue(announcement.unicast_locator)));
  }
  parameters.push_back(MakeParameter(
    kPidParticipantGuid,
    EncodeGuidParameterValue(announcement.participant_guid_prefix, kEntityIdParticipant)));
  parameters.push_back(MakeParameter(
    kPidTopicName,
    EncodeStringParameterValue(announcement.topic_name)));
  parameters.push_back(MakeParameter(
    kPidTypeName,
    EncodeStringParameterValue(announcement.type_name)));
  parameters.push_back(MakeParameter(kPidKeyHash, endpoint_guid));
  parameters.push_back(MakeParameter(
    kPidTypeMaxSizeSerialized,
    EncodeU32ParameterValue(announcement.type_max_serialized_size)));
  parameters.push_back(MakeParameter(kPidReliability, EncodeReliabilityParameterValue(0x01u, 0, 0)));
  parameters.push_back(MakeParameter(kPidDurability, EncodeDurabilityParameterValue(0x00u)));
  parameters.push_back(MakeParameter(kPidDataRepresentation, EncodeDataRepresentationParameterValue(0x0000u)));
  if (announcement.endpoint_kind == SedpEndpointKind::kSubscription) {
    parameters.push_back(MakeParameter(kPidExpectsInlineQos, EncodeBoolParameterValue(false)));
  }
  parameters.push_back(MakeParameter(
    kPidProtocolVersion,
    EncodeProtocolVersionParameterValue(announcement.protocol_version)));
  parameters.push_back(MakeParameter(kPidVendorId, EncodeVendorIdParameterValue(announcement.vendor_id)));

  DataSubmessage data;
  data.reader_id = kEntityIdUnknown;
  data.writer_id =
    announcement.endpoint_kind == SedpEndpointKind::kPublication ?
    kEntityIdSedpBuiltinPublicationsWriter :
    kEntityIdSedpBuiltinSubscriptionsWriter;
  data.writer_sequence_number = writer_sequence_number;
  data.serialized_payload.insert(
    data.serialized_payload.end(), std::begin(kPlCdrLittleEndianEncapsulation),
    std::end(kPlCdrLittleEndianEncapsulation));
  const std::vector<uint8_t> encoded_parameters = EncodeParameterList(parameters);
  data.serialized_payload.insert(
    data.serialized_payload.end(), encoded_parameters.begin(), encoded_parameters.end());
  return data;
}

std::vector<uint8_t> EncodeDataMessage(
  const RtpsHeader & header, const DataSubmessage & data)
{
  if (
    data.serialized_payload.size() >
    static_cast<size_t>(std::numeric_limits<uint16_t>::max() - kDataSubmessageFixedContentSize)) {
    return {};
  }

  const uint16_t submessage_size =
    static_cast<uint16_t>(kDataSubmessageFixedContentSize + data.serialized_payload.size());
  std::vector<uint8_t> out;
  out.reserve(kRtpsHeaderSize + kSubmessageHeaderSize + submessage_size);
  out.insert(out.end(), std::begin(kRtpsMagic), std::end(kRtpsMagic));
  out.insert(out.end(), header.protocol_version.begin(), header.protocol_version.end());
  out.insert(out.end(), header.vendor_id.begin(), header.vendor_id.end());
  out.insert(out.end(), header.guid_prefix.begin(), header.guid_prefix.end());

  out.push_back(kDataSubmessageId);
  out.push_back(kSubmessageFlagLittleEndian | kDataSubmessageFlagData);
  AppendU16Little(&out, submessage_size);
  AppendU16Little(&out, 0u);
  AppendU16Little(&out, kDataSubmessageOctetsToInlineQos);
  out.insert(out.end(), data.reader_id.begin(), data.reader_id.end());
  out.insert(out.end(), data.writer_id.begin(), data.writer_id.end());

  const uint64_t sequence_number = static_cast<uint64_t>(data.writer_sequence_number);
  AppendSequenceNumberLittle(&out, static_cast<int64_t>(sequence_number));
  out.insert(out.end(), data.serialized_payload.begin(), data.serialized_payload.end());
  return out;
}

std::vector<uint8_t> EncodeAckNackMessage(
  const RtpsHeader & header, const AckNackSubmessage & acknack)
{
  const size_t bitmap_words = (acknack.num_bits + 31u) / 32u;
  if (
    acknack.num_bits > 256u ||
    acknack.bitmap.size() != bitmap_words ||
    bitmap_words > static_cast<size_t>(
      (std::numeric_limits<uint16_t>::max() - kAckNackSubmessageFixedContentSize) / 4u)) {
    return {};
  }

  const uint16_t submessage_size = static_cast<uint16_t>(
    kAckNackSubmessageFixedContentSize + bitmap_words * sizeof(uint32_t));
  std::vector<uint8_t> out;
  out.reserve(kRtpsHeaderSize + kSubmessageHeaderSize + submessage_size);
  out.insert(out.end(), std::begin(kRtpsMagic), std::end(kRtpsMagic));
  out.insert(out.end(), header.protocol_version.begin(), header.protocol_version.end());
  out.insert(out.end(), header.vendor_id.begin(), header.vendor_id.end());
  out.insert(out.end(), header.guid_prefix.begin(), header.guid_prefix.end());

  out.push_back(kAckNackSubmessageId);
  out.push_back(kSubmessageFlagLittleEndian);
  AppendU16Little(&out, submessage_size);
  out.insert(out.end(), acknack.reader_id.begin(), acknack.reader_id.end());
  out.insert(out.end(), acknack.writer_id.begin(), acknack.writer_id.end());
  AppendSequenceNumberLittle(&out, acknack.bitmap_base);
  AppendU32Little(&out, acknack.num_bits);
  for (const uint32_t word : acknack.bitmap) {
    AppendU32Little(&out, word);
  }
  AppendI32Little(&out, acknack.count);
  return out;
}

std::vector<uint8_t> EncodeHeartbeatMessage(
  const RtpsHeader & header, const HeartbeatSubmessage & heartbeat)
{
  std::vector<uint8_t> out;
  out.reserve(kRtpsHeaderSize + kSubmessageHeaderSize + kHeartbeatSubmessageFixedContentSize);
  out.insert(out.end(), std::begin(kRtpsMagic), std::end(kRtpsMagic));
  out.insert(out.end(), header.protocol_version.begin(), header.protocol_version.end());
  out.insert(out.end(), header.vendor_id.begin(), header.vendor_id.end());
  out.insert(out.end(), header.guid_prefix.begin(), header.guid_prefix.end());

  out.push_back(kHeartbeatSubmessageId);
  out.push_back(kSubmessageFlagLittleEndian);
  AppendU16Little(&out, kHeartbeatSubmessageFixedContentSize);
  out.insert(out.end(), heartbeat.reader_id.begin(), heartbeat.reader_id.end());
  out.insert(out.end(), heartbeat.writer_id.begin(), heartbeat.writer_id.end());
  AppendSequenceNumberLittle(&out, heartbeat.first_sequence_number);
  AppendSequenceNumberLittle(&out, heartbeat.last_sequence_number);
  AppendI32Little(&out, heartbeat.count);
  return out;
}

bool DecodeDataMessage(
  const uint8_t * data, size_t size, RtpsHeader * header, DataSubmessage * submessage,
  std::string * error)
{
  if (!ValidateOutputPointers(header, submessage, error)) {
    return false;
  }
  RtpsHeader decoded_header;
  std::vector<DataSubmessage> decoded_messages;
  if (!DecodeDataMessages(data, size, &decoded_header, &decoded_messages, error)) {
    return false;
  }
  if (decoded_messages.empty()) {
    SetError(error, "RTPS DATA submessage is missing");
    return false;
  }

  *header = decoded_header;
  *submessage = std::move(decoded_messages.front());
  return true;
}

bool DecodeDataMessages(
  const uint8_t * data, size_t size, RtpsHeader * header,
  std::vector<DataSubmessage> * submessages, std::string * error)
{
  if (header == nullptr || submessages == nullptr) {
    SetError(error, "RTPS decode output is null");
    return false;
  }
  submessages->clear();
  if (data == nullptr && size != 0u) {
    SetError(error, "RTPS data is null");
    return false;
  }
  if (size < kRtpsHeaderSize + kSubmessageHeaderSize) {
    SetError(error, "RTPS packet is truncated");
    return false;
  }

  RtpsHeader decoded_header;
  if (!DecodeRtpsHeader(data, size, &decoded_header, error)) {
    return false;
  }

  size_t offset = kRtpsHeaderSize;
  while (offset < size) {
    if (offset + kSubmessageHeaderSize > size) {
      SetError(error, "RTPS submessage header is truncated");
      return false;
    }

    const uint8_t submessage_id = data[offset++];
    const uint8_t flags = data[offset++];
    const bool little_endian = (flags & kSubmessageFlagLittleEndian) != 0u;
    const bool has_inline_qos = (flags & kDataSubmessageFlagInlineQos) != 0u;
    const bool has_data = (flags & kDataSubmessageFlagData) != 0u;
    const bool has_key = (flags & kDataSubmessageFlagKey) != 0u;
    uint16_t submessage_size = 0u;
    if (!ReadU16(data, size, &offset, little_endian, &submessage_size)) {
      SetError(error, "RTPS DATA submessage header is truncated");
      return false;
    }
    const size_t content_start = offset;
    const size_t submessage_end =
      submessage_size == 0u ? size : content_start + submessage_size;
    if (submessage_end > size || submessage_end < content_start) {
      SetError(error, "RTPS submessage size is invalid");
      return false;
    }
    if (submessage_id != kDataSubmessageId) {
      offset = submessage_end;
      continue;
    }

    if (has_key && has_data) {
      SetError(error, "RTPS DATA submessage flags are not supported");
      return false;
    }
    if (!has_data) {
      offset = submessage_end;
      continue;
    }
    if (submessage_size < kDataSubmessageFixedContentSize) {
      SetError(error, "RTPS DATA submessage size is invalid");
      return false;
    }

    uint16_t extra_flags = 0u;
    uint16_t octets_to_inline_qos = 0u;
    if (
      !ReadU16(data, submessage_end, &offset, little_endian, &extra_flags) ||
      !ReadU16(data, submessage_end, &offset, little_endian, &octets_to_inline_qos)) {
      SetError(error, "RTPS DATA fixed fields are truncated");
      return false;
    }
    (void)extra_flags;
    if (octets_to_inline_qos < kDataSubmessageOctetsToInlineQos) {
      SetError(error, "RTPS DATA inline QoS offset is invalid");
      return false;
    }

    DataSubmessage decoded_data;
    if (
      !ReadEntityId(data, submessage_end, &offset, &decoded_data.reader_id) ||
      !ReadEntityId(data, submessage_end, &offset, &decoded_data.writer_id)) {
      SetError(error, "RTPS DATA entity ids are truncated");
      return false;
    }

    uint32_t sequence_high = 0u;
    uint32_t sequence_low = 0u;
    if (
      !ReadU32(data, submessage_end, &offset, little_endian, &sequence_high) ||
      !ReadU32(data, submessage_end, &offset, little_endian, &sequence_low)) {
      SetError(error, "RTPS DATA sequence number is truncated");
      return false;
    }
    decoded_data.writer_sequence_number =
      static_cast<int64_t>((static_cast<uint64_t>(sequence_high) << 32u) | sequence_low);

    size_t payload_offset = content_start + 4u + octets_to_inline_qos;
    if (payload_offset > submessage_end) {
      SetError(error, "RTPS DATA payload offset is invalid");
      return false;
    }
    if (has_inline_qos) {
      if (!SkipParameterList(data, submessage_end, &payload_offset, little_endian, error)) {
        return false;
      }
    }
    decoded_data.serialized_payload.assign(data + payload_offset, data + submessage_end);
    submessages->push_back(std::move(decoded_data));
    offset = submessage_end;
  }

  if (submessages->empty()) {
    SetError(error, "RTPS DATA submessage is missing");
    return false;
  }

  *header = decoded_header;
  return true;
}

bool DecodeHeartbeatMessages(
  const uint8_t * data, size_t size, RtpsHeader * header,
  std::vector<HeartbeatSubmessage> * submessages, std::string * error)
{
  if (header == nullptr || submessages == nullptr) {
    SetError(error, "RTPS heartbeat decode output is null");
    return false;
  }
  submessages->clear();

  RtpsHeader decoded_header;
  if (!DecodeRtpsHeader(data, size, &decoded_header, error)) {
    return false;
  }

  size_t offset = kRtpsHeaderSize;
  while (offset < size) {
    if (offset + kSubmessageHeaderSize > size) {
      SetError(error, "RTPS submessage header is truncated");
      return false;
    }

    const uint8_t submessage_id = data[offset++];
    const uint8_t flags = data[offset++];
    const bool little_endian = (flags & kSubmessageFlagLittleEndian) != 0u;
    uint16_t submessage_size = 0u;
    if (!ReadU16(data, size, &offset, little_endian, &submessage_size)) {
      SetError(error, "RTPS HEARTBEAT submessage header is truncated");
      return false;
    }
    const size_t content_start = offset;
    const size_t submessage_end =
      submessage_size == 0u ? size : content_start + submessage_size;
    if (submessage_end > size || submessage_end < content_start) {
      SetError(error, "RTPS submessage size is invalid");
      return false;
    }
    if (submessage_id != kHeartbeatSubmessageId) {
      offset = submessage_end;
      continue;
    }
    if (submessage_size < kHeartbeatSubmessageFixedContentSize) {
      SetError(error, "RTPS HEARTBEAT submessage size is invalid");
      return false;
    }

    HeartbeatSubmessage heartbeat;
    if (
      !ReadEntityId(data, submessage_end, &offset, &heartbeat.reader_id) ||
      !ReadEntityId(data, submessage_end, &offset, &heartbeat.writer_id)) {
      SetError(error, "RTPS HEARTBEAT entity ids are truncated");
      return false;
    }

    uint32_t first_high = 0u;
    uint32_t first_low = 0u;
    uint32_t last_high = 0u;
    uint32_t last_low = 0u;
    uint32_t count = 0u;
    if (
      !ReadU32(data, submessage_end, &offset, little_endian, &first_high) ||
      !ReadU32(data, submessage_end, &offset, little_endian, &first_low) ||
      !ReadU32(data, submessage_end, &offset, little_endian, &last_high) ||
      !ReadU32(data, submessage_end, &offset, little_endian, &last_low) ||
      !ReadU32(data, submessage_end, &offset, little_endian, &count)) {
      SetError(error, "RTPS HEARTBEAT fixed fields are truncated");
      return false;
    }
    heartbeat.first_sequence_number =
      static_cast<int64_t>((static_cast<uint64_t>(first_high) << 32u) | first_low);
    heartbeat.last_sequence_number =
      static_cast<int64_t>((static_cast<uint64_t>(last_high) << 32u) | last_low);
    heartbeat.count = static_cast<int32_t>(count);
    submessages->push_back(heartbeat);
    offset = submessage_end;
  }

  *header = decoded_header;
  return true;
}

}  // namespace rtps
}  // namespace rmw_mdds_cpp
