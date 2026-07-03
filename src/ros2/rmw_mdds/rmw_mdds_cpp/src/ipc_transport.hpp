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

#ifndef RMW_MDDS_CPP_SRC__IPC_TRANSPORT_HPP_
#define RMW_MDDS_CPP_SRC__IPC_TRANSPORT_HPP_

#include <string>

#include "ipc_protocol.hpp"

namespace rmw_mdds_cpp
{
namespace ipc
{

class UniqueFd
{
public:
  UniqueFd() = default;
  explicit UniqueFd(int fd);
  ~UniqueFd();

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd & operator=(const UniqueFd &) = delete;

  UniqueFd(UniqueFd && other) noexcept;
  UniqueFd & operator=(UniqueFd && other) noexcept;

  explicit operator bool() const;
  int get() const;
  int release();
  void reset(int fd = -1);

private:
  int fd_ = -1;
};

enum class ReadFrameStatus
{
  kOk,
  kClosed,
  kError,
};

UniqueFd ListenUnixSocket(const std::string & path, std::string * error);
UniqueFd ConnectUnixSocket(const std::string & path, std::string * error);
UniqueFd AcceptUnixSocket(int listener_fd, std::string * error);

bool WriteFrame(int fd, const Frame & frame, std::string * error);
ReadFrameStatus ReadFrame(int fd, Frame * frame, std::string * error);

}  // namespace ipc
}  // namespace rmw_mdds_cpp

#endif  // RMW_MDDS_CPP_SRC__IPC_TRANSPORT_HPP_
