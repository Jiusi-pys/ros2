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
#include <std_msgs/msg/int32.h>
#include <std_msgs/msg/int32_multi_array.h>
#include <std_msgs/msg/multi_array_dimension.h>
#include <std_msgs/msg/string.h>

#include <example_interfaces/srv/add_two_ints.hpp>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/int32_multi_array.hpp>
#include <std_msgs/msg/string.hpp>
#include <test_msgs/msg/bounded_plain_sequences.hpp>
#include <thread>
#include <type_description_interfaces/msg/field_type.hpp>
#include <vector>

#include "fastcdr/Cdr.h"
#include "fastcdr/FastBuffer.h"
#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/dynamic_message_type_support.h"
#include "rmw/get_network_flow_endpoints.h"
#include "rmw/message_sequence.h"
#include "rmw/network_flow_endpoint_array.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/security_options.h"
#include "rmw/serialized_message.h"
#include "rmw/subscription_content_filter_options.h"
#include "rmw/subscription_options.h"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_runtime_c/primitives_sequence_functions.h"
#include "rosidl_runtime_c/string_functions.h"
#include "rosidl_dynamic_typesupport/api/dynamic_data.h"
#include "rosidl_dynamic_typesupport/api/dynamic_type.h"
#include "rosidl_dynamic_typesupport/api/serialization_support.h"
#include "rosidl_dynamic_typesupport/types.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "std_msgs/msg/detail/string__rosidl_typesupport_fastrtps_cpp.hpp"

extern "C" void FakeMddsBridgeReset(void);
extern "C" int FakeMddsBridgeProtectedTransportActivateCount(void);
extern "C" int FakeMddsBridgeProtectedTransportAuthenticated(void);
extern "C" int FakeMddsBridgeProtectedTransportEncrypted(void);

namespace
{
class LocalOnlyTransportEnvironment : public testing::Environment
{
public:
  void SetUp() override
  {
    setenv("RMW_MDDS_BROKER", "0", 1);
    setenv("RMW_MDDS_BRIDGE", "0", 1);
    unsetenv("RMW_MDDS_BROKER_SOCKET");
    unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  }
};

testing::Environment * const g_local_only_transport_environment =
  testing::AddGlobalTestEnvironment(new LocalOnlyTransportEnvironment);

void SetEnclave(rmw_init_options_t * options, const char * enclave)
{
  options->allocator.deallocate(options->enclave, options->allocator.state);
  options->enclave = rcutils_strdup(enclave, options->allocator);
  ASSERT_NE(nullptr, options->enclave);
}

void SetSerializedBytes(rmw_serialized_message_t * message, std::initializer_list<uint8_t> bytes)
{
  ASSERT_NE(nullptr, message);
  ASSERT_EQ(RCUTILS_RET_OK, rmw_serialized_message_resize(message, bytes.size()));
  size_t index = 0;
  for (uint8_t byte : bytes) {
    message->buffer[index++] = byte;
  }
  message->buffer_length = bytes.size();
}

std::vector<uint8_t> GetSerializedBytes(const rmw_serialized_message_t & message)
{
  return std::vector<uint8_t>(message.buffer, message.buffer + message.buffer_length);
}

std::vector<uint8_t> SerializeWithFastCdr(const std_msgs::msg::String & msg)
{
  const size_t size = 4u + std_msgs::msg::typesupport_fastrtps_cpp::get_serialized_size(msg, 0);
  std::vector<uint8_t> payload(size);
  eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char *>(payload.data()), payload.size());
  eprosima::fastcdr::Cdr serializer(
    buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::CdrVersion::XCDRv1);
  serializer.set_encoding_flag(eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR);
  serializer.serialize_encapsulation();
  if (!std_msgs::msg::typesupport_fastrtps_cpp::cdr_serialize(msg, serializer)) {
    ADD_FAILURE() << "FastCDR reference serialization failed";
    return {};
  }
  payload.resize(serializer.get_serialized_data_length());
  return payload;
}

test_msgs::msg::BoundedPlainSequences MakeMaxBoundedPlainSequences()
{
  test_msgs::msg::BoundedPlainSequences msg;
  msg.bool_values = {{false, true, false}};
  msg.byte_values = {{0, 1, 255}};
  msg.char_values = {{0, 1, 127}};
  msg.float32_values = {{1.125f, 0.0f, -1.125f}};
  msg.float64_values = {{3.1415, 0.0, -3.1415}};
  msg.int8_values = {{0, 127, -128}};
  msg.uint8_values = {{0, 1, 255}};
  msg.int16_values = {{0, 32767, -32768}};
  msg.uint16_values = {{0, 1, 65535}};
  msg.int32_values = {{0, 2147483647, -2147483647 - 1}};
  msg.uint32_values = {{0, 1, 4294967295u}};
  msg.int64_values = {{0, 9223372036854775807ll, -9223372036854775807ll - 1}};
  msg.uint64_values = {{0, 1, 18446744073709551615ull}};
  msg.basic_types_values.resize(3);
  msg.constants_values.resize(3);
  msg.defaults_values.resize(3);
  return msg;
}

const rosidl_message_type_support_t * UnsupportedMessageTypeSupportHandle(
  const rosidl_message_type_support_t *, const char *)
{
  return nullptr;
}

std::string CreateSros2PolicyContractRoot(const char * label)
{
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    (std::string("rmw_mdds_sros2_policy_contract_") + label + "_" + std::to_string(stamp));
  std::filesystem::create_directories(root);

  // Local XML policy fixture for topic-grant enforcement. Protected transport
  // and signed artifact semantics are covered by the full-parity RED gate below.
  {
    std::ofstream governance(root / "governance.xml");
    governance <<
      "<dds><domain_access_rules><domain_rule>"
      "<domains><id>0</id></domains>"
      "<allow_unauthenticated_participants>true</allow_unauthenticated_participants>"
      "<enable_join_access_control>true</enable_join_access_control>"
      "<discovery_protection_kind>NONE</discovery_protection_kind>"
      "<liveliness_protection_kind>NONE</liveliness_protection_kind>"
      "<rtps_protection_kind>NONE</rtps_protection_kind>"
      "<topic_access_rules><topic_rule>"
      "<topic_expression>rt/mdds_sros2_allowed</topic_expression>"
      "<enable_discovery_protection>false</enable_discovery_protection>"
      "<enable_read_access_control>true</enable_read_access_control>"
      "<enable_write_access_control>true</enable_write_access_control>"
      "<metadata_protection_kind>NONE</metadata_protection_kind>"
      "<data_protection_kind>NONE</data_protection_kind>"
      "</topic_rule></topic_access_rules>"
      "</domain_rule></domain_access_rules></dds>";
  }
  {
    std::ofstream permissions(root / "permissions.xml");
    permissions <<
      "<dds><permissions><grant name=\"rmw_mdds_sros2_authorized\">"
      "<subject_name>CN=rmw_mdds_sros2_authorized</subject_name>"
      "<validity><not_before>2026-01-01T00:00:00</not_before>"
      "<not_after>2036-01-01T00:00:00</not_after></validity>"
      "<allow_rule><domains><id>0</id></domains>"
      "<publish><topics><topic>rt/mdds_sros2_allowed</topic></topics></publish>"
      "<subscribe><topics><topic>rt/mdds_sros2_allowed</topic></topics></subscribe>"
      "</allow_rule><default>DENY</default>"
      "</grant></permissions></dds>";
  }
  return root.string();
}

std::string CreateTamperedSros2PolicyContractRoot()
{
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("rmw_mdds_sros2_tampered_policy_" + std::to_string(stamp));
  std::filesystem::create_directories(root);

  {
    std::ofstream governance(root / "governance.xml");
    governance <<
      "<dds><domain_access_rules><domain_rule>"
      "<domains><id>0</id></domains>"
      "<rtps_protection_kind>ENCRYPT</rtps_protection_kind>"
      "</domain_rule></domain_access_rules></dds>";
  }
  {
    std::ofstream permissions(root / "permissions.xml");
    permissions <<
      "<dds><permissions><grant name=\"tampered_unsigned_grant\">"
      "<subject_name>CN=rmw_mdds_sros2_tampered</subject_name>"
      "<validity><not_before>2026-01-01T00:00:00</not_before>"
      "<not_after>2036-01-01T00:00:00</not_after></validity>"
      "<allow_rule><domains><id>0</id></domains>"
      "<publish><topics><topic>rt/mdds_sros2_forbidden</topic></topics></publish>"
      "<subscribe><topics><topic>rt/mdds_sros2_forbidden</topic></topics></subscribe>"
      "</allow_rule><default>DENY</default>"
      "</grant></permissions></dds>";
  }
  return root.string();
}

