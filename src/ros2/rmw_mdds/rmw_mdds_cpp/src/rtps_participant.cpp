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

#include "rtps_participant.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <utility>

namespace rmw_mdds_cpp
{
namespace rtps
{
namespace
{
constexpr uint8_t kPlCdrLittleEndianEncapsulation[] = {0x00u, 0x03u, 0x00u, 0x00u};

struct ReceivedDiscoveryDataMessage
{
  RtpsHeader header;
  DataSubmessage data;
  ParameterList parameters;
};

void SetError(std::string * error, const std::string & message)
{
  if (error != nullptr) {
    *error = message;
  }
}

bool MakeIpv4Locator(
  const std::string & address, uint16_t port, Locator * locator, std::string * error)
{
  if (locator == nullptr) {
    SetError(error, "RTPS locator output is null");
    return false;
  }
  if (address.empty()) {
    SetError(error, "RTPS locator address is empty");
    return false;
  }

  in_addr ipv4_address;
  if (inet_pton(AF_INET, address.c_str(), &ipv4_address) != 1) {
    SetError(error, "RTPS locator address is not a valid IPv4 literal");
    return false;
  }

  Locator result;
  result.kind = kLocatorKindUdpV4;
  result.port = port;
  const uint32_t host_order_address = ntohl(ipv4_address.s_addr);
  result.address[12] = static_cast<uint8_t>((host_order_address >> 24u) & 0xffu);
  result.address[13] = static_cast<uint8_t>((host_order_address >> 16u) & 0xffu);
  result.address[14] = static_cast<uint8_t>((host_order_address >> 8u) & 0xffu);
  result.address[15] = static_cast<uint8_t>(host_order_address & 0xffu);
  *locator = result;
  return true;
}

bool ExtractParticipantGuidPrefix(const ParameterList & parameters, GuidPrefix * guid_prefix)
{
  if (guid_prefix == nullptr) {
    return false;
  }
  const auto it = std::find_if(
    parameters.begin(), parameters.end(),
    [](const Parameter & parameter) {
      return parameter.parameter_id == kPidParticipantGuid;
    });
  if (
    it == parameters.end() ||
    it->value.size() != guid_prefix->size() + kEntityIdParticipant.size() ||
    !std::equal(
      kEntityIdParticipant.begin(), kEntityIdParticipant.end(),
      it->value.begin() + static_cast<std::ptrdiff_t>(guid_prefix->size()))) {
    return false;
  }

  std::copy(it->value.begin(), it->value.begin() + guid_prefix->size(), guid_prefix->begin());
  return true;
}

const Parameter * FindParameter(const ParameterList & parameters, uint16_t parameter_id)
{
  const auto it = std::find_if(
    parameters.begin(), parameters.end(),
    [parameter_id](const Parameter & parameter) {
      return parameter.parameter_id == parameter_id;
    });
  return it == parameters.end() ? nullptr : &*it;
}

bool ExtractEndpointGuid(
  const ParameterList & parameters, GuidPrefix * guid_prefix, EntityId * entity_id)
{
  if (guid_prefix == nullptr || entity_id == nullptr) {
    return false;
  }
  const Parameter * endpoint_guid = FindParameter(parameters, kPidEndpointGuid);
  if (endpoint_guid == nullptr) {
    endpoint_guid = FindParameter(parameters, kPidKeyHash);
  }
  if (
    endpoint_guid == nullptr ||
    endpoint_guid->value.size() != guid_prefix->size() + entity_id->size()) {
    return false;
  }

  std::copy(
    endpoint_guid->value.begin(),
    endpoint_guid->value.begin() + static_cast<std::ptrdiff_t>(guid_prefix->size()),
    guid_prefix->begin());
  std::copy(
    endpoint_guid->value.begin() + static_cast<std::ptrdiff_t>(guid_prefix->size()),
    endpoint_guid->value.end(), entity_id->begin());
  return true;
}

uint32_t ReadU32Little(const std::vector<uint8_t> & value, size_t offset)
{
  return static_cast<uint32_t>(value[offset]) |
         (static_cast<uint32_t>(value[offset + 1u]) << 8u) |
         (static_cast<uint32_t>(value[offset + 2u]) << 16u) |
         (static_cast<uint32_t>(value[offset + 3u]) << 24u);
}

bool DecodeCdrStringValue(const Parameter * parameter, std::string * value)
{
  if (parameter == nullptr || value == nullptr || parameter->value.size() < 4u) {
    return false;
  }
  const uint32_t string_size = ReadU32Little(parameter->value, 0u);
  if (
    string_size == 0u ||
    parameter->value.size() < 4u + static_cast<size_t>(string_size) ||
    parameter->value[4u + static_cast<size_t>(string_size) - 1u] != 0u) {
    return false;
  }

  *value = std::string(
    parameter->value.begin() + 4u,
    parameter->value.begin() + 4u + static_cast<std::ptrdiff_t>(string_size) - 1);
  return true;
}

bool ExtractUdpV4Endpoint(
  const Parameter & locator_parameter, const UdpEndpoint & fallback_remote,
  UdpEndpoint * endpoint)
{
  if (endpoint == nullptr || locator_parameter.value.size() != 24u) {
    return false;
  }
  if (static_cast<int32_t>(ReadU32Little(locator_parameter.value, 0u)) != kLocatorKindUdpV4) {
    return false;
  }

  const uint32_t port = ReadU32Little(locator_parameter.value, 4u);
  if (port > static_cast<uint32_t>(std::numeric_limits<uint16_t>::max())) {
    return false;
  }

  const std::array<uint8_t, 4> address = {
    locator_parameter.value[20],
    locator_parameter.value[21],
    locator_parameter.value[22],
    locator_parameter.value[23]};
  const bool unspecified_address =
    address[0] == 0u && address[1] == 0u && address[2] == 0u && address[3] == 0u;

  endpoint->address = unspecified_address ?
    fallback_remote.address :
    std::to_string(address[0]) + "." + std::to_string(address[1]) + "." +
    std::to_string(address[2]) + "." + std::to_string(address[3]);
  endpoint->port = static_cast<uint16_t>(port);
  return !endpoint->address.empty() && endpoint->port != 0u;
}

bool ExtractSedpEndpointKind(const EntityId & writer_id, SedpEndpointKind * endpoint_kind)
{
  if (endpoint_kind == nullptr) {
    return false;
  }
  if (writer_id == kEntityIdSedpBuiltinPublicationsWriter) {
    *endpoint_kind = SedpEndpointKind::kPublication;
    return true;
  }
  if (writer_id == kEntityIdSedpBuiltinSubscriptionsWriter) {
    *endpoint_kind = SedpEndpointKind::kSubscription;
    return true;
  }
  return false;
}

bool ReaderIdForSedpWriter(const EntityId & writer_id, EntityId * reader_id)
{
  if (reader_id == nullptr) {
    return false;
  }
  if (writer_id == kEntityIdSedpBuiltinPublicationsWriter) {
    *reader_id = kEntityIdSedpBuiltinPublicationsReader;
    return true;
  }
  if (writer_id == kEntityIdSedpBuiltinSubscriptionsWriter) {
    *reader_id = kEntityIdSedpBuiltinSubscriptionsReader;
    return true;
  }
  return false;
}

bool DiscoveryDebugEnabled()
{
  return std::getenv("RMW_MDDS_RTPS_DEBUG_DISCOVERY") != nullptr;
}

void PrintEntityId(const char * prefix, const EntityId & entity_id)
{
  if (!DiscoveryDebugEnabled()) {
    return;
  }
  std::fprintf(
    stderr, "%s%02x%02x%02x%02x",
    prefix,
    entity_id[0],
    entity_id[1],
    entity_id[2],
    entity_id[3]);
}

bool EndpointFromParticipantLocator(
  const DiscoveredParticipant & participant, uint16_t locator_parameter_id,
  UdpEndpoint * endpoint)
{
  if (endpoint == nullptr) {
    return false;
  }
  const Parameter * locator = FindParameter(participant.parameters, locator_parameter_id);
  return locator != nullptr && ExtractUdpV4Endpoint(*locator, participant.remote_endpoint, endpoint);
}

bool EndpointForDiscoveredParticipant(
  const DiscoveredParticipant & participant, UdpEndpoint * endpoint)
{
  if (EndpointFromParticipantLocator(participant, kPidMetatrafficUnicastLocator, endpoint)) {
    return true;
  }
  if (endpoint == nullptr) {
    return false;
  }
  *endpoint = participant.remote_endpoint;
  return !endpoint->address.empty() && endpoint->port != 0u;
}

bool UserDataEndpointForDiscoveredParticipant(
  const DiscoveredParticipant & participant, UdpEndpoint * endpoint)
{
  return EndpointFromParticipantLocator(participant, kPidDefaultUnicastLocator, endpoint);
}

bool DecodePlCdrParameterPayload(
  const DataSubmessage & data, ParameterList * parameters, std::string * error)
{
  if (
    data.serialized_payload.size() < sizeof(kPlCdrLittleEndianEncapsulation) ||
    !std::equal(
      std::begin(kPlCdrLittleEndianEncapsulation), std::end(kPlCdrLittleEndianEncapsulation),
      data.serialized_payload.begin())) {
    SetError(error, "RTPS ParameterList payload encapsulation is unsupported");
    return false;
  }

  return DecodeParameterList(
    data.serialized_payload.data() + sizeof(kPlCdrLittleEndianEncapsulation),
    data.serialized_payload.size() - sizeof(kPlCdrLittleEndianEncapsulation),
    parameters, error);
}

bool ReceiveDiscoveryDataMessage(
  const UdpSocket & socket, ReceivedDiscoveryDataMessage * message, UdpEndpoint * remote,
  int timeout_ms, std::string * error)
{
  if (message == nullptr) {
    SetError(error, "RTPS discovery message output is null");
    return false;
  }

  const auto deadline =
    std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(timeout_ms, 0));
  for (;;) {
    int receive_timeout_ms = timeout_ms;
    if (timeout_ms >= 0) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        SetError(error, "UDP receive timed out");
        return false;
      }
      receive_timeout_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
      receive_timeout_ms = std::max(receive_timeout_ms, 1);
    }

