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

#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ipc_protocol.hpp"
#include "ipc_transport.hpp"

namespace
{
class TempSocketPath
{
public:
  TempSocketPath()
  {
    char templ[] = "/tmp/rmw_mdds_ipc_transport_XXXXXX";
    char * dir = mkdtemp(templ);
    if (dir != nullptr) {
      dir_ = dir;
      path_ = dir_ + "/broker.sock";
    }
  }

  ~TempSocketPath()
  {
    if (!path_.empty()) {
      unlink(path_.c_str());
    }
    if (!dir_.empty()) {
      rmdir(dir_.c_str());
    }
  }

  const std::string & path() const
  {
    return path_;
  }

private:
  std::string dir_;
  std::string path_;
};

TEST(RmwMddsIpcTransport, UnixSocketListenerAcceptsClientAndTransfersFrames)
{
  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::UniqueFd listener =
    rmw_mdds_cpp::ipc::ListenUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(listener) << error;

  rmw_mdds_cpp::ipc::UniqueFd client =
    rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(client) << error;

  rmw_mdds_cpp::ipc::UniqueFd server =
    rmw_mdds_cpp::ipc::AcceptUnixSocket(listener.get(), &error);
  ASSERT_TRUE(server) << error;

  rmw_mdds_cpp::ipc::Frame hello;
  hello.kind = rmw_mdds_cpp::ipc::MessageKind::kHello;
  hello.request_id = 101u;
  hello.payload = {'h', 'e', 'l', 'l', 'o'};
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(client.get(), hello, &error)) << error;

  rmw_mdds_cpp::ipc::Frame received_hello;
  ASSERT_EQ(
    rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
    rmw_mdds_cpp::ipc::ReadFrame(server.get(), &received_hello, &error))
    << error;
  EXPECT_EQ(hello.kind, received_hello.kind);
  EXPECT_EQ(hello.request_id, received_hello.request_id);
  EXPECT_EQ(hello.payload, received_hello.payload);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 9u;
  sample.sequence_number = 77u;
  sample.payload = {0x01u, 0x02u, 0x03u, 0x04u};
  rmw_mdds_cpp::ipc::Frame delivery;
  delivery.kind = rmw_mdds_cpp::ipc::MessageKind::kDeliverSample;
  delivery.request_id = 102u;
  delivery.payload = rmw_mdds_cpp::ipc::EncodeSampleMessage(sample);
  ASSERT_TRUE(rmw_mdds_cpp::ipc::WriteFrame(server.get(), delivery, &error)) << error;

  rmw_mdds_cpp::ipc::Frame received_delivery;
  ASSERT_EQ(
    rmw_mdds_cpp::ipc::ReadFrameStatus::kOk,
    rmw_mdds_cpp::ipc::ReadFrame(client.get(), &received_delivery, &error))
    << error;
  EXPECT_EQ(delivery.kind, received_delivery.kind);
  EXPECT_EQ(delivery.request_id, received_delivery.request_id);

  rmw_mdds_cpp::ipc::SampleMessage decoded_sample;
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::DecodeSampleMessage(
      received_delivery.payload.data(), received_delivery.payload.size(), &decoded_sample,
      &error))
    << error;
  EXPECT_EQ(sample.entity_id, decoded_sample.entity_id);
  EXPECT_EQ(sample.sequence_number, decoded_sample.sequence_number);
  EXPECT_EQ(sample.payload, decoded_sample.payload);
}

TEST(RmwMddsIpcTransport, ReadFrameReportsClosedPeerAndMalformedFrame)
{
  int fds[2] = {-1, -1};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  rmw_mdds_cpp::ipc::UniqueFd writer(fds[0]);
  rmw_mdds_cpp::ipc::UniqueFd reader(fds[1]);

  std::string error;
  rmw_mdds_cpp::ipc::Frame frame;
  writer.reset();
  EXPECT_EQ(
    rmw_mdds_cpp::ipc::ReadFrameStatus::kClosed,
    rmw_mdds_cpp::ipc::ReadFrame(reader.get(), &frame, &error));

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  writer.reset(fds[0]);
  reader.reset(fds[1]);

  const std::vector<uint8_t> valid = rmw_mdds_cpp::ipc::EncodeFrame(
    rmw_mdds_cpp::ipc::Frame{rmw_mdds_cpp::ipc::MessageKind::kAck, 1u, {0x01u}});
  ASSERT_FALSE(valid.empty());
  std::vector<uint8_t> invalid = valid;
  invalid[0] ^= 0xffu;
  ASSERT_EQ(
    static_cast<ssize_t>(invalid.size()),
    write(writer.get(), invalid.data(), invalid.size()));

  EXPECT_EQ(
    rmw_mdds_cpp::ipc::ReadFrameStatus::kError,
    rmw_mdds_cpp::ipc::ReadFrame(reader.get(), &frame, &error));
}
}  // namespace
