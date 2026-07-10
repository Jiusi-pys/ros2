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

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "ipc_protocol.hpp"

namespace
{
TEST(RmwMddsIpcProtocol, EndpointDescriptorRoundTripPreservesGraphFields)
{
  rmw_mdds_cpp::ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = 0x1234567890abcdefu;
  endpoint.local_context_id = 0x0fedcba987654321u;
  endpoint.domain_id = 93u;
  endpoint.kind = rmw_mdds_cpp::ipc::EndpointKind::kPublisher;
  endpoint.node_name = "camera_node";
  endpoint.node_namespace = "/robot/front";
  endpoint.node_enclave = "/robot/front/camera_enclave";
  endpoint.topic_name = "/camera/pose";
  endpoint.type_name = "geometry_msgs/msg/PoseStamped";
  endpoint.mdds_type_name = "geometry_msgs::msg::dds_::PoseStamped_";
  endpoint.type_hash.version = 1u;
  for (size_t i = 0; i < ROSIDL_TYPE_HASH_SIZE; ++i) {
    endpoint.type_hash.value[i] = static_cast<uint8_t>(i + 1u);
  }
  endpoint.qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  endpoint.qos.depth = 13u;
  endpoint.qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  endpoint.qos.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
  endpoint.ignore_local_publications = true;

  const std::vector<uint8_t> frame = rmw_mdds_cpp::ipc::EncodeFrame(
    rmw_mdds_cpp::ipc::Frame{
      rmw_mdds_cpp::ipc::MessageKind::kRegisterPublisher, 42u,
      rmw_mdds_cpp::ipc::EncodeEndpointDescriptor(endpoint)});

  rmw_mdds_cpp::ipc::Frame decoded_frame;
  std::string error;
  ASSERT_EQ(
    rmw_mdds_cpp::ipc::DecodeStatus::kOk,
    rmw_mdds_cpp::ipc::DecodeFrame(frame.data(), frame.size(), &decoded_frame, &error))
    << error;
  EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kRegisterPublisher, decoded_frame.kind);
  EXPECT_EQ(42u, decoded_frame.request_id);

  rmw_mdds_cpp::ipc::EndpointDescriptor decoded_endpoint;
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::DecodeEndpointDescriptor(
      decoded_frame.payload.data(), decoded_frame.payload.size(), &decoded_endpoint, &error))
    << error;
  EXPECT_EQ(endpoint.entity_id, decoded_endpoint.entity_id);
  EXPECT_EQ(endpoint.local_context_id, decoded_endpoint.local_context_id);
  EXPECT_EQ(endpoint.domain_id, decoded_endpoint.domain_id);
  EXPECT_EQ(endpoint.kind, decoded_endpoint.kind);
  EXPECT_EQ(endpoint.node_name, decoded_endpoint.node_name);
  EXPECT_EQ(endpoint.node_namespace, decoded_endpoint.node_namespace);
  EXPECT_EQ(endpoint.node_enclave, decoded_endpoint.node_enclave);
  EXPECT_EQ(endpoint.topic_name, decoded_endpoint.topic_name);
  EXPECT_EQ(endpoint.type_name, decoded_endpoint.type_name);
  EXPECT_EQ(endpoint.mdds_type_name, decoded_endpoint.mdds_type_name);
  EXPECT_EQ(endpoint.type_hash.version, decoded_endpoint.type_hash.version);
  EXPECT_EQ(
    0,
    std::memcmp(
      endpoint.type_hash.value, decoded_endpoint.type_hash.value, ROSIDL_TYPE_HASH_SIZE));
  EXPECT_EQ(endpoint.qos.history, decoded_endpoint.qos.history);
  EXPECT_EQ(endpoint.qos.depth, decoded_endpoint.qos.depth);
  EXPECT_EQ(endpoint.qos.reliability, decoded_endpoint.qos.reliability);
  EXPECT_EQ(endpoint.qos.durability, decoded_endpoint.qos.durability);
  EXPECT_EQ(endpoint.ignore_local_publications, decoded_endpoint.ignore_local_publications);
}

TEST(RmwMddsIpcProtocol, SampleRoundTripPreservesEntitySequenceAndPayload)
{
  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 7u;
  sample.sequence_number = 99u;
  sample.mdds_payload = true;
  sample.payload = {0x00u, 0x01u, 0x02u, 0xfdu, 0xfeu, 0xffu};

  const std::vector<uint8_t> frame = rmw_mdds_cpp::ipc::EncodeFrame(
    rmw_mdds_cpp::ipc::Frame{
      rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 1001u,
      rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)});

  rmw_mdds_cpp::ipc::Frame decoded_frame;
  std::string error;
  ASSERT_EQ(
    rmw_mdds_cpp::ipc::DecodeStatus::kOk,
    rmw_mdds_cpp::ipc::DecodeFrame(frame.data(), frame.size(), &decoded_frame, &error))
    << error;
  EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kPublishSample, decoded_frame.kind);
  EXPECT_EQ(1001u, decoded_frame.request_id);

  rmw_mdds_cpp::ipc::SampleMessage decoded_sample;
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::DecodeSampleMessage(
      decoded_frame.payload.data(), decoded_frame.payload.size(), &decoded_sample, &error))
    << error;
  EXPECT_EQ(sample.entity_id, decoded_sample.entity_id);
  EXPECT_EQ(sample.sequence_number, decoded_sample.sequence_number);
  EXPECT_EQ(sample.mdds_payload, decoded_sample.mdds_payload);
  EXPECT_EQ(sample.payload, decoded_sample.payload);
}

