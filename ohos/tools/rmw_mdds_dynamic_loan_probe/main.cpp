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

#include <std_msgs/msg/int32_multi_array.hpp>
#include <std_msgs/msg/string.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"

namespace
{
constexpr size_t kBridgeLoanCapacity = 64u * 1024u;

std::string LastRmwError()
{
  const rmw_error_string_t error = rmw_get_error_string();
  return std::string(error.str);
}

bool RequireEnvironment()
{
  const char * implementation = std::getenv("RMW_IMPLEMENTATION");
  const char * broker = std::getenv("RMW_MDDS_BROKER");
  const char * bridge = std::getenv("RMW_MDDS_BRIDGE");
  const char * bridge_library = std::getenv("RMW_MDDS_BRIDGE_LIBRARY");
  if (implementation == nullptr || std::strcmp(implementation, "rmw_mdds_cpp") != 0 ||
    broker == nullptr || std::strcmp(broker, "0") != 0 ||
    bridge == nullptr || std::strcmp(bridge, "1") != 0 ||
    bridge_library == nullptr || bridge_library[0] == '\0')
  {
    std::cerr << "RESULT|rmw_mdds_dynamic_loan|FAIL|reason=invalid_environment" << std::endl;
    return false;
  }
  return true;
}

bool IsArenaBacked(const void * message, const void * data, size_t size)
{
  if (message == nullptr || data == nullptr || size == 0u) {
    return false;
  }
  const uintptr_t message_address = reinterpret_cast<uintptr_t>(message);
  const uintptr_t data_address = reinterpret_cast<uintptr_t>(data);
  if (data_address >= message_address || size > message_address - data_address) {
    return false;
  }
  return message_address - data_address <= kBridgeLoanCapacity;
}

template<typename MessageT, typename Populate, typename Verify>
bool RunLoanCase(
  rmw_node_t * node, const char * shape, const char * topic,
  Populate populate, Verify verify)
{
  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<MessageT>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, topic, &rmw_qos_profile_default, &publisher_options);
  if (publisher == nullptr) {
    std::cerr << "RESULT|rmw_mdds_dynamic_loan_case|FAIL|shape=" << shape <<
      "|stage=create_publisher|error=" << LastRmwError() << std::endl;
    rcutils_reset_error();
    return false;
  }

  bool passed = publisher->can_loan_messages;
  void * loaned_message = nullptr;
  if (passed) {
    const rmw_ret_t borrow_ret = rmw_borrow_loaned_message(
      publisher, type_support, &loaned_message);
    passed = borrow_ret == RMW_RET_OK && loaned_message != nullptr;
    if (!passed) {
      std::cerr << "RESULT|rmw_mdds_dynamic_loan_case|FAIL|shape=" << shape <<
        "|stage=borrow|ret=" << borrow_ret << "|error=" << LastRmwError() << std::endl;
      rcutils_reset_error();
    }
  } else {
    std::cerr << "RESULT|rmw_mdds_dynamic_loan_case|FAIL|shape=" << shape <<
      "|stage=capability" << std::endl;
  }

  if (passed) {
    try {
      auto * message = static_cast<MessageT *>(loaned_message);
      populate(*message);
      passed = verify(*message);
    } catch (const std::exception & error) {
      std::cerr << "RESULT|rmw_mdds_dynamic_loan_case|FAIL|shape=" << shape <<
        "|stage=populate|error=" << error.what() << std::endl;
      passed = false;
    }
  }

  if (loaned_message != nullptr) {
    if (passed) {
      const rmw_ret_t publish_ret = rmw_publish_loaned_message(
        publisher, loaned_message, nullptr);
      passed = publish_ret == RMW_RET_OK;
      if (!passed) {
        std::cerr << "RESULT|rmw_mdds_dynamic_loan_case|FAIL|shape=" << shape <<
          "|stage=publish|ret=" << publish_ret << "|error=" << LastRmwError() << std::endl;
        rcutils_reset_error();
      }
    } else {
      const rmw_ret_t return_ret = rmw_return_loaned_message_from_publisher(
        publisher, loaned_message);
      if (return_ret != RMW_RET_OK) {
        std::cerr << "RESULT|rmw_mdds_dynamic_loan_case|FAIL|shape=" << shape <<
          "|stage=return|ret=" << return_ret << "|error=" << LastRmwError() << std::endl;
        rcutils_reset_error();
      }
    }
  }

  const rmw_ret_t destroy_ret = rmw_destroy_publisher(node, publisher);
  passed = passed && destroy_ret == RMW_RET_OK;
  if (passed) {
    std::cout << "RESULT|rmw_mdds_dynamic_loan_case|PASS|shape=" << shape << std::endl;
  }
  return passed;
}

