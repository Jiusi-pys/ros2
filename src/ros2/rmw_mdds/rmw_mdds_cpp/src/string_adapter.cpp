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

#include "string_adapter.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#include "rmw/error_handling.h"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_runtime_c/string_functions.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"

namespace rmw_mdds_cpp
{

namespace
{
using CMessageMember = rosidl_typesupport_introspection_c__MessageMember;
using CMessageMembers = rosidl_typesupport_introspection_c__MessageMembers;
using CppMessageMember = rosidl_typesupport_introspection_cpp::MessageMember;
using CppMessageMembers = rosidl_typesupport_introspection_cpp::MessageMembers;

std::string MakeRosTypeName(const rosidl_typesupport_introspection_cpp::MessageMembers * members)
{
  std::string ns(members->message_namespace_);
  size_t pos = 0;
  while ((pos = ns.find("::", pos)) != std::string::npos) {
    ns.replace(pos, 2, "/");
    pos += 1;
  }
  return ns + "/" + members->message_name_;
}

std::string MakeRosTypeName(const rosidl_typesupport_introspection_c__MessageMembers * members)
{
  std::string ns(members->message_namespace_);
  size_t pos = 0;
  while ((pos = ns.find("__", pos)) != std::string::npos) {
    ns.replace(pos, 2, "/");
    pos += 1;
  }
  return ns + "/" + members->message_name_;
}

size_t ScalarSize(uint8_t type_id)
{
  switch (type_id) {
    case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
      return sizeof(float);
    case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE:
      return sizeof(double);
    case rosidl_typesupport_introspection_c__ROS_TYPE_LONG_DOUBLE:
      return sizeof(long double);
    case rosidl_typesupport_introspection_c__ROS_TYPE_CHAR:
      return sizeof(char);
    case rosidl_typesupport_introspection_c__ROS_TYPE_WCHAR:
      return sizeof(char16_t);
    case rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN:
      return sizeof(bool);
    case rosidl_typesupport_introspection_c__ROS_TYPE_OCTET:
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
      return sizeof(uint8_t);
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
      return sizeof(int8_t);
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
      return sizeof(uint16_t);
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
      return sizeof(int16_t);
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
      return sizeof(uint32_t);
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
      return sizeof(int32_t);
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
      return sizeof(uint64_t);
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
      return sizeof(int64_t);
    default:
      return 0;
  }
}

void AppendBytes(const void * data, size_t size, std::vector<uint8_t> * payload)
{
  const auto * bytes = static_cast<const uint8_t *>(data);
  payload->insert(payload->end(), bytes, bytes + size);
}

bool ReadBytes(const uint8_t * data, size_t len, size_t * offset, void * out, size_t size)
{
  if (
    data == nullptr || offset == nullptr || out == nullptr || *offset > len ||
    len - *offset < size) {
    return false;
  }
  std::memcpy(out, data + *offset, size);
  *offset += size;
  return true;
}

bool AppendSequenceSize(size_t size, std::vector<uint8_t> * payload)
{
  if (size > std::numeric_limits<uint32_t>::max() || payload == nullptr) {
    return false;
  }
  const uint32_t encoded_size = static_cast<uint32_t>(size);
  AppendBytes(&encoded_size, sizeof(encoded_size), payload);
  return true;
}

bool AddPayloadSize(size_t value, size_t * payload_size)
{
  if (payload_size == nullptr || value > std::numeric_limits<size_t>::max() - *payload_size) {
    return false;
  }
  *payload_size += value;
  return true;
}

bool AddRepeatedPayloadSize(size_t count, size_t value_size, size_t * payload_size)
{
  if (value_size != 0 && count > std::numeric_limits<size_t>::max() / value_size) {
    return false;
  }
  return AddPayloadSize(count * value_size, payload_size);
}

bool MeasureSequenceSize(size_t size, size_t * payload_size)
{
  if (size > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  return AddPayloadSize(sizeof(uint32_t), payload_size);
}

bool WriteBytes(
  const void * data, size_t size, uint8_t * buffer, size_t capacity, size_t * offset)
{
  if (
    buffer == nullptr || offset == nullptr || *offset > capacity ||
    capacity - *offset < size || (data == nullptr && size != 0)) {
    return false;
  }
  if (size != 0) {
    std::memcpy(buffer + *offset, data, size);
  }
  *offset += size;
  return true;
}

bool WriteSequenceSize(size_t size, uint8_t * buffer, size_t capacity, size_t * offset)
{
  if (size > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  const uint32_t encoded_size = static_cast<uint32_t>(size);
  return WriteBytes(&encoded_size, sizeof(encoded_size), buffer, capacity, offset);
}

bool ReadSequenceSize(const uint8_t * data, size_t len, size_t * offset, size_t * size)
{
  if (size == nullptr) {
    return false;
  }
  uint32_t encoded_size = 0;
  if (!ReadBytes(data, len, offset, &encoded_size, sizeof(encoded_size))) {
    return false;
  }
  *size = encoded_size;
  return true;
}

const CppMessageMembers * ResolveCppMembers(const rosidl_message_type_support_t * type_support)
{
  if (type_support == nullptr) {
    return nullptr;
  }
  const rosidl_message_type_support_t * introspection = get_message_typesupport_handle(
    type_support, rosidl_typesupport_introspection_cpp::typesupport_identifier);
  if (introspection == nullptr || introspection->data == nullptr) {
    return nullptr;
  }
  return static_cast<const CppMessageMembers *>(introspection->data);
}

const CMessageMembers * ResolveCMembers(const rosidl_message_type_support_t * type_support)
{
  if (type_support == nullptr) {
    return nullptr;
  }
  const rosidl_message_type_support_t * introspection =
    get_message_typesupport_handle(type_support, rosidl_typesupport_introspection_c__identifier);
  if (introspection == nullptr || introspection->data == nullptr) {
    return nullptr;
  }
  return static_cast<const CMessageMembers *>(introspection->data);
}

struct PoseStampedWireFields
{
  std::string frame_id;
  int32_t stamp_sec = 0;
  uint32_t stamp_nanosec = 0;
  double position[3] = {0.0, 0.0, 0.0};
  double orientation[4] = {0.0, 0.0, 0.0, 0.0};
};

const CppMessageMember * FindCppMember(const CppMessageMembers * members, const char * name)
{
  if (members == nullptr || name == nullptr || members->members_ == nullptr) {
    return nullptr;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const CppMessageMember & member = members->members_[i];
    if (member.name_ != nullptr && std::strcmp(member.name_, name) == 0) {
      return &member;
    }
  }
  return nullptr;
}

const CMessageMember * FindCMember(const CMessageMembers * members, const char * name)
{
  if (members == nullptr || name == nullptr || members->members_ == nullptr) {
    return nullptr;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const CMessageMember & member = members->members_[i];
    if (member.name_ != nullptr && std::strcmp(member.name_, name) == 0) {
      return &member;
    }
  }
  return nullptr;
}

bool ReadCppScalar(
  const CppMessageMember * member, const uint8_t * base, uint8_t expected_type, void * out,
  size_t size)
{
  if (
    member == nullptr || base == nullptr || out == nullptr || member->is_array_ ||
    member->type_id_ != expected_type) {
    return false;
  }
  std::memcpy(out, base + member->offset_, size);
  return true;
}

bool ReadCScalar(
  const CMessageMember * member, const uint8_t * base, uint8_t expected_type, void * out,
  size_t size)
{
  if (
    member == nullptr || base == nullptr || out == nullptr || member->is_array_ ||
    member->type_id_ != expected_type) {
    return false;
  }
  std::memcpy(out, base + member->offset_, size);
  return true;
}

bool WriteCppScalar(
  const CppMessageMember * member, uint8_t * base, uint8_t expected_type, const void * value,
  size_t size)
{
  if (
    member == nullptr || base == nullptr || value == nullptr || member->is_array_ ||
    member->type_id_ != expected_type) {
    return false;
  }
  std::memcpy(base + member->offset_, value, size);
  return true;
}

bool WriteCScalar(
  const CMessageMember * member, uint8_t * base, uint8_t expected_type, const void * value,
  size_t size)
{
  if (
    member == nullptr || base == nullptr || value == nullptr || member->is_array_ ||
    member->type_id_ != expected_type) {
    return false;
  }
  std::memcpy(base + member->offset_, value, size);
  return true;
}

bool AppendPoseStampedWire(const PoseStampedWireFields & fields, std::vector<uint8_t> * payload)
{
  if (payload == nullptr || !AppendSequenceSize(fields.frame_id.size(), payload)) {
    return false;
  }
  if (!fields.frame_id.empty()) {
    AppendBytes(fields.frame_id.data(), fields.frame_id.size(), payload);
  }
  AppendBytes(&fields.stamp_sec, sizeof(fields.stamp_sec), payload);
  AppendBytes(&fields.stamp_nanosec, sizeof(fields.stamp_nanosec), payload);
  for (double value : fields.position) {
    AppendBytes(&value, sizeof(value), payload);
  }
  for (double value : fields.orientation) {
    AppendBytes(&value, sizeof(value), payload);
  }
  return true;
}

size_t PoseStampedWireSize(const PoseStampedWireFields & fields)
{
  return sizeof(uint32_t) + fields.frame_id.size() + sizeof(fields.stamp_sec) +
         sizeof(fields.stamp_nanosec) + (sizeof(double) * 7u);
}

bool WritePoseStampedWire(
  const PoseStampedWireFields & fields, uint8_t * buffer, size_t capacity, size_t * payload_size)
{
  if (payload_size == nullptr) {
    return false;
  }
  *payload_size = PoseStampedWireSize(fields);
  size_t offset = 0;
  return WriteSequenceSize(fields.frame_id.size(), buffer, capacity, &offset) &&
         WriteBytes(fields.frame_id.data(), fields.frame_id.size(), buffer, capacity, &offset) &&
         WriteBytes(&fields.stamp_sec, sizeof(fields.stamp_sec), buffer, capacity, &offset) &&
         WriteBytes(
           &fields.stamp_nanosec, sizeof(fields.stamp_nanosec), buffer, capacity, &offset) &&
         WriteBytes(&fields.position[0], sizeof(fields.position[0]), buffer, capacity, &offset) &&
         WriteBytes(&fields.position[1], sizeof(fields.position[1]), buffer, capacity, &offset) &&
         WriteBytes(&fields.position[2], sizeof(fields.position[2]), buffer, capacity, &offset) &&
         WriteBytes(
           &fields.orientation[0], sizeof(fields.orientation[0]), buffer, capacity, &offset) &&
         WriteBytes(
           &fields.orientation[1], sizeof(fields.orientation[1]), buffer, capacity, &offset) &&
         WriteBytes(
           &fields.orientation[2], sizeof(fields.orientation[2]), buffer, capacity, &offset) &&
         WriteBytes(
           &fields.orientation[3], sizeof(fields.orientation[3]), buffer, capacity, &offset) &&
         offset == *payload_size;
}

bool ReadPoseStampedWire(const uint8_t * data, size_t len, PoseStampedWireFields * fields)
{
  if (fields == nullptr) {
    return false;
  }
  size_t offset = 0;
  size_t frame_id_size = 0;
  if (!ReadSequenceSize(data, len, &offset, &frame_id_size)) {
    return false;
  }
  if (data == nullptr || offset > len || len - offset < frame_id_size) {
    return false;
  }
  fields->frame_id.assign(reinterpret_cast<const char *>(data + offset), frame_id_size);
  offset += frame_id_size;
  if (
    !ReadBytes(data, len, &offset, &fields->stamp_sec, sizeof(fields->stamp_sec)) ||
    !ReadBytes(data, len, &offset, &fields->stamp_nanosec, sizeof(fields->stamp_nanosec))) {
    return false;
  }
  for (double & value : fields->position) {
    if (!ReadBytes(data, len, &offset, &value, sizeof(value))) {
      return false;
    }
  }
  for (double & value : fields->orientation) {
    if (!ReadBytes(data, len, &offset, &value, sizeof(value))) {
      return false;
    }
  }
  return offset == len;
}

bool ExtractCppPoseStamped(
  const CppMessageMembers * members, const void * ros_message, PoseStampedWireFields * fields)
{
  if (members == nullptr || ros_message == nullptr || fields == nullptr) {
    return false;
  }
  const auto * base = static_cast<const uint8_t *>(ros_message);
  const CppMessageMember * header = FindCppMember(members, "header");
  const CppMessageMember * pose = FindCppMember(members, "pose");
  const CppMessageMembers * header_members = ResolveCppMembers(header == nullptr ? nullptr : header->members_);
  const CppMessageMembers * pose_members = ResolveCppMembers(pose == nullptr ? nullptr : pose->members_);
  if (
    header == nullptr || pose == nullptr ||
    header->type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE ||
    pose->type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE ||
    header_members == nullptr || pose_members == nullptr) {
    return false;
  }

  const auto * header_base = base + header->offset_;
  const CppMessageMember * frame_id = FindCppMember(header_members, "frame_id");
  const CppMessageMember * stamp = FindCppMember(header_members, "stamp");
  const CppMessageMembers * stamp_members = ResolveCppMembers(stamp == nullptr ? nullptr : stamp->members_);
  if (
    frame_id == nullptr || stamp == nullptr || frame_id->is_array_ ||
    frame_id->type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING ||
    stamp->type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE ||
    stamp_members == nullptr) {
    return false;
  }
  fields->frame_id =
    *reinterpret_cast<const std::string *>(header_base + frame_id->offset_);

  const auto * stamp_base = header_base + stamp->offset_;
  if (
    !ReadCppScalar(
      FindCppMember(stamp_members, "sec"), stamp_base,
      rosidl_typesupport_introspection_cpp::ROS_TYPE_INT32, &fields->stamp_sec,
      sizeof(fields->stamp_sec)) ||
    !ReadCppScalar(
      FindCppMember(stamp_members, "nanosec"), stamp_base,
      rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT32, &fields->stamp_nanosec,
      sizeof(fields->stamp_nanosec))) {
    return false;
  }

  const auto * pose_base = base + pose->offset_;
  const CppMessageMember * position = FindCppMember(pose_members, "position");
  const CppMessageMember * orientation = FindCppMember(pose_members, "orientation");
  const CppMessageMembers * position_members =
    ResolveCppMembers(position == nullptr ? nullptr : position->members_);
  const CppMessageMembers * orientation_members =
    ResolveCppMembers(orientation == nullptr ? nullptr : orientation->members_);
  if (
    position == nullptr || orientation == nullptr ||
    position->type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE ||
    orientation->type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE ||
    position_members == nullptr || orientation_members == nullptr) {
    return false;
  }
  const auto * position_base = pose_base + position->offset_;
  const auto * orientation_base = pose_base + orientation->offset_;
  return ReadCppScalar(
           FindCppMember(position_members, "x"), position_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields->position[0],
           sizeof(fields->position[0])) &&
         ReadCppScalar(
           FindCppMember(position_members, "y"), position_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields->position[1],
           sizeof(fields->position[1])) &&
         ReadCppScalar(
           FindCppMember(position_members, "z"), position_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields->position[2],
           sizeof(fields->position[2])) &&
         ReadCppScalar(
           FindCppMember(orientation_members, "x"), orientation_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields->orientation[0],
           sizeof(fields->orientation[0])) &&
         ReadCppScalar(
           FindCppMember(orientation_members, "y"), orientation_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields->orientation[1],
           sizeof(fields->orientation[1])) &&
         ReadCppScalar(
           FindCppMember(orientation_members, "z"), orientation_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields->orientation[2],
           sizeof(fields->orientation[2])) &&
         ReadCppScalar(
           FindCppMember(orientation_members, "w"), orientation_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields->orientation[3],
           sizeof(fields->orientation[3]));
}

bool ExtractCPoseStamped(
  const CMessageMembers * members, const void * ros_message, PoseStampedWireFields * fields)
{
  if (members == nullptr || ros_message == nullptr || fields == nullptr) {
    return false;
  }
  const auto * base = static_cast<const uint8_t *>(ros_message);
  const CMessageMember * header = FindCMember(members, "header");
  const CMessageMember * pose = FindCMember(members, "pose");
  const CMessageMembers * header_members = ResolveCMembers(header == nullptr ? nullptr : header->members_);
  const CMessageMembers * pose_members = ResolveCMembers(pose == nullptr ? nullptr : pose->members_);
  if (
    header == nullptr || pose == nullptr ||
    header->type_id_ != rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE ||
    pose->type_id_ != rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE ||
    header_members == nullptr || pose_members == nullptr) {
    return false;
  }

  const auto * header_base = base + header->offset_;
  const CMessageMember * frame_id = FindCMember(header_members, "frame_id");
  const CMessageMember * stamp = FindCMember(header_members, "stamp");
  const CMessageMembers * stamp_members = ResolveCMembers(stamp == nullptr ? nullptr : stamp->members_);
  if (
    frame_id == nullptr || stamp == nullptr || frame_id->is_array_ ||
    frame_id->type_id_ != rosidl_typesupport_introspection_c__ROS_TYPE_STRING ||
    stamp->type_id_ != rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE ||
    stamp_members == nullptr) {
    return false;
  }
  const auto * frame_id_value =
    reinterpret_cast<const rosidl_runtime_c__String *>(header_base + frame_id->offset_);
  if (frame_id_value->data == nullptr && frame_id_value->size != 0) {
    return false;
  }
  fields->frame_id.assign(frame_id_value->data == nullptr ? "" : frame_id_value->data, frame_id_value->size);

  const auto * stamp_base = header_base + stamp->offset_;
  if (
    !ReadCScalar(
      FindCMember(stamp_members, "sec"), stamp_base,
      rosidl_typesupport_introspection_c__ROS_TYPE_INT32, &fields->stamp_sec,
      sizeof(fields->stamp_sec)) ||
    !ReadCScalar(
      FindCMember(stamp_members, "nanosec"), stamp_base,
      rosidl_typesupport_introspection_c__ROS_TYPE_UINT32, &fields->stamp_nanosec,
      sizeof(fields->stamp_nanosec))) {
    return false;
  }

  const auto * pose_base = base + pose->offset_;
  const CMessageMember * position = FindCMember(pose_members, "position");
  const CMessageMember * orientation = FindCMember(pose_members, "orientation");
  const CMessageMembers * position_members =
    ResolveCMembers(position == nullptr ? nullptr : position->members_);
  const CMessageMembers * orientation_members =
    ResolveCMembers(orientation == nullptr ? nullptr : orientation->members_);
  if (
    position == nullptr || orientation == nullptr ||
    position->type_id_ != rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE ||
    orientation->type_id_ != rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE ||
    position_members == nullptr || orientation_members == nullptr) {
    return false;
  }
  const auto * position_base = pose_base + position->offset_;
  const auto * orientation_base = pose_base + orientation->offset_;
  return ReadCScalar(
           FindCMember(position_members, "x"), position_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields->position[0],
           sizeof(fields->position[0])) &&
         ReadCScalar(
           FindCMember(position_members, "y"), position_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields->position[1],
           sizeof(fields->position[1])) &&
         ReadCScalar(
           FindCMember(position_members, "z"), position_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields->position[2],
           sizeof(fields->position[2])) &&
         ReadCScalar(
           FindCMember(orientation_members, "x"), orientation_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields->orientation[0],
           sizeof(fields->orientation[0])) &&
         ReadCScalar(
           FindCMember(orientation_members, "y"), orientation_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields->orientation[1],
           sizeof(fields->orientation[1])) &&
         ReadCScalar(
           FindCMember(orientation_members, "z"), orientation_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields->orientation[2],
           sizeof(fields->orientation[2])) &&
         ReadCScalar(
           FindCMember(orientation_members, "w"), orientation_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields->orientation[3],
           sizeof(fields->orientation[3]));
}

bool AssignCppPoseStamped(
  const CppMessageMembers * members, const PoseStampedWireFields & fields, void * ros_message)
{
  if (members == nullptr || ros_message == nullptr) {
    return false;
  }
  auto * base = static_cast<uint8_t *>(ros_message);
  const CppMessageMember * header = FindCppMember(members, "header");
  const CppMessageMember * pose = FindCppMember(members, "pose");
  const CppMessageMembers * header_members = ResolveCppMembers(header == nullptr ? nullptr : header->members_);
  const CppMessageMembers * pose_members = ResolveCppMembers(pose == nullptr ? nullptr : pose->members_);
  if (header_members == nullptr || pose_members == nullptr) {
    return false;
  }
  auto * header_base = base + header->offset_;
  const CppMessageMember * frame_id = FindCppMember(header_members, "frame_id");
  const CppMessageMember * stamp = FindCppMember(header_members, "stamp");
  const CppMessageMembers * stamp_members = ResolveCppMembers(stamp == nullptr ? nullptr : stamp->members_);
  if (frame_id == nullptr || stamp_members == nullptr) {
    return false;
  }
  auto * frame_id_value = reinterpret_cast<std::string *>(header_base + frame_id->offset_);
  *frame_id_value = fields.frame_id;
  auto * stamp_base = header_base + stamp->offset_;
  if (
    !WriteCppScalar(
      FindCppMember(stamp_members, "sec"), stamp_base,
      rosidl_typesupport_introspection_cpp::ROS_TYPE_INT32, &fields.stamp_sec,
      sizeof(fields.stamp_sec)) ||
    !WriteCppScalar(
      FindCppMember(stamp_members, "nanosec"), stamp_base,
      rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT32, &fields.stamp_nanosec,
      sizeof(fields.stamp_nanosec))) {
    return false;
  }

  auto * pose_base = base + pose->offset_;
  const CppMessageMember * position = FindCppMember(pose_members, "position");
  const CppMessageMember * orientation = FindCppMember(pose_members, "orientation");
  const CppMessageMembers * position_members =
    ResolveCppMembers(position == nullptr ? nullptr : position->members_);
  const CppMessageMembers * orientation_members =
    ResolveCppMembers(orientation == nullptr ? nullptr : orientation->members_);
  if (position_members == nullptr || orientation_members == nullptr) {
    return false;
  }
  auto * position_base = pose_base + position->offset_;
  auto * orientation_base = pose_base + orientation->offset_;
  return WriteCppScalar(
           FindCppMember(position_members, "x"), position_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields.position[0],
           sizeof(fields.position[0])) &&
         WriteCppScalar(
           FindCppMember(position_members, "y"), position_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields.position[1],
           sizeof(fields.position[1])) &&
         WriteCppScalar(
           FindCppMember(position_members, "z"), position_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields.position[2],
           sizeof(fields.position[2])) &&
         WriteCppScalar(
           FindCppMember(orientation_members, "x"), orientation_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields.orientation[0],
           sizeof(fields.orientation[0])) &&
         WriteCppScalar(
           FindCppMember(orientation_members, "y"), orientation_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields.orientation[1],
           sizeof(fields.orientation[1])) &&
         WriteCppScalar(
           FindCppMember(orientation_members, "z"), orientation_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields.orientation[2],
           sizeof(fields.orientation[2])) &&
         WriteCppScalar(
           FindCppMember(orientation_members, "w"), orientation_base,
           rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE, &fields.orientation[3],
           sizeof(fields.orientation[3]));
}

bool AssignCPoseStamped(
  const CMessageMembers * members, const PoseStampedWireFields & fields, void * ros_message)
{
  if (members == nullptr || ros_message == nullptr) {
    return false;
  }
  auto * base = static_cast<uint8_t *>(ros_message);
  const CMessageMember * header = FindCMember(members, "header");
  const CMessageMember * pose = FindCMember(members, "pose");
  const CMessageMembers * header_members = ResolveCMembers(header == nullptr ? nullptr : header->members_);
  const CMessageMembers * pose_members = ResolveCMembers(pose == nullptr ? nullptr : pose->members_);
  if (header_members == nullptr || pose_members == nullptr) {
    return false;
  }
  auto * header_base = base + header->offset_;
  const CMessageMember * frame_id = FindCMember(header_members, "frame_id");
  const CMessageMember * stamp = FindCMember(header_members, "stamp");
  const CMessageMembers * stamp_members = ResolveCMembers(stamp == nullptr ? nullptr : stamp->members_);
  if (frame_id == nullptr || stamp_members == nullptr) {
    return false;
  }
  auto * frame_id_value =
    reinterpret_cast<rosidl_runtime_c__String *>(header_base + frame_id->offset_);
  if (!rosidl_runtime_c__String__assignn(frame_id_value, fields.frame_id.data(), fields.frame_id.size())) {
    return false;
  }
  auto * stamp_base = header_base + stamp->offset_;
  if (
    !WriteCScalar(
      FindCMember(stamp_members, "sec"), stamp_base,
      rosidl_typesupport_introspection_c__ROS_TYPE_INT32, &fields.stamp_sec,
      sizeof(fields.stamp_sec)) ||
    !WriteCScalar(
      FindCMember(stamp_members, "nanosec"), stamp_base,
      rosidl_typesupport_introspection_c__ROS_TYPE_UINT32, &fields.stamp_nanosec,
      sizeof(fields.stamp_nanosec))) {
    return false;
  }

  auto * pose_base = base + pose->offset_;
  const CMessageMember * position = FindCMember(pose_members, "position");
  const CMessageMember * orientation = FindCMember(pose_members, "orientation");
  const CMessageMembers * position_members =
    ResolveCMembers(position == nullptr ? nullptr : position->members_);
  const CMessageMembers * orientation_members =
    ResolveCMembers(orientation == nullptr ? nullptr : orientation->members_);
  if (position_members == nullptr || orientation_members == nullptr) {
    return false;
  }
  auto * position_base = pose_base + position->offset_;
  auto * orientation_base = pose_base + orientation->offset_;
  return WriteCScalar(
           FindCMember(position_members, "x"), position_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields.position[0],
           sizeof(fields.position[0])) &&
         WriteCScalar(
           FindCMember(position_members, "y"), position_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields.position[1],
           sizeof(fields.position[1])) &&
         WriteCScalar(
           FindCMember(position_members, "z"), position_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields.position[2],
           sizeof(fields.position[2])) &&
         WriteCScalar(
           FindCMember(orientation_members, "x"), orientation_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields.orientation[0],
           sizeof(fields.orientation[0])) &&
         WriteCScalar(
           FindCMember(orientation_members, "y"), orientation_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields.orientation[1],
           sizeof(fields.orientation[1])) &&
         WriteCScalar(
           FindCMember(orientation_members, "z"), orientation_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields.orientation[2],
           sizeof(fields.orientation[2])) &&
         WriteCScalar(
           FindCMember(orientation_members, "w"), orientation_base,
           rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE, &fields.orientation[3],
           sizeof(fields.orientation[3]));
}

bool SupportsCppMessage(const CppMessageMembers * members);

bool SupportsCMessage(const CMessageMembers * members);

bool SupportsCppValue(const CppMessageMember & member)
{
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING) {
    return true;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE) {
    return SupportsCppMessage(ResolveCppMembers(member.members_));
  }
  return ScalarSize(member.type_id_) != 0;
}

bool SupportsCppMember(const CppMessageMember & member)
{
  if (!SupportsCppValue(member)) {
    return false;
  }
  if (!member.is_array_) {
    return true;
  }
  if (member.size_function == nullptr) {
    return false;
  }
  if (member.array_size_ == 0 && member.resize_function == nullptr) {
    return false;
  }
  if (
    member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING ||
    member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE) {
    return member.get_const_function != nullptr && member.get_function != nullptr;
  }
  if (member.get_const_function == nullptr && member.fetch_function == nullptr) {
    return false;
  }
  if (member.get_function == nullptr && member.assign_function == nullptr) {
    return false;
  }
  return true;
}

bool SupportsCppMessage(const CppMessageMembers * members)
{
  if (members == nullptr || (members->member_count_ != 0 && members->members_ == nullptr)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    if (!SupportsCppMember(members->members_[i])) {
      return false;
    }
  }
  return true;
}

bool SupportsCValue(const CMessageMember & member)
{
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_STRING) {
    return true;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
    return SupportsCMessage(ResolveCMembers(member.members_));
  }
  return ScalarSize(member.type_id_) != 0;
}

bool SupportsCMember(const CMessageMember & member)
{
  if (!SupportsCValue(member)) {
    return false;
  }
  if (!member.is_array_) {
    return true;
  }
  if (
    member.size_function == nullptr || member.get_const_function == nullptr ||
    member.get_function == nullptr) {
    return false;
  }
  if (member.array_size_ == 0 && member.resize_function == nullptr) {
    return false;
  }
  return true;
}

bool SupportsCMessage(const CMessageMembers * members)
{
  if (members == nullptr || (members->member_count_ != 0 && members->members_ == nullptr)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    if (!SupportsCMember(members->members_[i])) {
      return false;
    }
  }
  return true;
}

bool SerializeCppString(const std::string * value, std::vector<uint8_t> * payload)
{
  if (value == nullptr || !AppendSequenceSize(value->size(), payload)) {
    return false;
  }
  if (!value->empty()) {
    AppendBytes(value->data(), value->size(), payload);
  }
  return true;
}

bool DeserializeCppString(const uint8_t * data, size_t len, size_t * offset, std::string * value)
{
  if (value == nullptr) {
    return false;
  }
  size_t string_size = 0;
  if (!ReadSequenceSize(data, len, offset, &string_size)) {
    return false;
  }
  if (data == nullptr || offset == nullptr || *offset > len || len - *offset < string_size) {
    return false;
  }
  value->assign(reinterpret_cast<const char *>(data + *offset), string_size);
  *offset += string_size;
  return true;
}

bool SerializeCppMessage(
  const CppMessageMembers * members, const void * ros_message, std::vector<uint8_t> * payload);

bool SerializeCppValue(
  const CppMessageMember & member, const void * value, std::vector<uint8_t> * payload)
{
  if (value == nullptr || payload == nullptr) {
    return false;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING) {
    return SerializeCppString(static_cast<const std::string *>(value), payload);
  }
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE) {
    return SerializeCppMessage(ResolveCppMembers(member.members_), value, payload);
  }
  const size_t scalar_size = ScalarSize(member.type_id_);
  if (scalar_size == 0) {
    return false;
  }
  AppendBytes(value, scalar_size, payload);
  return true;
}

bool SerializeCppMember(
  const CppMessageMember & member, const uint8_t * base, std::vector<uint8_t> * payload)
{
  if (base == nullptr || payload == nullptr) {
    return false;
  }
  const void * field = base + member.offset_;
  if (!member.is_array_) {
    return SerializeCppValue(member, field, payload);
  }
  if (member.size_function == nullptr) {
    return false;
  }
  const size_t array_size = member.size_function(field);
  if (!AppendSequenceSize(array_size, payload)) {
    return false;
  }
  for (size_t i = 0; i < array_size; ++i) {
    if (
      member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING ||
      member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE) {
      if (
        member.get_const_function == nullptr ||
        !SerializeCppValue(member, member.get_const_function(field, i), payload)) {
        return false;
      }
      continue;
    }
    const size_t scalar_size = ScalarSize(member.type_id_);
    alignas(long double) uint8_t value[sizeof(long double)] = {};
    const void * item = nullptr;
    if (member.get_const_function != nullptr) {
      item = member.get_const_function(field, i);
    } else if (member.fetch_function != nullptr && scalar_size <= sizeof(value)) {
      member.fetch_function(field, i, value);
      item = value;
    }
    if (item == nullptr || !SerializeCppValue(member, item, payload)) {
      return false;
    }
  }
  return true;
}

bool SerializeCppMessage(
  const CppMessageMembers * members, const void * ros_message, std::vector<uint8_t> * payload)
{
  if (members == nullptr || ros_message == nullptr || payload == nullptr) {
    return false;
  }
  const auto * base = static_cast<const uint8_t *>(ros_message);
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    if (!SerializeCppMember(members->members_[i], base, payload)) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "failed to serialize C++ message member %s", members->members_[i].name_);
      return false;
    }
  }
  return true;
}