std::string ShellQuote(const std::filesystem::path & path)
{
  std::string quoted = "'";
  for (const char ch : path.string()) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += "'";
  return quoted;
}

bool RunOpenSslFixtureCommand(const std::string & command)
{
  const int status = std::system((command + " >/dev/null 2>&1").c_str());
  if (status != 0) {
    ADD_FAILURE() << "OpenSSL fixture command failed: " << command;
    return false;
  }
  return true;
}

bool CreateSelfSignedCertificate(
  const std::filesystem::path & key, const std::filesystem::path & cert,
  const char * common_name)
{
  bool ok = true;
  ok &= RunOpenSslFixtureCommand(
    "openssl genrsa -out " + ShellQuote(key) + " 2048");
  ok &= RunOpenSslFixtureCommand(
    "openssl req -new -x509 -key " + ShellQuote(key) + " -out " +
    ShellQuote(cert) + " -days 3650 -subj '/CN=" + common_name + "'");
  return ok;
}

void CreateDetachedSignatureFixture(const std::filesystem::path & root)
{
  const std::filesystem::path key = root / "permissions_ca.key.pem";
  const std::filesystem::path cert = root / "permissions_ca.cert.pem";
  const std::filesystem::path identity_key = root / "identity.key.pem";
  const std::filesystem::path identity_cert = root / "identity.pem";
  bool ok = true;
  ok &= CreateSelfSignedCertificate(key, cert, "rmw_mdds_test_permissions_ca");
  ok &= CreateSelfSignedCertificate(
    identity_key, identity_cert, "rmw_mdds_sros2_signed_authorized");
  ok &= RunOpenSslFixtureCommand(
    "openssl dgst -sha256 -sign " + ShellQuote(key) + " -out " +
    ShellQuote(root / "governance.xml.sig") + " " + ShellQuote(root / "governance.xml"));
  ok &= RunOpenSslFixtureCommand(
    "openssl dgst -sha256 -sign " + ShellQuote(key) + " -out " +
    ShellQuote(root / "permissions.xml.sig") + " " + ShellQuote(root / "permissions.xml"));
  if (ok) {
    std::filesystem::copy_file(
      identity_cert, root / "identity_ca.cert.pem",
      std::filesystem::copy_options::overwrite_existing);
  }
}

std::string CreateSignedProtectedSros2PolicyContractRoot(const char * label)
{
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    (std::string("rmw_mdds_sros2_signed_protected_") + label + "_" + std::to_string(stamp));
  std::filesystem::create_directories(root);

  {
    std::ofstream governance(root / "governance.xml");
    governance <<
      "<dds><domain_access_rules><domain_rule>"
      "<domains><id>0</id></domains>"
      "<allow_unauthenticated_participants>false</allow_unauthenticated_participants>"
      "<enable_join_access_control>true</enable_join_access_control>"
      "<discovery_protection_kind>SIGN</discovery_protection_kind>"
      "<liveliness_protection_kind>SIGN</liveliness_protection_kind>"
      "<rtps_protection_kind>ENCRYPT</rtps_protection_kind>"
      "<topic_access_rules><topic_rule>"
      "<topic_expression>rt/mdds_sros2_allowed</topic_expression>"
      "<enable_discovery_protection>true</enable_discovery_protection>"
      "<enable_read_access_control>true</enable_read_access_control>"
      "<enable_write_access_control>true</enable_write_access_control>"
      "<metadata_protection_kind>SIGN</metadata_protection_kind>"
      "<data_protection_kind>ENCRYPT</data_protection_kind>"
      "</topic_rule></topic_access_rules>"
      "</domain_rule></domain_access_rules></dds>";
  }
  {
    std::ofstream permissions(root / "permissions.xml");
    permissions <<
      "<dds><permissions><grant name=\"rmw_mdds_sros2_signed_authorized\">"
      "<subject_name>CN=rmw_mdds_sros2_signed_authorized</subject_name>"
      "<validity><not_before>2026-01-01T00:00:00</not_before>"
      "<not_after>2036-01-01T00:00:00</not_after></validity>"
      "<allow_rule><domains><id>0</id></domains>"
      "<publish><topics><topic>rt/mdds_sros2_allowed</topic></topics></publish>"
      "<subscribe><topics><topic>rt/mdds_sros2_allowed</topic></topics></subscribe>"
      "</allow_rule><default>DENY</default>"
      "</grant></permissions></dds>";
  }
  CreateDetachedSignatureFixture(root);
  return root.string();
}

void SetSecurityRoot(rmw_init_options_t * options, const std::string & security_root)
{
  ASSERT_NE(nullptr, options);
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_security_options_set_root_path(
      security_root.c_str(), &options->allocator, &options->security_options));
}
}  // namespace

TEST(RmwMddsPubSub, InitializesPublisherAndSubscriptionAllocations)
{
  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();

  rmw_publisher_allocation_t publisher_allocation{};
  ASSERT_EQ(
    RMW_RET_OK, rmw_init_publisher_allocation(type_support, nullptr, &publisher_allocation));
  EXPECT_STREQ("rmw_mdds_cpp", publisher_allocation.implementation_identifier);
  EXPECT_EQ(nullptr, publisher_allocation.data);
  ASSERT_EQ(RMW_RET_OK, rmw_fini_publisher_allocation(&publisher_allocation));
  EXPECT_EQ(nullptr, publisher_allocation.implementation_identifier);
  EXPECT_EQ(nullptr, publisher_allocation.data);

  rmw_subscription_allocation_t subscription_allocation{};
  ASSERT_EQ(
    RMW_RET_OK, rmw_init_subscription_allocation(type_support, nullptr, &subscription_allocation));
  EXPECT_STREQ("rmw_mdds_cpp", subscription_allocation.implementation_identifier);
  EXPECT_EQ(nullptr, subscription_allocation.data);
  ASSERT_EQ(RMW_RET_OK, rmw_fini_subscription_allocation(&subscription_allocation));
  EXPECT_EQ(nullptr, subscription_allocation.implementation_identifier);
  EXPECT_EQ(nullptr, subscription_allocation.data);
}

TEST(RmwMddsPubSub, AllocationInitRejectsUnsupportedTypeSupport)
{
  rosidl_message_type_support_t unsupported_type_support{};
  unsupported_type_support.typesupport_identifier = "rmw_mdds_invalid_type_support";
  unsupported_type_support.func = UnsupportedMessageTypeSupportHandle;

  rmw_publisher_allocation_t publisher_allocation{};
  EXPECT_EQ(
    RMW_RET_UNSUPPORTED,
    rmw_init_publisher_allocation(&unsupported_type_support, nullptr, &publisher_allocation));
  EXPECT_EQ(nullptr, publisher_allocation.implementation_identifier);
  EXPECT_EQ(nullptr, publisher_allocation.data);
  rmw_reset_error();

  rmw_subscription_allocation_t subscription_allocation{};
  EXPECT_EQ(
    RMW_RET_UNSUPPORTED,
    rmw_init_subscription_allocation(
      &unsupported_type_support, nullptr, &subscription_allocation));
  EXPECT_EQ(nullptr, subscription_allocation.implementation_identifier);
  EXPECT_EQ(nullptr, subscription_allocation.data);
  rmw_reset_error();
}

