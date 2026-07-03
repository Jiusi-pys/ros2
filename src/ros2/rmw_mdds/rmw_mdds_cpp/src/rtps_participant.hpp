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

#ifndef RMW_MDDS_CPP_SRC__RTPS_PARTICIPANT_HPP_
#define RMW_MDDS_CPP_SRC__RTPS_PARTICIPANT_HPP_

#include <array>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rtps_protocol.hpp"
#include "rtps_transport.hpp"

namespace rmw_mdds_cpp
{
namespace rtps
{

struct ParticipantConfig
{
  uint32_t domain_id = 0;
  uint32_t participant_id = 0;
  GuidPrefix guid_prefix{};
  std::array<uint8_t, 2> protocol_version{2u, 3u};
  std::array<uint8_t, 2> vendor_id{0u, 0u};
  uint32_t builtin_endpoint_set =
    kBuiltinEndpointParticipantAnnouncer |
    kBuiltinEndpointParticipantDetector |
    kBuiltinEndpointPublicationAnnouncer |
    kBuiltinEndpointPublicationDetector |
    kBuiltinEndpointSubscriptionAnnouncer |
    kBuiltinEndpointSubscriptionDetector;
  RtpsPortMapping port_mapping;
  std::string bind_address = "0.0.0.0";
  std::string advertised_address = "0.0.0.0";
  std::vector<UdpEndpoint> spdp_peer_endpoints;
  uint32_t spdp_announcement_period_ms = 30000;
  uint32_t spdp_receive_timeout_ms = 20;
};

struct ReceivedSpdpAnnouncement
{
  RtpsHeader header;
  DataSubmessage data;
  ParameterList parameters;
};

struct ReceivedSedpEndpointAnnouncement
{
  RtpsHeader header;
  DataSubmessage data;
  ParameterList parameters;
};

struct ReceivedUserDataMessage
{
  RtpsHeader header;
  DataSubmessage data;
};

struct DiscoveredParticipant
{
  GuidPrefix guid_prefix{};
  UdpEndpoint remote_endpoint;
  RtpsHeader header;
  ParameterList parameters;
  int64_t last_spdp_sequence_number = 0;
};

struct DiscoveredSedpEndpoint
{
  GuidPrefix participant_guid_prefix{};
  EntityId endpoint_entity_id{};
  SedpEndpointKind endpoint_kind = SedpEndpointKind::kPublication;
  std::string topic_name;
  std::string type_name;
  UdpEndpoint remote_endpoint;
  RtpsHeader header;
  ParameterList parameters;
  int64_t last_sedp_sequence_number = 0;
};

struct MatchedRemoteSedpEndpoint
{
  GuidPrefix participant_guid_prefix{};
  EntityId endpoint_entity_id{};
  UdpEndpoint user_data_endpoint;
};

class RtpsParticipant
{
public:
  static std::unique_ptr<RtpsParticipant> Create(
    const ParticipantConfig & config, std::string * error);
  ~RtpsParticipant();

  RtpsParticipant(const RtpsParticipant &) = delete;
  RtpsParticipant & operator=(const RtpsParticipant &) = delete;

  const GuidPrefix & guid_prefix() const;
  const RtpsPorts & ports() const;
  uint16_t local_metatraffic_unicast_port() const;
  uint16_t local_user_unicast_port() const;