bool MeasureCppMessageMembers(
  const CppMessageMembers * members, const void * ros_message, size_t * payload_size);

bool MeasureCppString(const std::string * value, size_t * payload_size)
{
  return value != nullptr && MeasureSequenceSize(value->size(), payload_size) &&
         AddPayloadSize(value->size(), payload_size);
}

bool MeasureCppValue(const CppMessageMember & member, const void * value, size_t * payload_size)
{
  if (value == nullptr || payload_size == nullptr) {
    return false;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING) {
    return MeasureCppString(static_cast<const std::string *>(value), payload_size);
  }
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE) {
    return MeasureCppMessageMembers(ResolveCppMembers(member.members_), value, payload_size);
  }
  const size_t scalar_size = ScalarSize(member.type_id_);
  return scalar_size != 0 && AddPayloadSize(scalar_size, payload_size);
}

bool MeasureCppMember(const CppMessageMember & member, const uint8_t * base, size_t * payload_size)
{
  if (base == nullptr || payload_size == nullptr) {
    return false;
  }
  const void * field = base + member.offset_;
  if (!member.is_array_) {
    return MeasureCppValue(member, field, payload_size);
  }
  if (member.size_function == nullptr) {
    return false;
  }
  const size_t array_size = member.size_function(field);
  if (!MeasureSequenceSize(array_size, payload_size)) {
    return false;
  }
  if (
    member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING ||
    member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE) {
    if (member.get_const_function == nullptr) {
      return false;
    }
    for (size_t i = 0; i < array_size; ++i) {
      if (!MeasureCppValue(member, member.get_const_function(field, i), payload_size)) {
        return false;
      }
    }
    return true;
  }
  const size_t scalar_size = ScalarSize(member.type_id_);
  return scalar_size != 0 && AddRepeatedPayloadSize(array_size, scalar_size, payload_size);
}

