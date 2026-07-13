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

#include "message_adapter.hpp"

#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <fastcdr/exceptions/Exception.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

#include "rmw/error_handling.h"
#include "rosidl_runtime_c/message_initialization.h"
#include "rosidl_runtime_c/string.h"
#include "rosidl_runtime_cpp/message_initialization.hpp"
#include "rosidl_typesupport_fastrtps_c/identifier.h"
#include "rosidl_typesupport_fastrtps_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"

namespace rmw_mdds_cpp
{

namespace
{
std::string MakeRosTypeName(const message_type_support_callbacks_t *callbacks)
{
  if (callbacks == nullptr || callbacks->message_namespace_ == nullptr ||
    callbacks->message_name_ == nullptr)
  {
    return {};
  }
  std::string ns(callbacks->message_namespace_);
  size_t pos = 0;
  while ((pos = ns.find("::", pos)) != std::string::npos) {
    ns.replace(pos, 2, "/");
    pos += 1;
  }
  return ns + "/" + callbacks->message_name_;
}

std::string MakeDdsTypeName(const message_type_support_callbacks_t *callbacks)
{
  if (callbacks == nullptr || callbacks->message_namespace_ == nullptr ||
    callbacks->message_name_ == nullptr)
  {
    return {};
  }
  std::string type_name(callbacks->message_namespace_);
  if (!type_name.empty()) {
    type_name += "::";
  }
  type_name += "dds_::";
  type_name += callbacks->message_name_;
  type_name += "_";
  return type_name;
}

const message_type_support_callbacks_t *
ResolveCdrCallbacks(const rosidl_message_type_support_t *type_support)
{
  if (type_support == nullptr) {
    return nullptr;
  }
  const rosidl_message_type_support_t *cdr_type_support =
    get_message_typesupport_handle(
          type_support,
          rosidl_typesupport_fastrtps_cpp::typesupport_identifier);
  if (cdr_type_support == nullptr || cdr_type_support->data == nullptr) {
    rmw_reset_error();
    cdr_type_support = get_message_typesupport_handle(
        type_support, rosidl_typesupport_fastrtps_c__identifier);
  }
  if (cdr_type_support == nullptr || cdr_type_support->data == nullptr) {
    rmw_reset_error();
    return nullptr;
  }
  auto *callbacks = static_cast<const message_type_support_callbacks_t *>(
    cdr_type_support->data);
  if (callbacks->cdr_serialize == nullptr ||
    callbacks->cdr_deserialize == nullptr ||
    callbacks->get_serialized_size == nullptr ||
    callbacks->max_serialized_size == nullptr)
  {
    return nullptr;
  }
  return callbacks;
}

const rosidl_typesupport_introspection_cpp::MessageMembers *
ResolveCppMembers(const rosidl_message_type_support_t *type_support)
{
  if (type_support == nullptr) {
    return nullptr;
  }
  const rosidl_message_type_support_t *introspection =
    get_message_typesupport_handle(
          type_support,
          rosidl_typesupport_introspection_cpp::typesupport_identifier);
  if (introspection == nullptr || introspection->data == nullptr) {
    rmw_reset_error();
    return nullptr;
  }
  return static_cast<
    const rosidl_typesupport_introspection_cpp::MessageMembers *>(
    introspection->data);
}

const rosidl_typesupport_introspection_c__MessageMembers *
ResolveCMembers(const rosidl_message_type_support_t *type_support)
{
  if (type_support == nullptr) {
    return nullptr;
  }
  const rosidl_message_type_support_t *introspection =
    get_message_typesupport_handle(
          type_support, rosidl_typesupport_introspection_c__identifier);
  if (introspection == nullptr || introspection->data == nullptr) {
    rmw_reset_error();
    return nullptr;
  }
  return static_cast<
    const rosidl_typesupport_introspection_c__MessageMembers *>(
    introspection->data);
}

} // namespace

bool MessageAdapter::Init(const rosidl_message_type_support_t *type_support)
{
  type_hash_ = rosidl_get_zero_initialized_type_hash();
  if (type_support != nullptr && type_support->get_type_hash_func != nullptr) {
    const rosidl_type_hash_t *type_hash =
      type_support->get_type_hash_func(type_support);
    if (type_hash != nullptr) {
      type_hash_ = *type_hash;
    }
  }
  const bool has_legacy_adapter = legacy_adapter_.Init(type_support);
  rmw_reset_error();
  c_members_ = nullptr;
  cpp_members_ = ResolveCppMembers(type_support);
  if (cpp_members_ != nullptr) {
    message_members_kind_ = MessageMembersKind::Cpp;
  } else {
    c_members_ = ResolveCMembers(type_support);
    message_members_kind_ = c_members_ == nullptr ? MessageMembersKind::None :
      MessageMembersKind::C;
  }
  cdr_callbacks_ = ResolveCdrCallbacks(type_support);
  if (cdr_callbacks_ != nullptr) {
    type_name_ = MakeRosTypeName(cdr_callbacks_);
    wire_type_name_ = MakeDdsTypeName(cdr_callbacks_);
    if (type_name_.empty() || wire_type_name_.empty()) {
      cdr_callbacks_ = nullptr;
      return false;
    }
    storage_kind_ = StorageKind::Cdr;
    return true;
  }
  if (has_legacy_adapter) {
    type_name_ = legacy_adapter_.TypeName();
    wire_type_name_ = type_name_;
    storage_kind_ = StorageKind::Legacy;
    return true;
  }
  storage_kind_ = StorageKind::None;
  message_members_kind_ = MessageMembersKind::None;
  c_members_ = nullptr;
  cpp_members_ = nullptr;
  return false;
}

bool MessageAdapter::IsValid() const
{
  return storage_kind_ != StorageKind::None;
}

const std::string & MessageAdapter::TypeName() const {return type_name_;}

const rosidl_type_hash_t & MessageAdapter::TypeHash() const
{
  return type_hash_;
}

const std::string & MessageAdapter::WireTypeName() const
{
  return wire_type_name_;
}

const std::string & MessageAdapter::MddsTypeName() const
{
  if (legacy_adapter_.IsValid()) {
    return legacy_adapter_.TypeName();
  }
  return type_name_;
}

bool MessageAdapter::Encode(
  const void *ros_message,
  std::vector<uint8_t> *payload) const
{
  if (storage_kind_ == StorageKind::Legacy) {
    return legacy_adapter_.Encode(ros_message, payload);
  }
  if (ros_message == nullptr || payload == nullptr ||
    cdr_callbacks_ == nullptr)
  {
    return false;
  }
  const size_t payload_size =
    4u + cdr_callbacks_->get_serialized_size(ros_message);
  payload->assign(payload_size, 0);
  try {
    eprosima::fastcdr::FastBuffer buffer(
      reinterpret_cast<char *>(payload->data()), payload->size());
    eprosima::fastcdr::Cdr serializer(buffer,
      eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
      eprosima::fastcdr::CdrVersion::XCDRv1);
    serializer.set_encoding_flag(
        eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR);
    serializer.serialize_encapsulation();
    if (!cdr_callbacks_->cdr_serialize(ros_message, serializer)) {
      return false;
    }
    payload->resize(serializer.get_serialized_data_length());
  } catch (const eprosima::fastcdr::exception::Exception & exception) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("Fast CDR serialization failed: %s",
                                         exception.what());
    return false;
  }
  return true;
}