  std::vector<uint8_t> BuildSpdpAnnouncement(int64_t writer_sequence_number) const;
  std::vector<uint8_t> BuildSedpEndpointAnnouncement(
    const SedpEndpointAnnouncement & announcement, int64_t writer_sequence_number) const;
  std::vector<uint8_t> BuildUserDataMessage(
    const EntityId & reader_id, const EntityId & writer_id,
    const std::vector<uint8_t> & serialized_payload, int64_t writer_sequence_number) const;
  bool SendSpdpAnnouncement(const UdpEndpoint & endpoint, std::string * error);
  bool SendSedpEndpointAnnouncement(
    const UdpEndpoint & endpoint, const SedpEndpointAnnouncement & announcement,
    std::string * error);
  bool SendUserDataMessage(
    const UdpEndpoint & endpoint, const EntityId & reader_id, const EntityId & writer_id,
    const std::vector<uint8_t> & serialized_payload, int64_t writer_sequence_number,
    std::string * error);
  size_t RegisterSedpEndpointAnnouncement(
    const SedpEndpointAnnouncement & announcement, std::string * error);
  void UnregisterSedpEndpointAnnouncement(const EntityId & endpoint_entity_id);
  size_t AnnounceSedpEndpointToDiscoveredParticipants(
    const SedpEndpointAnnouncement & announcement, std::string * error);
  bool StartSpdpAnnouncer(
    const std::vector<UdpEndpoint> & endpoints, uint32_t period_ms, std::string * error);
  void StopSpdpAnnouncer();
  bool StartSpdpReceiver(uint32_t timeout_ms, std::string * error);
  void StopSpdpReceiver();
  bool ReceiveSpdpAnnouncement(
    ReceivedSpdpAnnouncement * announcement, UdpEndpoint * remote, int timeout_ms,
    std::string * error) const;
  bool ReceiveSedpEndpointAnnouncement(
    ReceivedSedpEndpointAnnouncement * announcement, UdpEndpoint * remote, int timeout_ms,
    std::string * error) const;
  bool ReceiveUserDataMessage(
    ReceivedUserDataMessage * message, UdpEndpoint * remote, int timeout_ms,
    std::string * error) const;
  std::vector<DiscoveredParticipant> GetDiscoveredParticipants() const;
  std::vector<DiscoveredSedpEndpoint> GetDiscoveredSedpEndpoints() const;
  std::vector<MatchedRemoteSedpEndpoint> GetMatchedRemoteSedpEndpoints(
    const std::string & topic_name, const std::string & type_name,
    SedpEndpointKind endpoint_kind) const;

private:
  RtpsParticipant(
    const ParticipantConfig & config, const RtpsPorts & ports,
    const Locator & metatraffic_unicast_locator, const Locator & default_unicast_locator,
    UdpSocket metatraffic_unicast_socket, UdpSocket user_unicast_socket);
  void StoreDiscoveredParticipant(
    const ReceivedSpdpAnnouncement & announcement, const UdpEndpoint & remote);
  void StoreDiscoveredSedpEndpoint(
    const ReceivedSedpEndpointAnnouncement & announcement, const UdpEndpoint & remote);
  bool SendAckNackForHeartbeat(
    const HeartbeatSubmessage & heartbeat, const GuidPrefix & remote_guid_prefix,
    const UdpEndpoint & remote);
  void ReplaySedpEndpointsToParticipant(const DiscoveredParticipant & participant);

  ParticipantConfig config_;
  RtpsPorts ports_;
  Locator metatraffic_unicast_locator_;
  Locator default_unicast_locator_;
  UdpSocket metatraffic_unicast_socket_;
  UdpSocket user_unicast_socket_;
  std::mutex spdp_send_mutex_;
  int64_t next_spdp_sequence_number_ = 1;
  std::mutex sedp_send_mutex_;
  int64_t next_sedp_publication_sequence_number_ = 1;
  int64_t next_sedp_subscription_sequence_number_ = 1;
  int32_t next_sedp_heartbeat_count_ = 1;
  std::mutex acknack_send_mutex_;
  int32_t next_acknack_count_ = 1;
  mutable std::mutex local_sedp_endpoints_mutex_;
  std::vector<SedpEndpointAnnouncement> local_sedp_endpoints_;
  std::mutex spdp_announcer_mutex_;
  std::condition_variable spdp_announcer_cv_;
  bool spdp_announcer_running_ = false;
  std::thread spdp_announcer_thread_;
  std::mutex spdp_receiver_mutex_;
  bool spdp_receiver_running_ = false;
  std::thread spdp_receiver_thread_;
  mutable std::mutex discovered_participants_mutex_;
  std::vector<DiscoveredParticipant> discovered_participants_;
  mutable std::mutex discovered_sedp_endpoints_mutex_;
  std::vector<DiscoveredSedpEndpoint> discovered_sedp_endpoints_;
};

}  // namespace rtps
}  // namespace rmw_mdds_cpp

#endif  // RMW_MDDS_CPP_SRC__RTPS_PARTICIPANT_HPP_
