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

#include "ipc_protocol.hpp"

#include <limits>
#include <utility>

namespace rmw_mdds_cpp
{
namespace ipc
{
namespace
{
constexpr uint32_t kFrameMagic = 0x3149504du;  // "MPI1" in little-endian byte order.
constexpr uint16_t kFrameVersion = 1u;

void SetError(std::string * error, const char * message)
{
  if (error != nullptr) {
    *error = message;
  }
}

void AppendU8(std::vector<uint8_t> * out, uint8_t value)
{
  out->push_back(value);
}

void AppendU16(std::vector<uint8_t> * out, uint16_t value)
{
  out->push_back(static_cast<uint8_t>(value & 0xffu));
  out->push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
}

void AppendU32(std::vector<uint8_t> * out, uint32_t value)
{
  out->push_back(static_cast<uint8_t>(value & 0xffu));
  out->push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  out->push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  out->push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

void AppendU64(std::vector<uint8_t> * out, uint64_t value)
{
  for (size_t i = 0; i < sizeof(value); ++i) {
    out->push_back(static_cast<uint8_t>((value >> (8u * i)) & 0xffu));
  }
}

void AppendString(std::vector<uint8_t> * out, const std::string & value)
{
  if (value.size() > std::numeric_limits<uint32_t>::max()) {
    AppendU32(out, 0u);
    return;
  }
  AppendU32(out, static_cast<uint32_t>(value.size()));
  out->insert(out->end(), value.begin(), value.end());
}

void AppendTypeHash(std::vector<uint8_t> * out, const rosidl_type_hash_t & type_hash)
{
  AppendU8(out, type_hash.version);
  for (size_t i = 0; i < ROSIDL_TYPE_HASH_SIZE; ++i) {
    out->push_back(type_hash.value[i]);
  }
}

bool ReadU8(const uint8_t * data, size_t size, size_t * offset, uint8_t * value)
{
  if (data == nullptr || offset == nullptr || value == nullptr || *offset + 1u > size) {
    return false;
  }
  *value = data[*offset];
  ++(*offset);
  return true;
}

bool ReadU16(const uint8_t * data, size_t size, size_t * offset, uint16_t * value)
{
  if (data == nullptr || offset == nullptr || value == nullptr || *offset + 2u > size) {
    return false;
  }
  *value = static_cast<uint16_t>(data[*offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[*offset + 1u]) << 8u);
  *offset += 2u;
  return true;
}

bool ReadU32(const uint8_t * data, size_t size, size_t * offset, uint32_t * value)
{
  if (data == nullptr || offset == nullptr || value == nullptr || *offset + 4u > size) {
    return false;
  }
  *value = static_cast<uint32_t>(data[*offset]) |
           (static_cast<uint32_t>(data[*offset + 1u]) << 8u) |
           (static_cast<uint32_t>(data[*offset + 2u]) << 16u) |
           (static_cast<uint32_t>(data[*offset + 3u]) << 24u);
  *offset += 4u;
  return true;
}

bool ReadU64(const uint8_t * data, size_t size, size_t * offset, uint64_t * value)
{
  if (data == nullptr || offset == nullptr || value == nullptr || *offset + 8u > size) {
    return false;
  }
  uint64_t result = 0u;
  for (size_t i = 0; i < sizeof(result); ++i) {
    result |= static_cast<uint64_t>(data[*offset + i]) << (8u * i);
  }
  *value = result;
  *offset += 8u;
  return true;
}

bool ReadTypeHash(const uint8_t * data, size_t size, size_t * offset, rosidl_type_hash_t * type_hash)
{
  if (type_hash == nullptr) {
    return false;
  }
  rosidl_type_hash_t decoded = rosidl_get_zero_initialized_type_hash();
  if (!ReadU8(data, size, offset, &decoded.version)) {
    return false;
  }
  if (offset == nullptr || *offset + ROSIDL_TYPE_HASH_SIZE > size) {
    return false;
  }
  for (size_t i = 0; i < ROSIDL_TYPE_HASH_SIZE; ++i) {
    decoded.value[i] = data[*offset + i];
  }
  *offset += ROSIDL_TYPE_HASH_SIZE;
  *type_hash = decoded;
  return true;
}

bool ReadString(
  const uint8_t * data, size_t size, size_t * offset, std::string * value, std::string * error)
{
  uint32_t length = 0u;
  if (!ReadU32(data, size, offset, &length)) {
    SetError(error, "missing string length");
    return false;
  }
  if (*offset > size || size - *offset < length) {
    SetError(error, "truncated string payload");
    return false;
  }
  value->assign(
    reinterpret_cast<const char *>(data + *offset),
    reinterpret_cast<const char *>(data + *offset + length));
  *offset += length;
  return true;
}

bool IsKnownMessageKind(uint16_t kind)
{
  switch (static_cast<MessageKind>(kind)) {
    case MessageKind::kHello:
    case MessageKind::kAck:
    case MessageKind::kError:
    case MessageKind::kRegisterPublisher:
    case MessageKind::kRegisterSubscription:
    case MessageKind::kRegisterClient:
    case MessageKind::kRegisterService:
    case MessageKind::kUnregisterEntity:
    case MessageKind::kPublishSample:
    case MessageKind::kDeliverSample:
    case MessageKind::kGraphUpdate:
      return true;
  }
  return false;
}

bool IsKnownEndpointKind(uint8_t kind)
{
  switch (static_cast<EndpointKind>(kind)) {
    case EndpointKind::kPublisher:
    case EndpointKind::kSubscription:
    case EndpointKind::kClient:
    case EndpointKind::kService:
      return true;
  }
  return false;
}

void AppendQos(std::vector<uint8_t> * out, const rmw_qos_profile_t & qos)
{
  AppendU32(out, static_cast<uint32_t>(qos.history));
  AppendU64(out, static_cast<uint64_t>(qos.depth));
  AppendU32(out, static_cast<uint32_t>(qos.reliability));
  AppendU32(out, static_cast<uint32_t>(qos.durability));
  AppendU64(out, qos.deadline.sec);
  AppendU64(out, qos.deadline.nsec);
  AppendU64(out, qos.lifespan.sec);
  AppendU64(out, qos.lifespan.nsec);
  AppendU32(out, static_cast<uint32_t>(qos.liveliness));
  AppendU64(out, qos.liveliness_lease_duration.sec);
  AppendU64(out, qos.liveliness_lease_duration.nsec);
  AppendU8(out, qos.avoid_ros_namespace_conventions ? 1u : 0u);
}

bool ReadQos(const uint8_t * data, size_t size, size_t * offset, rmw_qos_profile_t * qos)
{
  uint32_t policy = 0u;
  uint64_t depth = 0u;
  uint8_t avoid_ros_namespace_conventions = 0u;

  if (!ReadU32(data, size, offset, &policy)) {
    return false;
  }
  qos->history = static_cast<rmw_qos_history_policy_t>(policy);
  if (!ReadU64(data, size, offset, &depth)) {
    return false;
  }
  qos->depth = static_cast<size_t>(depth);
  if (!ReadU32(data, size, offset, &policy)) {
    return false;
  }
  qos->reliability = static_cast<rmw_qos_reliability_policy_t>(policy);
  if (!ReadU32(data, size, offset, &policy)) {
    return false;
  }
  qos->durability = static_cast<rmw_qos_durability_policy_t>(policy);
  if (!ReadU64(data, size, offset, &qos->deadline.sec) ||
      !ReadU64(data, size, offset, &qos->deadline.nsec) ||
      !ReadU64(data, size, offset, &qos->lifespan.sec) ||
      !ReadU64(data, size, offset, &qos->lifespan.nsec)) {
    return false;
  }
  if (!ReadU32(data, size, offset, &policy)) {
    return false;
  }
  qos->liveliness = static_cast<rmw_qos_liveliness_policy_t>(policy);
  if (!ReadU64(data, size, offset, &qos->liveliness_lease_duration.sec) ||
      !ReadU64(data, size, offset, &qos->liveliness_lease_duration.nsec) ||
      !ReadU8(data, size, offset, &avoid_ros_namespace_conventions)) {
    return false;
  }
  qos->avoid_ros_namespace_conventions = avoid_ros_namespace_conventions != 0u;
  return true;
}
}  // namespace

std::vector<uint8_t> EncodeFrame(const Frame & frame)
{
  if (frame.payload.size() > kMaxFramePayloadSize) {
    return {};
  }
  std::vector<uint8_t> out;
  out.reserve(kFrameHeaderSize + frame.payload.size());
  AppendU32(&out, kFrameMagic);
  AppendU16(&out, kFrameVersion);
  AppendU16(&out, static_cast<uint16_t>(frame.kind));
  AppendU64(&out, frame.request_id);
  AppendU32(&out, static_cast<uint32_t>(frame.payload.size()));
  out.insert(out.end(), frame.payload.begin(), frame.payload.end());
  return out;
}

DecodeStatus DecodeFrame(const uint8_t * data, size_t size, Frame * frame, std::string * error)
{
  if (frame == nullptr) {
    SetError(error, "frame output is null");
    return DecodeStatus::kError;
  }
  if (data == nullptr && size != 0u) {
    SetError(error, "frame data is null");
    return DecodeStatus::kError;
  }
  if (size < kFrameHeaderSize) {
    return DecodeStatus::kNeedMore;
  }

  size_t offset = 0u;
  uint32_t magic = 0u;
  uint16_t version = 0u;
  uint16_t raw_kind = 0u;
  uint64_t request_id = 0u;
  uint32_t payload_size = 0u;
  if (!ReadU32(data, size, &offset, &magic) || !ReadU16(data, size, &offset, &version) ||
      !ReadU16(data, size, &offset, &raw_kind) ||
      !ReadU64(data, size, &offset, &request_id) ||
      !ReadU32(data, size, &offset, &payload_size)) {
    return DecodeStatus::kNeedMore;
  }
  if (magic != kFrameMagic) {
    SetError(error, "invalid frame magic");
    return DecodeStatus::kError;
  }
  if (version != kFrameVersion) {
    SetError(error, "unsupported frame version");
    return DecodeStatus::kError;
  }
  if (!IsKnownMessageKind(raw_kind)) {
    SetError(error, "unknown frame message kind");
    return DecodeStatus::kError;
  }
  if (payload_size > kMaxFramePayloadSize) {
    SetError(error, "frame payload exceeds maximum size");
    return DecodeStatus::kError;
  }
  if (size - kFrameHeaderSize < payload_size) {
    return DecodeStatus::kNeedMore;
  }

  frame->kind = static_cast<MessageKind>(raw_kind);
  frame->request_id = request_id;
  frame->payload.assign(data + kFrameHeaderSize, data + kFrameHeaderSize + payload_size);
  return DecodeStatus::kOk;
}

std::vector<uint8_t> EncodeEndpointDescriptor(const EndpointDescriptor & endpoint)
{
  std::vector<uint8_t> out;
  AppendU64(&out, endpoint.entity_id);
  AppendU64(&out, endpoint.local_context_id);
  AppendU8(&out, static_cast<uint8_t>(endpoint.kind));
  AppendQos(&out, endpoint.qos);
  AppendU8(&out, endpoint.ignore_local_publications ? 1u : 0u);
  AppendString(&out, endpoint.node_name);
  AppendString(&out, endpoint.node_namespace);
  AppendString(&out, endpoint.node_enclave);
  AppendString(&out, endpoint.topic_name);
  AppendString(&out, endpoint.type_name);
  AppendString(&out, endpoint.mdds_type_name);
  AppendTypeHash(&out, endpoint.type_hash);
  return out;
}

bool DecodeEndpointDescriptor(
  const uint8_t * data, size_t size, EndpointDescriptor * endpoint, std::string * error)
{
  if (endpoint == nullptr) {
    SetError(error, "endpoint output is null");
    return false;
  }
  size_t offset = 0u;
  uint8_t raw_kind = 0u;
  EndpointDescriptor decoded;
  if (!ReadU64(data, size, &offset, &decoded.entity_id) ||
      !ReadU64(data, size, &offset, &decoded.local_context_id) ||
      !ReadU8(data, size, &offset, &raw_kind)) {
    SetError(error, "truncated endpoint header");
    return false;
  }
  if (!IsKnownEndpointKind(raw_kind)) {
    SetError(error, "unknown endpoint kind");
    return false;
  }
  decoded.kind = static_cast<EndpointKind>(raw_kind);
  if (!ReadQos(data, size, &offset, &decoded.qos)) {
    SetError(error, "truncated endpoint qos");
    return false;
  }
  uint8_t ignore_local_publications = 0u;
  if (!ReadU8(data, size, &offset, &ignore_local_publications)) {
    SetError(error, "truncated endpoint ignore-local flag");
    return false;
  }
  decoded.ignore_local_publications = ignore_local_publications != 0u;
  if (!ReadString(data, size, &offset, &decoded.node_name, error) ||
      !ReadString(data, size, &offset, &decoded.node_namespace, error) ||
      !ReadString(data, size, &offset, &decoded.node_enclave, error) ||
      !ReadString(data, size, &offset, &decoded.topic_name, error) ||
      !ReadString(data, size, &offset, &decoded.type_name, error) ||
      !ReadString(data, size, &offset, &decoded.mdds_type_name, error)) {
    return false;
  }
  if (offset < size && !ReadTypeHash(data, size, &offset, &decoded.type_hash)) {
    SetError(error, "truncated endpoint type hash");
    return false;
  }
  if (offset != size) {
    SetError(error, "endpoint payload has trailing bytes");
    return false;
  }
  *endpoint = std::move(decoded);
  return true;
}

std::vector<uint8_t> EncodeEndpointList(const std::vector<EndpointDescriptor> & endpoints)
{
  if (endpoints.size() > std::numeric_limits<uint32_t>::max()) {
    return {};
  }
  std::vector<uint8_t> out;
  AppendU32(&out, static_cast<uint32_t>(endpoints.size()));
  for (const auto & endpoint : endpoints) {
    std::vector<uint8_t> encoded_endpoint = EncodeEndpointDescriptor(endpoint);
    if (encoded_endpoint.size() > std::numeric_limits<uint32_t>::max()) {
      return {};
    }
    AppendU32(&out, static_cast<uint32_t>(encoded_endpoint.size()));
    out.insert(out.end(), encoded_endpoint.begin(), encoded_endpoint.end());
  }
  return out;
}

bool DecodeEndpointList(
  const uint8_t * data, size_t size, std::vector<EndpointDescriptor> * endpoints,
  std::string * error)
{
  if (endpoints == nullptr) {
    SetError(error, "endpoint list output is null");
    return false;
  }
  size_t offset = 0u;
  uint32_t endpoint_count = 0u;
  if (!ReadU32(data, size, &offset, &endpoint_count)) {
    SetError(error, "truncated endpoint list header");
    return false;
  }
  std::vector<EndpointDescriptor> decoded;
  decoded.reserve(endpoint_count);
  for (uint32_t i = 0; i < endpoint_count; ++i) {
    uint32_t endpoint_size = 0u;
    if (!ReadU32(data, size, &offset, &endpoint_size)) {
      SetError(error, "truncated endpoint list entry header");
      return false;
    }
    if (size - offset < endpoint_size) {
      SetError(error, "truncated endpoint list entry payload");
      return false;
    }
    EndpointDescriptor endpoint;
    if (!DecodeEndpointDescriptor(data + offset, endpoint_size, &endpoint, error)) {
      return false;
    }
    offset += endpoint_size;
    decoded.push_back(std::move(endpoint));
  }
  if (offset != size) {
    SetError(error, "endpoint list payload has trailing bytes");
    return false;
  }
  *endpoints = std::move(decoded);
  return true;
}

std::vector<uint8_t> EncodeSampleMessage(const SampleMessage & sample)
{
  if (sample.payload.size() > std::numeric_limits<uint32_t>::max()) {
    return {};
  }
  std::vector<uint8_t> out;
  out.reserve(8u + 8u + 1u + 4u + sample.payload.size());
  AppendU64(&out, sample.entity_id);
  AppendU64(&out, sample.sequence_number);
  AppendU8(&out, sample.mdds_payload ? 1u : 0u);
  AppendU32(&out, static_cast<uint32_t>(sample.payload.size()));
  out.insert(out.end(), sample.payload.begin(), sample.payload.end());
  return out;
}

bool DecodeSampleMessage(
  const uint8_t * data, size_t size, SampleMessage * sample, std::string * error)
{
  if (sample == nullptr) {
    SetError(error, "sample output is null");
    return false;
  }
  size_t offset = 0u;
  SampleMessage decoded;
  uint32_t payload_size = 0u;
  uint8_t mdds_payload = 0u;
  if (!ReadU64(data, size, &offset, &decoded.entity_id) ||
      !ReadU64(data, size, &offset, &decoded.sequence_number) ||
      !ReadU8(data, size, &offset, &mdds_payload) ||
      !ReadU32(data, size, &offset, &payload_size)) {
    SetError(error, "truncated sample header");
    return false;
  }
  decoded.mdds_payload = mdds_payload != 0u;
  if (size - offset < payload_size) {
    SetError(error, "truncated sample payload");
    return false;
  }
  decoded.payload.assign(data + offset, data + offset + payload_size);
  offset += payload_size;
  if (offset != size) {
    SetError(error, "sample payload has trailing bytes");
    return false;
  }
  *sample = std::move(decoded);
  return true;
}

}  // namespace ipc
}  // namespace rmw_mdds_cpp
