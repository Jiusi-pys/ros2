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

#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <std_msgs/msg/int32_multi_array.hpp>
#include <std_msgs/msg/string.hpp>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/subscription_content_filter_options.h"
#include "rmw/subscription_options.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"

namespace
{
constexpr std::chrono::milliseconds kPollInterval{2};
constexpr std::chrono::seconds kLoanTimeout{30};

std::string ConsumeRmwError()
{
  const rmw_error_string_t error = rmw_get_error_string();
  const std::string value(error.str);
  rmw_reset_error();
  return value;
}

void ReportFailure(const char * test_case, const char * stage, const std::string & detail = {})
{
  std::cerr << "RESULT|rmw_mdds_broker_dynamic_loan_case|FAIL|case=" << test_case <<
    "|stage=" << stage;
  if (!detail.empty()) {
    std::cerr << "|detail=" << detail;
  }
  std::cerr << std::endl;
}

void ReportPass(const char * test_case)
{
  std::cout << "RESULT|rmw_mdds_broker_dynamic_loan_case|PASS|case=" << test_case <<
    std::endl;
}

bool RequireEnvironment()
{
  const char * implementation = std::getenv("RMW_IMPLEMENTATION");
  const char * broker = std::getenv("RMW_MDDS_BROKER");
  if (implementation == nullptr || std::strcmp(implementation, "rmw_mdds_cpp") != 0 ||
    broker == nullptr || std::strcmp(broker, "1") != 0)
  {
    ReportFailure("environment", "require", "expected_rmw_mdds_cpp_broker_1");
    return false;
  }
  return true;
}

bool SetEnclave(rmw_init_options_t * options, const char * enclave)
{
  if (options == nullptr) {
    return false;
  }
  if (options->enclave != nullptr) {
    options->allocator.deallocate(options->enclave, options->allocator.state);
  }
  options->enclave = rcutils_strdup(enclave, options->allocator);
  return options->enclave != nullptr;
}

class Runtime
{
public:
  bool Start()
  {
    allocator_ = rcutils_get_default_allocator();
    options_ = rmw_get_zero_initialized_init_options();
    context_ = rmw_get_zero_initialized_context();
    if (rmw_init_options_init(&options_, allocator_) != RMW_RET_OK ||
      !SetEnclave(&options_, "/rmw_mdds_broker_dynamic_loan_probe"))
    {
      return false;
    }
    options_initialized_ = true;
    if (rmw_init(&options_, &context_) != RMW_RET_OK) {
      return false;
    }
    context_initialized_ = true;
    node_ = rmw_create_node(&context_, "rmw_mdds_broker_dynamic_loan_probe", "/mdds");
    return node_ != nullptr;
  }

  bool Stop()
  {
    bool passed = true;
    if (node_ != nullptr) {
      passed = rmw_destroy_node(node_) == RMW_RET_OK && passed;
      node_ = nullptr;
    }
    if (context_initialized_) {
      passed = rmw_shutdown(&context_) == RMW_RET_OK && passed;
      passed = rmw_context_fini(&context_) == RMW_RET_OK && passed;
      context_initialized_ = false;
    }
    if (options_initialized_) {
      passed = rmw_init_options_fini(&options_) == RMW_RET_OK && passed;
      options_initialized_ = false;
    }
    return passed;
  }

  ~Runtime()
  {
    (void)Stop();
  }

  rmw_node_t * node() const
  {
    return node_;
  }

  rmw_context_t * context()
  {
    return &context_;
  }

  rcutils_allocator_t allocator() const
  {
    return allocator_;
  }

private:
  rcutils_allocator_t allocator_{};
  rmw_init_options_t options_ = rmw_get_zero_initialized_init_options();
  rmw_context_t context_ = rmw_get_zero_initialized_context();
  rmw_node_t * node_ = nullptr;
  bool options_initialized_ = false;
  bool context_initialized_ = false;
};

std::string MappingPathForAddress(const void * address)
{
  if (address == nullptr) {
    return {};
  }
  FILE * maps = std::fopen("/proc/self/maps", "r");
  if (maps == nullptr) {
    return {};
  }
  const uintptr_t target = reinterpret_cast<uintptr_t>(address);
  char line[1024];
  std::string path;
  while (std::fgets(line, sizeof(line), maps) != nullptr) {
    unsigned long long begin = 0u;
    unsigned long long end = 0u;
    int consumed = 0;
    if (std::sscanf(line, "%llx-%llx %*s %*s %*s %*s %n", &begin, &end, &consumed) != 2) {
      continue;
    }
    if (target < begin || target >= end || consumed <= 0) {
      continue;
    }
    const char * mapping_path = line + consumed;
    while (*mapping_path == ' ' || *mapping_path == '\t') {
      ++mapping_path;
    }
    path = mapping_path;
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
      path.pop_back();
    }
    break;
  }
  std::fclose(maps);
  return path;
}