    std::vector<uint8_t> packet;
    if (!socket.Receive(&packet, remote, receive_timeout_ms, error)) {
      return false;
    }

    std::vector<DataSubmessage> decoded_data_messages;
    RtpsHeader decoded_header;
    std::string data_error;
    if (DecodeDataMessages(packet.data(), packet.size(), &decoded_header, &decoded_data_messages, &data_error)) {
      ReceivedDiscoveryDataMessage decoded;
      decoded.header = decoded_header;
      decoded.data = std::move(decoded_data_messages.front());
      if (!DecodePlCdrParameterPayload(decoded.data, &decoded.parameters, error)) {
        return false;
      }

      *message = std::move(decoded);
      return true;
    }

    std::vector<HeartbeatSubmessage> heartbeats;
    RtpsHeader heartbeat_header;
    std::string heartbeat_error;
    if (
      DecodeHeartbeatMessages(
        packet.data(), packet.size(), &heartbeat_header, &heartbeats, &heartbeat_error) &&
      !heartbeats.empty()) {
      continue;
    }

    SetError(error, data_error);
    return false;
  }
}

bool ReceiveDiscoveryPacket(
  const UdpSocket & socket, std::vector<ReceivedDiscoveryDataMessage> * messages,
  std::vector<HeartbeatSubmessage> * heartbeats, RtpsHeader * packet_header, UdpEndpoint * remote,
  int timeout_ms, std::string * error)
{
  if (messages == nullptr || heartbeats == nullptr || packet_header == nullptr) {
    SetError(error, "RTPS discovery packet outputs are null");
    return false;
  }
  messages->clear();
  heartbeats->clear();

  std::vector<uint8_t> packet;
  if (!socket.Receive(&packet, remote, timeout_ms, error)) {
    return false;
  }

  RtpsHeader heartbeat_header;
  std::string heartbeat_error;
  if (!DecodeHeartbeatMessages(packet.data(), packet.size(), &heartbeat_header, heartbeats, &heartbeat_error)) {
    SetError(error, heartbeat_error);
    return false;
  }
  *packet_header = heartbeat_header;

  RtpsHeader decoded_header;
  std::vector<DataSubmessage> decoded_data_messages;
  std::string data_error;
  if (!DecodeDataMessages(packet.data(), packet.size(), &decoded_header, &decoded_data_messages, &data_error)) {
    if (heartbeats->empty()) {
      SetError(error, data_error);
      return false;
    }
    return true;
  }

  messages->reserve(decoded_data_messages.size());
  for (auto & decoded_data : decoded_data_messages) {
    ReceivedDiscoveryDataMessage decoded;
    decoded.header = decoded_header;
    decoded.data = std::move(decoded_data);
    if (!DecodePlCdrParameterPayload(decoded.data, &decoded.parameters, error)) {
      return false;
    }
    messages->push_back(std::move(decoded));
  }
  return true;
}