bool MessageAdapter::EncodeMdds(
  const void *ros_message,
  std::vector<uint8_t> *payload) const
{
  if (legacy_adapter_.IsValid()) {
    return legacy_adapter_.Encode(ros_message, payload);
  }
  return Encode(ros_message, payload);
}

bool MessageAdapter::EncodedMddsSize(
  const void *ros_message,
  size_t *payload_size) const
{
  if (payload_size == nullptr) {
    return false;
  }
  if (legacy_adapter_.IsValid()) {
    return legacy_adapter_.EncodedSize(ros_message, payload_size);
  }
  if (ros_message == nullptr || cdr_callbacks_ == nullptr) {
    return false;
  }
  *payload_size = 4u + cdr_callbacks_->get_serialized_size(ros_message);
  return true;
}

bool MessageAdapter::EncodeMddsIntoBuffer(
  const void *ros_message, void *buffer,
  size_t capacity,
  size_t *payload_size) const
{
  if (payload_size == nullptr) {
    return false;
  }
  if (legacy_adapter_.IsValid()) {
    return legacy_adapter_.EncodeIntoBuffer(ros_message, buffer, capacity,
                                            payload_size);
  }
  if (ros_message == nullptr || buffer == nullptr ||
    cdr_callbacks_ == nullptr)
  {
    return false;
  }
  const size_t required_size =
    4u + cdr_callbacks_->get_serialized_size(ros_message);
  *payload_size = required_size;
  if (required_size > capacity) {
    return false;
  }
  try {
    eprosima::fastcdr::FastBuffer fast_buffer(static_cast<char *>(buffer),
      capacity);
    eprosima::fastcdr::Cdr serializer(fast_buffer,
      eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
      eprosima::fastcdr::CdrVersion::XCDRv1);
    serializer.set_encoding_flag(
        eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR);
    serializer.serialize_encapsulation();
    if (!cdr_callbacks_->cdr_serialize(ros_message, serializer)) {
      return false;
    }
    *payload_size = serializer.get_serialized_data_length();
  } catch (const eprosima::fastcdr::exception::Exception & exception) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("Fast CDR serialization failed: %s",
                                         exception.what());
    return false;
  }
  return true;
}

bool MessageAdapter::Decode(
  const uint8_t *data, size_t len,
  void *ros_message) const
{
  if (storage_kind_ == StorageKind::Legacy) {
    try {
      return legacy_adapter_.Decode(data, len, ros_message);
    } catch (const std::bad_alloc &) {
      RMW_SET_ERROR_MSG("message deserialization exceeded the active memory resource");
      return false;
    }
  }
  if ((data == nullptr && len != 0) || ros_message == nullptr ||
    cdr_callbacks_ == nullptr)
  {
    return false;
  }
  try {
    eprosima::fastcdr::FastBuffer buffer(
      const_cast<char *>(reinterpret_cast<const char *>(data)), len);
    eprosima::fastcdr::Cdr deserializer(buffer,
      eprosima::fastcdr::Cdr::DEFAULT_ENDIAN);
    deserializer.read_encapsulation();
    return cdr_callbacks_->cdr_deserialize(deserializer, ros_message);
  } catch (const eprosima::fastcdr::exception::Exception & exception) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("Fast CDR deserialization failed: %s",
                                         exception.what());
    return false;
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("message deserialization exceeded the active memory resource");
    return false;
  }
}

bool MessageAdapter::DecodeMdds(
  const uint8_t *data, size_t len,
  void *ros_message) const
{
  if (legacy_adapter_.IsValid()) {
    try {
      return legacy_adapter_.Decode(data, len, ros_message);
    } catch (const std::bad_alloc &) {
      RMW_SET_ERROR_MSG("MDDS deserialization exceeded the active memory resource");
      return false;
    }
  }
  return Decode(data, len, ros_message);
}