TEST(RmwMddsPubSub, SecurityEnforceInitFailsClosedWithoutPolicyEnforcement)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_security_enforce_test");
  options.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_ENFORCE;

  rmw_context_t context = rmw_get_zero_initialized_context();
  EXPECT_EQ(RMW_RET_UNSUPPORTED, rmw_init(&options, &context));
  EXPECT_EQ(nullptr, context.implementation_identifier);
  EXPECT_EQ(nullptr, context.impl);

  const std::string error = rmw_get_error_string().str;
  EXPECT_NE(std::string::npos, error.find("security"));
  EXPECT_NE(std::string::npos, error.find("not supported"));
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, DISABLED_Sros2PolicyAllowsAuthorizedPublisherAndSubscription)
{
  const std::string security_root = CreateSros2PolicyContractRoot("authorized");
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_sros2_authorized");
  SetSecurityRoot(&options, security_root);
  options.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_ENFORCE;

  rmw_context_t context = rmw_get_zero_initialized_context();
  const rmw_ret_t init_ret = rmw_init(&options, &context);
  EXPECT_EQ(RMW_RET_OK, init_ret) << rmw_get_error_string().str;
  if (init_ret != RMW_RET_OK) {
    rmw_reset_error();
    EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
    std::filesystem::remove_all(security_root);
    return;
  }

  rmw_node_t * node = rmw_create_node(&context, "mdds_sros2_authorized_node", "/mdds");
  ASSERT_NE(nullptr, node);
  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_sros2_allowed", &rmw_qos_profile_default, &publisher_options);
  EXPECT_NE(nullptr, publisher) << rmw_get_error_string().str;
  rmw_reset_error();
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_sros2_allowed", &rmw_qos_profile_default, &subscription_options);
  EXPECT_NE(nullptr, subscription) << rmw_get_error_string().str;
  rmw_reset_error();

  if (subscription != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  }
  if (publisher != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  }
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  std::filesystem::remove_all(security_root);
}

TEST(RmwMddsPubSub, DISABLED_Sros2PolicyRejectsUnauthorizedPublisher)
{
  const std::string security_root = CreateSros2PolicyContractRoot("unauthorized");
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_sros2_authorized");
  SetSecurityRoot(&options, security_root);
  options.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_ENFORCE;

  rmw_context_t context = rmw_get_zero_initialized_context();
  const rmw_ret_t init_ret = rmw_init(&options, &context);
  EXPECT_EQ(RMW_RET_OK, init_ret) << rmw_get_error_string().str;
  if (init_ret != RMW_RET_OK) {
    rmw_reset_error();
    EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
    std::filesystem::remove_all(security_root);
    return;
  }

  rmw_node_t * node = rmw_create_node(&context, "mdds_sros2_unauthorized_node", "/mdds");
  ASSERT_NE(nullptr, node);
  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_sros2_forbidden", &rmw_qos_profile_default, &publisher_options);
  EXPECT_EQ(nullptr, publisher);
  if (publisher != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  }
  const std::string error = rmw_get_error_string().str;
  EXPECT_NE(std::string::npos, error.find("security"));
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  std::filesystem::remove_all(security_root);
}

TEST(RmwMddsPubSub, DISABLED_FullParitySros2RejectsTamperedUnsignedPermissions)
{
  const std::string security_root = CreateTamperedSros2PolicyContractRoot();
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_sros2_tampered");
  SetSecurityRoot(&options, security_root);
  options.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_ENFORCE;

  rmw_context_t context = rmw_get_zero_initialized_context();
  const rmw_ret_t init_ret = rmw_init(&options, &context);
  EXPECT_NE(RMW_RET_OK, init_ret)
    << "full SROS2 parity must reject unsigned or tampered permissions/governance artifacts";
  if (init_ret == RMW_RET_OK) {
    EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
    EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  } else {
    rmw_reset_error();
  }

  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  std::filesystem::remove_all(security_root);
}

TEST(RmwMddsPubSub, DISABLED_FullParitySros2AcceptsSignedProtectedPolicyWithAuthenticatedTransport)
{
  const std::string security_root = CreateSignedProtectedSros2PolicyContractRoot("authorized");
  ASSERT_EQ(0, setenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED", "1", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_sros2_signed_authorized");
  SetSecurityRoot(&options, security_root);
  options.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_ENFORCE;

  rmw_context_t context = rmw_get_zero_initialized_context();
  const rmw_ret_t init_ret = rmw_init(&options, &context);
  EXPECT_EQ(RMW_RET_OK, init_ret)
    << "signed protected policy must load when authenticated/encrypted transport is available: "
    << rmw_get_error_string().str;
  if (init_ret != RMW_RET_OK) {
    rmw_reset_error();
    EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
    unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED");
    unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED");
    std::filesystem::remove_all(security_root);
    return;
  }

  rmw_node_t * node =
    rmw_create_node(&context, "mdds_sros2_signed_authorized_node", "/mdds");
  ASSERT_NE(nullptr, node);
  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_sros2_allowed", &rmw_qos_profile_default, &publisher_options);
  EXPECT_NE(nullptr, publisher) << rmw_get_error_string().str;
  rmw_reset_error();
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_sros2_allowed", &rmw_qos_profile_default, &subscription_options);
  EXPECT_NE(nullptr, subscription) << rmw_get_error_string().str;
  rmw_reset_error();

  if (subscription != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  }
  if (publisher != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  }
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED");
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED");
  std::filesystem::remove_all(security_root);
}

TEST(RmwMddsPubSub, DISABLED_FullParitySros2RejectsTamperedSignedPermissions)
{
  const std::string security_root = CreateSignedProtectedSros2PolicyContractRoot("tampered");
  {
    std::ofstream permissions(
      std::filesystem::path(security_root) / "permissions.xml", std::ios::app);
    permissions << "<!-- tampered after signing -->";
  }
  ASSERT_EQ(0, setenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED", "1", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_sros2_signed_tampered");
  SetSecurityRoot(&options, security_root);
  options.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_ENFORCE;

  rmw_context_t context = rmw_get_zero_initialized_context();
  const rmw_ret_t init_ret = rmw_init(&options, &context);
  EXPECT_NE(RMW_RET_OK, init_ret)
    << "tampered signed permissions must be rejected before endpoint creation";
  const std::string error = rmw_get_error_string().str;
  EXPECT_NE(std::string::npos, error.find("signature"))
    << "tampered signed policy rejection must identify signature validation";
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED");
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED");
  std::filesystem::remove_all(security_root);
}

TEST(RmwMddsPubSub, DISABLED_FullParitySros2RejectsSignedIdentityMismatch)
{
  const std::string security_root = CreateSignedProtectedSros2PolicyContractRoot("identity_mismatch");
  const std::filesystem::path root_path(security_root);
  ASSERT_TRUE(CreateSelfSignedCertificate(
    root_path / "identity.key.pem", root_path / "identity.pem",
    "rmw_mdds_sros2_wrong_identity"));
  std::filesystem::copy_file(
    root_path / "identity.pem", root_path / "identity_ca.cert.pem",
    std::filesystem::copy_options::overwrite_existing);
  ASSERT_EQ(0, setenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED", "1", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_sros2_signed_identity_mismatch");
  SetSecurityRoot(&options, security_root);
  options.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_ENFORCE;

  rmw_context_t context = rmw_get_zero_initialized_context();
  const rmw_ret_t init_ret = rmw_init(&options, &context);
  EXPECT_NE(RMW_RET_OK, init_ret)
    << "signed permissions must be rejected when identity material does not match the grant";
  const std::string error = rmw_get_error_string().str;
  EXPECT_NE(std::string::npos, error.find("identity"))
    << "identity mismatch rejection must identify the mismatched identity material";
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED");
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED");
  std::filesystem::remove_all(security_root);
}

TEST(RmwMddsPubSub, DISABLED_FullParitySros2RejectsSignedPolicyWithoutAuthenticatedTransport)
{
  const std::string security_root = CreateSignedProtectedSros2PolicyContractRoot("no_transport");
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED");
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED");

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_sros2_signed_no_transport");
  SetSecurityRoot(&options, security_root);
  options.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_ENFORCE;

  rmw_context_t context = rmw_get_zero_initialized_context();
  const rmw_ret_t init_ret = rmw_init(&options, &context);
  EXPECT_NE(RMW_RET_OK, init_ret)
    << "signed protected policy must fail closed when authenticated/encrypted transport is absent";
  const std::string error = rmw_get_error_string().str;
  EXPECT_NE(std::string::npos, error.find("authenticated"))
    << "protected policy rejection must identify missing authenticated transport";
  EXPECT_NE(std::string::npos, error.find("transport"))
    << "protected policy rejection must identify missing authenticated transport";
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  std::filesystem::remove_all(security_root);
}