bool BuildDiscoveredSedpEndpoint(
  const ReceivedSedpEndpointAnnouncement & announcement, const UdpEndpoint & remote,
  DiscoveredSedpEndpoint * discovered)
{
  if (discovered == nullptr) {
    return false;
  }

  DiscoveredSedpEndpoint endpoint;
  if (
    !ExtractEndpointGuid(
      announcement.parameters, &endpoint.participant_guid_prefix, &endpoint.endpoint_entity_id) ||
    !ExtractSedpEndpointKind(announcement.data.writer_id, &endpoint.endpoint_kind) ||
    !DecodeCdrStringValue(FindParameter(announcement.parameters, kPidTopicName), &endpoint.topic_name) ||
    !DecodeCdrStringValue(FindParameter(announcement.parameters, kPidTypeName), &endpoint.type_name)) {
    return false;
  }

  endpoint.remote_endpoint = remote;
  endpoint.header = announcement.header;
  endpoint.parameters = announcement.parameters;
  endpoint.last_sedp_sequence_number = announcement.data.writer_sequence_number;
  *discovered = std::move(endpoint);
  return true;
}
}  // namespace

std::unique_ptr<RtpsParticipant> RtpsParticipant::Create(
  const ParticipantConfig & config_in, std::string * error)
{
  // Advance the participant id until both unicast ports are free. Two RMW
  // contexts in one process otherwise share options.instance_id -> identical
  // participant id -> identical ports + identical GUID; with exclusive binds
  // the collision surfaces here and we pick the next free slot.
  ParticipantConfig config = config_in;
  RtpsPorts ports;
  UdpSocket metatraffic_socket;
  UdpSocket user_socket;
  constexpr uint32_t kMaxParticipantIdScan = 120u;
  const uint32_t base_participant_id = config_in.participant_id;
  const bool guid_prefix_tracks_participant_id =
    config.guid_prefix[10] == static_cast<uint8_t>((base_participant_id >> 8u) & 0xffu) &&
    config.guid_prefix[11] == static_cast<uint8_t>(base_participant_id & 0xffu);
  bool bound = false;
  std::string last_error;
  for (uint32_t offset = 0u; offset < kMaxParticipantIdScan; ++offset) {
    const uint32_t pid = base_participant_id + offset;
    if (!CalculateRtpsPorts(config_in.port_mapping, config_in.domain_id, pid, &ports, &last_error)) {
      break;
    }
    UdpSocket meta = UdpSocket::Bind(config_in.bind_address, ports.metatraffic_unicast, &last_error);
    if (!meta) {
      continue;
    }
    UdpSocket usr = UdpSocket::Bind(config_in.bind_address, ports.user_unicast, &last_error);
    if (!usr) {
      continue;  // metatraffic bound but user port taken: try next id
    }
    metatraffic_socket = std::move(meta);
    user_socket = std::move(usr);
    config.participant_id = pid;
    if (guid_prefix_tracks_participant_id) {
      config.guid_prefix[10] = static_cast<uint8_t>((pid >> 8u) & 0xffu);
      config.guid_prefix[11] = static_cast<uint8_t>(pid & 0xffu);
    }
    bound = true;
    break;
  }
  if (!bound) {
    SetError(error, last_error.empty() ? "RTPS participant could not bind a free port slot" : last_error);
    return nullptr;
  }
  if (ports.metatraffic_unicast == 0u) {
    ports.metatraffic_unicast = metatraffic_socket.local_port();
  }
  if (ports.user_unicast == 0u) {
    ports.user_unicast = user_socket.local_port();
  }

  Locator metatraffic_locator;
  if (!MakeIpv4Locator(
      config.advertised_address, ports.metatraffic_unicast, &metatraffic_locator, error)) {
    return nullptr;
  }
  Locator default_locator;
  if (!MakeIpv4Locator(config.advertised_address, ports.user_unicast, &default_locator, error)) {
    return nullptr;
  }

  return std::unique_ptr<RtpsParticipant>(
    new RtpsParticipant(
      config, ports, metatraffic_locator, default_locator, std::move(metatraffic_socket),
      std::move(user_socket)));
}