namespace
{
bool IsNumericIntrospectionType(uint8_t type_id)
{
  namespace ic = rosidl_typesupport_introspection_cpp;
  switch (type_id) {
    case ic::ROS_TYPE_FLOAT:
    case ic::ROS_TYPE_DOUBLE:
    case ic::ROS_TYPE_CHAR:
    case ic::ROS_TYPE_WCHAR:
    case ic::ROS_TYPE_BOOLEAN:
    case ic::ROS_TYPE_BYTE:
    case ic::ROS_TYPE_UINT8:
    case ic::ROS_TYPE_INT8:
    case ic::ROS_TYPE_UINT16:
    case ic::ROS_TYPE_INT16:
    case ic::ROS_TYPE_UINT32:
    case ic::ROS_TYPE_INT32:
    case ic::ROS_TYPE_UINT64:
    case ic::ROS_TYPE_INT64:
      return true;
    default:
      return false;
  }
}

bool ReadScalarAsDouble(const uint8_t *field, uint8_t type_id, double *out)
{
  namespace ic = rosidl_typesupport_introspection_cpp;
  switch (type_id) {
    case ic::ROS_TYPE_FLOAT:
      *out = *reinterpret_cast<const float *>(field);
      return true;
    case ic::ROS_TYPE_DOUBLE:
      *out = *reinterpret_cast<const double *>(field);
      return true;
    case ic::ROS_TYPE_BOOLEAN:
      *out = *reinterpret_cast<const bool *>(field) ? 1.0 : 0.0;
      return true;
    case ic::ROS_TYPE_CHAR:
    case ic::ROS_TYPE_BYTE:
    case ic::ROS_TYPE_UINT8:
      *out = *reinterpret_cast<const uint8_t *>(field);
      return true;
    case ic::ROS_TYPE_INT8:
      *out = *reinterpret_cast<const int8_t *>(field);
      return true;
    case ic::ROS_TYPE_WCHAR:
    case ic::ROS_TYPE_UINT16:
      *out = *reinterpret_cast<const uint16_t *>(field);
      return true;
    case ic::ROS_TYPE_INT16:
      *out = *reinterpret_cast<const int16_t *>(field);
      return true;
    case ic::ROS_TYPE_UINT32:
      *out = *reinterpret_cast<const uint32_t *>(field);
      return true;
    case ic::ROS_TYPE_INT32:
      *out = *reinterpret_cast<const int32_t *>(field);
      return true;
    case ic::ROS_TYPE_UINT64:
      *out = static_cast<double>(*reinterpret_cast<const uint64_t *>(field));
      return true;
    case ic::ROS_TYPE_INT64:
      *out = static_cast<double>(*reinterpret_cast<const int64_t *>(field));
      return true;
    default:
      return false;
  }
}

bool CppScalarStorageSize(uint8_t type_id, size_t *size)
{
  if (size == nullptr) {
    return false;
  }
  namespace ic = rosidl_typesupport_introspection_cpp;
  switch (type_id) {
    case ic::ROS_TYPE_FLOAT:
      *size = sizeof(float);
      return true;
    case ic::ROS_TYPE_DOUBLE:
      *size = sizeof(double);
      return true;
    case ic::ROS_TYPE_BOOLEAN:
      *size = sizeof(bool);
      return true;
    case ic::ROS_TYPE_CHAR:
      *size = sizeof(char);
      return true;
    case ic::ROS_TYPE_BYTE:
      *size = sizeof(uint8_t);
      return true;
    case ic::ROS_TYPE_UINT8:
      *size = sizeof(uint8_t);
      return true;
    case ic::ROS_TYPE_INT8:
      *size = sizeof(int8_t);
      return true;
    case ic::ROS_TYPE_WCHAR:
      *size = sizeof(uint16_t);
      return true;
    case ic::ROS_TYPE_UINT16:
      *size = sizeof(uint16_t);
      return true;
    case ic::ROS_TYPE_INT16:
      *size = sizeof(int16_t);
      return true;
    case ic::ROS_TYPE_UINT32:
      *size = sizeof(uint32_t);
      return true;
    case ic::ROS_TYPE_INT32:
      *size = sizeof(int32_t);
      return true;
    case ic::ROS_TYPE_UINT64:
      *size = sizeof(uint64_t);
      return true;
    case ic::ROS_TYPE_INT64:
      *size = sizeof(int64_t);
      return true;
    default:
      return false;
  }
}

bool CScalarStorageSize(uint8_t type_id, size_t *size)
{
  if (size == nullptr) {
    return false;
  }
  switch (type_id) {
    case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
      *size = sizeof(float);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE:
      *size = sizeof(double);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN:
      *size = sizeof(bool);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_CHAR:
      *size = sizeof(char);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_BYTE:
      *size = sizeof(uint8_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
      *size = sizeof(uint8_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
      *size = sizeof(int8_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_WCHAR:
      *size = sizeof(uint16_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
      *size = sizeof(uint16_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
      *size = sizeof(int16_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
      *size = sizeof(uint32_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
      *size = sizeof(int32_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
      *size = sizeof(uint64_t);
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
      *size = sizeof(int64_t);
      return true;
    default:
      return false;
  }
}

bool SupportsFlatRawLoanedMessageCpp(
  const rosidl_typesupport_introspection_cpp::MessageMembers *members)
{
  if (members == nullptr || members->size_of_ == 0 ||
    members->member_count_ == 0 || members->init_function == nullptr ||
    members->fini_function == nullptr)
  {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    size_t scalar_size = 0;
    if (member.is_array_ ||
      !CppScalarStorageSize(member.type_id_, &scalar_size))
    {
      return false;
    }
    if (member.offset_ > members->size_of_ ||
      scalar_size > members->size_of_ - member.offset_)
    {
      return false;
    }
  }
  return true;
}

bool SupportsFlatRawLoanedMessageC(
  const rosidl_typesupport_introspection_c__MessageMembers *members)
{
  if (members == nullptr || members->size_of_ == 0 ||
    members->member_count_ == 0 || members->init_function == nullptr ||
    members->fini_function == nullptr)
  {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    size_t scalar_size = 0;
    if (member.is_array_ ||
      !CScalarStorageSize(member.type_id_, &scalar_size))
    {
      return false;
    }
    if (member.offset_ > members->size_of_ ||
      scalar_size > members->size_of_ - member.offset_)
    {
      return false;
    }
  }
  return true;
}

bool SupportsSingleScalarRawLayoutCpp(
  const rosidl_typesupport_introspection_cpp::MessageMembers *members)
{
  if (members == nullptr || members->member_count_ != 1u ||
    members->members_ == nullptr)
  {
    return false;
  }
  const auto & member = members->members_[0];
  size_t scalar_size = 0u;
  return !member.is_array_ && member.offset_ == 0u &&
         CppScalarStorageSize(member.type_id_, &scalar_size) &&
         scalar_size == members->size_of_;
}

bool SupportsSingleScalarRawLayoutC(
  const rosidl_typesupport_introspection_c__MessageMembers *members)
{
  if (members == nullptr || members->member_count_ != 1u ||
    members->members_ == nullptr)
  {
    return false;
  }
  const auto & member = members->members_[0];
  size_t scalar_size = 0u;
  return !member.is_array_ && member.offset_ == 0u &&
         CScalarStorageSize(member.type_id_, &scalar_size) &&
         scalar_size == members->size_of_;
}

bool SupportsSingleUnboundedStringLoanedMessageCpp(
  const rosidl_typesupport_introspection_cpp::MessageMembers *members)
{
  if (members == nullptr || members->size_of_ == 0 ||
    members->member_count_ != 1u || members->init_function == nullptr ||
    members->fini_function == nullptr)
  {
    return false;
  }
  const auto & member = members->members_[0];
  return !member.is_array_ &&
         member.type_id_ ==
         rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING &&
         member.offset_ < members->size_of_;
}

bool RangeWithin(
  const void *ptr, size_t size, const void *storage,
  size_t capacity)
{
  if (ptr == nullptr || storage == nullptr) {
    return false;
  }
  const uintptr_t begin = reinterpret_cast<uintptr_t>(storage);
  const uintptr_t end = begin + capacity;
  const uintptr_t value = reinterpret_cast<uintptr_t>(ptr);
  if (end < begin || value < begin || value > end) {
    return false;
  }
  return size <= static_cast<size_t>(end - value);
}

size_t CppMemberElementSize(
  const rosidl_typesupport_introspection_cpp::MessageMember & member)
{
  namespace ic = rosidl_typesupport_introspection_cpp;
  size_t scalar_size = 0;
  if (CppScalarStorageSize(member.type_id_, &scalar_size)) {
    return scalar_size;
  }
  if (member.type_id_ == ic::ROS_TYPE_STRING) {
    return 1u;
  }
  if (member.type_id_ == ic::ROS_TYPE_WSTRING) {
    return 1u;
  }
  if (member.type_id_ == ic::ROS_TYPE_MESSAGE && member.members_ != nullptr) {
    const auto *nested = ResolveCppMembers(member.members_);
    return nested == nullptr ? 0u : nested->size_of_;
  }
  return 0u;
}

bool HasDynamicCppMember(
  const rosidl_typesupport_introspection_cpp::MessageMembers *members,
  size_t depth = 0u)
{
  namespace ic = rosidl_typesupport_introspection_cpp;
  if (members == nullptr || members->members_ == nullptr || depth > 16u) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    if (member.is_array_ && member.array_size_ == 0u) {
      return true;
    }
    if (!member.is_array_ &&
      (member.type_id_ == ic::ROS_TYPE_STRING ||
      member.type_id_ == ic::ROS_TYPE_WSTRING))
    {
      return true;
    }
    if (member.type_id_ == ic::ROS_TYPE_MESSAGE && member.members_ != nullptr) {
      const auto *nested = ResolveCppMembers(member.members_);
      if (HasDynamicCppMember(nested, depth + 1u)) {
        return true;
      }
    }
  }
  return false;
}

bool DynamicStringDataWithinLoanCpp(
  const rosidl_typesupport_introspection_cpp::MessageMember & member,
  const void *field, size_t index, const void *storage, size_t capacity)
{
  namespace ic = rosidl_typesupport_introspection_cpp;
  if (member.string_size_function == nullptr ||
    member.get_const_string_data_function == nullptr)
  {
    return false;
  }
  const size_t count = member.string_size_function(field, index);
  if (count == 0u) {
    return true;
  }
  const size_t code_unit_size =
    member.type_id_ == ic::ROS_TYPE_WSTRING ? sizeof(char16_t) : sizeof(char);
  if (count > std::numeric_limits<size_t>::max() / code_unit_size) {
    return false;
  }
  return RangeWithin(member.get_const_string_data_function(field, index),
                     count * code_unit_size, storage, capacity);
}

bool DynamicStorageWithinLoanCpp(
  const rosidl_typesupport_introspection_cpp::MessageMembers *members,
  const uint8_t *base, const void *storage, size_t capacity,
  size_t depth = 0u)
{
  namespace ic = rosidl_typesupport_introspection_cpp;
  if (members == nullptr || members->members_ == nullptr || base == nullptr ||
    depth > 16u)
  {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    const uint8_t *field = base + member.offset_;
    if (member.is_array_) {
      if (member.array_size_ != 0u) {
        continue;
      }
      if (member.size_function == nullptr ||
        member.get_const_function == nullptr)
      {
        return false;
      }
      const size_t count = member.size_function(field);
      if (count == 0u) {
        continue;
      }
      const void *first = member.get_const_function(field, 0u);
      const size_t element_size = CppMemberElementSize(member);
      if (element_size == 0u ||
        count > std::numeric_limits<size_t>::max() / element_size ||
        !RangeWithin(first, count * element_size, storage, capacity))
      {
        return false;
      }
      if (member.type_id_ == ic::ROS_TYPE_STRING ||
        member.type_id_ == ic::ROS_TYPE_WSTRING)
      {
        for (size_t j = 0; j < count; ++j) {
          if (!DynamicStringDataWithinLoanCpp(member, field, j, storage,
                                              capacity))
          {
            return false;
          }
        }
      } else if (member.type_id_ == ic::ROS_TYPE_MESSAGE &&
        member.members_ != nullptr)
      {
        const auto *nested = ResolveCppMembers(member.members_);
        for (size_t j = 0; j < count; ++j) {
          const auto *nested_base =
            static_cast<const uint8_t *>(member.get_const_function(field, j));
          if (!DynamicStorageWithinLoanCpp(nested, nested_base, storage,
                                           capacity, depth + 1u))
          {
            return false;
          }
        }
      }
      continue;
    }
    if (member.type_id_ == ic::ROS_TYPE_STRING ||
      member.type_id_ == ic::ROS_TYPE_WSTRING)
    {
      if (!DynamicStringDataWithinLoanCpp(member, field, 0u, storage,
                                          capacity))
      {
        return false;
      }
      continue;
    }
    if (member.type_id_ == ic::ROS_TYPE_MESSAGE && member.members_ != nullptr) {
      const auto *nested = ResolveCppMembers(member.members_);
      if (!DynamicStorageWithinLoanCpp(nested, field, storage, capacity,
                                       depth + 1u))
      {
        return false;
      }
    }
  }
  return true;
}

bool LoanedStringFieldCpp(
  const rosidl_typesupport_introspection_cpp::MessageMembers *members,
  const void *ros_message, const uint8_t **data, size_t *size)
{
  if (!SupportsSingleUnboundedStringLoanedMessageCpp(members) ||
    ros_message == nullptr || data == nullptr || size == nullptr)
  {
    return false;
  }
  const auto & member = members->members_[0];
  if (member.string_size_function == nullptr ||
    member.get_const_string_data_function == nullptr)
  {
    return false;
  }
  const auto *base = static_cast<const uint8_t *>(ros_message);
  const void *field = base + member.offset_;
  *size = member.string_size_function(field, 0u);
  *data = static_cast<const uint8_t *>(
    member.get_const_string_data_function(field, 0u));
  return *data != nullptr || *size == 0u;
}

void WriteU32LittleEndian(uint8_t *out, uint32_t value)
{
  out[0] = static_cast<uint8_t>(value & 0xffu);
  out[1] = static_cast<uint8_t>((value >> 8u) & 0xffu);
  out[2] = static_cast<uint8_t>((value >> 16u) & 0xffu);
  out[3] = static_cast<uint8_t>((value >> 24u) & 0xffu);
}

bool SplitFieldPath(
  const std::string & field_path, std::string *head,
  std::string *tail)
{
  if (field_path.empty() || head == nullptr || tail == nullptr) {
    return false;
  }
  const size_t dot = field_path.find('.');
  if (dot == std::string::npos) {
    *head = field_path;
    tail->clear();
    return true;
  }
  if (dot == 0u || dot + 1u == field_path.size()) {
    return false;
  }
  *head = field_path.substr(0u, dot);
  *tail = field_path.substr(dot + 1u);
  return true;
}

const rosidl_typesupport_introspection_cpp::MessageMembers * NestedCppMembers(
  const rosidl_typesupport_introspection_cpp::MessageMember & member)
{
  namespace ic = rosidl_typesupport_introspection_cpp;
  if (member.is_array_ || member.type_id_ != ic::ROS_TYPE_MESSAGE ||
    member.members_ == nullptr)
  {
    return nullptr;
  }
  return ResolveCppMembers(member.members_);
}

const rosidl_typesupport_introspection_c__MessageMembers * NestedCMembers(
  const rosidl_typesupport_introspection_c__MessageMember & member)
{
  if (member.is_array_ ||
    member.type_id_ != rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE ||
    member.members_ == nullptr)
  {
    return nullptr;
  }
  return ResolveCMembers(member.members_);
}

bool HasNumericFieldCpp(
  const rosidl_typesupport_introspection_cpp::MessageMembers *members,
  const std::string & field_path, size_t depth = 0)
{
  if (members == nullptr || field_path.empty() || depth > 16u) {
    return false;
  }
  std::string head;
  std::string tail;
  if (!SplitFieldPath(field_path, &head, &tail)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    if (member.name_ == nullptr || head != member.name_) {
      continue;
    }
    if (tail.empty()) {
      return !member.is_array_ && IsNumericIntrospectionType(member.type_id_);
    }
    return HasNumericFieldCpp(NestedCppMembers(member), tail, depth + 1u);
  }
  return false;
}

bool HasNumericFieldC(
  const rosidl_typesupport_introspection_c__MessageMembers *members,
  const std::string & field_path, size_t depth = 0)
{
  if (members == nullptr || field_path.empty() || depth > 16u) {
    return false;
  }
  std::string head;
  std::string tail;
  if (!SplitFieldPath(field_path, &head, &tail)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    if (member.name_ == nullptr || head != member.name_) {
      continue;
    }
    if (tail.empty()) {
      return !member.is_array_ && IsNumericIntrospectionType(member.type_id_);
    }
    return HasNumericFieldC(NestedCMembers(member), tail, depth + 1u);
  }
  return false;
}

bool HasStringFieldCpp(
  const rosidl_typesupport_introspection_cpp::MessageMembers *members,
  const std::string & field_path, size_t depth = 0)
{
  if (members == nullptr || field_path.empty() || depth > 16u) {
    return false;
  }
  std::string head;
  std::string tail;
  if (!SplitFieldPath(field_path, &head, &tail)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    if (member.name_ == nullptr || head != member.name_) {
      continue;
    }
    if (tail.empty()) {
      return !member.is_array_ &&
             member.type_id_ ==
             rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING;
    }
    return HasStringFieldCpp(NestedCppMembers(member), tail, depth + 1u);
  }
  return false;
}

bool HasStringFieldC(
  const rosidl_typesupport_introspection_c__MessageMembers *members,
  const std::string & field_path, size_t depth = 0)
{
  if (members == nullptr || field_path.empty() || depth > 16u) {
    return false;
  }
  std::string head;
  std::string tail;
  if (!SplitFieldPath(field_path, &head, &tail)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    if (member.name_ == nullptr || head != member.name_) {
      continue;
    }
    if (tail.empty()) {
      return !member.is_array_ &&
             member.type_id_ ==
             rosidl_typesupport_introspection_c__ROS_TYPE_STRING;
    }
    return HasStringFieldC(NestedCMembers(member), tail, depth + 1u);
  }
  return false;
}

bool ReadNumericFieldCpp(
  const rosidl_typesupport_introspection_cpp::MessageMembers *members,
  const uint8_t *base, const std::string & field_path, double *out,
  size_t depth = 0)
{
  if (members == nullptr || base == nullptr || out == nullptr ||
    field_path.empty() || depth > 16u)
  {
    return false;
  }
  std::string head;
  std::string tail;
  if (!SplitFieldPath(field_path, &head, &tail)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    if (member.name_ == nullptr || head != member.name_) {
      continue;
    }
    const uint8_t *field = base + member.offset_;
    if (tail.empty()) {
      return !member.is_array_ &&
             ReadScalarAsDouble(field, member.type_id_, out);
    }
    return ReadNumericFieldCpp(NestedCppMembers(member), field, tail, out,
                               depth + 1u);
  }
  return false;
}

bool ReadNumericFieldC(
  const rosidl_typesupport_introspection_c__MessageMembers *members,
  const uint8_t *base, const std::string & field_path, double *out,
  size_t depth = 0)
{
  if (members == nullptr || base == nullptr || out == nullptr ||
    field_path.empty() || depth > 16u)
  {
    return false;
  }
  std::string head;
  std::string tail;
  if (!SplitFieldPath(field_path, &head, &tail)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    if (member.name_ == nullptr || head != member.name_) {
      continue;
    }
    const uint8_t *field = base + member.offset_;
    if (tail.empty()) {
      return !member.is_array_ &&
             ReadScalarAsDouble(field, member.type_id_, out);
    }
    return ReadNumericFieldC(NestedCMembers(member), field, tail, out,
                             depth + 1u);
  }
  return false;
}

bool ReadStringFieldCpp(
  const rosidl_typesupport_introspection_cpp::MessageMembers *members,
  const uint8_t *base, const std::string & field_path, std::string *out,
  size_t depth = 0)
{
  if (members == nullptr || base == nullptr || out == nullptr ||
    field_path.empty() || depth > 16u)
  {
    return false;
  }
  std::string head;
  std::string tail;
  if (!SplitFieldPath(field_path, &head, &tail)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    if (member.name_ == nullptr || head != member.name_) {
      continue;
    }
    const uint8_t *field = base + member.offset_;
    if (tail.empty()) {
      if (member.is_array_ ||
        member.type_id_ !=
        rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING ||
        member.string_size_function == nullptr ||
        member.get_const_string_data_function == nullptr)
      {
        return false;
      }
      const size_t size = member.string_size_function(field, 0u);
      const auto *data = static_cast<const char *>(
        member.get_const_string_data_function(field, 0u));
      if (data == nullptr && size != 0u) {
        return false;
      }
      out->assign(data == nullptr ? "" : data, size);
      return true;
    }
    return ReadStringFieldCpp(NestedCppMembers(member), field, tail, out,
                              depth + 1u);
  }
  return false;
}

bool ReadStringFieldC(
  const rosidl_typesupport_introspection_c__MessageMembers *members,
  const uint8_t *base, const std::string & field_path, std::string *out,
  size_t depth = 0)
{
  if (members == nullptr || base == nullptr || out == nullptr ||
    field_path.empty() || depth > 16u)
  {
    return false;
  }
  std::string head;
  std::string tail;
  if (!SplitFieldPath(field_path, &head, &tail)) {
    return false;
  }
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & member = members->members_[i];
    if (member.name_ == nullptr || head != member.name_) {
      continue;
    }
    const uint8_t *field = base + member.offset_;
    if (tail.empty()) {
      if (member.is_array_ ||
        member.type_id_ !=
        rosidl_typesupport_introspection_c__ROS_TYPE_STRING)
      {
        return false;
      }
      const auto *value =
        reinterpret_cast<const rosidl_runtime_c__String *>(field);
      if (value->data == nullptr && value->size != 0u) {
        return false;
      }
      out->assign(value->data == nullptr ? "" : value->data, value->size);
      return true;
    }
    return ReadStringFieldC(NestedCMembers(member), field, tail, out,
                            depth + 1u);
  }
  return false;
}
} // namespace

bool MessageAdapter::HasNumericField(const std::string & field_name) const
{
  if (message_members_kind_ == MessageMembersKind::Cpp &&
    cpp_members_ != nullptr)
  {
    return HasNumericFieldCpp(cpp_members_, field_name);
  } else if (message_members_kind_ == MessageMembersKind::C &&
    c_members_ != nullptr)
  {
    return HasNumericFieldC(c_members_, field_name);
  }
  return false;
}

bool MessageAdapter::HasStringField(const std::string & field_name) const
{
  if (message_members_kind_ == MessageMembersKind::Cpp &&
    cpp_members_ != nullptr)
  {
    return HasStringFieldCpp(cpp_members_, field_name);
  } else if (message_members_kind_ == MessageMembersKind::C &&
    c_members_ != nullptr)
  {
    return HasStringFieldC(c_members_, field_name);
  }
  return false;
}

bool MessageAdapter::ReadNumericField(
  const void *ros_message,
  const std::string & field_name,
  double *out) const
{
  if (ros_message == nullptr || out == nullptr) {
    return false;
  }
  const uint8_t *base = static_cast<const uint8_t *>(ros_message);
  if (message_members_kind_ == MessageMembersKind::Cpp &&
    cpp_members_ != nullptr)
  {
    return ReadNumericFieldCpp(cpp_members_, base, field_name, out);
  } else if (message_members_kind_ == MessageMembersKind::C &&
    c_members_ != nullptr)
  {
    return ReadNumericFieldC(c_members_, base, field_name, out);
  }
  return false;
}

bool MessageAdapter::ReadStringField(
  const void *ros_message,
  const std::string & field_name,
  std::string *out) const
{
  if (ros_message == nullptr || out == nullptr) {
    return false;
  }
  const uint8_t *base = static_cast<const uint8_t *>(ros_message);
  if (message_members_kind_ == MessageMembersKind::Cpp &&
    cpp_members_ != nullptr)
  {
    return ReadStringFieldCpp(cpp_members_, base, field_name, out);
  } else if (message_members_kind_ == MessageMembersKind::C &&
    c_members_ != nullptr)
  {
    return ReadStringFieldC(c_members_, base, field_name, out);
  }
  return false;
}

void * MessageAdapter::AllocateTemporaryMessage() const
{
  if (message_members_kind_ == MessageMembersKind::Cpp) {
    if (cpp_members_ == nullptr || cpp_members_->size_of_ == 0 ||
      cpp_members_->init_function == nullptr ||
      cpp_members_->fini_function == nullptr)
    {
      return nullptr;
    }
    void *message = ::operator new(cpp_members_->size_of_, std::nothrow);
    if (message == nullptr) {
      return nullptr;
    }
    try {
      cpp_members_->init_function(
          message, rosidl_runtime_cpp::MessageInitialization::ALL);
    } catch (...) {
      ::operator delete(message);
      return nullptr;
    }
    return message;
  }
  if (message_members_kind_ == MessageMembersKind::C) {
    if (c_members_ == nullptr || c_members_->size_of_ == 0 ||
      c_members_->init_function == nullptr ||
      c_members_->fini_function == nullptr)
    {
      return nullptr;
    }
    void *message = ::operator new(c_members_->size_of_, std::nothrow);
    if (message == nullptr) {
      return nullptr;
    }
    c_members_->init_function(message, ROSIDL_RUNTIME_C_MSG_INIT_ALL);
    return message;
  }
  return nullptr;
}

void * MessageAdapter::AllocateMessage() const
{
  return AllocateTemporaryMessage();
}

void MessageAdapter::DestroyMessage(void *message) const
{
  DestroyTemporaryMessage(message);
}

size_t MessageAdapter::MessageSize() const
{
  if (message_members_kind_ == MessageMembersKind::Cpp &&
    cpp_members_ != nullptr)
  {
    return cpp_members_->size_of_;
  }
  if (message_members_kind_ == MessageMembersKind::C && c_members_ != nullptr) {
    return c_members_->size_of_;
  }
  return 0;
}

bool MessageAdapter::SupportsRawLoanedMessage() const
{
  if (type_name_ == "std_msgs/msg/String" ||
    type_name_ == "std_msgs/msg/dds_/String_")
  {
    return false;
  }
  if (SupportsDynamicStringLoanedMessage() || SupportsDynamicLoanedMessage()) {
    return false;
  }
  if (message_members_kind_ == MessageMembersKind::Cpp) {
    return !HasDynamicCppMember(cpp_members_) &&
           SupportsFlatRawLoanedMessageCpp(cpp_members_);
  }
  if (message_members_kind_ == MessageMembersKind::C) {
    return SupportsFlatRawLoanedMessageC(c_members_);
  }
  return false;
}

bool MessageAdapter::SupportsBrokerRawLoanedMessage() const
{
  if (!SupportsRawLoanedMessage()) {
    return false;
  }
  if (message_members_kind_ == MessageMembersKind::Cpp) {
    return SupportsSingleScalarRawLayoutCpp(cpp_members_);
  }
  if (message_members_kind_ == MessageMembersKind::C) {
    return SupportsSingleScalarRawLayoutC(c_members_);
  }
  return false;
}

bool MessageAdapter::SupportsDynamicStringLoanedMessage() const
{
  return message_members_kind_ == MessageMembersKind::Cpp &&
         SupportsSingleUnboundedStringLoanedMessageCpp(cpp_members_);
}

bool MessageAdapter::SupportsDynamicLoanedMessage() const
{
  return message_members_kind_ == MessageMembersKind::Cpp &&
         HasDynamicCppMember(cpp_members_);
}

bool MessageAdapter::DynamicStorageWithinLoan(
  const void *ros_message,
  const void *storage,
  size_t capacity) const
{
  if (message_members_kind_ != MessageMembersKind::Cpp ||
    ros_message == nullptr || storage == nullptr)
  {
    return false;
  }
  return DynamicStorageWithinLoanCpp(cpp_members_,
                                     static_cast<const uint8_t *>(ros_message),
                                     storage, capacity);
}

void * MessageAdapter::ConstructMessageInPlace(
  void *storage,
  size_t capacity) const
{
  if (storage == nullptr) {
    return nullptr;
  }
  const auto address = reinterpret_cast<uintptr_t>(storage);
  if (address % alignof(std::max_align_t) != 0) {
    return nullptr;
  }
  if (message_members_kind_ == MessageMembersKind::Cpp) {
    if (cpp_members_ == nullptr || cpp_members_->size_of_ == 0 ||
      cpp_members_->size_of_ > capacity ||
      cpp_members_->init_function == nullptr ||
      cpp_members_->fini_function == nullptr)
    {
      return nullptr;
    }
    try {
      cpp_members_->init_function(
          storage, rosidl_runtime_cpp::MessageInitialization::ALL);
    } catch (...) {
      return nullptr;
    }
    return storage;
  }
  if (message_members_kind_ == MessageMembersKind::C) {
    if (c_members_ == nullptr || c_members_->size_of_ == 0 ||
      c_members_->size_of_ > capacity ||
      c_members_->init_function == nullptr ||
      c_members_->fini_function == nullptr)
    {
      return nullptr;
    }
    c_members_->init_function(storage, ROSIDL_RUNTIME_C_MSG_INIT_ALL);
    return storage;
  }
  return nullptr;
}

void * MessageAdapter::MessageStorageAtEnd(
  void *storage,
  size_t capacity) const
{
  const size_t message_size = MessageSize();
  if (storage == nullptr || message_size == 0 || message_size > capacity) {
    return nullptr;
  }
  const uintptr_t begin = reinterpret_cast<uintptr_t>(storage);
  if (capacity > std::numeric_limits<uintptr_t>::max() - begin) {
    return nullptr;
  }
  const uintptr_t end = begin + capacity;
  constexpr uintptr_t alignment = alignof(std::max_align_t);
  const uintptr_t aligned_start = (end - message_size) & ~(alignment - 1u);
  if (aligned_start < begin) {
    return nullptr;
  }
  return reinterpret_cast<void *>(aligned_start);
}

void * MessageAdapter::ConstructMessageInPlaceAtEnd(
  void *storage,
  size_t capacity) const
{
  void *message_storage = MessageStorageAtEnd(storage, capacity);
  if (message_storage == nullptr) {
    return nullptr;
  }
  const uintptr_t end = reinterpret_cast<uintptr_t>(storage) + capacity;
  return ConstructMessageInPlace(
      message_storage, end - reinterpret_cast<uintptr_t>(message_storage));
}

void MessageAdapter::DestroyMessageInPlace(void *message) const
{
  if (message == nullptr) {
    return;
  }
  if (message_members_kind_ == MessageMembersKind::Cpp &&
    cpp_members_ != nullptr && cpp_members_->fini_function != nullptr)
  {
    cpp_members_->fini_function(message);
  } else if (message_members_kind_ == MessageMembersKind::C &&
    c_members_ != nullptr && c_members_->fini_function != nullptr)
  {
    c_members_->fini_function(message);
  }
}

bool MessageAdapter::EncodeLoanedStringMddsIntoBuffer(
  const void *ros_message, void *buffer, size_t capacity,
  size_t *payload_size) const
{
  if (payload_size == nullptr) {
    return false;
  }
  *payload_size = 0u;
  if (!SupportsDynamicStringLoanedMessage() || ros_message == nullptr ||
    buffer == nullptr)
  {
    return false;
  }
  const uint8_t *string_data = nullptr;
  size_t string_size = 0u;
  if (!LoanedStringFieldCpp(cpp_members_, ros_message, &string_data,
                            &string_size) ||
    string_size > std::numeric_limits<uint32_t>::max() - 1u)
  {
    return false;
  }
  constexpr size_t kCdrEncapsulationSize = 4u;
  constexpr size_t kCdrStringLengthSize = 4u;
  constexpr size_t kCdrStringDataOffset =
    kCdrEncapsulationSize + kCdrStringLengthSize;
  auto *out = static_cast<uint8_t *>(buffer);
  if (string_data != out + kCdrStringDataOffset) {
    return false;
  }
  const size_t string_wire_size = string_size + 1u;
  const size_t unpadded_size = kCdrStringDataOffset + string_wire_size;
  const size_t padding = (4u - (unpadded_size % 4u)) % 4u;
  const size_t required_size = unpadded_size + padding;
  if (required_size > capacity) {
    return false;
  }
  out[0] = 0x00u;
  out[1] = 0x01u;
  out[2] = 0x00u;
  out[3] = 0x00u;
  WriteU32LittleEndian(out + kCdrEncapsulationSize,
                       static_cast<uint32_t>(string_wire_size));
  out[kCdrStringDataOffset + string_size] = 0u;
  if (padding != 0u) {
    std::memset(out + unpadded_size, 0, padding);
  }
  *payload_size = required_size;
  return true;
}

bool MessageAdapter::PrepareLoanedDynamicMddsPayload(
  const void *ros_message, void *buffer, size_t capacity,
  size_t *payload_size) const
{
  if (payload_size == nullptr) {
    return false;
  }
  *payload_size = 0u;
  if (!SupportsDynamicLoanedMessage() || ros_message == nullptr ||
    buffer == nullptr ||
    !DynamicStorageWithinLoan(ros_message, buffer, capacity))
  {
    return false;
  }
  if (!legacy_adapter_.IsValid()) {
    return false;
  }
  return legacy_adapter_.EncodeIntoBuffer(ros_message, buffer, capacity,
                                          payload_size);
}

void MessageAdapter::DestroyTemporaryMessage(void *message) const
{
  if (message == nullptr) {
    return;
  }
  DestroyMessageInPlace(message);
  ::operator delete(message);
}

bool MessageAdapter::SerializedToMddsPayload(
  const uint8_t *data, size_t len, std::vector<uint8_t> *payload) const
{
  if ((data == nullptr && len != 0) || payload == nullptr) {
    return false;
  }
  if (!legacy_adapter_.IsValid()) {
    if (len == 0) {
      payload->clear();
      return true;
    }
    payload->assign(data, data + len);
    return true;
  }
  void *message = AllocateTemporaryMessage();
  if (message == nullptr) {
    return false;
  }
  const bool ok = Decode(data, len, message) && EncodeMdds(message, payload);
  DestroyTemporaryMessage(message);
  return ok;
}

bool MessageAdapter::MddsPayloadToSerialized(
  const uint8_t *data, size_t len, std::vector<uint8_t> *payload) const
{
  if ((data == nullptr && len != 0) || payload == nullptr) {
    return false;
  }
  if (!legacy_adapter_.IsValid()) {
    if (len == 0) {
      payload->clear();
      return true;
    }
    payload->assign(data, data + len);
    return true;
  }
  void *message = AllocateTemporaryMessage();
  if (message == nullptr) {
    return false;
  }
  const bool ok = DecodeMdds(data, len, message) && Encode(message, payload);
  DestroyTemporaryMessage(message);
  return ok;
}

bool CdrMaxSerializedMessageSize(
  const rosidl_message_type_support_t *type_support, size_t *size)
{
  if (size == nullptr) {
    return false;
  }
  const message_type_support_callbacks_t *callbacks =
    ResolveCdrCallbacks(type_support);
  if (callbacks == nullptr) {
    return false;
  }
  char bounds_info = ROSIDL_TYPESUPPORT_FASTRTPS_UNBOUNDED_TYPE;
  const size_t payload_size = callbacks->max_serialized_size(bounds_info);
  if ((bounds_info & ROSIDL_TYPESUPPORT_FASTRTPS_BOUNDED_TYPE) == 0) {
    return false;
  }
  *size = 4u + payload_size;
  return true;
}

} // namespace rmw_mdds_cpp