TEST(RmwMddsPubSub, DISABLED_FullParitySros2ActivatesBridgeProtectedTransport)
{
  const std::string security_root = CreateSignedProtectedSros2PolicyContractRoot("bridge_transport");
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "0", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE", "1", 1));
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED");
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED");
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  FakeMddsBridgeReset();

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_sros2_signed_bridge_transport");
  SetSecurityRoot(&options, security_root);
  options.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_ENFORCE;

  rmw_context_t context = rmw_get_zero_initialized_context();
  const rmw_ret_t init_ret = rmw_init(&options, &context);
  EXPECT_EQ(RMW_RET_OK, init_ret)
    << "signed protected policy must activate authenticated/encrypted bridge transport: "
    << rmw_get_error_string().str;
  if (init_ret == RMW_RET_OK) {
    EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
    EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  } else {
    rmw_reset_error();
  }

  EXPECT_EQ(1, FakeMddsBridgeProtectedTransportActivateCount())
    << "protected policy should activate the MDDS bridge protected transport lane";
  EXPECT_EQ(1, FakeMddsBridgeProtectedTransportAuthenticated());
  EXPECT_EQ(1, FakeMddsBridgeProtectedTransportEncrypted());

  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  FakeMddsBridgeReset();
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE", "0", 1));
  std::filesystem::remove_all(security_root);
}

TEST(RmwMddsPubSub, BrokerModeProtectedPolicyPreparesProtectedBroker) {
  const std::string security_root =
      CreateSignedProtectedSros2PolicyContractRoot("broker_transport");
  unsetenv("RMW_MDDS_BRIDGE");
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE_LIBRARY", FAKE_MDDS_BRIDGE_PATH, 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET",
                      "/tmp/rmw_mdds_security_base.sock", 1));
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED");
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED");
  FakeMddsBridgeReset();

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_sros2_broker_transport");
  SetSecurityRoot(&options, security_root);
  options.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_ENFORCE;

  rmw_context_t context = rmw_get_zero_initialized_context();
  const rmw_ret_t init_ret = rmw_init(&options, &context);
  EXPECT_EQ(RMW_RET_OK, init_ret) << rmw_get_error_string().str;
  if (init_ret == RMW_RET_OK) {
    const char *authenticated =
        std::getenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED");
    const char *encrypted =
        std::getenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED");
    const char *broker_socket = std::getenv("RMW_MDDS_BROKER_SOCKET");
    EXPECT_STREQ("1", authenticated);
    EXPECT_STREQ("1", encrypted);
    EXPECT_STREQ("/tmp/rmw_mdds_security_base.sock.protected", broker_socket);
    EXPECT_EQ(0, FakeMddsBridgeProtectedTransportActivateCount())
      << "broker mode must not initialize a second MDDS bridge in the client process";
    EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
    EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  } else {
    rmw_reset_error();
  }

  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_ENCRYPTED");
  unsetenv("RMW_MDDS_PROTECTED_TRANSPORT_AUTHENTICATED");
  unsetenv("RMW_MDDS_BROKER_SOCKET");
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "0", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BRIDGE", "0", 1));
  FakeMddsBridgeReset();
  std::filesystem::remove_all(security_root);
}