RtpsParticipant::RtpsParticipant(
  const ParticipantConfig & config, const RtpsPorts & ports,
  const Locator & metatraffic_unicast_locator, const Locator & default_unicast_locator,
  UdpSocket metatraffic_unicast_socket, UdpSocket user_unicast_socket)
  : config_(config),
    ports_(ports),
    metatraffic_unicast_locator_(metatraffic_unicast_locator),
    default_unicast_locator_(default_unicast_locator),
    metatraffic_unicast_socket_(std::move(metatraffic_unicast_socket)),
    user_unicast_socket_(std::move(user_unicast_socket))
{
}

RtpsParticipant::~RtpsParticipant()
{
  StopSpdpReceiver();
  StopSpdpAnnouncer();
}

const GuidPrefix & RtpsParticipant::guid_prefix() const
{
  return config_.guid_prefix;
}

const RtpsPorts & RtpsParticipant::ports() const
{
  return ports_;
}

uint16_t RtpsParticipant::local_metatraffic_unicast_port() const
{
  return metatraffic_unicast_socket_.local_port();
}

uint16_t RtpsParticipant::local_user_unicast_port() const
{
  return user_unicast_socket_.local_port();
}

std::vector<uint8_t> RtpsParticipant::BuildSpdpAnnouncement(
  int64_t writer_sequence_number) const
{
  RtpsHeader header;
  header.protocol_version = config_.protocol_version;
  header.vendor_id = config_.vendor_id;
  header.guid_prefix = config_.guid_prefix;

  ParticipantAnnouncement announcement;
  announcement.participant_guid_prefix = config_.guid_prefix;
  announcement.protocol_version = config_.protocol_version;
  announcement.vendor_id = config_.vendor_id;
  announcement.builtin_endpoint_set = config_.builtin_endpoint_set;
  announcement.metatraffic_unicast_locator = metatraffic_unicast_locator_;
  announcement.default_unicast_locator = default_unicast_locator_;

  return EncodeDataMessage(
    header, EncodeSpdpParticipantData(announcement, writer_sequence_number));
}

std::vector<uint8_t> RtpsParticipant::BuildSedpEndpointAnnouncement(
  const SedpEndpointAnnouncement & announcement, int64_t writer_sequence_number) const
{
  RtpsHeader header;
  header.protocol_version = config_.protocol_version;
  header.vendor_id = config_.vendor_id;
  header.guid_prefix = config_.guid_prefix;

  SedpEndpointAnnouncement local_announcement = announcement;
  local_announcement.participant_guid_prefix = config_.guid_prefix;
  local_announcement.protocol_version = config_.protocol_version;
  local_announcement.vendor_id = config_.vendor_id;
  local_announcement.unicast_locator = default_unicast_locator_;
  return EncodeDataMessage(
    header, EncodeSedpEndpointData(local_announcement, writer_sequence_number));
}

std::vector<uint8_t> RtpsParticipant::BuildUserDataMessage(
  const EntityId & reader_id, const EntityId & writer_id,
  const std::vector<uint8_t> & serialized_payload, int64_t writer_sequence_number) const
{
  RtpsHeader header;
  header.protocol_version = config_.protocol_version;
  header.vendor_id = config_.vendor_id;
  header.guid_prefix = config_.guid_prefix;

  DataSubmessage data;
  data.reader_id = reader_id;
  data.writer_id = writer_id;
  data.writer_sequence_number = writer_sequence_number;
  data.serialized_payload = serialized_payload;
  return EncodeDataMessage(header, data);
}

bool RtpsParticipant::SendSpdpAnnouncement(
  const UdpEndpoint & endpoint, std::string * error)
{
  std::lock_guard<std::mutex> lock(spdp_send_mutex_);
  const std::vector<uint8_t> packet = BuildSpdpAnnouncement(next_spdp_sequence_number_++);
  return metatraffic_unicast_socket_.SendTo(packet.data(), packet.size(), endpoint, error);
}