TEST(RmwMddsIpcProtocol, PublishSampleFrameAcceptsMaxUserPayloadWithSampleEnvelope)
{
  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 17u;
  sample.sequence_number = 20260707u;
  sample.mdds_payload = true;
  sample.payload.assign(rmw_mdds_cpp::ipc::kMaxSampleUserPayloadSize, 0xabu);

  const std::vector<uint8_t> sample_payload =
    rmw_mdds_cpp::ipc::EncodeSampleMessage(sample);
  ASSERT_EQ(
    rmw_mdds_cpp::ipc::kMaxFramePayloadSize,
    sample_payload.size());

  const std::vector<uint8_t> frame = rmw_mdds_cpp::ipc::EncodeFrame(
    rmw_mdds_cpp::ipc::Frame{
      rmw_mdds_cpp::ipc::MessageKind::kPublishSample, 1002u,
      sample_payload});
  ASSERT_FALSE(frame.empty());

  rmw_mdds_cpp::ipc::Frame decoded_frame;
  std::string error;
  ASSERT_EQ(
    rmw_mdds_cpp::ipc::DecodeStatus::kOk,
    rmw_mdds_cpp::ipc::DecodeFrame(frame.data(), frame.size(), &decoded_frame, &error))
    << error;
  EXPECT_EQ(rmw_mdds_cpp::ipc::MessageKind::kPublishSample, decoded_frame.kind);
  EXPECT_EQ(1002u, decoded_frame.request_id);

  rmw_mdds_cpp::ipc::SampleMessage decoded_sample;
  ASSERT_TRUE(
    rmw_mdds_cpp::ipc::DecodeSampleMessage(
      decoded_frame.payload.data(), decoded_frame.payload.size(), &decoded_sample, &error))
    << error;
  EXPECT_EQ(sample.entity_id, decoded_sample.entity_id);
  EXPECT_EQ(sample.sequence_number, decoded_sample.sequence_number);
  EXPECT_EQ(sample.mdds_payload, decoded_sample.mdds_payload);
  EXPECT_EQ(sample.payload.size(), decoded_sample.payload.size());
  EXPECT_EQ(0xabu, decoded_sample.payload.front());
  EXPECT_EQ(0xabu, decoded_sample.payload.back());
}

TEST(RmwMddsIpcProtocol, DecodeFrameDistinguishesNeedMoreFromCorruptData)
{
  const std::vector<uint8_t> frame = rmw_mdds_cpp::ipc::EncodeFrame(
    rmw_mdds_cpp::ipc::Frame{rmw_mdds_cpp::ipc::MessageKind::kAck, 1u, {0x10u, 0x20u}});
  ASSERT_GT(frame.size(), rmw_mdds_cpp::ipc::kFrameHeaderSize);

  rmw_mdds_cpp::ipc::Frame decoded_frame;
  std::string error;
  EXPECT_EQ(
    rmw_mdds_cpp::ipc::DecodeStatus::kNeedMore,
    rmw_mdds_cpp::ipc::DecodeFrame(frame.data(), frame.size() - 1u, &decoded_frame, &error));

  std::vector<uint8_t> corrupt_magic = frame;
  corrupt_magic[0] ^= 0xffu;
  EXPECT_EQ(
    rmw_mdds_cpp::ipc::DecodeStatus::kError,
    rmw_mdds_cpp::ipc::DecodeFrame(
      corrupt_magic.data(), corrupt_magic.size(), &decoded_frame, &error));

  std::vector<uint8_t> oversized = frame;
  const uint32_t too_large =
    static_cast<uint32_t>(rmw_mdds_cpp::ipc::kMaxFramePayloadSize + 1u);
  oversized[rmw_mdds_cpp::ipc::kFramePayloadSizeOffset + 0u] =
    static_cast<uint8_t>(too_large & 0xffu);
  oversized[rmw_mdds_cpp::ipc::kFramePayloadSizeOffset + 1u] =
    static_cast<uint8_t>((too_large >> 8u) & 0xffu);
  oversized[rmw_mdds_cpp::ipc::kFramePayloadSizeOffset + 2u] =
    static_cast<uint8_t>((too_large >> 16u) & 0xffu);
  oversized[rmw_mdds_cpp::ipc::kFramePayloadSizeOffset + 3u] =
    static_cast<uint8_t>((too_large >> 24u) & 0xffu);
  EXPECT_EQ(
    rmw_mdds_cpp::ipc::DecodeStatus::kError,
    rmw_mdds_cpp::ipc::DecodeFrame(oversized.data(), oversized.size(), &decoded_frame, &error));
}

TEST(RmwMddsIpcProtocol, DecodeEndpointListRejectsImpossibleCountBeforeReserve)
{
  std::vector<uint8_t> impossible_count = {
    0xffu, 0xffu, 0xffu, 0x7fu,  // endpoint count: INT32_MAX
    0x00u, 0x00u, 0x00u, 0x00u   // one empty entry header at most
  };

  std::vector<rmw_mdds_cpp::ipc::EndpointDescriptor> endpoints;
  std::string error;
  EXPECT_FALSE(rmw_mdds_cpp::ipc::DecodeEndpointList(
    impossible_count.data(), impossible_count.size(), &endpoints, &error));
  EXPECT_TRUE(endpoints.empty());
  EXPECT_EQ("endpoint list count exceeds payload size", error);
}
}  // namespace