TEST(RmwMddsPubSub, CreatePublisherRejectsUnsupportedTypeSupport) {
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_invalid_publisher_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_invalid_publisher_node", "/mdds");
  ASSERT_NE(nullptr, node);

  rosidl_message_type_support_t unsupported_type_support{};
  unsupported_type_support.typesupport_identifier =
      "rmw_mdds_invalid_type_support";
  unsupported_type_support.func = UnsupportedMessageTypeSupportHandle;
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();

  rmw_publisher_t *publisher = rmw_create_publisher(
      node, &unsupported_type_support, "/mdds_test_invalid_type_support",
      &rmw_qos_profile_default, &publisher_options);
  EXPECT_EQ(nullptr, publisher);
  if (publisher != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  }
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, CreatePubSubRejectsInvalidTopicNames)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_invalid_topic_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_invalid_topic_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  const char * invalid_topic_names[] = {"", "relative_topic", "/foo bar"};

  for (const char * topic_name : invalid_topic_names) {
    rmw_publisher_t * publisher = rmw_create_publisher(
      node, type_support, topic_name, &rmw_qos_profile_default, &publisher_options);
    EXPECT_EQ(nullptr, publisher) << topic_name;
    if (publisher != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
    }
    rmw_reset_error();

    rmw_subscription_t * subscription = rmw_create_subscription(
      node, type_support, topic_name, &rmw_qos_profile_default, &subscription_options);
    EXPECT_EQ(nullptr, subscription) << topic_name;
    if (subscription != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
    }
    rmw_reset_error();
  }

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, CreatePubSubAllowsNativeRelativeTopicNames)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_native_topic_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_native_topic_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  rmw_qos_profile_t native_qos = rmw_qos_profile_default;
  native_qos.avoid_ros_namespace_conventions = true;

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "mdds_native_topic", &native_qos, &publisher_options);
  EXPECT_NE(nullptr, publisher);
  if (publisher != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  }
  rmw_reset_error();

  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "mdds_native_topic", &native_qos, &subscription_options);
  EXPECT_NE(nullptr, subscription);
  if (subscription != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  }
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, CreatePubSubRejectsInvalidQosProfiles)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_invalid_qos_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_invalid_qos_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  const rmw_qos_profile_t * invalid_qos_profiles[] = {nullptr, &rmw_qos_profile_unknown};

  for (const rmw_qos_profile_t * qos_profile : invalid_qos_profiles) {
    const char * qos_label = qos_profile == nullptr ? "null" : "unknown";

    rmw_publisher_t * publisher = rmw_create_publisher(
      node, type_support, "/mdds_invalid_qos_topic", qos_profile, &publisher_options);
    EXPECT_EQ(nullptr, publisher) << qos_label;
    if (publisher != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
    }
    rmw_reset_error();

    rmw_subscription_t * subscription = rmw_create_subscription(
      node, type_support, "/mdds_invalid_qos_topic", qos_profile, &subscription_options);
    EXPECT_EQ(nullptr, subscription) << qos_label;
    if (subscription != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
    }
    rmw_reset_error();
  }

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, ActualQosResolvesSystemDefaults)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_system_default_qos_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_system_default_qos_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_system_default_qos", &rmw_qos_profile_system_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_system_default_qos", &rmw_qos_profile_system_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_qos_profile_t publisher_qos = rmw_qos_profile_unknown;
  ASSERT_EQ(RMW_RET_OK, rmw_publisher_get_actual_qos(publisher, &publisher_qos));
  EXPECT_EQ(RMW_QOS_POLICY_HISTORY_KEEP_LAST, publisher_qos.history);
  EXPECT_EQ(10u, publisher_qos.depth);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_RELIABLE, publisher_qos.reliability);
  EXPECT_EQ(RMW_QOS_POLICY_DURABILITY_VOLATILE, publisher_qos.durability);
  EXPECT_EQ(RMW_QOS_POLICY_LIVELINESS_AUTOMATIC, publisher_qos.liveliness);

  rmw_qos_profile_t subscription_qos = rmw_qos_profile_unknown;
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_get_actual_qos(subscription, &subscription_qos));
  EXPECT_EQ(RMW_QOS_POLICY_HISTORY_KEEP_LAST, subscription_qos.history);
  EXPECT_EQ(10u, subscription_qos.depth);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_RELIABLE, subscription_qos.reliability);
  EXPECT_EQ(RMW_QOS_POLICY_DURABILITY_VOLATILE, subscription_qos.durability);
  EXPECT_EQ(RMW_QOS_POLICY_LIVELINESS_AUTOMATIC, subscription_qos.liveliness);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, CountMatchedEndpointsExcludesIncompatibleQos)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_incompatible_qos_count_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_incompatible_qos_count_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  rmw_qos_profile_t offered_qos = rmw_qos_profile_default;
  offered_qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  rmw_qos_profile_t requested_qos = rmw_qos_profile_default;
  requested_qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_incompatible_qos_count", &offered_qos, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_incompatible_qos_count", &requested_qos, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  size_t subscription_count = 1;
  EXPECT_EQ(
    RMW_RET_OK, rmw_publisher_count_matched_subscriptions(publisher, &subscription_count));
  EXPECT_EQ(0u, subscription_count);

  size_t publisher_count = 1;
  EXPECT_EQ(RMW_RET_OK, rmw_subscription_count_matched_publishers(subscription, &publisher_count));
  EXPECT_EQ(0u, publisher_count);

  std_msgs::msg::String msg;
  msg.data = "incompatible";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  std_msgs::msg::String received;
  bool taken = true;
  EXPECT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, InProcessStringRoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_pubsub_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_pubsub_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_string", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_string", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs::msg::String msg;
  msg.data = "hello rmw_mdds";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  void * subscription_handle = subscription->data;
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 1;
  subscriptions.subscribers = &subscription_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set, &timeout));
  EXPECT_NE(nullptr, subscriptions.subscribers[0]);

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ("hello rmw_mdds", received.data);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, IgnoreLocalPublicationsSkipsSameContextTypedSamples)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_ignore_local_typed_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_ignore_local_typed_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t ignored_subscription_options =
    rmw_get_default_subscription_options();
  ignored_subscription_options.ignore_local_publications = true;
  rmw_subscription_options_t normal_subscription_options =
    rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_ignore_local_typed", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * ignored_subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_ignore_local_typed", &rmw_qos_profile_default,
    &ignored_subscription_options);
  ASSERT_NE(nullptr, ignored_subscription);
  rmw_subscription_t * normal_subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_ignore_local_typed", &rmw_qos_profile_default,
    &normal_subscription_options);
  ASSERT_NE(nullptr, normal_subscription);

  std_msgs::msg::String msg;
  msg.data = "local only";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  std_msgs::msg::String ignored_received;
  bool taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(ignored_subscription, &ignored_received, &taken, nullptr));
  EXPECT_FALSE(taken);

  std_msgs::msg::String normal_received;
  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(normal_subscription, &normal_received, &taken, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ(msg.data, normal_received.data);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, normal_subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, ignored_subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, ContentFilterKeepsMatchingStringSamples)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_content_filter_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_content_filter_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_content_filter", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_content_filter", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  const char * parameters[] = {"keep"};
  rmw_subscription_content_filter_options_t filter_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_init(
      "data = %0", 1, parameters, &allocator, &filter_options));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_content_filter(subscription, &filter_options));

  rmw_subscription_content_filter_options_t returned_options =
    rmw_get_zero_initialized_content_filter_options();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_content_filter(subscription, &allocator, &returned_options));
  ASSERT_STREQ("data = %0", returned_options.filter_expression);
  ASSERT_EQ(1u, returned_options.expression_parameters.size);
  ASSERT_NE(nullptr, returned_options.expression_parameters.data);
  ASSERT_STREQ("keep", returned_options.expression_parameters.data[0]);

  std_msgs::msg::String drop;
  drop.data = "drop";
  std_msgs::msg::String keep;
  keep.data = "keep";
  std_msgs::msg::String late_drop;
  late_drop.data = "late_drop";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &drop, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &keep, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &late_drop, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ("keep", received.data);

  std_msgs::msg::String unexpected;
  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &unexpected, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(
    RMW_RET_OK,
    rmw_subscription_content_filter_options_fini(&returned_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_subscription_content_filter_options_fini(&filter_options, &allocator));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, TransientLocalPublisherReplaysRetainedSampleToLateSubscription)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_transient_local_late_subscription_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_transient_local_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  qos.depth = 1;

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_transient_local", &qos, &publisher_options);
  ASSERT_NE(nullptr, publisher);

  std_msgs::msg::String msg;
  msg.data = "retained transient local";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_transient_local", &qos, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs::msg::String received;
  bool taken = false;
  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_with_info(subscription, &received, &taken, &message_info, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ("retained transient local", received.data);
  EXPECT_EQ(1u, message_info.publication_sequence_number);
  EXPECT_EQ(1u, message_info.reception_sequence_number);

  std_msgs::msg::String unexpected;
  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &unexpected, &taken, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, TakeWithInfoReportsPublisherGid)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_take_info_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_take_info_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_take_info", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_take_info", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_gid_t expected_gid{};
  ASSERT_EQ(RMW_RET_OK, rmw_get_gid_for_publisher(publisher, &expected_gid));

  std_msgs::msg::String msg;
  msg.data = "hello info";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  void * subscription_handle = subscription->data;
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 1;
  subscriptions.subscribers = &subscription_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set, &timeout));

  std_msgs::msg::String received;
  bool taken = false;
  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_with_info(subscription, &received, &taken, &message_info, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ("hello info", received.data);
  EXPECT_GT(message_info.source_timestamp, 0);
  EXPECT_GE(message_info.received_timestamp, message_info.source_timestamp);
  bool same_gid = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_compare_gids_equal(&expected_gid, &message_info.publisher_gid, &same_gid));
  EXPECT_TRUE(same_gid);
  EXPECT_FALSE(message_info.from_intra_process);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, TakeWithInfoReportsMessageSequenceNumbers)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_take_sequence_info_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_take_sequence_info_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_sequence_info", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_sequence_info", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs::msg::String first;
  first.data = "first sequence";
  std_msgs::msg::String second;
  second.data = "second sequence";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &first, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &second, nullptr));

  std_msgs::msg::String received;
  bool taken = false;
  rmw_message_info_t first_info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_with_info(subscription, &received, &taken, &first_info, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("first sequence", received.data);
  EXPECT_EQ(1u, first_info.publication_sequence_number);
  EXPECT_EQ(1u, first_info.reception_sequence_number);

  rmw_message_info_t second_info = rmw_get_zero_initialized_message_info();
  received.data.clear();
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_with_info(subscription, &received, &taken, &second_info, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("second sequence", received.data);
  EXPECT_EQ(2u, second_info.publication_sequence_number);
  EXPECT_EQ(2u, second_info.reception_sequence_number);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, NetworkFlowEndpointsReportDirectRtpsUserDataPort)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_network_flow_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_network_flow_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_network_flow", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_network_flow", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_network_flow_endpoint_array_t publisher_endpoints =
    rmw_get_zero_initialized_network_flow_endpoint_array();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_publisher_get_network_flow_endpoints(publisher, &allocator, &publisher_endpoints));
  ASSERT_EQ(1u, publisher_endpoints.size);
  ASSERT_NE(nullptr, publisher_endpoints.network_flow_endpoint);
  ASSERT_NE(nullptr, publisher_endpoints.allocator);
  EXPECT_EQ(
    RMW_TRANSPORT_PROTOCOL_UDP,
    publisher_endpoints.network_flow_endpoint[0].transport_protocol);
  EXPECT_EQ(
    RMW_INTERNET_PROTOCOL_IPV4,
    publisher_endpoints.network_flow_endpoint[0].internet_protocol);
  EXPECT_NE(0u, publisher_endpoints.network_flow_endpoint[0].transport_port);
  EXPECT_STREQ("0.0.0.0", publisher_endpoints.network_flow_endpoint[0].internet_address);

  rmw_network_flow_endpoint_array_t subscription_endpoints =
    rmw_get_zero_initialized_network_flow_endpoint_array();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_subscription_get_network_flow_endpoints(
      subscription, &allocator, &subscription_endpoints));
  ASSERT_EQ(1u, subscription_endpoints.size);
  ASSERT_NE(nullptr, subscription_endpoints.network_flow_endpoint);
  ASSERT_NE(nullptr, subscription_endpoints.allocator);
  EXPECT_EQ(
    RMW_TRANSPORT_PROTOCOL_UDP,
    subscription_endpoints.network_flow_endpoint[0].transport_protocol);
  EXPECT_EQ(
    RMW_INTERNET_PROTOCOL_IPV4,
    subscription_endpoints.network_flow_endpoint[0].internet_protocol);
  EXPECT_EQ(
    publisher_endpoints.network_flow_endpoint[0].transport_port,
    subscription_endpoints.network_flow_endpoint[0].transport_port);
  EXPECT_STREQ("0.0.0.0", subscription_endpoints.network_flow_endpoint[0].internet_address);

  EXPECT_EQ(RMW_RET_OK, rmw_network_flow_endpoint_array_fini(&subscription_endpoints));
  EXPECT_EQ(RMW_RET_OK, rmw_network_flow_endpoint_array_fini(&publisher_endpoints));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, SerializedRoundTripCopiesPayloadAndInfo)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_serialized_pubsub_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_serialized_pubsub_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_serialized", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_serialized", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  rmw_gid_t expected_gid{};
  ASSERT_EQ(RMW_RET_OK, rmw_get_gid_for_publisher(publisher, &expected_gid));

  rmw_serialized_message_t outgoing = rmw_get_zero_initialized_serialized_message();
  rmw_serialized_message_t received = rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RCUTILS_RET_OK, rmw_serialized_message_init(&outgoing, 1, &allocator));
  ASSERT_EQ(RCUTILS_RET_OK, rmw_serialized_message_init(&received, 1, &allocator));

  SetSerializedBytes(&outgoing, {0x11, 0x22, 0x00, 0xff, 0x34});
  ASSERT_EQ(RMW_RET_OK, rmw_publish_serialized_message(publisher, &outgoing, nullptr));
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_serialized_message(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ((std::vector<uint8_t>{0x11, 0x22, 0x00, 0xff, 0x34}), GetSerializedBytes(received));

  SetSerializedBytes(&outgoing, {0xaa, 0xbb, 0xcc});
  ASSERT_EQ(RMW_RET_OK, rmw_publish_serialized_message(publisher, &outgoing, nullptr));
  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  taken = false;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_serialized_message_with_info(subscription, &received, &taken, &message_info, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ((std::vector<uint8_t>{0xaa, 0xbb, 0xcc}), GetSerializedBytes(received));
  bool same_gid = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_compare_gids_equal(&expected_gid, &message_info.publisher_gid, &same_gid));
  EXPECT_TRUE(same_gid);
  EXPECT_FALSE(message_info.from_intra_process);

  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&received));
  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&outgoing));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, IgnoreLocalPublicationsSkipsSameContextSerializedSamples)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_ignore_local_serialized_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_ignore_local_serialized_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t ignored_subscription_options =
    rmw_get_default_subscription_options();
  ignored_subscription_options.ignore_local_publications = true;
  rmw_subscription_options_t normal_subscription_options =
    rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_ignore_local_serialized", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * ignored_subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_ignore_local_serialized", &rmw_qos_profile_default,
    &ignored_subscription_options);
  ASSERT_NE(nullptr, ignored_subscription);
  rmw_subscription_t * normal_subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_ignore_local_serialized", &rmw_qos_profile_default,
    &normal_subscription_options);
  ASSERT_NE(nullptr, normal_subscription);

  rmw_serialized_message_t outgoing = rmw_get_zero_initialized_serialized_message();
  rmw_serialized_message_t ignored_received = rmw_get_zero_initialized_serialized_message();
  rmw_serialized_message_t normal_received = rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RCUTILS_RET_OK, rmw_serialized_message_init(&outgoing, 1, &allocator));
  ASSERT_EQ(RCUTILS_RET_OK, rmw_serialized_message_init(&ignored_received, 1, &allocator));
  ASSERT_EQ(RCUTILS_RET_OK, rmw_serialized_message_init(&normal_received, 1, &allocator));

  SetSerializedBytes(&outgoing, {0x01, 0x23, 0x45, 0x67});
  ASSERT_EQ(RMW_RET_OK, rmw_publish_serialized_message(publisher, &outgoing, nullptr));

  bool taken = true;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_serialized_message(ignored_subscription, &ignored_received, &taken, nullptr));
  EXPECT_FALSE(taken);

  taken = false;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_serialized_message(normal_subscription, &normal_received, &taken, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ(GetSerializedBytes(outgoing), GetSerializedBytes(normal_received));

  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&normal_received));
  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&ignored_received));
  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&outgoing));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, normal_subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, ignored_subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, DynamicSerializationSupportInitFastCdr)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rosidl_dynamic_typesupport_serialization_support_t support =
    rosidl_dynamic_typesupport_get_zero_initialized_serialization_support();

  ASSERT_EQ(RMW_RET_OK, rmw_serialization_support_init("fastcdr", &allocator, &support));
  ASSERT_STREQ("fastcdr", support.serialization_library_identifier);
  ASSERT_NE(nullptr, support.methods.dynamic_data_deserialize);

  EXPECT_EQ(RCUTILS_RET_OK, rosidl_dynamic_typesupport_serialization_support_fini(&support));
}