bool RtpsParticipant::SendSedpEndpointAnnouncement(
  const UdpEndpoint & endpoint, const SedpEndpointAnnouncement & announcement,
  std::string * error)
{
  std::lock_guard<std::mutex> lock(sedp_send_mutex_);
  int64_t & next_sequence_number =
    announcement.endpoint_kind == SedpEndpointKind::kPublication ?
    next_sedp_publication_sequence_number_ :
    next_sedp_subscription_sequence_number_;
  const int64_t writer_sequence_number = next_sequence_number++;
  const std::vector<uint8_t> packet =
    BuildSedpEndpointAnnouncement(announcement, writer_sequence_number);
  if (DiscoveryDebugEnabled()) {
    std::fprintf(
      stderr,
      "[mdds-discovery] send sedp kind=%s topic=%s target=%s:%u size=%zu ",
      announcement.endpoint_kind == SedpEndpointKind::kPublication ? "publication" : "subscription",
      announcement.topic_name.c_str(), endpoint.address.c_str(), endpoint.port, packet.size());
    PrintEntityId("endpoint=", announcement.endpoint_entity_id);
    std::fprintf(stderr, "\n");
  }
  if (!metatraffic_unicast_socket_.SendTo(packet.data(), packet.size(), endpoint, error)) {
    return false;
  }

  RtpsHeader header;
  header.protocol_version = config_.protocol_version;
  header.vendor_id = config_.vendor_id;
  header.guid_prefix = config_.guid_prefix;

  HeartbeatSubmessage heartbeat;
  if (announcement.endpoint_kind == SedpEndpointKind::kPublication) {
    heartbeat.reader_id = kEntityIdSedpBuiltinPublicationsReader;
    heartbeat.writer_id = kEntityIdSedpBuiltinPublicationsWriter;
  } else {
    heartbeat.reader_id = kEntityIdSedpBuiltinSubscriptionsReader;
    heartbeat.writer_id = kEntityIdSedpBuiltinSubscriptionsWriter;
  }
  heartbeat.first_sequence_number = writer_sequence_number;
  heartbeat.last_sequence_number = writer_sequence_number;
  heartbeat.count = next_sedp_heartbeat_count_++;

  const std::vector<uint8_t> heartbeat_packet = EncodeHeartbeatMessage(header, heartbeat);
  return metatraffic_unicast_socket_.SendTo(
    heartbeat_packet.data(), heartbeat_packet.size(), endpoint, error);
}

bool RtpsParticipant::SendUserDataMessage(
  const UdpEndpoint & endpoint, const EntityId & reader_id, const EntityId & writer_id,
  const std::vector<uint8_t> & serialized_payload, int64_t writer_sequence_number,
  std::string * error)
{
  const std::vector<uint8_t> packet =
    BuildUserDataMessage(reader_id, writer_id, serialized_payload, writer_sequence_number);
  if (packet.empty() && !serialized_payload.empty()) {
    SetError(error, "RTPS user DATA packet is too large");
    return false;
  }
  return user_unicast_socket_.SendTo(packet.data(), packet.size(), endpoint, error);
}

bool RtpsParticipant::SendAckNackForHeartbeat(
  const HeartbeatSubmessage & heartbeat, const GuidPrefix & remote_guid_prefix,
  const UdpEndpoint & remote)
{
  EntityId reader_id;
  if (!ReaderIdForSedpWriter(heartbeat.writer_id, &reader_id)) {
    return false;
  }
  const int64_t bitmap_base = std::max<int64_t>(heartbeat.first_sequence_number, 1);
  if (heartbeat.last_sequence_number < bitmap_base) {
    return false;
  }

  const uint64_t missing_count =
    static_cast<uint64_t>(heartbeat.last_sequence_number - bitmap_base) + 1u;
  const uint32_t num_bits = static_cast<uint32_t>(std::min<uint64_t>(missing_count, 32u));
  uint32_t bitmap_word = 0u;
  for (uint32_t bit = 0u; bit < num_bits; ++bit) {
    bitmap_word |= 0x80000000u >> bit;
  }

  RtpsHeader header;
  header.protocol_version = config_.protocol_version;
  header.vendor_id = config_.vendor_id;
  header.guid_prefix = config_.guid_prefix;

  AckNackSubmessage acknack;
  acknack.reader_id = reader_id;
  acknack.writer_id = heartbeat.writer_id;
  acknack.bitmap_base = bitmap_base;
  acknack.num_bits = num_bits;
  acknack.bitmap = {bitmap_word};

  std::lock_guard<std::mutex> lock(acknack_send_mutex_);
  acknack.count = next_acknack_count_++;
  const std::vector<uint8_t> packet = EncodeAckNackMessage(header, acknack);
  if (packet.empty()) {
    return false;
  }

  std::vector<UdpEndpoint> targets;
  const auto participants = GetDiscoveredParticipants();
  const auto participant = std::find_if(
    participants.begin(), participants.end(),
    [&remote_guid_prefix](const DiscoveredParticipant & discovered_participant) {
      return discovered_participant.guid_prefix == remote_guid_prefix;
    });
  if (participant != participants.end()) {
    UdpEndpoint participant_endpoint;
    if (EndpointForDiscoveredParticipant(*participant, &participant_endpoint)) {
      targets.push_back(participant_endpoint);
    }
  }
  const auto same_remote = [&remote](const UdpEndpoint & endpoint) {
    return endpoint.address == remote.address && endpoint.port == remote.port;
  };
  if (std::find_if(targets.begin(), targets.end(), same_remote) == targets.end()) {
    targets.push_back(remote);
  }

  bool sent = false;
  std::string last_error;
  for (const auto & target : targets) {
    std::string send_error;
    const bool target_sent =
      metatraffic_unicast_socket_.SendTo(packet.data(), packet.size(), target, &send_error);
    sent = sent || target_sent;
    if (!send_error.empty()) {
      last_error = send_error;
    }
  }
  if (DiscoveryDebugEnabled()) {
    PrintEntityId("[mdds-discovery] acknack writer=", heartbeat.writer_id);
    std::fprintf(
      stderr,
      " base=%lld bits=%u targets=%zu source=%s:%u sent=%d%s%s\n",
      static_cast<long long>(bitmap_base), num_bits, targets.size(), remote.address.c_str(), remote.port,
      sent ? 1 : 0,
      last_error.empty() ? "" : " error=",
      last_error.empty() ? "" : last_error.c_str());
  }
  return sent;
}