bool SetEnclave(rmw_init_options_t * options, const char * enclave)
{
  if (options->enclave != nullptr) {
    options->allocator.deallocate(options->enclave, options->allocator.state);
  }
  options->enclave = rcutils_strdup(enclave, options->allocator);
  return options->enclave != nullptr;
}
}  // namespace

int main()
{
  if (!RequireEnvironment()) {
    return 2;
  }

  const char * identifier = rmw_get_implementation_identifier();
  if (identifier == nullptr || std::strcmp(identifier, "rmw_mdds_cpp") != 0) {
    std::cerr << "RESULT|rmw_mdds_dynamic_loan|FAIL|reason=wrong_rmw|actual=" <<
      (identifier == nullptr ? "null" : identifier) << std::endl;
    return 3;
  }

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  rmw_context_t context = rmw_get_zero_initialized_context();
  rmw_node_t * node = nullptr;
  bool initialized = false;
  bool passed = rmw_init_options_init(&options, allocator) == RMW_RET_OK;
  passed = passed && SetEnclave(&options, "/rmw_mdds_dynamic_loan_probe");
  if (passed) {
    passed = rmw_init(&options, &context) == RMW_RET_OK;
    initialized = passed;
  }
  if (passed) {
    node = rmw_create_node(&context, "rmw_mdds_dynamic_loan_probe", "/mdds");
    passed = node != nullptr;
  }
  if (!passed) {
    std::cerr << "RESULT|rmw_mdds_dynamic_loan|FAIL|reason=initialization|error=" <<
      LastRmwError() << std::endl;
    rcutils_reset_error();
  }

  if (passed) {
    passed = RunLoanCase<std_msgs::msg::String>(
      node, "string", "/mdds_dynamic_loan_string",
      [](std_msgs::msg::String & message) {message.data.assign(4096u, 's');},
      [](const std_msgs::msg::String & message) {
        return message.data.get_allocator().resource() != nullptr &&
               IsArenaBacked(&message, message.data.data(), message.data.size());
      });
  }
  if (passed) {
    passed = RunLoanCase<std_msgs::msg::Int32MultiArray>(
      node, "sequence", "/mdds_dynamic_loan_sequence",
      [](std_msgs::msg::Int32MultiArray & message) {message.data.assign(1024u, 3588);},
      [](const std_msgs::msg::Int32MultiArray & message) {
        return message.data.get_allocator().resource() != nullptr &&
               IsArenaBacked(
          &message, message.data.data(), message.data.size() * sizeof(message.data[0]));
      });
  }
  if (passed) {
    passed = RunLoanCase<std_msgs::msg::Int32MultiArray>(
      node, "nested_dynamic", "/mdds_dynamic_loan_nested",
      [](std_msgs::msg::Int32MultiArray & message) {
        message.layout.dim.resize(1u);
        message.layout.dim[0].label.assign(2048u, 'n');
        message.layout.dim[0].size = 16u;
        message.layout.dim[0].stride = 16u;
        message.data.assign(1024u, 42);
      },
      [](const std_msgs::msg::Int32MultiArray & message) {
        if (message.layout.dim.size() != 1u) {
          return false;
        }
        const auto & dimension = message.layout.dim[0];
        return message.layout.dim.get_allocator().resource() != nullptr &&
               dimension.label.get_allocator().resource() != nullptr &&
               message.data.get_allocator().resource() != nullptr &&
               IsArenaBacked(
          &message, message.layout.dim.data(),
          message.layout.dim.size() * sizeof(message.layout.dim[0])) &&
               IsArenaBacked(
          &message, dimension.label.data(), dimension.label.size()) &&
               IsArenaBacked(
          &message, message.data.data(), message.data.size() * sizeof(message.data[0]));
      });
  }

  if (node != nullptr) {
    passed = rmw_destroy_node(node) == RMW_RET_OK && passed;
  }
  if (initialized) {
    passed = rmw_shutdown(&context) == RMW_RET_OK && passed;
    passed = rmw_context_fini(&context) == RMW_RET_OK && passed;
  }
  passed = rmw_init_options_fini(&options) == RMW_RET_OK && passed;

  if (!passed) {
    std::cerr << "RESULT|rmw_mdds_dynamic_loan|FAIL" << std::endl;
    return 1;
  }
  std::cout << "RESULT|rmw_mdds_dynamic_loan|PASS|rmw=rmw_mdds_cpp|broker=0|bridge=1|shapes=3" <<
    std::endl;
  return 0;
}