TEST(RmwMddsPubSub, DynamicSerializationSupportInitAdvertisedCdrFormat)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rosidl_dynamic_typesupport_serialization_support_t support =
    rosidl_dynamic_typesupport_get_zero_initialized_serialization_support();

  ASSERT_STREQ("cdr", rmw_get_serialization_format());
  ASSERT_EQ(
    RMW_RET_OK, rmw_serialization_support_init(rmw_get_serialization_format(), &allocator, &support));
  ASSERT_STREQ("fastcdr", support.serialization_library_identifier);
  ASSERT_NE(nullptr, support.methods.dynamic_data_deserialize);

  EXPECT_EQ(RCUTILS_RET_OK, rosidl_dynamic_typesupport_serialization_support_fini(&support));
}

TEST(RmwMddsPubSub, DynamicTakeNoMessageReturnsNotTaken)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_dynamic_take_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_dynamic_take_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_dynamic_take", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  bool taken = true;
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_take_dynamic_message(subscription, nullptr, &taken, nullptr));

  rosidl_dynamic_typesupport_serialization_support_t support =
    rosidl_dynamic_typesupport_get_zero_initialized_serialization_support();
  ASSERT_EQ(RMW_RET_OK, rmw_serialization_support_init("fastcdr", &allocator, &support));

  rosidl_dynamic_typesupport_dynamic_type_builder_t builder =
    rosidl_dynamic_typesupport_get_zero_initialized_dynamic_type_builder();
  constexpr const char * type_name = "std_msgs::msg::dds_::String_";
  constexpr const char * member_name = "data";
  ASSERT_EQ(
    RCUTILS_RET_OK,
    rosidl_dynamic_typesupport_dynamic_type_builder_init(
      &support, type_name, std::strlen(type_name), &allocator, &builder));
  ASSERT_EQ(
    RCUTILS_RET_OK,
    rosidl_dynamic_typesupport_dynamic_type_builder_add_string_member(
      &builder, 0, member_name, std::strlen(member_name), "", 0));

  rosidl_dynamic_typesupport_dynamic_data_t dynamic_data =
    rosidl_dynamic_typesupport_get_zero_initialized_dynamic_data();
  ASSERT_EQ(
    RCUTILS_RET_OK,
    rosidl_dynamic_typesupport_dynamic_data_init_from_dynamic_type_builder(
      &builder, &allocator, &dynamic_data));
  taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take_dynamic_message(subscription, &dynamic_data, &taken, nullptr));
  EXPECT_FALSE(taken);

  rmw_message_info_t message_info = rmw_get_zero_initialized_message_info();
  taken = true;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_dynamic_message_with_info(subscription, &dynamic_data, &taken, &message_info, nullptr));
  EXPECT_FALSE(taken);

  EXPECT_EQ(RCUTILS_RET_OK, rosidl_dynamic_typesupport_dynamic_data_fini(&dynamic_data));
  EXPECT_EQ(RCUTILS_RET_OK, rosidl_dynamic_typesupport_dynamic_type_builder_fini(&builder));
  EXPECT_EQ(RCUTILS_RET_OK, rosidl_dynamic_typesupport_serialization_support_fini(&support));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, DynamicTakeInvalidDataDoesNotConsumeSample)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_dynamic_take_invalid_data_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_dynamic_take_invalid_data_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_dynamic_take_invalid_data", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_dynamic_take_invalid_data", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs::msg::String msg;
  msg.data = "preserve dynamic take sample";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  rosidl_dynamic_typesupport_dynamic_data_t invalid_dynamic_data =
    rosidl_dynamic_typesupport_get_zero_initialized_dynamic_data();
  bool taken = true;
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_take_dynamic_message(subscription, &invalid_dynamic_data, &taken, nullptr));
  EXPECT_FALSE(taken);
  rmw_reset_error();

  std_msgs::msg::String received;
  taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ(msg.data, received.data);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, DynamicTakeStringPayloadRoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rosidl_dynamic_typesupport_serialization_support_t support =
    rosidl_dynamic_typesupport_get_zero_initialized_serialization_support();
  ASSERT_EQ(RMW_RET_OK, rmw_serialization_support_init("fastcdr", &allocator, &support));

  rosidl_dynamic_typesupport_dynamic_type_builder_t builder =
    rosidl_dynamic_typesupport_get_zero_initialized_dynamic_type_builder();
  constexpr const char * type_name = "std_msgs::msg::dds_::String_";
  constexpr const char * member_name = "data";
  ASSERT_EQ(
    RCUTILS_RET_OK,
    rosidl_dynamic_typesupport_dynamic_type_builder_init(
      &support, type_name, std::strlen(type_name), &allocator, &builder));
  ASSERT_EQ(
    RCUTILS_RET_OK,
    rosidl_dynamic_typesupport_dynamic_type_builder_add_string_member(
      &builder, 0, member_name, std::strlen(member_name), "", 0));

  rosidl_dynamic_typesupport_dynamic_data_t dynamic_data =
    rosidl_dynamic_typesupport_get_zero_initialized_dynamic_data();
  ASSERT_EQ(
    RCUTILS_RET_OK,
    rosidl_dynamic_typesupport_dynamic_data_init_from_dynamic_type_builder(
      &builder, &allocator, &dynamic_data));

  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_dynamic_take_payload_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_dynamic_take_payload_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_dynamic_take_payload", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_dynamic_take_payload", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs::msg::String msg;
  msg.data = "dynamic mdds payload";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_dynamic_message(subscription, &dynamic_data, &taken, nullptr));
  ASSERT_TRUE(taken);

  rosidl_dynamic_typesupport_member_id_t data_member = 0;
  ASSERT_EQ(
    RCUTILS_RET_OK,
    rosidl_dynamic_typesupport_dynamic_data_get_member_id_by_name(
      &dynamic_data, member_name, std::strlen(member_name), &data_member));
  char * value = nullptr;
  size_t value_length = 0;
  ASSERT_EQ(
    RCUTILS_RET_OK,
    rosidl_dynamic_typesupport_dynamic_data_get_string_value(
      &dynamic_data, data_member, &value, &value_length));
  ASSERT_NE(nullptr, value);
  EXPECT_EQ(msg.data, std::string(value, value_length));
  delete[] value;

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
  EXPECT_EQ(RCUTILS_RET_OK, rosidl_dynamic_typesupport_dynamic_data_fini(&dynamic_data));
  EXPECT_EQ(RCUTILS_RET_OK, rosidl_dynamic_typesupport_dynamic_type_builder_fini(&builder));
  EXPECT_EQ(RCUTILS_RET_OK, rosidl_dynamic_typesupport_serialization_support_fini(&support));
}