size_t RtpsParticipant::RegisterSedpEndpointAnnouncement(
  const SedpEndpointAnnouncement & announcement, std::string * error)
{
  SedpEndpointAnnouncement local_announcement = announcement;
  local_announcement.participant_guid_prefix = config_.guid_prefix;
  local_announcement.protocol_version = config_.protocol_version;
  local_announcement.vendor_id = config_.vendor_id;
  local_announcement.unicast_locator = default_unicast_locator_;
  {
    std::lock_guard<std::mutex> lock(local_sedp_endpoints_mutex_);
    const auto it = std::find_if(
      local_sedp_endpoints_.begin(), local_sedp_endpoints_.end(),
      [&local_announcement](const SedpEndpointAnnouncement & stored) {
        return stored.endpoint_entity_id == local_announcement.endpoint_entity_id;
      });
    if (it == local_sedp_endpoints_.end()) {
      local_sedp_endpoints_.push_back(local_announcement);
    } else {
      *it = local_announcement;
    }
  }
  return AnnounceSedpEndpointToDiscoveredParticipants(local_announcement, error);
}

void RtpsParticipant::UnregisterSedpEndpointAnnouncement(const EntityId & endpoint_entity_id)
{
  std::lock_guard<std::mutex> lock(local_sedp_endpoints_mutex_);
  local_sedp_endpoints_.erase(
    std::remove_if(
      local_sedp_endpoints_.begin(), local_sedp_endpoints_.end(),
      [&endpoint_entity_id](const SedpEndpointAnnouncement & stored) {
        return stored.endpoint_entity_id == endpoint_entity_id;
      }),
    local_sedp_endpoints_.end());
}

size_t RtpsParticipant::AnnounceSedpEndpointToDiscoveredParticipants(
  const SedpEndpointAnnouncement & announcement, std::string * error)
{
  const auto participants = GetDiscoveredParticipants();
  size_t sent_count = 0u;
  std::string last_error;
  for (const auto & participant : participants) {
    UdpEndpoint endpoint;
    if (!EndpointForDiscoveredParticipant(participant, &endpoint)) {
      last_error = "discovered participant has no usable metatraffic endpoint";
      continue;
    }
    std::string send_error;
    if (SendSedpEndpointAnnouncement(endpoint, announcement, &send_error)) {
      ++sent_count;
    } else {
      last_error = send_error;
    }
  }
  if (sent_count == 0u && !participants.empty() && !last_error.empty()) {
    SetError(error, last_error);
  }
  return sent_count;
}

bool RtpsParticipant::StartSpdpAnnouncer(
  const std::vector<UdpEndpoint> & endpoints, uint32_t period_ms, std::string * error)
{
  if (endpoints.empty()) {
    SetError(error, "SPDP announcer endpoints are empty");
    return false;
  }
  if (period_ms == 0u) {
    SetError(error, "SPDP announcer period is zero");
    return false;
  }

  std::lock_guard<std::mutex> lock(spdp_announcer_mutex_);
  if (spdp_announcer_running_) {
    SetError(error, "SPDP announcer is already running");
    return false;
  }
  spdp_announcer_running_ = true;

  try {
    spdp_announcer_thread_ = std::thread(
      [this, endpoints, period_ms]() {
        for (;;) {
          for (const auto & endpoint : endpoints) {
            {
              std::lock_guard<std::mutex> lock(spdp_announcer_mutex_);
              if (!spdp_announcer_running_) {
                return;
              }
            }
            std::string ignored_error;
            (void)SendSpdpAnnouncement(endpoint, &ignored_error);
          }

          std::unique_lock<std::mutex> lock(spdp_announcer_mutex_);
          if (
            spdp_announcer_cv_.wait_for(
              lock, std::chrono::milliseconds(period_ms),
              [this]() { return !spdp_announcer_running_; })) {
            return;
          }
        }
      });
  } catch (const std::exception & e) {
    spdp_announcer_running_ = false;
    SetError(error, std::string("failed to start SPDP announcer thread: ") + e.what());
    return false;
  }
  return true;
}

void RtpsParticipant::StopSpdpAnnouncer()
{
  const bool should_join = spdp_announcer_thread_.joinable();
  {
    std::lock_guard<std::mutex> lock(spdp_announcer_mutex_);
    if (!spdp_announcer_running_ && !should_join) {
      return;
    }
    spdp_announcer_running_ = false;
  }
  spdp_announcer_cv_.notify_all();
  if (should_join) {
    spdp_announcer_thread_.join();
  }
}