bool MeasureCppMessageMembers(
  const CppMessageMembers * members, const void * ros_message, size_t * payload_size)
{
  if (members == nullptr || ros_message == nullptr || payload_size == nullptr) {
    return false;
  }
  const auto * base = static_cast<const uint8_t *>(ros_message);
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    if (!MeasureCppMember(members->members_[i], base, payload_size)) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "failed to measure C++ message member %s", members->members_[i].name_);
      return false;
    }
  }
  return true;
}

bool MeasureCppMessage(
  const CppMessageMembers * members, const void * ros_message, size_t * payload_size)
{
  if (payload_size == nullptr) {
    return false;
  }
  *payload_size = 0;
  return MeasureCppMessageMembers(members, ros_message, payload_size);
}

bool WriteCppMessage(
  const CppMessageMembers * members, const void * ros_message, uint8_t * buffer,
  size_t capacity, size_t * offset);

bool WriteCppString(
  const std::string * value, uint8_t * buffer, size_t capacity, size_t * offset)
{
  return value != nullptr && WriteSequenceSize(value->size(), buffer, capacity, offset) &&
         WriteBytes(value->data(), value->size(), buffer, capacity, offset);
}

bool WriteCppValue(
  const CppMessageMember & member, const void * value, uint8_t * buffer, size_t capacity,
  size_t * offset)
{
  if (value == nullptr) {
    return false;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING) {
    return WriteCppString(static_cast<const std::string *>(value), buffer, capacity, offset);
  }
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE) {
    return WriteCppMessage(ResolveCppMembers(member.members_), value, buffer, capacity, offset);
  }
  const size_t scalar_size = ScalarSize(member.type_id_);
  return scalar_size != 0 && WriteBytes(value, scalar_size, buffer, capacity, offset);
}