TEST(RmwMddsPubSub, SerializeDeserializeInt32MultiArrayRoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32MultiArray>();

  std_msgs::msg::Int32MultiArray msg;
  msg.layout.dim.resize(1);
  msg.layout.dim[0].label = "serialized_axis";
  msg.layout.dim[0].size = 4;
  msg.layout.dim[0].stride = 4;
  msg.layout.data_offset = 1;
  msg.data = {3, -7, 42, 3588};

  rmw_serialized_message_t serialized = rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RCUTILS_RET_OK, rmw_serialized_message_init(&serialized, 1, &allocator));
  ASSERT_EQ(RMW_RET_OK, rmw_serialize(&msg, type_support, &serialized));
  EXPECT_GT(serialized.buffer_length, 0u);

  std_msgs::msg::Int32MultiArray received;
  ASSERT_EQ(RMW_RET_OK, rmw_deserialize(&serialized, type_support, &received));
  ASSERT_EQ(1u, received.layout.dim.size());
  EXPECT_EQ("serialized_axis", received.layout.dim[0].label);
  EXPECT_EQ(4u, received.layout.dim[0].size);
  EXPECT_EQ(4u, received.layout.dim[0].stride);
  EXPECT_EQ(1u, received.layout.data_offset);
  EXPECT_EQ((std::vector<int32_t>{3, -7, 42, 3588}), received.data);

  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&serialized));
}

TEST(RmwMddsPubSub, SerializeStringUsesFastCdrCompatiblePayload)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();

  std_msgs::msg::String msg;
  msg.data = "mdds-fastdds";

  rmw_serialized_message_t serialized = rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RCUTILS_RET_OK, rmw_serialized_message_init(&serialized, 1, &allocator));
  ASSERT_EQ(RMW_RET_OK, rmw_serialize(&msg, type_support, &serialized));

  EXPECT_EQ(SerializeWithFastCdr(msg), GetSerializedBytes(serialized));

  std_msgs::msg::String received;
  ASSERT_EQ(RMW_RET_OK, rmw_deserialize(&serialized, type_support, &received));
  EXPECT_EQ(msg.data, received.data);

  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&serialized));
}

TEST(RmwMddsPubSub, SerializedSizeReportsFixedScalarPayloadSizes)
{
  size_t size = 0;
  const rosidl_message_type_support_t * int32_type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  ASSERT_EQ(RMW_RET_OK, rmw_get_serialized_message_size(int32_type_support, nullptr, &size));
  EXPECT_EQ(8u, size);

  const rosidl_message_type_support_t * add_two_ints_type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<
      example_interfaces::srv::AddTwoInts_Request>();
  size = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_get_serialized_message_size(add_two_ints_type_support, nullptr, &size));
  EXPECT_EQ(20u, size);
}

TEST(RmwMddsPubSub, SerializedSizeReportsBoundedSequenceMaximumPayloadSize)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<
      test_msgs::msg::BoundedPlainSequences>();

  const test_msgs::msg::BoundedPlainSequences msg = MakeMaxBoundedPlainSequences();
  rmw_serialized_message_t serialized = rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RCUTILS_RET_OK, rmw_serialized_message_init(&serialized, 1, &allocator));
  ASSERT_EQ(RMW_RET_OK, rmw_serialize(&msg, type_support, &serialized));

  test_msgs::msg::BoundedPlainSequences received;
  ASSERT_EQ(RMW_RET_OK, rmw_deserialize(&serialized, type_support, &received));
  EXPECT_EQ(msg, received);

  size_t size = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_get_serialized_message_size(type_support, nullptr, &size));
  EXPECT_EQ(serialized.buffer_length, size);

  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&serialized));
}

TEST(RmwMddsPubSub, SerializedSizeReportsBoundedStringMaximumPayloadSize)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<
      type_description_interfaces::msg::FieldType>();

  type_description_interfaces::msg::FieldType msg;
  msg.type_id = type_description_interfaces::msg::FieldType::FIELD_TYPE_BOUNDED_STRING;
  msg.capacity = 0u;
  msg.string_capacity = 255u;
  msg.nested_type_name.assign(255u, 'm');

  rmw_serialized_message_t serialized = rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RCUTILS_RET_OK, rmw_serialized_message_init(&serialized, 1, &allocator));
  ASSERT_EQ(RMW_RET_OK, rmw_serialize(&msg, type_support, &serialized));

  type_description_interfaces::msg::FieldType received;
  ASSERT_EQ(RMW_RET_OK, rmw_deserialize(&serialized, type_support, &received));
  EXPECT_EQ(msg, received);

  size_t size = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_get_serialized_message_size(type_support, nullptr, &size));
  EXPECT_EQ(serialized.buffer_length, size);

  EXPECT_EQ(RCUTILS_RET_OK, rmw_serialized_message_fini(&serialized));
}

