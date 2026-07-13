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
  endpoint.loaned_message_size = 256u;

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
  EXPECT_EQ(endpoint.loaned_message_size, decoded_endpoint.loaned_message_size);
}

TEST(RmwMddsIpcProtocol, LoanPoolAndSampleDescriptorsRoundTripWithoutPayloadBytes)
{
  rmw_mdds_cpp::ipc::LoanPoolDescriptor pool;
  pool.path = "/tmp/rmw_mdds_loan_test.pool";
  pool.generation = 0x123456789abcdef0u;
  pool.slot_size = 64u;
  pool.slot_count = 32u;
  const std::vector<uint8_t> encoded_pool =
    rmw_mdds_cpp::ipc::EncodeLoanPoolDescriptor(pool);

  rmw_mdds_cpp::ipc::LoanPoolDescriptor decoded_pool;
  std::string error;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeLoanPoolDescriptor(
    encoded_pool.data(), encoded_pool.size(), &decoded_pool, &error)) << error;
  EXPECT_EQ(pool.path, decoded_pool.path);
  EXPECT_EQ(pool.generation, decoded_pool.generation);
  EXPECT_EQ(pool.slot_size, decoded_pool.slot_size);
  EXPECT_EQ(pool.slot_count, decoded_pool.slot_count);

  rmw_mdds_cpp::ipc::LoanedSampleMessage sample;
  sample.entity_id = 17u;
  sample.loan_id = 19u;
  sample.pool_generation = pool.generation;
  sample.sequence_number = 23u;
  sample.slot_index = 7u;
  sample.payload_size = 64u;
  sample.mdds_payload = true;
  const std::vector<uint8_t> encoded_sample =
    rmw_mdds_cpp::ipc::EncodeLoanedSampleMessage(sample);
  EXPECT_LT(encoded_sample.size(), rmw_mdds_cpp::ipc::kSampleMessageHeaderSize + sample.payload_size);

  rmw_mdds_cpp::ipc::LoanedSampleMessage decoded_sample;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeLoanedSampleMessage(
    encoded_sample.data(), encoded_sample.size(), &decoded_sample, &error)) << error;
  EXPECT_EQ(sample.entity_id, decoded_sample.entity_id);
  EXPECT_EQ(sample.loan_id, decoded_sample.loan_id);
  EXPECT_EQ(sample.pool_generation, decoded_sample.pool_generation);
  EXPECT_EQ(sample.sequence_number, decoded_sample.sequence_number);
  EXPECT_EQ(sample.slot_index, decoded_sample.slot_index);
  EXPECT_EQ(sample.payload_size, decoded_sample.payload_size);
  EXPECT_EQ(sample.mdds_payload, decoded_sample.mdds_payload);

  const std::vector<uint8_t> encoded_return =
    rmw_mdds_cpp::ipc::EncodeLoanReturn(sample.loan_id);
  uint64_t returned_loan = 0u;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeLoanReturn(
    encoded_return.data(), encoded_return.size(), &returned_loan, &error)) << error;
  EXPECT_EQ(sample.loan_id, returned_loan);
}

TEST(RmwMddsIpcProtocol, DynamicLoanRequestAndPoolDescriptorRoundTrip)
{
  rmw_mdds_cpp::ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = 0x7071727374757677u;
  endpoint.local_context_id = 0x6162636465666768u;
  endpoint.domain_id = 197u;
  endpoint.kind = rmw_mdds_cpp::ipc::EndpointKind::kSubscription;
  endpoint.node_name = "dynamic_loan_node";
  endpoint.node_namespace = "/mdds";
  endpoint.topic_name = "/dynamic_loan";
  endpoint.type_name = "std_msgs/msg/String";
  endpoint.mdds_type_name = "std_msgs/msg/String";
  endpoint.loan_pool_version = rmw_mdds_cpp::ipc::kDynamicLoanPoolVersion;
  endpoint.loaned_payload_capacity = rmw_mdds_cpp::ipc::kDefaultDynamicLoanPayloadCapacity;
  endpoint.loaned_arena_capacity = rmw_mdds_cpp::ipc::kDefaultDynamicLoanArenaCapacity;
  endpoint.loaned_slot_count = rmw_mdds_cpp::ipc::kDefaultDynamicLoanPoolSlotCount;
  endpoint.loan_pool_flags = rmw_mdds_cpp::ipc::kLoanPoolFlagTypedArena;

  const std::vector<uint8_t> encoded_endpoint =
    rmw_mdds_cpp::ipc::EncodeEndpointDescriptor(endpoint);
  rmw_mdds_cpp::ipc::EndpointDescriptor decoded_endpoint;
  std::string error;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeEndpointDescriptor(
    encoded_endpoint.data(), encoded_endpoint.size(), &decoded_endpoint, &error)) << error;
  EXPECT_EQ(endpoint.loan_pool_version, decoded_endpoint.loan_pool_version);
  EXPECT_EQ(endpoint.loaned_payload_capacity, decoded_endpoint.loaned_payload_capacity);
  EXPECT_EQ(endpoint.loaned_arena_capacity, decoded_endpoint.loaned_arena_capacity);
  EXPECT_EQ(endpoint.loaned_slot_count, decoded_endpoint.loaned_slot_count);
  EXPECT_EQ(endpoint.loan_pool_flags, decoded_endpoint.loan_pool_flags);
  EXPECT_EQ(0u, decoded_endpoint.loaned_message_size);

  rmw_mdds_cpp::ipc::LoanPoolDescriptor pool;
  pool.path = "/tmp/rmw_mdds_dynamic_loan_test.pool";
  pool.generation = 0x1020304050607080u;
  pool.version = rmw_mdds_cpp::ipc::kDynamicLoanPoolVersion;
  pool.flags = rmw_mdds_cpp::ipc::kLoanPoolFlagTypedArena;
  pool.slot_size = endpoint.loaned_payload_capacity;
  pool.arena_size = endpoint.loaned_arena_capacity;
  pool.slot_count = endpoint.loaned_slot_count;
  const std::vector<uint8_t> encoded_pool =
    rmw_mdds_cpp::ipc::EncodeLoanPoolDescriptor(pool);
  rmw_mdds_cpp::ipc::LoanPoolDescriptor decoded_pool;
  ASSERT_TRUE(rmw_mdds_cpp::ipc::DecodeLoanPoolDescriptor(
    encoded_pool.data(), encoded_pool.size(), &decoded_pool, &error)) << error;
  EXPECT_EQ(pool.path, decoded_pool.path);
  EXPECT_EQ(pool.generation, decoded_pool.generation);
  EXPECT_EQ(pool.version, decoded_pool.version);
  EXPECT_EQ(pool.flags, decoded_pool.flags);
  EXPECT_EQ(pool.slot_size, decoded_pool.slot_size);
  EXPECT_EQ(pool.arena_size, decoded_pool.arena_size);
  EXPECT_EQ(pool.slot_count, decoded_pool.slot_count);
}