bool WriteCppMember(
  const CppMessageMember & member, const uint8_t * base, uint8_t * buffer, size_t capacity,
  size_t * offset)
{
  if (base == nullptr) {
    return false;
  }
  const void * field = base + member.offset_;
  if (!member.is_array_) {
    return WriteCppValue(member, field, buffer, capacity, offset);
  }
  if (member.size_function == nullptr) {
    return false;
  }
  const size_t array_size = member.size_function(field);
  if (!WriteSequenceSize(array_size, buffer, capacity, offset)) {
    return false;
  }
  for (size_t i = 0; i < array_size; ++i) {
    if (
      member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING ||
      member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE) {
      if (
        member.get_const_function == nullptr ||
        !WriteCppValue(member, member.get_const_function(field, i), buffer, capacity, offset)) {
        return false;
      }
      continue;
    }
    const size_t scalar_size = ScalarSize(member.type_id_);
    alignas(long double) uint8_t value[sizeof(long double)] = {};
    const void * item = nullptr;
    if (member.get_const_function != nullptr) {
      item = member.get_const_function(field, i);
    } else if (member.fetch_function != nullptr && scalar_size <= sizeof(value)) {
      member.fetch_function(field, i, value);
      item = value;
    }
    if (item == nullptr || !WriteCppValue(member, item, buffer, capacity, offset)) {
      return false;
    }
  }
  return true;
}