TEST(RmwMddsPubSub, TakeSequenceTakesAvailableSamplesInOrder)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_take_sequence_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_take_sequence_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_take_sequence", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_take_sequence", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs::msg::String first;
  first.data = "one";
  std_msgs::msg::String second;
  second.data = "two";
  std_msgs::msg::String third;
  third.data = "three";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &first, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &second, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &third, nullptr));

  std_msgs::msg::String received_first;
  std_msgs::msg::String received_second;
  rmw_message_sequence_t messages = rmw_get_zero_initialized_message_sequence();
  ASSERT_EQ(RMW_RET_OK, rmw_message_sequence_init(&messages, 2, &allocator));
  messages.data[0] = &received_first;
  messages.data[1] = &received_second;
  rmw_message_info_sequence_t infos = rmw_get_zero_initialized_message_info_sequence();
  ASSERT_EQ(RMW_RET_OK, rmw_message_info_sequence_init(&infos, 2, &allocator));

  size_t taken = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_take_sequence(subscription, 2, &messages, &infos, &taken, nullptr));
  EXPECT_EQ(2u, taken);
  EXPECT_EQ(2u, messages.size);
  EXPECT_EQ(2u, infos.size);
  EXPECT_EQ("one", received_first.data);
  EXPECT_EQ("two", received_second.data);

  std_msgs::msg::String remaining;
  bool single_taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &remaining, &single_taken, nullptr));
  EXPECT_TRUE(single_taken);
  EXPECT_EQ("three", remaining.data);

  EXPECT_EQ(RMW_RET_OK, rmw_message_info_sequence_fini(&infos));
  EXPECT_EQ(RMW_RET_OK, rmw_message_sequence_fini(&messages));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, TakeSequenceSkipsExpiredLifespanSamples)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_take_sequence_lifespan_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_take_sequence_lifespan_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.lifespan.sec = 0;
  qos.lifespan.nsec = 1000000;

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_take_sequence_lifespan", &qos, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_take_sequence_lifespan", &qos, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs::msg::String expired;
  expired.data = "expired";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &expired, nullptr));
  std::this_thread::sleep_for(std::chrono::milliseconds(3));

  std_msgs::msg::String received;
  rmw_message_sequence_t messages = rmw_get_zero_initialized_message_sequence();
  ASSERT_EQ(RMW_RET_OK, rmw_message_sequence_init(&messages, 1, &allocator));
  messages.data[0] = &received;
  rmw_message_info_sequence_t infos = rmw_get_zero_initialized_message_info_sequence();
  ASSERT_EQ(RMW_RET_OK, rmw_message_info_sequence_init(&infos, 1, &allocator));

  size_t taken = 1;
  ASSERT_EQ(RMW_RET_OK, rmw_take_sequence(subscription, 1, &messages, &infos, &taken, nullptr));
  EXPECT_EQ(0u, taken);
  EXPECT_EQ(0u, messages.size);
  EXPECT_EQ(0u, infos.size);

  bool single_taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &single_taken, nullptr));
  EXPECT_FALSE(single_taken);

  EXPECT_EQ(RMW_RET_OK, rmw_message_info_sequence_fini(&infos));
  EXPECT_EQ(RMW_RET_OK, rmw_message_sequence_fini(&messages));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, InProcessCStringRoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_c_pubsub_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_c_pubsub_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String);
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_c_string", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_c_string", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs__msg__String msg;
  ASSERT_TRUE(std_msgs__msg__String__init(&msg));
  ASSERT_TRUE(rosidl_runtime_c__String__assign(&msg.data, "hello c rmw_mdds"));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  void * subscription_handle = subscription->data;
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 1;
  subscriptions.subscribers = &subscription_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set, &timeout));

  std_msgs__msg__String received;
  ASSERT_TRUE(std_msgs__msg__String__init(&received));
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  ASSERT_NE(nullptr, received.data.data);
  EXPECT_STREQ("hello c rmw_mdds", received.data.data);

  std_msgs__msg__String__fini(&received);
  std_msgs__msg__String__fini(&msg);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, InProcessInt32RoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_int32_pubsub_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_int32_pubsub_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_int32", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_int32", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs::msg::Int32 msg;
  msg.data = 3588;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  void * subscription_handle = subscription->data;
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 1;
  subscriptions.subscribers = &subscription_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set, &timeout));

  std_msgs::msg::Int32 received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ(3588, received.data);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, InProcessCInt32RoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_c_int32_pubsub_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_c_int32_pubsub_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32);
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_c_int32", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_c_int32", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs__msg__Int32 msg;
  std_msgs__msg__Int32__init(&msg);
  msg.data = -3588;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  void * subscription_handle = subscription->data;
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 1;
  subscriptions.subscribers = &subscription_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set, &timeout));

  std_msgs__msg__Int32 received;
  std_msgs__msg__Int32__init(&received);
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ(-3588, received.data);

  std_msgs__msg__Int32__fini(&received);
  std_msgs__msg__Int32__fini(&msg);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, InProcessCInt32MultiArrayRoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_c_int32_multi_array_pubsub_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_c_int32_multi_array_pubsub_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32MultiArray);
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_c_int32_multi_array", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_c_int32_multi_array", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs__msg__Int32MultiArray msg;
  ASSERT_TRUE(std_msgs__msg__Int32MultiArray__init(&msg));
  ASSERT_TRUE(std_msgs__msg__MultiArrayDimension__Sequence__init(&msg.layout.dim, 1));
  ASSERT_TRUE(rosidl_runtime_c__String__assign(&msg.layout.dim.data[0].label, "c_axis"));
  msg.layout.dim.data[0].size = 3;
  msg.layout.dim.data[0].stride = 3;
  msg.layout.data_offset = 0;
  ASSERT_TRUE(rosidl_runtime_c__int32__Sequence__init(&msg.data, 3));
  msg.data.data[0] = 11;
  msg.data.data[1] = -22;
  msg.data.data[2] = 3588;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  void * subscription_handle = subscription->data;
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 1;
  subscriptions.subscribers = &subscription_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set, &timeout));

  std_msgs__msg__Int32MultiArray received;
  ASSERT_TRUE(std_msgs__msg__Int32MultiArray__init(&received));
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  ASSERT_EQ(1u, received.layout.dim.size);
  ASSERT_NE(nullptr, received.layout.dim.data);
  ASSERT_NE(nullptr, received.layout.dim.data[0].label.data);
  EXPECT_STREQ("c_axis", received.layout.dim.data[0].label.data);
  EXPECT_EQ(3u, received.layout.dim.data[0].size);
  EXPECT_EQ(3u, received.layout.dim.data[0].stride);
  EXPECT_EQ(0u, received.layout.data_offset);
  ASSERT_EQ(3u, received.data.size);
  ASSERT_NE(nullptr, received.data.data);
  EXPECT_EQ(11, received.data.data[0]);
  EXPECT_EQ(-22, received.data.data[1]);
  EXPECT_EQ(3588, received.data.data[2]);

  std_msgs__msg__Int32MultiArray__fini(&received);
  std_msgs__msg__Int32MultiArray__fini(&msg);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, InProcessMultiScalarRoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_multi_scalar_pubsub_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_multi_scalar_pubsub_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<
      example_interfaces::srv::AddTwoInts_Request>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_multi_scalar", &rmw_qos_profile_default, &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_multi_scalar", &rmw_qos_profile_default, &subscription_options);
  ASSERT_NE(nullptr, subscription);

  example_interfaces::srv::AddTwoInts_Request msg;
  msg.a = 3588;
  msg.b = -42;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  void * subscription_handle = subscription->data;
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 1;
  subscriptions.subscribers = &subscription_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set, &timeout));

  example_interfaces::srv::AddTwoInts_Request received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  EXPECT_EQ(3588, received.a);
  EXPECT_EQ(-42, received.b);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsPubSub, InProcessInt32MultiArrayRoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_int32_multi_array_pubsub_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_int32_multi_array_pubsub_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_message_type_support_t * type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::Int32MultiArray>();
  rmw_publisher_options_t publisher_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t * publisher = rmw_create_publisher(
    node, type_support, "/mdds_test_int32_multi_array", &rmw_qos_profile_default,
    &publisher_options);
  ASSERT_NE(nullptr, publisher);
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, type_support, "/mdds_test_int32_multi_array", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  std_msgs::msg::Int32MultiArray msg;
  msg.layout.dim.resize(1);
  msg.layout.dim[0].label = "axis";
  msg.layout.dim[0].size = 3;
  msg.layout.dim[0].stride = 3;
  msg.layout.data_offset = 0;
  msg.data = {7, -35, 3588};
  ASSERT_EQ(RMW_RET_OK, rmw_publish(publisher, &msg, nullptr));

  void * subscription_handle = subscription->data;
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 1;
  subscriptions.subscribers = &subscription_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set, &timeout));

  std_msgs::msg::Int32MultiArray received;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(subscription, &received, &taken, nullptr));
  EXPECT_TRUE(taken);
  ASSERT_EQ(1u, received.layout.dim.size());
  EXPECT_EQ("axis", received.layout.dim[0].label);
  EXPECT_EQ(3u, received.layout.dim[0].size);
  EXPECT_EQ(3u, received.layout.dim[0].stride);
  EXPECT_EQ(0u, received.layout.data_offset);
  EXPECT_EQ((std::vector<int32_t>{7, -35, 3588}), received.data);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, publisher));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}
