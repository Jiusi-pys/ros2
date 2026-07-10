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

#ifndef RMW_MDDS_CPP_SRC__IPC_PROTOCOL_HPP_
#define RMW_MDDS_CPP_SRC__IPC_PROTOCOL_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rmw/qos_profiles.h"
#include "rosidl_runtime_c/type_hash.h"

namespace rmw_mdds_cpp
{
namespace ipc
{

constexpr size_t kFrameHeaderSize = 20u;
constexpr size_t kFramePayloadSizeOffset = 16u;
constexpr size_t kMaxSampleUserPayloadSize = 16u * 1024u * 1024u;
constexpr size_t kSampleMessageHeaderSize = 8u + 8u + 1u + 4u;
constexpr size_t kMaxFramePayloadSize = kMaxSampleUserPayloadSize + kSampleMessageHeaderSize;

enum class MessageKind : uint16_t
{
  kHello = 1u,
  kAck = 2u,
  kError = 3u,
  kRegisterPublisher = 10u,
  kRegisterSubscription = 11u,
  kRegisterClient = 12u,
  kRegisterService = 13u,
  kUnregisterEntity = 14u,
  kPublishSample = 20u,
  kDeliverSample = 21u,
  kGraphUpdate = 30u,
};

enum class EndpointKind : uint8_t
{
  kPublisher = 1u,
  kSubscription = 2u,
  kClient = 3u,
  kService = 4u,
};

enum class DecodeStatus
{
  kOk,
  kNeedMore,
  kError,
};

struct Frame
{
  MessageKind kind = MessageKind::kError;
  uint64_t request_id = 0u;
  std::vector<uint8_t> payload;
};

struct EndpointDescriptor
{
  uint64_t entity_id = 0u;
  uint64_t local_context_id = 0u;
  uint32_t domain_id = 0u;
  EndpointKind kind = EndpointKind::kPublisher;
  std::string node_name;
  std::string node_namespace;
  std::string node_enclave;
  std::string topic_name;
  std::string type_name;
  std::string mdds_type_name;
  rosidl_type_hash_t type_hash = rosidl_get_zero_initialized_type_hash();
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  bool ignore_local_publications = false;
};

struct SampleMessage
{
  uint64_t entity_id = 0u;
  uint64_t sequence_number = 0u;
  bool mdds_payload = false;
  std::vector<uint8_t> payload;
};

struct GraphUpdateMessage
{
  uint64_t broker_id = 0u;
  uint64_t epoch = 0u;
  std::vector<EndpointDescriptor> endpoints;
};

std::vector<uint8_t> EncodeFrame(const Frame & frame);
DecodeStatus DecodeFrame(const uint8_t * data, size_t size, Frame * frame, std::string * error);

std::vector<uint8_t> EncodeEndpointDescriptor(const EndpointDescriptor & endpoint);
bool DecodeEndpointDescriptor(
  const uint8_t * data, size_t size, EndpointDescriptor * endpoint, std::string * error);

std::vector<uint8_t> EncodeEndpointList(const std::vector<EndpointDescriptor> & endpoints);
bool DecodeEndpointList(
  const uint8_t * data, size_t size, std::vector<EndpointDescriptor> * endpoints,
  std::string * error);

std::vector<uint8_t> EncodeGraphUpdate(
  uint64_t broker_id, uint64_t epoch, const std::vector<EndpointDescriptor> & endpoints);
bool DecodeGraphUpdate(
  const uint8_t * data, size_t size, GraphUpdateMessage * update, std::string * error);

std::vector<uint8_t> EncodeEntityId(uint64_t entity_id);
bool DecodeEntityId(const uint8_t * data, size_t size, uint64_t * entity_id, std::string * error);

std::vector<uint8_t> EncodeSampleMessage(const SampleMessage & sample);
bool DecodeSampleMessage(
  const uint8_t * data, size_t size, SampleMessage * sample, std::string * error);

}  // namespace ipc
}  // namespace rmw_mdds_cpp

#endif  // RMW_MDDS_CPP_SRC__IPC_PROTOCOL_HPP_