bool WriteCppMessage(
  const CppMessageMembers * members, const void * ros_message, uint8_t * buffer,
  size_t capacity, size_t * offset)
{
  if (members == nullptr || ros_message == nullptr || offset == nullptr) {
    return false;
  }
  const auto * base = static_cast<const uint8_t *>(ros_message);
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    if (!WriteCppMember(members->members_[i], base, buffer, capacity, offset)) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "failed to write C++ message member %s", members->members_[i].name_);
      return false;
    }
  }
  return true;
}

bool DeserializeCppMessage(
  const CppMessageMembers * members, const uint8_t * data, size_t len, size_t * offset,
  void * ros_message);

bool DeserializeCppValue(
  const CppMessageMember & member, const uint8_t * data, size_t len, size_t * offset, void * value)
{
  if (value == nullptr) {
    return false;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING) {
    return DeserializeCppString(data, len, offset, static_cast<std::string *>(value));
  }
  if (member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE) {
    return DeserializeCppMessage(ResolveCppMembers(member.members_), data, len, offset, value);
  }
  const size_t scalar_size = ScalarSize(member.type_id_);
  return scalar_size != 0 && ReadBytes(data, len, offset, value, scalar_size);
}

bool DeserializeCppScalarArrayItem(
  const CppMessageMember & member, const uint8_t * data, size_t len, size_t * offset, void * field,
  size_t index)
{
  const size_t scalar_size = ScalarSize(member.type_id_);
  if (scalar_size == 0) {
    return false;
  }
  alignas(long double) uint8_t value[sizeof(long double)] = {};
  if (scalar_size > sizeof(value) || !ReadBytes(data, len, offset, value, scalar_size)) {
    return false;
  }
  if (member.assign_function != nullptr) {
    member.assign_function(field, index, value);
    return true;
  }
  if (member.get_function == nullptr) {
    return false;
  }
  std::memcpy(member.get_function(field, index), value, scalar_size);
  return true;
}

