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

#ifndef RMW_MDDS_CPP_SRC__RTPS_TRANSPORT_HPP_
#define RMW_MDDS_CPP_SRC__RTPS_TRANSPORT_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rmw_mdds_cpp
{
namespace rtps
{

struct RtpsPortMapping
{
  uint16_t port_base = 7400;
  uint16_t domain_id_gain = 250;
  uint16_t participant_id_gain = 2;
  uint16_t offset_d0 = 0;
  uint16_t offset_d1 = 10;
  uint16_t offset_d2 = 1;
  uint16_t offset_d3 = 11;
};

struct RtpsPorts
{
  uint16_t metatraffic_multicast = 0;
  uint16_t user_multicast = 0;
  uint16_t metatraffic_unicast = 0;
  uint16_t user_unicast = 0;
};

struct UdpEndpoint
{
  std::string address;
  uint16_t port = 0;
};

bool CalculateRtpsPorts(
  const RtpsPortMapping & mapping, uint32_t domain_id, uint32_t participant_id,
  RtpsPorts * ports, std::string * error);

bool CalculateRtpsPorts(
  uint32_t domain_id, uint32_t participant_id, RtpsPorts * ports, std::string * error);

class UdpSocket
{
public:
  UdpSocket() = default;
  explicit UdpSocket(int fd);
  ~UdpSocket();

  UdpSocket(const UdpSocket &) = delete;
  UdpSocket & operator=(const UdpSocket &) = delete;

  UdpSocket(UdpSocket && other) noexcept;
  UdpSocket & operator=(UdpSocket && other) noexcept;

  explicit operator bool() const;
  int get() const;
  uint16_t local_port() const;
  void reset(int fd = -1);

  static UdpSocket Bind(const std::string & address, uint16_t port, std::string * error);

  bool SendTo(
    const uint8_t * data, size_t size, const UdpEndpoint & endpoint, std::string * error) const;
  bool Receive(
    std::vector<uint8_t> * data, UdpEndpoint * remote, int timeout_ms, std::string * error) const;

private:
  int fd_ = -1;
  uint16_t local_port_ = 0;
};

}  // namespace rtps
}  // namespace rmw_mdds_cpp

#endif  // RMW_MDDS_CPP_SRC__RTPS_TRANSPORT_HPP_
