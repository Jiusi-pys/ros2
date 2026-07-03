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

#include "rtps_transport.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>

namespace rmw_mdds_cpp
{
namespace rtps
{
namespace
{
constexpr size_t kMaxUdpDatagramSize = 65535u;

void SetError(std::string * error, const std::string & message)
{
  if (error != nullptr) {
    *error = message;
  }
}

std::string ErrnoMessage(const char * action)
{
  return std::string(action) + ": " + std::strerror(errno);
}

bool PortFromValue(uint64_t value, uint16_t * port, std::string * error)
{
  if (port == nullptr) {
    SetError(error, "RTPS port output is null");
    return false;
  }
  if (value > std::numeric_limits<uint16_t>::max()) {
    SetError(error, "RTPS calculated port exceeds UDP port range");
    return false;
  }
  *port = static_cast<uint16_t>(value);
  return true;
}

bool FillIpv4Sockaddr(
  const std::string & address, uint16_t port, sockaddr_in * addr, std::string * error)
{
  if (addr == nullptr) {
    SetError(error, "UDP address output is null");
    return false;
  }
  if (address.empty()) {
    SetError(error, "UDP address is empty");
    return false;
  }

  *addr = {};
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  if (inet_pton(AF_INET, address.c_str(), &addr->sin_addr) != 1) {
    SetError(error, "UDP address is not a valid IPv4 literal");
    return false;
  }
  return true;
}

bool EndpointFromSockaddr(const sockaddr_in & addr, UdpEndpoint * endpoint, std::string * error)
{
  if (endpoint == nullptr) {
    return true;
  }
  char text[INET_ADDRSTRLEN] = {};
  if (inet_ntop(AF_INET, &addr.sin_addr, text, sizeof(text)) == nullptr) {
    SetError(error, ErrnoMessage("inet_ntop failed"));
    return false;
  }
  endpoint->address = text;
  endpoint->port = ntohs(addr.sin_port);
  return true;
}
}  // namespace

bool CalculateRtpsPorts(
  const RtpsPortMapping & mapping, uint32_t domain_id, uint32_t participant_id,
  RtpsPorts * ports, std::string * error)
{
  if (ports == nullptr) {
    SetError(error, "RTPS ports output is null");
    return false;
  }

  const uint64_t domain_base =
    static_cast<uint64_t>(mapping.port_base) +
    static_cast<uint64_t>(mapping.domain_id_gain) * domain_id;
  RtpsPorts calculated;
  if (
    !PortFromValue(domain_base + mapping.offset_d0, &calculated.metatraffic_multicast, error) ||
    !PortFromValue(domain_base + mapping.offset_d2, &calculated.user_multicast, error) ||
    !PortFromValue(
      domain_base + mapping.offset_d1 +
      static_cast<uint64_t>(mapping.participant_id_gain) * participant_id,
      &calculated.metatraffic_unicast, error) ||
    !PortFromValue(
      domain_base + mapping.offset_d3 +
      static_cast<uint64_t>(mapping.participant_id_gain) * participant_id,
      &calculated.user_unicast, error)) {
    return false;
  }

  *ports = calculated;
  return true;
}

bool CalculateRtpsPorts(
  uint32_t domain_id, uint32_t participant_id, RtpsPorts * ports, std::string * error)
{
  return CalculateRtpsPorts(RtpsPortMapping{}, domain_id, participant_id, ports, error);
}

UdpSocket::UdpSocket(int fd) : fd_(fd) {}

UdpSocket::~UdpSocket()
{
  reset();
}

UdpSocket::UdpSocket(UdpSocket && other) noexcept
  : fd_(other.fd_), local_port_(other.local_port_)
{
  other.fd_ = -1;
  other.local_port_ = 0;
}

UdpSocket & UdpSocket::operator=(UdpSocket && other) noexcept
{
  if (this != &other) {
    reset(other.fd_);
    local_port_ = other.local_port_;
    other.fd_ = -1;
    other.local_port_ = 0;
  }
  return *this;
}

UdpSocket::operator bool() const
{
  return fd_ >= 0;
}

int UdpSocket::get() const
{
  return fd_;
}

uint16_t UdpSocket::local_port() const
{
  return local_port_;
}

void UdpSocket::reset(int fd)
{
  if (fd_ >= 0) {
    close(fd_);
  }
  fd_ = fd;
  local_port_ = 0;
}

UdpSocket UdpSocket::Bind(const std::string & address, uint16_t port, std::string * error)
{
  sockaddr_in addr;
  if (!FillIpv4Sockaddr(address, port, &addr, error)) {
    return UdpSocket();
  }

  UdpSocket socket_fd(socket(AF_INET, SOCK_DGRAM, 0));
  if (!socket_fd) {
    SetError(error, ErrnoMessage("socket failed"));
    return UdpSocket();
  }

  // NOTE: intentionally NOT setting SO_REUSEADDR on unicast RTPS sockets.
  // SO_REUSEADDR lets two participants in the same process bind the identical
  // (addr, port) silently; the kernel then delivers user DATA to whichever
  // socket it picks, which need not be the one the user-data receiver polls,
  // so packets are silently dropped. Exclusive bind makes a port collision
  // surface as EADDRINUSE, which RtpsParticipant::Create handles by advancing
  // the participant id to a free slot.
  if (bind(socket_fd.get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
    SetError(error, ErrnoMessage("bind failed"));
    return UdpSocket();
  }

  sockaddr_in local_addr;
  socklen_t local_addr_size = sizeof(local_addr);
  if (
    getsockname(
      socket_fd.get(), reinterpret_cast<sockaddr *>(&local_addr), &local_addr_size) != 0) {
    SetError(error, ErrnoMessage("getsockname failed"));
    return UdpSocket();
  }
  socket_fd.local_port_ = ntohs(local_addr.sin_port);
  return socket_fd;
}

bool UdpSocket::SendTo(
  const uint8_t * data, size_t size, const UdpEndpoint & endpoint, std::string * error) const
{
  if (fd_ < 0) {
    SetError(error, "UDP send socket is invalid");
    return false;
  }
  if (data == nullptr && size != 0u) {
    SetError(error, "UDP send data is null");
    return false;
  }
  sockaddr_in addr;
  if (!FillIpv4Sockaddr(endpoint.address, endpoint.port, &addr, error)) {
    return false;
  }
  const ssize_t sent = sendto(
    fd_, data, size, 0, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
  if (sent < 0) {
    SetError(error, ErrnoMessage("sendto failed"));
    return false;
  }
  if (static_cast<size_t>(sent) != size) {
    SetError(error, "sendto wrote a partial UDP datagram");
    return false;
  }
  return true;
}

bool UdpSocket::Receive(
  std::vector<uint8_t> * data, UdpEndpoint * remote, int timeout_ms, std::string * error) const
{
  if (fd_ < 0) {
    SetError(error, "UDP receive socket is invalid");
    return false;
  }
  if (data == nullptr) {
    SetError(error, "UDP receive data output is null");
    return false;
  }

  pollfd pfd;
  pfd.fd = fd_;
  pfd.events = POLLIN;
  pfd.revents = 0;
  int poll_result = 0;
  do {
    poll_result = poll(&pfd, 1, timeout_ms);
  } while (poll_result < 0 && errno == EINTR);

  if (poll_result == 0) {
    SetError(error, "UDP receive timed out");
    return false;
  }
  if (poll_result < 0) {
    SetError(error, ErrnoMessage("poll failed"));
    return false;
  }

  std::vector<uint8_t> buffer(kMaxUdpDatagramSize);
  sockaddr_in remote_addr;
  socklen_t remote_addr_size = sizeof(remote_addr);
  const ssize_t received = recvfrom(
    fd_, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr *>(&remote_addr),
    &remote_addr_size);
  if (received < 0) {
    SetError(error, ErrnoMessage("recvfrom failed"));
    return false;
  }

  buffer.resize(static_cast<size_t>(received));
  if (!EndpointFromSockaddr(remote_addr, remote, error)) {
    return false;
  }
  *data = std::move(buffer);
  return true;
}

}  // namespace rtps
}  // namespace rmw_mdds_cpp
