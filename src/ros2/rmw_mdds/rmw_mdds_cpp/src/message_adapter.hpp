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

#ifndef RMW_MDDS_CPP_SRC__MESSAGE_ADAPTER_HPP_
#define RMW_MDDS_CPP_SRC__MESSAGE_ADAPTER_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_fastrtps_cpp/message_type_support.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "string_adapter.hpp"

namespace rmw_mdds_cpp
{

class MessageAdapter {
public:
  bool Init(const rosidl_message_type_support_t *type_support);
  bool IsValid() const;
  const std::string & TypeName() const;
  const rosidl_type_hash_t & TypeHash() const;
  const std::string & WireTypeName() const;
  const std::string & MddsTypeName() const;
  bool Encode(const void *ros_message, std::vector<uint8_t> *payload) const;
  bool Decode(const uint8_t *data, size_t len, void *ros_message) const;
  bool EncodeMdds(const void *ros_message, std::vector<uint8_t> *payload) const;
  bool EncodedMddsSize(const void *ros_message, size_t *payload_size) const;
  bool EncodeMddsIntoBuffer(
    const void *ros_message, void *buffer,
    size_t capacity, size_t *payload_size) const;
  bool DecodeMdds(const uint8_t *data, size_t len, void *ros_message) const;
  bool SerializedToMddsPayload(
    const uint8_t *data, size_t len,
    std::vector<uint8_t> *payload) const;
  bool MddsPayloadToSerialized(
    const uint8_t *data, size_t len,
    std::vector<uint8_t> *payload) const;

  // Allocate/destroy a default-constructed message of the concrete type, used
  // by the loaned-message API (rmw_borrow_loaned_message /
  // take_loaned_message). The returned buffer is owned by the caller until
  // handed to DestroyMessage().
  void * AllocateMessage() const;
  void DestroyMessage(void *message) const;
  size_t MessageSize() const;
  bool SupportsRawLoanedMessage() const;
  bool SupportsBrokerRawLoanedMessage() const;
  bool SupportsDynamicStringLoanedMessage() const;
  bool SupportsDynamicLoanedMessage() const;
  bool DynamicStorageWithinLoan(
    const void *ros_message, const void *storage,
    size_t capacity) const;
  void * ConstructMessageInPlace(void *storage, size_t capacity) const;
  void * MessageStorageAtEnd(void *storage, size_t capacity) const;
  void * ConstructMessageInPlaceAtEnd(void *storage, size_t capacity) const;
  void DestroyMessageInPlace(void *message) const;
  bool EncodeLoanedStringMddsIntoBuffer(
    const void *ros_message, void *buffer,
    size_t capacity,
    size_t *payload_size) const;
  bool PrepareLoanedDynamicMddsPayload(
    const void *ros_message, void *buffer,
    size_t capacity,
    size_t *payload_size) const;

  // Content-filter support for scalar fields (DDS-SQL `field OP value`).
  // Has*Field reports whether a scalar member exists at this field path (for
  // example `data`, `layout.data_offset`, or `header.frame_id`). Read*Field
  // reads such a member from a decoded message into `out`. These helpers
  // consult introspection members and return false for absent / array /
  // wrong-type fields.
  bool HasNumericField(const std::string & field_name) const;
  bool ReadNumericField(
    const void *ros_message, const std::string & field_name,
    double *out) const;
  bool HasStringField(const std::string & field_name) const;
  bool ReadStringField(
    const void *ros_message, const std::string & field_name,
    std::string *out) const;

private:
  enum class StorageKind
  {
    None,
    Cdr,
    Legacy,
  };

  enum class MessageMembersKind
  {
    None,
    C,
    Cpp,
  };

  void * AllocateTemporaryMessage() const;
  void DestroyTemporaryMessage(void *message) const;

  std::string type_name_;
  rosidl_type_hash_t type_hash_ = rosidl_get_zero_initialized_type_hash();
  std::string wire_type_name_;
  StorageKind storage_kind_ = StorageKind::None;
  MessageMembersKind message_members_kind_ = MessageMembersKind::None;
  const message_type_support_callbacks_t *cdr_callbacks_ = nullptr;
  const rosidl_typesupport_introspection_c__MessageMembers *c_members_ =
    nullptr;
  const rosidl_typesupport_introspection_cpp::MessageMembers *cpp_members_ =
    nullptr;
  StringAdapter legacy_adapter_;
};

bool CdrMaxSerializedMessageSize(
  const rosidl_message_type_support_t *type_support, size_t *size);

} // namespace rmw_mdds_cpp

#endif // RMW_MDDS_CPP_SRC__MESSAGE_ADAPTER_HPP_