bool RtpsParticipant::StartSpdpReceiver(uint32_t timeout_ms, std::string * error)
{
  if (timeout_ms == 0u) {
    SetError(error, "SPDP receiver timeout is zero");
    return false;
  }
  if (timeout_ms > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    SetError(error, "SPDP receiver timeout exceeds poll timeout range");
    return false;
  }

  std::lock_guard<std::mutex> lock(spdp_receiver_mutex_);
  if (spdp_receiver_running_) {
    SetError(error, "SPDP receiver is already running");
    return false;
  }
  spdp_receiver_running_ = true;

  try {
    spdp_receiver_thread_ = std::thread(
      [this, timeout_ms]() {
        for (;;) {
          {
            std::lock_guard<std::mutex> lock(spdp_receiver_mutex_);
            if (!spdp_receiver_running_) {
              return;
            }
          }

          std::vector<ReceivedDiscoveryDataMessage> messages;
          std::vector<HeartbeatSubmessage> heartbeats;
          RtpsHeader packet_header;
          UdpEndpoint remote;
          std::string ignored_error;
          if (!ReceiveDiscoveryPacket(
              metatraffic_unicast_socket_, &messages, &heartbeats, &packet_header, &remote,
              static_cast<int>(timeout_ms), &ignored_error)) {
            if (DiscoveryDebugEnabled() && ignored_error != "UDP receive timed out") {
              std::fprintf(stderr, "[mdds-discovery] receive failed: %s\n", ignored_error.c_str());
            }
            continue;
          }
          if (DiscoveryDebugEnabled()) {
            std::fprintf(
              stderr,
              "[mdds-discovery] received %zu discovery DATA message(s), %zu HEARTBEAT message(s) from %s:%u\n",
              messages.size(), heartbeats.size(), remote.address.c_str(), remote.port);
          }
          for (const auto & heartbeat : heartbeats) {
            (void)SendAckNackForHeartbeat(heartbeat, packet_header.guid_prefix, remote);
          }
          for (auto & message : messages) {
            if (message.data.writer_id == kEntityIdSpdpBuiltinParticipantWriter) {
              ReceivedSpdpAnnouncement announcement;
              announcement.header = std::move(message.header);
              announcement.data = std::move(message.data);
              announcement.parameters = std::move(message.parameters);
              StoreDiscoveredParticipant(announcement, remote);
            } else if (
              message.data.writer_id == kEntityIdSedpBuiltinPublicationsWriter ||
              message.data.writer_id == kEntityIdSedpBuiltinSubscriptionsWriter) {
              ReceivedSedpEndpointAnnouncement announcement;
              announcement.header = std::move(message.header);
              announcement.data = std::move(message.data);
              announcement.parameters = std::move(message.parameters);
              StoreDiscoveredSedpEndpoint(announcement, remote);
            } else if (DiscoveryDebugEnabled()) {
              PrintEntityId("[mdds-discovery] ignored discovery writer=", message.data.writer_id);
              std::fprintf(stderr, "\n");
            }
          }
        }
      });
  } catch (const std::exception & e) {
    spdp_receiver_running_ = false;
    SetError(error, std::string("failed to start SPDP receiver thread: ") + e.what());
    return false;
  }
  return true;
}

void RtpsParticipant::StopSpdpReceiver()
{
  const bool should_join = spdp_receiver_thread_.joinable();
  {
    std::lock_guard<std::mutex> lock(spdp_receiver_mutex_);
    spdp_receiver_running_ = false;
  }
  if (should_join) {
    spdp_receiver_thread_.join();
  }
}

bool RtpsParticipant::ReceiveSpdpAnnouncement(
  ReceivedSpdpAnnouncement * announcement, UdpEndpoint * remote, int timeout_ms,
  std::string * error) const
{
  if (announcement == nullptr) {
    SetError(error, "SPDP announcement output is null");
    return false;
  }

  ReceivedDiscoveryDataMessage decoded;
  if (!ReceiveDiscoveryDataMessage(
      metatraffic_unicast_socket_, &decoded, remote, timeout_ms, error)) {
    return false;
  }
  if (decoded.data.writer_id != kEntityIdSpdpBuiltinParticipantWriter) {
    SetError(error, "RTPS DATA submessage is not an SPDP participant announcement");
    return false;
  }

  announcement->header = std::move(decoded.header);
  announcement->data = std::move(decoded.data);
  announcement->parameters = std::move(decoded.parameters);
  return true;
}

bool RtpsParticipant::ReceiveSedpEndpointAnnouncement(
  ReceivedSedpEndpointAnnouncement * announcement, UdpEndpoint * remote, int timeout_ms,
  std::string * error) const
{
  if (announcement == nullptr) {
    SetError(error, "SEDP endpoint announcement output is null");
    return false;
  }

  ReceivedDiscoveryDataMessage decoded;
  if (!ReceiveDiscoveryDataMessage(
      metatraffic_unicast_socket_, &decoded, remote, timeout_ms, error)) {
    return false;
  }
  if (
    decoded.data.writer_id != kEntityIdSedpBuiltinPublicationsWriter &&
    decoded.data.writer_id != kEntityIdSedpBuiltinSubscriptionsWriter) {
    SetError(error, "RTPS DATA submessage is not an SEDP endpoint announcement");
    return false;
  }

  announcement->header = std::move(decoded.header);
  announcement->data = std::move(decoded.data);
  announcement->parameters = std::move(decoded.parameters);
  return true;
}

bool RtpsParticipant::ReceiveUserDataMessage(
  ReceivedUserDataMessage * message, UdpEndpoint * remote, int timeout_ms,
  std::string * error) const
{
  if (message == nullptr) {
    SetError(error, "RTPS user DATA message output is null");
    return false;
  }

  std::vector<uint8_t> packet;
  if (!user_unicast_socket_.Receive(&packet, remote, timeout_ms, error)) {
    return false;
  }

  ReceivedUserDataMessage decoded;
  if (!DecodeDataMessage(packet.data(), packet.size(), &decoded.header, &decoded.data, error)) {
    return false;
  }

  *message = std::move(decoded);
  return true;
}

std::vector<DiscoveredParticipant> RtpsParticipant::GetDiscoveredParticipants() const
{
  std::lock_guard<std::mutex> lock(discovered_participants_mutex_);
  return discovered_participants_;
}

std::vector<DiscoveredSedpEndpoint> RtpsParticipant::GetDiscoveredSedpEndpoints() const
{
  std::lock_guard<std::mutex> lock(discovered_sedp_endpoints_mutex_);
  return discovered_sedp_endpoints_;
}

