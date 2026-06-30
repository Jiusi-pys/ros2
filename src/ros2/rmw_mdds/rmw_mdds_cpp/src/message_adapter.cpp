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

#include <new>

#include "rmw/error_handling.h"
#include "rosidl_runtime_c/message_initialization.h"
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
std::string MakeRosTypeName(const message_type_support_callbacks_t * callbacks)
{
  if (callbacks == nullptr || callbacks->message_namespace_ == nullptr ||
    callbacks->message_name_ == nullptr) {
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

std::string MakeDdsTypeName(const message_type_support_callbacks_t * callbacks)
{
  if (callbacks == nullptr || callbacks->message_namespace_ == nullptr ||
    callbacks->message_name_ == nullptr) {
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

const message_type_support_callbacks_t * ResolveCdrCallbacks(
  const rosidl_message_type_support_t * type_support)
{
  if (type_support == nullptr) {
    return nullptr;
  }
  const rosidl_message_type_support_t * cdr_type_support = get_message_typesupport_handle(
    type_support, rosidl_typesupport_fastrtps_cpp::typesupport_identifier);
  if (cdr_type_support == nullptr || cdr_type_support->data == nullptr) {
    rmw_reset_error();
    cdr_type_support = get_message_typesupport_handle(
      type_support, rosidl_typesupport_fastrtps_c__identifier);
  }
  if (cdr_type_support == nullptr || cdr_type_support->data == nullptr) {
    rmw_reset_error();
    return nullptr;
  }
  auto * callbacks =
    static_cast<const message_type_support_callbacks_t *>(cdr_type_support->data);
  if (callbacks->cdr_serialize == nullptr || callbacks->cdr_deserialize == nullptr ||
    callbacks->get_serialized_size == nullptr || callbacks->max_serialized_size == nullptr) {
    return nullptr;
  }
  return callbacks;
}

const rosidl_typesupport_introspection_cpp::MessageMembers * ResolveCppMembers(
  const rosidl_message_type_support_t * type_support)
{
  if (type_support == nullptr) {
    return nullptr;
  }
  const rosidl_message_type_support_t * introspection = get_message_typesupport_handle(
    type_support, rosidl_typesupport_introspection_cpp::typesupport_identifier);
  if (introspection == nullptr || introspection->data == nullptr) {
    rmw_reset_error();
    return nullptr;
  }
  return static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(
    introspection->data);
}

const rosidl_typesupport_introspection_c__MessageMembers * ResolveCMembers(
  const rosidl_message_type_support_t * type_support)
{
  if (type_support == nullptr) {
    return nullptr;
  }
  const rosidl_message_type_support_t * introspection =
    get_message_typesupport_handle(type_support, rosidl_typesupport_introspection_c__identifier);
  if (introspection == nullptr || introspection->data == nullptr) {
    rmw_reset_error();
    return nullptr;
  }
  return static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(
    introspection->data);
}

}  // namespace

bool MessageAdapter::Init(const rosidl_message_type_support_t * type_support)
{
  const bool has_legacy_adapter = legacy_adapter_.Init(type_support);
  rmw_reset_error();
  c_members_ = nullptr;
  cpp_members_ = ResolveCppMembers(type_support);
  if (cpp_members_ != nullptr) {
    message_members_kind_ = MessageMembersKind::Cpp;
  } else {
    c_members_ = ResolveCMembers(type_support);
    message_members_kind_ =
      c_members_ == nullptr ? MessageMembersKind::None : MessageMembersKind::C;
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

const std::string & MessageAdapter::TypeName() const
{
  return type_name_;
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

bool MessageAdapter::Encode(const void * ros_message, std::vector<uint8_t> * payload) const
{
  if (storage_kind_ == StorageKind::Legacy) {
    return legacy_adapter_.Encode(ros_message, payload);
  }
  if (ros_message == nullptr || payload == nullptr || cdr_callbacks_ == nullptr) {
    return false;
  }
  const size_t payload_size = 4u + cdr_callbacks_->get_serialized_size(ros_message);
  payload->assign(payload_size, 0);
  try {
    eprosima::fastcdr::FastBuffer buffer(
      reinterpret_cast<char *>(payload->data()), payload->size());
    eprosima::fastcdr::Cdr serializer(
      buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::CdrVersion::XCDRv1);
    serializer.set_encoding_flag(eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR);
    serializer.serialize_encapsulation();
    if (!cdr_callbacks_->cdr_serialize(ros_message, serializer)) {
      return false;
    }
    payload->resize(serializer.get_serialized_data_length());
  } catch (const eprosima::fastcdr::exception::Exception & exception) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("Fast CDR serialization failed: %s", exception.what());
    return false;
  }
  return true;
}

bool MessageAdapter::EncodeMdds(const void * ros_message, std::vector<uint8_t> * payload) const
{
  if (legacy_adapter_.IsValid()) {
    return legacy_adapter_.Encode(ros_message, payload);
  }
  return Encode(ros_message, payload);
}

bool MessageAdapter::Decode(const uint8_t * data, size_t len, void * ros_message) const
{
  if (storage_kind_ == StorageKind::Legacy) {
    return legacy_adapter_.Decode(data, len, ros_message);
  }
  if ((data == nullptr && len != 0) || ros_message == nullptr || cdr_callbacks_ == nullptr) {
    return false;
  }
  try {
    eprosima::fastcdr::FastBuffer buffer(
      const_cast<char *>(reinterpret_cast<const char *>(data)), len);
    eprosima::fastcdr::Cdr deserializer(buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN);
    deserializer.read_encapsulation();
    return cdr_callbacks_->cdr_deserialize(deserializer, ros_message);
  } catch (const eprosima::fastcdr::exception::Exception & exception) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("Fast CDR deserialization failed: %s", exception.what());
    return false;
  }
}

bool MessageAdapter::DecodeMdds(const uint8_t * data, size_t len, void * ros_message) const
{
  if (legacy_adapter_.IsValid()) {
    return legacy_adapter_.Decode(data, len, ros_message);
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

bool ReadScalarAsDouble(const uint8_t * field, uint8_t type_id, double * out)
{
  namespace ic = rosidl_typesupport_introspection_cpp;
  switch (type_id) {
    case ic::ROS_TYPE_FLOAT: *out = *reinterpret_cast<const float *>(field); return true;
    case ic::ROS_TYPE_DOUBLE: *out = *reinterpret_cast<const double *>(field); return true;
    case ic::ROS_TYPE_BOOLEAN: *out = *reinterpret_cast<const bool *>(field) ? 1.0 : 0.0; return true;
    case ic::ROS_TYPE_CHAR:
    case ic::ROS_TYPE_BYTE:
    case ic::ROS_TYPE_UINT8: *out = *reinterpret_cast<const uint8_t *>(field); return true;
    case ic::ROS_TYPE_INT8: *out = *reinterpret_cast<const int8_t *>(field); return true;
    case ic::ROS_TYPE_WCHAR:
    case ic::ROS_TYPE_UINT16: *out = *reinterpret_cast<const uint16_t *>(field); return true;
    case ic::ROS_TYPE_INT16: *out = *reinterpret_cast<const int16_t *>(field); return true;
    case ic::ROS_TYPE_UINT32: *out = *reinterpret_cast<const uint32_t *>(field); return true;
    case ic::ROS_TYPE_INT32: *out = *reinterpret_cast<const int32_t *>(field); return true;
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
}  // namespace

bool MessageAdapter::HasNumericField(const std::string & field_name) const
{
  if (message_members_kind_ == MessageMembersKind::Cpp && cpp_members_ != nullptr) {
    for (uint32_t i = 0; i < cpp_members_->member_count_; ++i) {
      const auto & m = cpp_members_->members_[i];
      if (m.name_ != nullptr && field_name == m.name_) {
        return !m.is_array_ && IsNumericIntrospectionType(m.type_id_);
      }
    }
  } else if (message_members_kind_ == MessageMembersKind::C && c_members_ != nullptr) {
    for (uint32_t i = 0; i < c_members_->member_count_; ++i) {
      const auto & m = c_members_->members_[i];
      if (m.name_ != nullptr && field_name == m.name_) {
        return !m.is_array_ && IsNumericIntrospectionType(m.type_id_);
      }
    }
  }
  return false;
}

bool MessageAdapter::ReadNumericField(
  const void * ros_message, const std::string & field_name, double * out) const
{
  if (ros_message == nullptr || out == nullptr) {
    return false;
  }
  const uint8_t * base = static_cast<const uint8_t *>(ros_message);
  if (message_members_kind_ == MessageMembersKind::Cpp && cpp_members_ != nullptr) {
    for (uint32_t i = 0; i < cpp_members_->member_count_; ++i) {
      const auto & m = cpp_members_->members_[i];
      if (m.name_ != nullptr && field_name == m.name_ && !m.is_array_) {
        return ReadScalarAsDouble(base + m.offset_, m.type_id_, out);
      }
    }
  } else if (message_members_kind_ == MessageMembersKind::C && c_members_ != nullptr) {
    for (uint32_t i = 0; i < c_members_->member_count_; ++i) {
      const auto & m = c_members_->members_[i];
      if (m.name_ != nullptr && field_name == m.name_ && !m.is_array_) {
        return ReadScalarAsDouble(base + m.offset_, m.type_id_, out);
      }
    }
  }
  return false;
}

void * MessageAdapter::AllocateTemporaryMessage() const
{
  if (message_members_kind_ == MessageMembersKind::Cpp) {
    if (
      cpp_members_ == nullptr || cpp_members_->size_of_ == 0 ||
      cpp_members_->init_function == nullptr || cpp_members_->fini_function == nullptr) {
      return nullptr;
    }
    void * message = ::operator new(cpp_members_->size_of_, std::nothrow);
    if (message == nullptr) {
      return nullptr;
    }
    try {
      cpp_members_->init_function(message, rosidl_runtime_cpp::MessageInitialization::ALL);
    } catch (...) {
      ::operator delete(message);
      return nullptr;
    }
    return message;
  }
  if (message_members_kind_ == MessageMembersKind::C) {
    if (
      c_members_ == nullptr || c_members_->size_of_ == 0 ||
      c_members_->init_function == nullptr || c_members_->fini_function == nullptr) {
      return nullptr;
    }
    void * message = ::operator new(c_members_->size_of_, std::nothrow);
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

void MessageAdapter::DestroyMessage(void * message) const
{
  DestroyTemporaryMessage(message);
}

void MessageAdapter::DestroyTemporaryMessage(void * message) const
{
  if (message == nullptr) {
    return;
  }
  if (message_members_kind_ == MessageMembersKind::Cpp && cpp_members_ != nullptr &&
    cpp_members_->fini_function != nullptr) {
    cpp_members_->fini_function(message);
  } else if (message_members_kind_ == MessageMembersKind::C && c_members_ != nullptr &&
    c_members_->fini_function != nullptr) {
    c_members_->fini_function(message);
  }
  ::operator delete(message);
}

bool MessageAdapter::SerializedToMddsPayload(
  const uint8_t * data, size_t len, std::vector<uint8_t> * payload) const
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
  void * message = AllocateTemporaryMessage();
  if (message == nullptr) {
    return false;
  }
  const bool ok = Decode(data, len, message) && EncodeMdds(message, payload);
  DestroyTemporaryMessage(message);
  return ok;
}

bool MessageAdapter::MddsPayloadToSerialized(
  const uint8_t * data, size_t len, std::vector<uint8_t> * payload) const
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
  void * message = AllocateTemporaryMessage();
  if (message == nullptr) {
    return false;
  }
  const bool ok = DecodeMdds(data, len, message) && Encode(message, payload);
  DestroyTemporaryMessage(message);
  return ok;
}

bool CdrMaxSerializedMessageSize(
  const rosidl_message_type_support_t * type_support, size_t * size)
{
  if (size == nullptr) {
    return false;
  }
  const message_type_support_callbacks_t * callbacks = ResolveCdrCallbacks(type_support);
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

}  // namespace rmw_mdds_cpp