bool AddressUsesBrokerLoanMapping(const void * address)
{
  return MappingPathForAddress(address).find("rmw_mdds_loan_") != std::string::npos;
}

std::string Topic(const char * suffix)
{
  return std::string("/mdds_broker_dynamic_loan_") + suffix;
}

template<typename MessageT>
const rosidl_message_type_support_t * TypeSupport()
{
  return rosidl_typesupport_cpp::get_message_type_support_handle<MessageT>();
}

rmw_publisher_t * CreatePublisher(
  rmw_node_t * node, const rosidl_message_type_support_t * type_support,
  const std::string & topic, const rmw_qos_profile_t & qos)
{
  rmw_publisher_options_t options = rmw_get_default_publisher_options();
  return rmw_create_publisher(node, type_support, topic.c_str(), &qos, &options);
}

rmw_subscription_t * CreateSubscription(
  rmw_node_t * node, const rosidl_message_type_support_t * type_support,
  const std::string & topic, const rmw_qos_profile_t & qos,
  const rmw_subscription_options_t * requested_options = nullptr)
{
  rmw_subscription_options_t options = requested_options == nullptr ?
    rmw_get_default_subscription_options() : *requested_options;
  return rmw_create_subscription(node, type_support, topic.c_str(), &qos, &options);
}

bool Publish(rmw_publisher_t * publisher, const void * message, const char * test_case)
{
  const rmw_ret_t ret = rmw_publish(publisher, message, nullptr);
  if (ret == RMW_RET_OK) {
    return true;
  }
  ReportFailure(test_case, "publish", ConsumeRmwError());
  return false;
}