std::vector<MatchedRemoteSedpEndpoint> RtpsParticipant::GetMatchedRemoteSedpEndpoints(
  const std::string & topic_name, const std::string & type_name,
  SedpEndpointKind endpoint_kind) const
{
  const auto endpoints = GetDiscoveredSedpEndpoints();
  const auto participants = GetDiscoveredParticipants();
  std::vector<MatchedRemoteSedpEndpoint> matches;
  for (const auto & endpoint : endpoints) {
    if (
      endpoint.endpoint_kind != endpoint_kind ||
      endpoint.topic_name != topic_name ||
      endpoint.type_name != type_name) {
      continue;
    }

    const auto participant = std::find_if(
      participants.begin(), participants.end(),
      [&endpoint](const DiscoveredParticipant & discovered_participant) {
        return discovered_participant.guid_prefix == endpoint.participant_guid_prefix;
      });
    if (participant == participants.end()) {
      continue;
    }

    UdpEndpoint user_data_endpoint;
    if (!UserDataEndpointForDiscoveredParticipant(*participant, &user_data_endpoint)) {
      continue;
    }
    matches.push_back(
      MatchedRemoteSedpEndpoint{
        endpoint.participant_guid_prefix, endpoint.endpoint_entity_id, user_data_endpoint});
  }
  return matches;
}

void RtpsParticipant::StoreDiscoveredParticipant(
  const ReceivedSpdpAnnouncement & announcement, const UdpEndpoint & remote)
{
  GuidPrefix guid_prefix;
  if (!ExtractParticipantGuidPrefix(announcement.parameters, &guid_prefix)) {
    guid_prefix = announcement.header.guid_prefix;
  }
  if (guid_prefix == config_.guid_prefix) {
    return;
  }
  if (DiscoveryDebugEnabled()) {
    std::fprintf(
      stderr,
      "[mdds-discovery] participant remote=%s:%u params=%zu\n",
      remote.address.c_str(), remote.port, announcement.parameters.size());
  }

  DiscoveredParticipant discovered;
  discovered.guid_prefix = guid_prefix;
  discovered.remote_endpoint = remote;
  discovered.header = announcement.header;
  discovered.parameters = announcement.parameters;
  discovered.last_spdp_sequence_number = announcement.data.writer_sequence_number;

  DiscoveredParticipant participant_for_replay;
  {
    std::lock_guard<std::mutex> lock(discovered_participants_mutex_);
    const auto it = std::find_if(
      discovered_participants_.begin(), discovered_participants_.end(),
      [&guid_prefix](const DiscoveredParticipant & participant) {
        return participant.guid_prefix == guid_prefix;
      });
    if (it == discovered_participants_.end()) {
      discovered_participants_.push_back(discovered);
      participant_for_replay = discovered_participants_.back();
    } else {
      *it = discovered;
      participant_for_replay = *it;
    }
  }
  ReplaySedpEndpointsToParticipant(participant_for_replay);
}

void RtpsParticipant::StoreDiscoveredSedpEndpoint(
  const ReceivedSedpEndpointAnnouncement & announcement, const UdpEndpoint & remote)
{
  DiscoveredSedpEndpoint discovered;
  if (!BuildDiscoveredSedpEndpoint(announcement, remote, &discovered)) {
    if (DiscoveryDebugEnabled()) {
      PrintEntityId("[mdds-discovery] rejected sedp writer=", announcement.data.writer_id);
      std::fprintf(stderr, " params=%zu\n", announcement.parameters.size());
    }
    return;
  }
  if (DiscoveryDebugEnabled()) {
    std::fprintf(
      stderr,
      "[mdds-discovery] sedp kind=%s topic=%s type=%s remote=%s:%u\n",
      discovered.endpoint_kind == SedpEndpointKind::kPublication ? "publication" : "subscription",
      discovered.topic_name.c_str(), discovered.type_name.c_str(),
      discovered.remote_endpoint.address.c_str(), discovered.remote_endpoint.port);
  }
  if (discovered.participant_guid_prefix == config_.guid_prefix) {
    return;
  }

  std::lock_guard<std::mutex> lock(discovered_sedp_endpoints_mutex_);
  const auto it = std::find_if(
    discovered_sedp_endpoints_.begin(), discovered_sedp_endpoints_.end(),
    [&discovered](const DiscoveredSedpEndpoint & endpoint) {
      return endpoint.participant_guid_prefix == discovered.participant_guid_prefix &&
             endpoint.endpoint_entity_id == discovered.endpoint_entity_id;
    });
  if (it == discovered_sedp_endpoints_.end()) {
    discovered_sedp_endpoints_.push_back(std::move(discovered));
  } else {
    *it = std::move(discovered);
  }
}

void RtpsParticipant::ReplaySedpEndpointsToParticipant(const DiscoveredParticipant & participant)
{
  UdpEndpoint endpoint;
  if (!EndpointForDiscoveredParticipant(participant, &endpoint)) {
    return;
  }

  std::vector<SedpEndpointAnnouncement> announcements;
  {
    std::lock_guard<std::mutex> lock(local_sedp_endpoints_mutex_);
    announcements = local_sedp_endpoints_;
  }
  for (const auto & announcement : announcements) {
    std::string ignored_error;
    (void)SendSedpEndpointAnnouncement(endpoint, announcement, &ignored_error);
  }
}

}  // namespace rtps
}  // namespace rmw_mdds_cpp