bool DeserializeCppMember(
  const CppMessageMember & member, const uint8_t * data, size_t len, size_t * offset,
  uint8_t * base)
{
  if (base == nullptr) {
    return false;
  }
  void * field = base + member.offset_;
  if (!member.is_array_) {
    return DeserializeCppValue(member, data, len, offset, field);
  }

  size_t array_size = 0;
  if (!ReadSequenceSize(data, len, offset, &array_size)) {
    return false;
  }
  if (member.resize_function != nullptr) {
    member.resize_function(field, array_size);
  } else if (member.array_size_ != array_size) {
    return false;
  }
  for (size_t i = 0; i < array_size; ++i) {
    if (
      member.type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING &&
      member.type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE) {
      if (!DeserializeCppScalarArrayItem(member, data, len, offset, field, i)) {
        return false;
      }
      continue;
    }
    if (member.get_function == nullptr) {
      return false;
    }
    if (!DeserializeCppValue(member, data, len, offset, member.get_function(field, i))) {
      return false;
    }
  }
  return true;
}

bool DeserializeCppMessage(
  const CppMessageMembers * members, const uint8_t * data, size_t len, size_t * offset,
  void * ros_message)
{
  if (members == nullptr || ros_message == nullptr) {
    return false;
  }
  auto * base = static_cast<uint8_t *>(ros_message);
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    if (!DeserializeCppMember(members->members_[i], data, len, offset, base)) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "failed to deserialize C++ message member %s", members->members_[i].name_);
      return false;
    }
  }
  return true;
}

bool SerializeCString(const rosidl_runtime_c__String * value, std::vector<uint8_t> * payload)
{
  if (value == nullptr || !AppendSequenceSize(value->size, payload)) {
    return false;
  }
  if (value->size != 0) {
    if (value->data == nullptr) {
      return false;
    }
    AppendBytes(value->data, value->size, payload);
  }
  return true;
}

bool DeserializeCString(
  const uint8_t * data, size_t len, size_t * offset, rosidl_runtime_c__String * value)
{
  if (value == nullptr) {
    return false;
  }
  size_t string_size = 0;
  if (!ReadSequenceSize(data, len, offset, &string_size)) {
    return false;
  }
  if (data == nullptr || offset == nullptr || *offset > len || len - *offset < string_size) {
    return false;
  }
  const char * string_data = string_size == 0 ? "" : reinterpret_cast<const char *>(data + *offset);
  if (!rosidl_runtime_c__String__assignn(value, string_data, string_size)) {
    return false;
  }
  *offset += string_size;
  return true;
}

bool SerializeCMessage(
  const CMessageMembers * members, const void * ros_message, std::vector<uint8_t> * payload);

bool SerializeCValue(
  const CMessageMember & member, const void * value, std::vector<uint8_t> * payload)
{
  if (value == nullptr || payload == nullptr) {
    return false;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_STRING) {
    return SerializeCString(static_cast<const rosidl_runtime_c__String *>(value), payload);
  }
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
    return SerializeCMessage(ResolveCMembers(member.members_), value, payload);
  }
  const size_t scalar_size = ScalarSize(member.type_id_);
  if (scalar_size == 0) {
    return false;
  }
  AppendBytes(value, scalar_size, payload);
  return true;
}

bool SerializeCMember(
  const CMessageMember & member, const uint8_t * base, std::vector<uint8_t> * payload)
{
  if (base == nullptr || payload == nullptr) {
    return false;
  }
  const void * field = base + member.offset_;
  if (!member.is_array_) {
    return SerializeCValue(member, field, payload);
  }
  if (member.size_function == nullptr || member.get_const_function == nullptr) {
    return false;
  }
  const size_t array_size = member.size_function(field);
  if (!AppendSequenceSize(array_size, payload)) {
    return false;
  }
  for (size_t i = 0; i < array_size; ++i) {
    const void * item = member.get_const_function(field, i);
    if (!SerializeCValue(member, item, payload)) {
      return false;
    }
  }
  return true;
}

bool SerializeCMessage(
  const CMessageMembers * members, const void * ros_message, std::vector<uint8_t> * payload)
{
  if (members == nullptr || ros_message == nullptr || payload == nullptr) {
    return false;
  }
  const auto * base = static_cast<const uint8_t *>(ros_message);
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    if (!SerializeCMember(members->members_[i], base, payload)) {
      return false;
    }
  }
  return true;
}

bool MeasureCMessageMembers(
  const CMessageMembers * members, const void * ros_message, size_t * payload_size);

bool MeasureCString(const rosidl_runtime_c__String * value, size_t * payload_size)
{
  if (value == nullptr || (value->data == nullptr && value->size != 0)) {
    return false;
  }
  return MeasureSequenceSize(value->size, payload_size) &&
         AddPayloadSize(value->size, payload_size);
}

bool MeasureCValue(const CMessageMember & member, const void * value, size_t * payload_size)
{
  if (value == nullptr || payload_size == nullptr) {
    return false;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_STRING) {
    return MeasureCString(static_cast<const rosidl_runtime_c__String *>(value), payload_size);
  }
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
    return MeasureCMessageMembers(ResolveCMembers(member.members_), value, payload_size);
  }
  const size_t scalar_size = ScalarSize(member.type_id_);
  return scalar_size != 0 && AddPayloadSize(scalar_size, payload_size);
}

bool MeasureCMember(const CMessageMember & member, const uint8_t * base, size_t * payload_size)
{
  if (base == nullptr || payload_size == nullptr) {
    return false;
  }
  const void * field = base + member.offset_;
  if (!member.is_array_) {
    return MeasureCValue(member, field, payload_size);
  }
  if (member.size_function == nullptr || member.get_const_function == nullptr) {
    return false;
  }
  const size_t array_size = member.size_function(field);
  if (!MeasureSequenceSize(array_size, payload_size)) {
    return false;
  }
  for (size_t i = 0; i < array_size; ++i) {
    if (!MeasureCValue(member, member.get_const_function(field, i), payload_size)) {
      return false;
    }
  }
  return true;
}

bool MeasureCMessageMembers(
  const CMessageMembers * members, const void * ros_message, size_t * payload_size)
{
  if (members == nullptr || ros_message == nullptr || payload_size == nullptr) {
    return false;
  }
  const auto * base = static_cast<const uint8_t *>(ros_message);
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    if (!MeasureCMember(members->members_[i], base, payload_size)) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "failed to measure C message member %s", members->members_[i].name_);
      return false;
    }
  }
  return true;
}

bool MeasureCMessage(const CMessageMembers * members, const void * ros_message, size_t * payload_size)
{
  if (payload_size == nullptr) {
    return false;
  }
  *payload_size = 0;
  return MeasureCMessageMembers(members, ros_message, payload_size);
}

bool WriteCMessage(
  const CMessageMembers * members, const void * ros_message, uint8_t * buffer, size_t capacity,
  size_t * offset);

bool WriteCString(
  const rosidl_runtime_c__String * value, uint8_t * buffer, size_t capacity, size_t * offset)
{
  if (value == nullptr || (value->data == nullptr && value->size != 0)) {
    return false;
  }
  return WriteSequenceSize(value->size, buffer, capacity, offset) &&
         WriteBytes(value->data, value->size, buffer, capacity, offset);
}

