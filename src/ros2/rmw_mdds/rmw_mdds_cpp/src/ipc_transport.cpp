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

#include "ipc_transport.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace rmw_mdds_cpp
{
namespace ipc
{
namespace
{
constexpr int kUnixSocketListenBacklog = 128;

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

bool FillSockaddr(const std::string & path, sockaddr_un * addr, socklen_t * length, std::string * error)
{
  if (addr == nullptr || length == nullptr) {
    SetError(error, "socket address output is null");
    return false;
  }
  if (path.empty()) {
    SetError(error, "socket path is empty");
    return false;
  }
  if (path.size() >= sizeof(addr->sun_path)) {
    SetError(error, "socket path is too long");
    return false;
  }

  *addr = {};
  addr->sun_family = AF_UNIX;
  std::memcpy(addr->sun_path, path.c_str(), path.size() + 1u);
  *length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1u);
  return true;
}

bool ConnectExistingSocket(const sockaddr_un & addr, socklen_t length, bool * connected, std::string * error)
{
  if (connected == nullptr) {
    SetError(error, "connected output is null");
    return false;
  }
  *connected = false;

  UniqueFd probe(socket(AF_UNIX, SOCK_STREAM, 0));
  if (!probe) {
    SetError(error, ErrnoMessage("socket probe failed"));
    return false;
  }

  for (;;) {
    if (connect(probe.get(), reinterpret_cast<const sockaddr *>(&addr), length) == 0) {
      *connected = true;
      return true;
    }
    if (errno == EINTR) {
      continue;
    }
    return true;
  }
}

bool ReadPayloadSize(const uint8_t * header, uint32_t * payload_size)
{
  if (header == nullptr || payload_size == nullptr) {
    return false;
  }
  const size_t offset = kFramePayloadSizeOffset;
  *payload_size = static_cast<uint32_t>(header[offset]) |
                  (static_cast<uint32_t>(header[offset + 1u]) << 8u) |
                  (static_cast<uint32_t>(header[offset + 2u]) << 16u) |
                  (static_cast<uint32_t>(header[offset + 3u]) << 24u);
  return true;
}

ReadFrameStatus ReadExact(int fd, uint8_t * data, size_t size, bool closed_is_ok, std::string * error)
{
  size_t offset = 0u;
  while (offset < size) {
    const ssize_t n = recv(fd, data + offset, size - offset, 0);
    if (n > 0) {
      offset += static_cast<size_t>(n);
      continue;
    }
    if (n == 0) {
      if (offset == 0u && closed_is_ok) {
        return ReadFrameStatus::kClosed;
      }
      SetError(error, "socket closed while reading frame");
      return ReadFrameStatus::kError;
    }
    if (errno == EINTR) {
      continue;
    }
    SetError(error, ErrnoMessage("recv failed"));
    return ReadFrameStatus::kError;
  }
  return ReadFrameStatus::kOk;
}

bool WriteAll(int fd, const uint8_t * data, size_t size, std::string * error)
{
  size_t offset = 0u;
  while (offset < size) {
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    const ssize_t n = send(fd, data + offset, size - offset, flags);
    if (n > 0) {
      offset += static_cast<size_t>(n);
      continue;
    }
    if (n == 0) {
      SetError(error, "socket made no progress while writing frame");
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    SetError(error, ErrnoMessage("send failed"));
    return false;
  }
  return true;
}
}  // namespace

UniqueFd::UniqueFd(int fd) : fd_(fd) {}

UniqueFd::~UniqueFd()
{
  reset();
}

UniqueFd::UniqueFd(UniqueFd && other) noexcept : fd_(other.release()) {}

UniqueFd & UniqueFd::operator=(UniqueFd && other) noexcept
{
  if (this != &other) {
    reset(other.release());
  }
  return *this;
}

UniqueFd::operator bool() const
{
  return fd_ >= 0;
}

int UniqueFd::get() const
{
  return fd_;
}

int UniqueFd::release()
{
  const int fd = fd_;
  fd_ = -1;
  return fd;
}

void UniqueFd::reset(int fd)
{
  if (fd_ >= 0) {
    close(fd_);
  }
  fd_ = fd;
}

UniqueFd ListenUnixSocket(const std::string & path, std::string * error)
{
  sockaddr_un addr;
  socklen_t length = 0u;
  if (!FillSockaddr(path, &addr, &length, error)) {
    return UniqueFd();
  }

  UniqueFd fd(socket(AF_UNIX, SOCK_STREAM, 0));
  if (!fd) {
    SetError(error, ErrnoMessage("socket failed"));
    return UniqueFd();
  }

  if (bind(fd.get(), reinterpret_cast<const sockaddr *>(&addr), length) != 0) {
    const int bind_errno = errno;
    if (bind_errno != EADDRINUSE) {
      SetError(error, std::string("bind failed: ") + std::strerror(bind_errno));
      return UniqueFd();
    }

    bool active_listener = false;
    if (!ConnectExistingSocket(addr, length, &active_listener, error)) {
      return UniqueFd();
    }
    if (active_listener) {
      SetError(error, "socket path already has an active listener");
      return UniqueFd();
    }

    struct stat st;
    if (lstat(path.c_str(), &st) == 0 && !S_ISSOCK(st.st_mode)) {
      SetError(error, "socket path already exists and is not a socket");
      return UniqueFd();
    }
    if (unlink(path.c_str()) != 0 && errno != ENOENT) {
      SetError(error, ErrnoMessage("unlink stale socket failed"));
      return UniqueFd();
    }
    if (bind(fd.get(), reinterpret_cast<const sockaddr *>(&addr), length) != 0) {
      SetError(error, ErrnoMessage("bind retry failed"));
      return UniqueFd();
    }
  }
  if (listen(fd.get(), kUnixSocketListenBacklog) != 0) {
    SetError(error, ErrnoMessage("listen failed"));
    return UniqueFd();
  }
  return fd;
}

UniqueFd ConnectUnixSocket(const std::string & path, std::string * error)
{
  sockaddr_un addr;
  socklen_t length = 0u;
  if (!FillSockaddr(path, &addr, &length, error)) {
    return UniqueFd();
  }

  UniqueFd fd(socket(AF_UNIX, SOCK_STREAM, 0));
  if (!fd) {
    SetError(error, ErrnoMessage("socket failed"));
    return UniqueFd();
  }
  if (connect(fd.get(), reinterpret_cast<const sockaddr *>(&addr), length) != 0) {
    SetError(error, ErrnoMessage("connect failed"));
    return UniqueFd();
  }
  return fd;
}

UniqueFd AcceptUnixSocket(int listener_fd, std::string * error)
{
  while (true) {
    const int fd = accept(listener_fd, nullptr, nullptr);
    if (fd >= 0) {
      return UniqueFd(fd);
    }
    if (errno == EINTR) {
      continue;
    }
    SetError(error, ErrnoMessage("accept failed"));
    return UniqueFd();
  }
}

bool WriteFrame(int fd, const Frame & frame, std::string * error)
{
  if (fd < 0) {
    SetError(error, "frame write fd is invalid");
    return false;
  }
  const std::vector<uint8_t> encoded = EncodeFrame(frame);
  if (encoded.empty()) {
    SetError(error, "failed to encode frame");
    return false;
  }
  return WriteAll(fd, encoded.data(), encoded.size(), error);
}

ReadFrameStatus ReadFrame(int fd, Frame * frame, std::string * error)
{
  if (fd < 0) {
    SetError(error, "frame read fd is invalid");
    return ReadFrameStatus::kError;
  }
  if (frame == nullptr) {
    SetError(error, "frame output is null");
    return ReadFrameStatus::kError;
  }

  std::vector<uint8_t> data(kFrameHeaderSize);
  ReadFrameStatus status = ReadExact(fd, data.data(), data.size(), true, error);
  if (status != ReadFrameStatus::kOk) {
    return status;
  }

  Frame decoded_header;
  const DecodeStatus header_status = DecodeFrame(data.data(), data.size(), &decoded_header, error);
  if (header_status == DecodeStatus::kError) {
    return ReadFrameStatus::kError;
  }
  if (header_status == DecodeStatus::kOk) {
    *frame = std::move(decoded_header);
    return ReadFrameStatus::kOk;
  }

  uint32_t payload_size = 0u;
  if (!ReadPayloadSize(data.data(), &payload_size)) {
    SetError(error, "failed to read frame payload size");
    return ReadFrameStatus::kError;
  }
  if (payload_size > kMaxFramePayloadSize) {
    SetError(error, "frame payload exceeds maximum size");
    return ReadFrameStatus::kError;
  }

  const size_t old_size = data.size();
  data.resize(old_size + payload_size);
  status = ReadExact(fd, data.data() + old_size, payload_size, false, error);
  if (status != ReadFrameStatus::kOk) {
    return status;
  }

  const DecodeStatus decode_status = DecodeFrame(data.data(), data.size(), frame, error);
  return decode_status == DecodeStatus::kOk ? ReadFrameStatus::kOk : ReadFrameStatus::kError;
}

}  // namespace ipc
}  // namespace rmw_mdds_cpp