TEST(RmwMddsIpcProtocol, DynamicLoanRequestRejectsUnknownVersionAndFlags)
{
  rmw_mdds_cpp::ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = 99u;
  endpoint.kind = rmw_mdds_cpp::ipc::EndpointKind::kSubscription;
  endpoint.topic_name = "/dynamic_loan_invalid";
  endpoint.type_name = "std_msgs/msg/String";
  endpoint.mdds_type_name = endpoint.type_name;
  endpoint.loan_pool_version = rmw_mdds_cpp::ipc::kDynamicLoanPoolVersion + 1u;
  endpoint.loaned_payload_capacity = 1024u;
  endpoint.loaned_arena_capacity = 4096u;
  endpoint.loaned_slot_count = 1u;
  endpoint.loan_pool_flags = rmw_mdds_cpp::ipc::kLoanPoolFlagTypedArena;
  std::vector<uint8_t> encoded = rmw_mdds_cpp::ipc::EncodeEndpointDescriptor(endpoint);
  rmw_mdds_cpp::ipc::EndpointDescriptor decoded;
  std::string error;
  EXPECT_FALSE(rmw_mdds_cpp::ipc::DecodeEndpointDescriptor(
    encoded.data(), encoded.size(), &decoded, &error));

  endpoint.loan_pool_version = rmw_mdds_cpp::ipc::kDynamicLoanPoolVersion;
  endpoint.loan_pool_flags = rmw_mdds_cpp::ipc::kLoanPoolFlagTypedArena | 0x80000000u;
  encoded = rmw_mdds_cpp::ipc::EncodeEndpointDescriptor(endpoint);
  EXPECT_FALSE(rmw_mdds_cpp::ipc::DecodeEndpointDescriptor(
    encoded.data(), encoded.size(), &decoded, &error));
}

TEST(RmwMddsIpcProtocol, EndpointRejectsMixedFixedAndDynamicLoanRequests)
{
  rmw_mdds_cpp::ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = 100u;
  endpoint.kind = rmw_mdds_cpp::ipc::EndpointKind::kSubscription;
  endpoint.topic_name = "/mixed_loan_request";
  endpoint.type_name = "std_msgs/msg/String";
  endpoint.mdds_type_name = endpoint.type_name;
  endpoint.loaned_message_size = sizeof(int32_t);
  endpoint.loan_pool_version = rmw_mdds_cpp::ipc::kDynamicLoanPoolVersion;
  endpoint.loaned_payload_capacity =
    rmw_mdds_cpp::ipc::kDefaultDynamicLoanPayloadCapacity;
  endpoint.loaned_arena_capacity =
    rmw_mdds_cpp::ipc::kDefaultDynamicLoanArenaCapacity;
  endpoint.loaned_slot_count =
    rmw_mdds_cpp::ipc::kDefaultDynamicLoanPoolSlotCount;
  endpoint.loan_pool_flags = rmw_mdds_cpp::ipc::kLoanPoolFlagTypedArena;

  const std::vector<uint8_t> encoded =
    rmw_mdds_cpp::ipc::EncodeEndpointDescriptor(endpoint);
  EXPECT_TRUE(encoded.empty())
    << "an endpoint must not advertise both v1 fixed and v2 dynamic loan pools";
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