bool WriteCValue(
  const CMessageMember & member, const void * value, uint8_t * buffer, size_t capacity,
  size_t * offset)
{
  if (value == nullptr) {
    return false;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_STRING) {
    return WriteCString(
      static_cast<const rosidl_runtime_c__String *>(value), buffer, capacity, offset);
  }
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
    return WriteCMessage(ResolveCMembers(member.members_), value, buffer, capacity, offset);
  }
  const size_t scalar_size = ScalarSize(member.type_id_);
  return scalar_size != 0 && WriteBytes(value, scalar_size, buffer, capacity, offset);
}

bool WriteCMember(
  const CMessageMember & member, const uint8_t * base, uint8_t * buffer, size_t capacity,
  size_t * offset)
{
  if (base == nullptr) {
    return false;
  }
  const void * field = base + member.offset_;
  if (!member.is_array_) {
    return WriteCValue(member, field, buffer, capacity, offset);
  }
  if (member.size_function == nullptr || member.get_const_function == nullptr) {
    return false;
  }
  const size_t array_size = member.size_function(field);
  if (!WriteSequenceSize(array_size, buffer, capacity, offset)) {
    return false;
  }
  for (size_t i = 0; i < array_size; ++i) {
    if (!WriteCValue(member, member.get_const_function(field, i), buffer, capacity, offset)) {
      return false;
    }
  }
  return true;
}

bool WriteCMessage(
  const CMessageMembers * members, const void * ros_message, uint8_t * buffer, size_t capacity,
  size_t * offset)
{
  if (members == nullptr || ros_message == nullptr || offset == nullptr) {
    return false;
  }
  const auto * base = static_cast<const uint8_t *>(ros_message);
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    if (!WriteCMember(members->members_[i], base, buffer, capacity, offset)) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "failed to write C message member %s", members->members_[i].name_);
      return false;
    }
  }
  return true;
}

bool DeserializeCMessage(
  const CMessageMembers * members, const uint8_t * data, size_t len, size_t * offset,
  void * ros_message);

bool DeserializeCValue(
  const CMessageMember & member, const uint8_t * data, size_t len, size_t * offset, void * value)
{
  if (value == nullptr) {
    return false;
  }
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_STRING) {
    return DeserializeCString(data, len, offset, static_cast<rosidl_runtime_c__String *>(value));
  }
  if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
    return DeserializeCMessage(ResolveCMembers(member.members_), data, len, offset, value);
  }
  const size_t scalar_size = ScalarSize(member.type_id_);
  return scalar_size != 0 && ReadBytes(data, len, offset, value, scalar_size);
}

bool DeserializeCScalarArrayItem(
  const CMessageMember & member, const uint8_t * data, size_t len, size_t * offset, void * field,
  size_t index)
{
  const size_t scalar_size = ScalarSize(member.type_id_);
  if (scalar_size == 0) {
    return false;
  }
  alignas(long double) uint8_t value[sizeof(long double)] = {};
  if (scalar_size > sizeof(value) || !ReadBytes(data, len, offset, value, scalar_size)) {
    return false;
  }
  if (member.assign_function != nullptr) {
    member.assign_function(field, index, value);
    return true;
  }
  if (member.get_function == nullptr) {
    return false;
  }
  std::memcpy(member.get_function(field, index), value, scalar_size);
  return true;
}

bool ResizeCArray(const CMessageMember & member, void * field, size_t array_size)
{
  if (member.resize_function != nullptr) {
    if (member.is_upper_bound_ && array_size > member.array_size_) {
      return false;
    }
    return member.resize_function(field, array_size);
  }
  return member.array_size_ == array_size;
}

bool DeserializeCMember(
  const CMessageMember & member, const uint8_t * data, size_t len, size_t * offset, uint8_t * base)
{
  if (base == nullptr) {
    return false;
  }
  void * field = base + member.offset_;
  if (!member.is_array_) {
    return DeserializeCValue(member, data, len, offset, field);
  }

  size_t array_size = 0;
  if (
    !ReadSequenceSize(data, len, offset, &array_size) || !ResizeCArray(member, field, array_size)) {
    return false;
  }
  if (member.get_function == nullptr) {
    return false;
  }
  for (size_t i = 0; i < array_size; ++i) {
    if (
      member.type_id_ != rosidl_typesupport_introspection_c__ROS_TYPE_STRING &&
      member.type_id_ != rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
      if (!DeserializeCScalarArrayItem(member, data, len, offset, field, i)) {
        return false;
      }
      continue;
    }
    if (!DeserializeCValue(member, data, len, offset, member.get_function(field, i))) {
      return false;
    }
  }
  return true;
}

bool DeserializeCMessage(
  const CMessageMembers * members, const uint8_t * data, size_t len, size_t * offset,
  void * ros_message)
{
  if (members == nullptr || ros_message == nullptr) {
    return false;
  }
  auto * base = static_cast<uint8_t *>(ros_message);
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    if (!DeserializeCMember(members->members_[i], data, len, offset, base)) {
      return false;
    }
  }
  return true;
}
}  // namespace

bool StringAdapter::Init(const rosidl_message_type_support_t * type_support)
{
  if (type_support == nullptr) {
    return false;
  }
  if (InitCpp(type_support)) {
    return true;
  }
  rmw_reset_error();
  return InitC(type_support);
}

bool StringAdapter::InitCpp(const rosidl_message_type_support_t * type_support)
{
  const rosidl_message_type_support_t * introspection = get_message_typesupport_handle(
    type_support, rosidl_typesupport_introspection_cpp::typesupport_identifier);
  if (introspection == nullptr || introspection->data == nullptr) {
    return false;
  }

  auto members =
    static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(introspection->data);
  return InitCpp(members);
}

bool StringAdapter::InitCpp(const rosidl_typesupport_introspection_cpp::MessageMembers * members)
{
  if (!SupportsCppMessage(members)) {
    return false;
  }
  type_name_ = MakeRosTypeName(members);
  storage_kind_ = StorageKind::Cpp;
  c_members_ = nullptr;
  cpp_members_ = members;
  raw_string_payload_ =
    members->member_count_ == 1 && members->members_ != nullptr &&
    !members->members_[0].is_array_ &&
    members->members_[0].type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING;
  raw_pose_stamped_payload_ = type_name_ == "geometry_msgs/msg/PoseStamped";
  return true;
}

bool StringAdapter::InitC(const rosidl_message_type_support_t * type_support)
{
  const rosidl_message_type_support_t * introspection =
    get_message_typesupport_handle(type_support, rosidl_typesupport_introspection_c__identifier);
  if (introspection == nullptr || introspection->data == nullptr) {
    return false;
  }

  auto members =
    static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(introspection->data);
  return InitC(members);
}

bool StringAdapter::InitC(const rosidl_typesupport_introspection_c__MessageMembers * members)
{
  if (!SupportsCMessage(members)) {
    return false;
  }
  type_name_ = MakeRosTypeName(members);
  storage_kind_ = StorageKind::C;
  c_members_ = members;
  cpp_members_ = nullptr;
  raw_string_payload_ =
    members->member_count_ == 1 && members->members_ != nullptr &&
    !members->members_[0].is_array_ &&
    members->members_[0].type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_STRING;
  raw_pose_stamped_payload_ = type_name_ == "geometry_msgs/msg/PoseStamped";
  return true;
}

bool StringAdapter::IsValid() const
{
  return storage_kind_ != StorageKind::None && (cpp_members_ != nullptr || c_members_ != nullptr);
}

const std::string & StringAdapter::TypeName() const { return type_name_; }

