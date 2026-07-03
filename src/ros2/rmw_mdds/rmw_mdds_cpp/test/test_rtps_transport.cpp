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

#include <string>
#include <vector>

#include "rtps_protocol.hpp"
#include "rtps_transport.hpp"

namespace
{
rmw_mdds_cpp::rtps::RtpsHeader MakeHeader()
{
  rmw_mdds_cpp::rtps::RtpsHeader header;
  header.protocol_version = {2u, 3u};
  header.guid_prefix = {
    0xa0u, 0xa1u, 0xa2u, 0xa3u, 0xa4u, 0xa5u,
    0xa6u, 0xa7u, 0xa8u, 0xa9u, 0xaau, 0xabu};
  return header;
}

rmw_mdds_cpp::rtps::DataSubmessage MakeDataSubmessage()
{
  rmw_mdds_cpp::rtps::DataSubmessage data;
  data.reader_id = rmw_mdds_cpp::rtps::kEntityIdUnknown;
  data.writer_id = {0x00u, 0x00u, 0x10u, 0xc2u};
  data.writer_sequence_number = 9;
  data.serialized_payload = {0x00u, 0x01u, 0x00u, 0x00u, 'r', 't', 'p', 's'};
  return data;
}
}  // namespace

TEST(RmwMddsRtpsTransport, DefaultPortsMatchFastDdsRtpsFormula)
{
  std::string error;
  rmw_mdds_cpp::rtps::RtpsPorts ports;
  ASSERT_TRUE(rmw_mdds_cpp::rtps::CalculateRtpsPorts(0, 0, &ports, &error)) << error;
  EXPECT_EQ(7400u, ports.metatraffic_multicast);
  EXPECT_EQ(7401u, ports.user_multicast);
  EXPECT_EQ(7410u, ports.metatraffic_unicast);
  EXPECT_EQ(7411u, ports.user_unicast);

  ASSERT_TRUE(rmw_mdds_cpp::rtps::CalculateRtpsPorts(123, 3, &ports, &error)) << error;
  EXPECT_EQ(38150u, ports.metatraffic_multicast);
  EXPECT_EQ(38151u, ports.user_multicast);
  EXPECT_EQ(38166u, ports.metatraffic_unicast);
  EXPECT_EQ(38167u, ports.user_unicast);

  EXPECT_FALSE(rmw_mdds_cpp::rtps::CalculateRtpsPorts(233, 0, &ports, &error));
}

TEST(RmwMddsRtpsTransport, UdpSocketSendsAndReceivesRtpsDatagrams)
{
  std::string error;
  rmw_mdds_cpp::rtps::UdpSocket receiver =
    rmw_mdds_cpp::rtps::UdpSocket::Bind("127.0.0.1", 0, &error);
  ASSERT_TRUE(receiver) << error;

  rmw_mdds_cpp::rtps::UdpSocket sender =
    rmw_mdds_cpp::rtps::UdpSocket::Bind("127.0.0.1", 0, &error);
  ASSERT_TRUE(sender) << error;

  const std::vector<uint8_t> packet =
    rmw_mdds_cpp::rtps::EncodeDataMessage(MakeHeader(), MakeDataSubmessage());
  const rmw_mdds_cpp::rtps::UdpEndpoint destination{"127.0.0.1", receiver.local_port()};
  ASSERT_TRUE(sender.SendTo(packet.data(), packet.size(), destination, &error)) << error;

  std::vector<uint8_t> received;
  rmw_mdds_cpp::rtps::UdpEndpoint remote;
  ASSERT_TRUE(receiver.Receive(&received, &remote, 1000, &error)) << error;
  EXPECT_EQ(packet, received);
  EXPECT_EQ("127.0.0.1", remote.address);
  EXPECT_EQ(sender.local_port(), remote.port);
}
