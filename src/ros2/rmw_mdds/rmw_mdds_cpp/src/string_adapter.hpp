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

#ifndef RMW_MDDS_CPP_SRC__STRING_ADAPTER_HPP_
#define RMW_MDDS_CPP_SRC__STRING_ADAPTER_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"

namespace rmw_mdds_cpp
{

class StringAdapter
{
public:
  bool Init(const rosidl_message_type_support_t * type_support);
  bool InitC(const rosidl_typesupport_introspection_c__MessageMembers * members);
  bool InitCpp(const rosidl_typesupport_introspection_cpp::MessageMembers * members);
  bool IsValid() const;
  const std::string & TypeName() const;
  bool Encode(const void * ros_message, std::vector<uint8_t> * payload) const;
  bool EncodedSize(const void * ros_message, size_t * payload_size) const;
  bool EncodeIntoBuffer(
    const void * ros_message, void * buffer, size_t capacity, size_t * payload_size) const;
  bool Decode(const uint8_t * data, size_t len, void * ros_message) const;

private:
  enum class StorageKind
  {
    None,
    C,
    Cpp,
  };

  bool InitCpp(const rosidl_message_type_support_t * type_support);
  bool InitC(const rosidl_message_type_support_t * type_support);

  std::string type_name_;
  StorageKind storage_kind_ = StorageKind::None;
  const rosidl_typesupport_introspection_c__MessageMembers * c_members_ = nullptr;
  const rosidl_typesupport_introspection_cpp::MessageMembers * cpp_members_ = nullptr;
  bool raw_string_payload_ = false;
  bool raw_pose_stamped_payload_ = false;
};

}  // namespace rmw_mdds_cpp

#endif  // RMW_MDDS_CPP_SRC__STRING_ADAPTER_HPP_