bool StringAdapter::Encode(const void * ros_message, std::vector<uint8_t> * payload) const
{
  if (ros_message == nullptr || payload == nullptr || !IsValid()) {
    return false;
  }
  const auto * base = static_cast<const uint8_t *>(ros_message);
  if (storage_kind_ == StorageKind::Cpp) {
    if (raw_string_payload_) {
      const uint8_t * value_base = base + cpp_members_->members_[0].offset_;
      const auto * value = reinterpret_cast<const std::string *>(value_base);
      payload->assign(value->begin(), value->end());
      return true;
    }
    if (raw_pose_stamped_payload_) {
      PoseStampedWireFields fields;
      payload->clear();
      return ExtractCppPoseStamped(cpp_members_, ros_message, &fields) &&
             AppendPoseStampedWire(fields, payload);
    }
    payload->clear();
    return SerializeCppMessage(cpp_members_, ros_message, payload);
  }
  if (
    c_members_ == nullptr || (c_members_->member_count_ != 0 && c_members_->members_ == nullptr)) {
    return false;
  }
  if (raw_string_payload_) {
    const uint8_t * value_base = base + c_members_->members_[0].offset_;
    const auto * value = reinterpret_cast<const rosidl_runtime_c__String *>(value_base);
    if (value->data == nullptr && value->size != 0) {
      return false;
    }
    payload->assign(
      reinterpret_cast<const uint8_t *>(value->data),
      reinterpret_cast<const uint8_t *>(value->data) + value->size);
    return true;
  }
  if (raw_pose_stamped_payload_) {
    PoseStampedWireFields fields;
    payload->clear();
    return ExtractCPoseStamped(c_members_, ros_message, &fields) &&
           AppendPoseStampedWire(fields, payload);
  }
  payload->clear();
  return SerializeCMessage(c_members_, ros_message, payload);
}

bool StringAdapter::EncodedSize(const void * ros_message, size_t * payload_size) const
{
  if (ros_message == nullptr || payload_size == nullptr || !IsValid()) {
    return false;
  }
  const auto * base = static_cast<const uint8_t *>(ros_message);
  if (storage_kind_ == StorageKind::Cpp) {
    if (raw_string_payload_) {
      const auto * value = reinterpret_cast<const std::string *>(
        base + cpp_members_->members_[0].offset_);
      *payload_size = value->size();
      return true;
    }
    if (raw_pose_stamped_payload_) {
      PoseStampedWireFields fields;
      if (!ExtractCppPoseStamped(cpp_members_, ros_message, &fields)) {
        return false;
      }
      *payload_size = PoseStampedWireSize(fields);
      return true;
    }
    return MeasureCppMessage(cpp_members_, ros_message, payload_size);
  }
  if (storage_kind_ == StorageKind::C) {
    if (c_members_ == nullptr || (c_members_->member_count_ != 0 && c_members_->members_ == nullptr)) {
      return false;
    }
    if (raw_string_payload_) {
      const auto * value = reinterpret_cast<const rosidl_runtime_c__String *>(
        base + c_members_->members_[0].offset_);
      if (value->data == nullptr && value->size != 0) {
        return false;
      }
      *payload_size = value->size;
      return true;
    }
    if (raw_pose_stamped_payload_) {
      PoseStampedWireFields fields;
      if (!ExtractCPoseStamped(c_members_, ros_message, &fields)) {
        return false;
      }
      *payload_size = PoseStampedWireSize(fields);
      return true;
    }
    return MeasureCMessage(c_members_, ros_message, payload_size);
  }
  return false;
}

bool StringAdapter::EncodeIntoBuffer(
  const void * ros_message, void * buffer, size_t capacity, size_t * payload_size) const
{
  if (ros_message == nullptr || buffer == nullptr || payload_size == nullptr || !IsValid()) {
    return false;
  }
  const auto * base = static_cast<const uint8_t *>(ros_message);
  auto * out = static_cast<uint8_t *>(buffer);
  if (storage_kind_ == StorageKind::Cpp) {
    if (raw_string_payload_) {
      const auto * value = reinterpret_cast<const std::string *>(
        base + cpp_members_->members_[0].offset_);
      *payload_size = value->size();
      size_t offset = 0;
      return WriteBytes(value->data(), value->size(), out, capacity, &offset);
    }
    if (raw_pose_stamped_payload_) {
      PoseStampedWireFields fields;
      return ExtractCppPoseStamped(cpp_members_, ros_message, &fields) &&
             WritePoseStampedWire(fields, out, capacity, payload_size);
    }
    size_t required_size = 0;
    if (!MeasureCppMessage(cpp_members_, ros_message, &required_size)) {
      return false;
    }
    *payload_size = required_size;
    if (capacity < required_size) {
      return false;
    }
    size_t offset = 0;
    return WriteCppMessage(cpp_members_, ros_message, out, capacity, &offset) &&
           offset == required_size;
  }
  if (storage_kind_ == StorageKind::C) {
    if (c_members_ == nullptr || (c_members_->member_count_ != 0 && c_members_->members_ == nullptr)) {
      return false;
    }
    if (raw_string_payload_) {
      const auto * value = reinterpret_cast<const rosidl_runtime_c__String *>(
        base + c_members_->members_[0].offset_);
      if (value->data == nullptr && value->size != 0) {
        return false;
      }
      *payload_size = value->size;
      size_t offset = 0;
      return WriteBytes(value->data, value->size, out, capacity, &offset);
    }
    if (raw_pose_stamped_payload_) {
      PoseStampedWireFields fields;
      return ExtractCPoseStamped(c_members_, ros_message, &fields) &&
             WritePoseStampedWire(fields, out, capacity, payload_size);
    }
    size_t required_size = 0;
    if (!MeasureCMessage(c_members_, ros_message, &required_size)) {
      return false;
    }
    *payload_size = required_size;
    if (capacity < required_size) {
      return false;
    }
    size_t offset = 0;
    return WriteCMessage(c_members_, ros_message, out, capacity, &offset) &&
           offset == required_size;
  }
  return false;
}

bool StringAdapter::Decode(const uint8_t * data, size_t len, void * ros_message) const
{
  if ((data == nullptr && len != 0) || ros_message == nullptr || !IsValid()) {
    return false;
  }
  auto * base = static_cast<uint8_t *>(ros_message);
  if (storage_kind_ == StorageKind::Cpp) {
    if (raw_string_payload_) {
      size_t effective_len = (len > 0 && data[len - 1] == '\0') ? len - 1 : len;
      uint8_t * value_base = base + cpp_members_->members_[0].offset_;
      auto * value = reinterpret_cast<std::string *>(value_base);
      value->assign(reinterpret_cast<const char *>(data), effective_len);
      return true;
    }
    if (raw_pose_stamped_payload_) {
      PoseStampedWireFields fields;
      return ReadPoseStampedWire(data, len, &fields) &&
             AssignCppPoseStamped(cpp_members_, fields, ros_message);
    }
    size_t offset = 0;
    return DeserializeCppMessage(cpp_members_, data, len, &offset, ros_message) && offset == len;
  }
  if (
    c_members_ == nullptr || (c_members_->member_count_ != 0 && c_members_->members_ == nullptr)) {
    return false;
  }
  if (raw_string_payload_) {
    size_t effective_len = (len > 0 && data[len - 1] == '\0') ? len - 1 : len;
    uint8_t * value_base = base + c_members_->members_[0].offset_;
    auto * value = reinterpret_cast<rosidl_runtime_c__String *>(value_base);
    return rosidl_runtime_c__String__assignn(
      value, reinterpret_cast<const char *>(data), effective_len);
  }
  if (raw_pose_stamped_payload_) {
    PoseStampedWireFields fields;
    return ReadPoseStampedWire(data, len, &fields) &&
           AssignCPoseStamped(c_members_, fields, ros_message);
  }

  size_t offset = 0;
  return DeserializeCMessage(c_members_, data, len, &offset, ros_message) && offset == len;
}

}  // namespace rmw_mdds_cpp