bool WaitForLoan(
  rmw_subscription_t * subscription, void ** message, std::chrono::seconds timeout,
  const char * test_case)
{
  if (message == nullptr) {
    return false;
  }
  *message = nullptr;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    bool taken = false;
    const rmw_ret_t ret = rmw_take_loaned_message(subscription, message, &taken, nullptr);
    if (ret != RMW_RET_OK) {
      ReportFailure(test_case, "take", ConsumeRmwError());
      return false;
    }
    if (taken && *message != nullptr) {
      return true;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  ReportFailure(test_case, "take_timeout");
  return false;
}

bool ReturnLoan(
  rmw_subscription_t * subscription, void * message, const char * test_case)
{
  const rmw_ret_t ret = rmw_return_loaned_message_from_subscription(subscription, message);
  if (ret == RMW_RET_OK) {
    return true;
  }
  ReportFailure(test_case, "return", ConsumeRmwError());
  return false;
}

bool DestroyPublisher(rmw_node_t * node, rmw_publisher_t ** publisher)
{
  if (publisher == nullptr || *publisher == nullptr) {
    return true;
  }
  const bool passed = rmw_destroy_publisher(node, *publisher) == RMW_RET_OK;
  *publisher = nullptr;
  return passed;
}

bool DestroySubscription(rmw_node_t * node, rmw_subscription_t ** subscription)
{
  if (subscription == nullptr || *subscription == nullptr) {
    return true;
  }
  const bool passed = rmw_destroy_subscription(node, *subscription) == RMW_RET_OK;
  *subscription = nullptr;
  return passed;
}

std_msgs::msg::String MakeString(const std::string & value)
{
  std_msgs::msg::String message;
  message.data = value;
  return message;
}

std_msgs::msg::Int32MultiArray MakeSequence()
{
  std_msgs::msg::Int32MultiArray message;
  for (int32_t i = 0; i < 1024; ++i) {
    message.data.push_back(i * 3);
  }
  return message;
}

std_msgs::msg::Int32MultiArray MakeNested()
{
  std_msgs::msg::Int32MultiArray message;
  std_msgs::msg::MultiArrayDimension dimension;
  dimension.label.assign(2048u, 'n');
  dimension.size = 16u;
  dimension.stride = 16u;
  message.layout.dim.push_back(dimension);
  message.layout.data_offset = 1u;
  message.data.assign(1024u, 42);
  return message;
}

template<typename StringT>
bool VerifyStringLoan(const std_msgs::msg::String * message, const StringT & expected)
{
  return message != nullptr && message->data == expected &&
         AddressUsesBrokerLoanMapping(message) &&
         AddressUsesBrokerLoanMapping(message->data.data());
}

bool VerifySequenceLoan(
  const std_msgs::msg::Int32MultiArray * message,
  const std_msgs::msg::Int32MultiArray & expected)
{
  return message != nullptr && message->data == expected.data &&
         AddressUsesBrokerLoanMapping(message) &&
         AddressUsesBrokerLoanMapping(message->data.data());
}

bool VerifyNestedLoan(
  const std_msgs::msg::Int32MultiArray * message,
  const std_msgs::msg::Int32MultiArray & expected)
{
  return message != nullptr && message->layout.dim.size() == 1u &&
         message->layout.dim[0].label == expected.layout.dim[0].label &&
         message->data == expected.data && AddressUsesBrokerLoanMapping(message) &&
         AddressUsesBrokerLoanMapping(message->layout.dim.data()) &&
         AddressUsesBrokerLoanMapping(message->layout.dim[0].label.data()) &&
         AddressUsesBrokerLoanMapping(message->data.data());
}

template<typename MessageT, typename Verify>
bool RunMappedLoanCase(
  Runtime * runtime, const char * test_case, const std::string & topic,
  const MessageT & outgoing, Verify verify)
{
  const auto * type_support = TypeSupport<MessageT>();
  rmw_publisher_t * publisher = CreatePublisher(
    runtime->node(), type_support, topic, rmw_qos_profile_default);
  rmw_subscription_t * subscription = CreateSubscription(
    runtime->node(), type_support, topic, rmw_qos_profile_default);
  bool passed = publisher != nullptr && subscription != nullptr &&
    subscription->can_loan_messages;
  if (!passed) {
    ReportFailure(test_case, "create_or_capability", ConsumeRmwError());
  }
  void * loan = nullptr;
  if (passed) {
    passed = Publish(publisher, &outgoing, test_case) &&
      WaitForLoan(subscription, &loan, kLoanTimeout, test_case);
  }
  if (passed) {
    passed = verify(static_cast<const MessageT *>(loan), outgoing);
    if (!passed) {
      ReportFailure(test_case, "mapping_or_value");
    }
  }
  if (loan != nullptr) {
    passed = ReturnLoan(subscription, loan, test_case) && passed;
  }
  passed = DestroySubscription(runtime->node(), &subscription) && passed;
  passed = DestroyPublisher(runtime->node(), &publisher) && passed;
  if (passed) {
    ReportPass(test_case);
  }
  return passed;
}

bool RunFilterCase(Runtime * runtime)
{
  constexpr const char * kCase = "filter";
  const auto * type_support = TypeSupport<std_msgs::msg::String>();
  const std::string topic = Topic("filter");
  rmw_publisher_t * publisher = CreatePublisher(
    runtime->node(), type_support, topic, rmw_qos_profile_default);

  const char * parameters[] = {"keep%"};
  rmw_subscription_content_filter_options_t filter =
    rmw_get_zero_initialized_content_filter_options();
  rcutils_allocator_t allocator = runtime->allocator();
  bool filter_initialized = rmw_subscription_content_filter_options_init(
    "data LIKE %0", 1u, parameters, &allocator, &filter) == RMW_RET_OK;
  rmw_subscription_options_t options = rmw_get_default_subscription_options();
  options.content_filter_options = filter_initialized ? &filter : nullptr;
  rmw_subscription_t * subscription = filter_initialized ? CreateSubscription(
    runtime->node(), type_support, topic, rmw_qos_profile_default, &options) : nullptr;
  bool passed = publisher != nullptr && subscription != nullptr &&
    subscription->can_loan_messages;
  if (!passed) {
    ReportFailure(kCase, "create_or_capability", ConsumeRmwError());
  }

  if (passed) {
    const auto rejected = MakeString("drop-filtered-sample");
    passed = Publish(publisher, &rejected, kCase);
  }
  rmw_wait_set_t * wait_set = passed ? rmw_create_wait_set(runtime->context(), 1u) : nullptr;
  if (passed && wait_set == nullptr) {
    ReportFailure(kCase, "create_wait_set", ConsumeRmwError());
    passed = false;
  }
  if (passed) {
    void * handle = subscription->data;
    rmw_subscriptions_t subscriptions{1u, &handle};
    rmw_time_t timeout{0u, 100000000u};
    const rmw_ret_t ret = rmw_wait(
      &subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set, &timeout);
    passed = ret == RMW_RET_TIMEOUT && subscriptions.subscribers[0] == nullptr;
    if (!passed) {
      ReportFailure(kCase, "rejected_wait_visibility", ConsumeRmwError());
    }
  }

  const std::string accepted_value = "keep-filtered-arena-sample";
  void * loan = nullptr;
  if (passed) {
    const auto accepted = MakeString(accepted_value);
    passed = Publish(publisher, &accepted, kCase) &&
      WaitForLoan(subscription, &loan, kLoanTimeout, kCase);
  }
  if (passed) {
    passed = VerifyStringLoan(
      static_cast<const std_msgs::msg::String *>(loan), accepted_value);
    if (!passed) {
      ReportFailure(kCase, "accepted_mapping_or_value");
    }
  }
  if (loan != nullptr) {
    passed = ReturnLoan(subscription, loan, kCase) && passed;
  }
  if (wait_set != nullptr) {
    passed = rmw_destroy_wait_set(wait_set) == RMW_RET_OK && passed;
  }
  passed = DestroySubscription(runtime->node(), &subscription) && passed;
  passed = DestroyPublisher(runtime->node(), &publisher) && passed;
  if (filter_initialized) {
    passed = rmw_subscription_content_filter_options_fini(&filter, &allocator) == RMW_RET_OK &&
      passed;
  }
  if (passed) {
    ReportPass(kCase);
  }
  return passed;
}

bool RunForeignAndDuplicateReturnCase(Runtime * runtime)
{
  constexpr const char * kCase = "foreign_duplicate_return";
  const auto * type_support = TypeSupport<std_msgs::msg::String>();
  const std::string topic = Topic("return_ownership");
  rmw_publisher_t * publisher = CreatePublisher(
    runtime->node(), type_support, topic, rmw_qos_profile_default);
  rmw_subscription_t * first = CreateSubscription(
    runtime->node(), type_support, topic, rmw_qos_profile_default);
  rmw_subscription_t * second = CreateSubscription(
    runtime->node(), type_support, topic, rmw_qos_profile_default);
  bool passed = publisher != nullptr && first != nullptr && second != nullptr &&
    first->can_loan_messages && second->can_loan_messages;
  if (!passed) {
    ReportFailure(kCase, "create_or_capability", ConsumeRmwError());
  }

  void * first_loan = nullptr;
  void * second_loan = nullptr;
  const std::string value = "ownership-check";
  if (passed) {
    const auto outgoing = MakeString(value);
    passed = Publish(publisher, &outgoing, kCase) &&
      WaitForLoan(first, &first_loan, kLoanTimeout, kCase) &&
      WaitForLoan(second, &second_loan, kLoanTimeout, kCase);
  }
  if (passed) {
    const rmw_ret_t foreign_ret =
      rmw_return_loaned_message_from_subscription(second, first_loan);
    passed = foreign_ret == RMW_RET_ERROR;
    (void)ConsumeRmwError();
    if (!passed) {
      ReportFailure(kCase, "foreign_return_not_rejected");
    }
  }
  bool first_returned = false;
  if (first_loan != nullptr) {
    first_returned = ReturnLoan(first, first_loan, kCase);
    passed = first_returned && passed;
  }
  if (first_returned) {
    const rmw_ret_t duplicate_ret =
      rmw_return_loaned_message_from_subscription(first, first_loan);
    passed = duplicate_ret == RMW_RET_ERROR && passed;
    (void)ConsumeRmwError();
    if (duplicate_ret != RMW_RET_ERROR) {
      ReportFailure(kCase, "duplicate_return_not_rejected");
    }
  }
  if (second_loan != nullptr) {
    passed = ReturnLoan(second, second_loan, kCase) && passed;
  }
  passed = DestroySubscription(runtime->node(), &second) && passed;
  passed = DestroySubscription(runtime->node(), &first) && passed;
  passed = DestroyPublisher(runtime->node(), &publisher) && passed;
  if (passed) {
    ReportPass(kCase);
  }
  return passed;
}

bool RunSlotPressureCase(Runtime * runtime)
{
  constexpr const char * kCase = "two_slot_pressure";
  const auto * type_support = TypeSupport<std_msgs::msg::String>();
  const std::string topic = Topic("pressure");
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  qos.depth = 3u;
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  rmw_publisher_t * publisher = CreatePublisher(runtime->node(), type_support, topic, qos);
  rmw_subscription_t * subscription = CreateSubscription(
    runtime->node(), type_support, topic, qos);
  bool passed = publisher != nullptr && subscription != nullptr &&
    subscription->can_loan_messages;
  if (!passed) {
    ReportFailure(kCase, "create_or_capability", ConsumeRmwError());
  }

  if (passed) {
    for (int i = 1; i <= 3 && passed; ++i) {
      const auto outgoing = MakeString("pressure-" + std::to_string(i));
      passed = Publish(publisher, &outgoing, kCase);
    }
  }
  void * first = nullptr;
  void * second = nullptr;
  void * third = nullptr;
  if (passed) {
    passed = WaitForLoan(subscription, &first, kLoanTimeout, kCase) &&
      WaitForLoan(subscription, &second, kLoanTimeout, kCase);
  }
  if (passed) {
    bool taken = false;
    const rmw_ret_t ret = rmw_take_loaned_message(subscription, &third, &taken, nullptr);
    passed = ret == RMW_RET_OK && !taken && third == nullptr;
    if (!passed) {
      ReportFailure(kCase, "third_visible_while_two_slots_pinned", ConsumeRmwError());
    }
  }
  if (first != nullptr) {
    passed = ReturnLoan(subscription, first, kCase) && passed;
    first = nullptr;
  }
  if (passed) {
    passed = WaitForLoan(subscription, &third, kLoanTimeout, kCase);
  }
  if (passed) {
    const auto * message = static_cast<const std_msgs::msg::String *>(third);
    passed = VerifyStringLoan(message, "pressure-3");
    if (!passed) {
      ReportFailure(kCase, "pending_delivery_mapping_or_value");
    }
  }
  if (second != nullptr) {
    passed = ReturnLoan(subscription, second, kCase) && passed;
  }
  if (third != nullptr) {
    passed = ReturnLoan(subscription, third, kCase) && passed;
  }
  passed = DestroySubscription(runtime->node(), &subscription) && passed;
  passed = DestroyPublisher(runtime->node(), &publisher) && passed;
  if (passed) {
    ReportPass(kCase);
  }
  return passed;
}

bool RunTeardownCleanupCase(Runtime * runtime)
{
  constexpr const char * kCase = "teardown_cleanup";
  const auto * type_support = TypeSupport<std_msgs::msg::String>();
  const std::string topic = Topic("cleanup");
  rmw_publisher_t * publisher = CreatePublisher(
    runtime->node(), type_support, topic, rmw_qos_profile_default);
  rmw_subscription_t * subscription = CreateSubscription(
    runtime->node(), type_support, topic, rmw_qos_profile_default);
  bool passed = publisher != nullptr && subscription != nullptr &&
    subscription->can_loan_messages;
  if (!passed) {
    ReportFailure(kCase, "create_or_capability", ConsumeRmwError());
  }
  if (passed) {
    for (int i = 0; i < 3 && passed; ++i) {
      const auto outgoing = MakeString("cleanup-" + std::to_string(i));
      passed = Publish(publisher, &outgoing, kCase);
    }
  }
  void * active_loan = nullptr;
  if (passed) {
    passed = WaitForLoan(subscription, &active_loan, kLoanTimeout, kCase);
  }
  const std::string pool_path = passed ? MappingPathForAddress(active_loan) : std::string();
  if (passed) {
    passed = pool_path.find("rmw_mdds_loan_") != std::string::npos &&
      access(pool_path.c_str(), F_OK) == 0;
    if (!passed) {
      ReportFailure(kCase, "pool_path_before_teardown", pool_path);
    }
  }
  passed = DestroySubscription(runtime->node(), &subscription) && passed;
  if (!pool_path.empty()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (access(pool_path.c_str(), F_OK) == 0 &&
      std::chrono::steady_clock::now() < deadline)
    {
      std::this_thread::sleep_for(kPollInterval);
    }
    if (access(pool_path.c_str(), F_OK) == 0) {
      ReportFailure(kCase, "pool_not_unlinked", pool_path);
      passed = false;
    }
  }
  passed = DestroyPublisher(runtime->node(), &publisher) && passed;
  if (passed) {
    ReportPass(kCase);
  }
  return passed;
}

bool RunSelfTest(Runtime * runtime)
{
  bool passed = true;
  const std::string string_value = "mapped-string-" + std::string(4096u, 's');
  passed = RunMappedLoanCase(
    runtime, "string", Topic("string"), MakeString(string_value),
    [](const std_msgs::msg::String * message, const std_msgs::msg::String & expected) {
      return VerifyStringLoan(message, expected.data);
    }) && passed;
  passed = RunMappedLoanCase(
    runtime, "sequence", Topic("sequence"), MakeSequence(), VerifySequenceLoan) && passed;
  passed = RunMappedLoanCase(
    runtime, "nested", Topic("nested"), MakeNested(), VerifyNestedLoan) && passed;
  passed = RunFilterCase(runtime) && passed;
  passed = RunForeignAndDuplicateReturnCase(runtime) && passed;
  passed = RunSlotPressureCase(runtime) && passed;
  passed = RunTeardownCleanupCase(runtime) && passed;
  return passed;
}

bool WaitForRemoteSubscribers(Runtime * runtime)
{
  const std::vector<std::string> topics = {
    Topic("remote_string"), Topic("remote_sequence"), Topic("remote_nested")};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < deadline) {
    bool all_matched = true;
    for (const auto & topic : topics) {
      size_t count = 0u;
      if (rmw_count_subscribers(runtime->node(), topic.c_str(), &count) != RMW_RET_OK || count == 0u) {
        all_matched = false;
        rmw_reset_error();
        break;
      }
    }
    if (all_matched) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  return false;
}

bool RunRemotePublisher(Runtime * runtime)
{
  const auto * string_type = TypeSupport<std_msgs::msg::String>();
  const auto * array_type = TypeSupport<std_msgs::msg::Int32MultiArray>();
  rmw_publisher_t * string_publisher = CreatePublisher(
    runtime->node(), string_type, Topic("remote_string"), rmw_qos_profile_default);
  rmw_publisher_t * sequence_publisher = CreatePublisher(
    runtime->node(), array_type, Topic("remote_sequence"), rmw_qos_profile_default);
  rmw_publisher_t * nested_publisher = CreatePublisher(
    runtime->node(), array_type, Topic("remote_nested"), rmw_qos_profile_default);
  bool passed = string_publisher != nullptr && sequence_publisher != nullptr &&
    nested_publisher != nullptr;
  const bool matched = passed && WaitForRemoteSubscribers(runtime);
  const auto string_message = MakeString("remote-mapped-string-" + std::string(4096u, 'r'));
  const auto sequence_message = MakeSequence();
  const auto nested_message = MakeNested();
  for (size_t round = 0u; round < 60u && passed; ++round) {
    passed = Publish(string_publisher, &string_message, "remote_publish") &&
      Publish(sequence_publisher, &sequence_message, "remote_publish") &&
      Publish(nested_publisher, &nested_message, "remote_publish");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  passed = DestroyPublisher(runtime->node(), &nested_publisher) && passed;
  passed = DestroyPublisher(runtime->node(), &sequence_publisher) && passed;
  passed = DestroyPublisher(runtime->node(), &string_publisher) && passed;
  if (passed) {
    std::cout << "RESULT|rmw_mdds_broker_dynamic_loan_remote_publisher|PASS|matched=" <<
      (matched ? 1 : 0) << "|rounds=60" << std::endl;
  }
  return passed;
}

bool RunRemoteSubscriber(Runtime * runtime)
{
  const auto * string_type = TypeSupport<std_msgs::msg::String>();
  const auto * array_type = TypeSupport<std_msgs::msg::Int32MultiArray>();
  rmw_subscription_t * string_subscription = CreateSubscription(
    runtime->node(), string_type, Topic("remote_string"), rmw_qos_profile_default);
  rmw_subscription_t * sequence_subscription = CreateSubscription(
    runtime->node(), array_type, Topic("remote_sequence"), rmw_qos_profile_default);
  rmw_subscription_t * nested_subscription = CreateSubscription(
    runtime->node(), array_type, Topic("remote_nested"), rmw_qos_profile_default);
  bool passed = string_subscription != nullptr && sequence_subscription != nullptr &&
    nested_subscription != nullptr && string_subscription->can_loan_messages &&
    sequence_subscription->can_loan_messages && nested_subscription->can_loan_messages;
  if (!passed) {
    ReportFailure("remote_subscribe", "create_or_capability", ConsumeRmwError());
  }

  void * string_loan = nullptr;
  void * sequence_loan = nullptr;
  void * nested_loan = nullptr;
  if (passed) {
    passed = WaitForLoan(string_subscription, &string_loan, std::chrono::seconds(90), "remote_string");
  }
  if (passed) {
    passed = VerifyStringLoan(
      static_cast<const std_msgs::msg::String *>(string_loan),
      "remote-mapped-string-" + std::string(4096u, 'r'));
    if (passed) {
      ReportPass("remote_string");
    } else {
      ReportFailure("remote_string", "mapping_or_value");
    }
  }
  if (string_loan != nullptr) {
    passed = ReturnLoan(string_subscription, string_loan, "remote_string") && passed;
  }

  const auto expected_sequence = MakeSequence();
  if (passed) {
    passed = WaitForLoan(
      sequence_subscription, &sequence_loan, std::chrono::seconds(90), "remote_sequence");
  }
  if (passed) {
    passed = VerifySequenceLoan(
      static_cast<const std_msgs::msg::Int32MultiArray *>(sequence_loan), expected_sequence);
    if (passed) {
      ReportPass("remote_sequence");
    } else {
      ReportFailure("remote_sequence", "mapping_or_value");
    }
  }
  if (sequence_loan != nullptr) {
    passed = ReturnLoan(sequence_subscription, sequence_loan, "remote_sequence") && passed;
  }

  const auto expected_nested = MakeNested();
  if (passed) {
    passed = WaitForLoan(
      nested_subscription, &nested_loan, std::chrono::seconds(90), "remote_nested");
  }
  if (passed) {
    passed = VerifyNestedLoan(
      static_cast<const std_msgs::msg::Int32MultiArray *>(nested_loan), expected_nested);
    if (passed) {
      ReportPass("remote_nested");
    } else {
      ReportFailure("remote_nested", "mapping_or_value");
    }
  }
  if (nested_loan != nullptr) {
    passed = ReturnLoan(nested_subscription, nested_loan, "remote_nested") && passed;
  }

  passed = DestroySubscription(runtime->node(), &nested_subscription) && passed;
  passed = DestroySubscription(runtime->node(), &sequence_subscription) && passed;
  passed = DestroySubscription(runtime->node(), &string_subscription) && passed;
  if (passed) {
    std::cout << "RESULT|rmw_mdds_broker_dynamic_loan_remote_subscriber|PASS|shapes=3" <<
      std::endl;
  }
  return passed;
}
}  // namespace

int main(int argc, char ** argv)
{
  if (!RequireEnvironment()) {
    return 2;
  }
  const char * identifier = rmw_get_implementation_identifier();
  if (identifier == nullptr || std::strcmp(identifier, "rmw_mdds_cpp") != 0) {
    ReportFailure("environment", "implementation_identifier",
      identifier == nullptr ? "null" : identifier);
    return 3;
  }

  Runtime runtime;
  if (!runtime.Start()) {
    ReportFailure("runtime", "start", ConsumeRmwError());
    return 4;
  }
  const std::string mode = argc > 1 ? argv[1] : "--self-test";
  bool passed = false;
  if (mode == "--self-test") {
    passed = RunSelfTest(&runtime);
  } else if (mode == "--remote-publisher") {
    passed = RunRemotePublisher(&runtime);
  } else if (mode == "--remote-subscriber") {
    passed = RunRemoteSubscriber(&runtime);
  } else {
    ReportFailure("arguments", "mode", mode);
  }
  passed = runtime.Stop() && passed;
  if (!passed) {
    std::cerr << "RESULT|rmw_mdds_broker_dynamic_loan|FAIL|mode=" << mode << std::endl;
    return 1;
  }
  std::cout << "RESULT|rmw_mdds_broker_dynamic_loan|PASS|mode=" << mode <<
    "|rmw=rmw_mdds_cpp|broker=1" << std::endl;
  return 0;
}
