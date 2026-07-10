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
#include <std_srvs/srv/detail/trigger__functions.h>
#include <std_srvs/srv/detail/trigger__type_support.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <example_interfaces/srv/detail/add_two_ints__type_support.hpp>
#include <string>
#include <std_srvs/srv/detail/trigger__type_support.hpp>
#include <std_msgs/msg/string.hpp>
#include <test_msgs/srv/arrays.hpp>
#include <thread>
#include <vector>

#include "ipc_protocol.hpp"
#include "ipc_transport.hpp"
#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/subscription_options.h"
#include "rosidl_runtime_c/string_functions.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_interface/macros.h"

namespace
{
class LocalOnlyTransportEnvironment : public testing::Environment
{
public:
  void SetUp() override
  {
    setenv("RMW_MDDS_BROKER", "0", 1);
    unsetenv("RMW_MDDS_BROKER_SOCKET");
    unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
  }
};

testing::Environment * const g_local_only_transport_environment =
  testing::AddGlobalTestEnvironment(new LocalOnlyTransportEnvironment);

struct AddTwoIntsRequest
{
  int64_t a;
  int64_t b;
};

struct AddTwoIntsResponse
{
  int64_t sum;
};

class EnvVarGuard
{
public:
  explicit EnvVarGuard(const char * name)
  : name_(name)
  {
    const char * value = std::getenv(name);
    if (value != nullptr) {
      had_value_ = true;
      value_ = value;
    }
  }

  ~EnvVarGuard()
  {
    if (had_value_) {
      setenv(name_.c_str(), value_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

private:
  std::string name_;
  bool had_value_ = false;
  std::string value_;
};

class TempSocketPath
{
public:
  TempSocketPath()
  {
    char templ[] = "/tmp/rmw_mdds_service_inproc_XXXXXX";
    char * dir = mkdtemp(templ);
    if (dir != nullptr) {
      dir_ = dir;
      path_ = dir_ + "/broker.sock";
    }
  }

  ~TempSocketPath()
  {
    if (!path_.empty()) {
      unlink(path_.c_str());
    }
    if (!dir_.empty()) {
      rmdir(dir_.c_str());
    }
  }

  const std::string & path() const
  {
    return path_;
  }

private:
  std::string dir_;
  std::string path_;
};

void SetEnclave(rmw_init_options_t * options, const char * enclave)
{
  options->allocator.deallocate(options->enclave, options->allocator.state);
  options->enclave = rcutils_strdup(enclave, options->allocator);
  ASSERT_NE(nullptr, options->enclave);
}

void CountEventCallback(const void * user_data, size_t number_of_events)
{
  auto * callback_count = static_cast<size_t *>(const_cast<void *>(user_data));
  if (callback_count != nullptr) {
    *callback_count += number_of_events;
  }
}

const rosidl_service_type_support_t * UnsupportedServiceTypeSupportHandle(
  const rosidl_service_type_support_t *, const char *)
{
  return nullptr;
}

void AppendI64(std::vector<uint8_t> * out, int64_t value)
{
  ASSERT_NE(nullptr, out);
  for (size_t i = 0; i < sizeof(value); ++i) {
    out->push_back(static_cast<uint8_t>(
      (static_cast<uint64_t>(value) >> (8u * i)) & 0xffu));
  }
}

std::vector<uint8_t> MakeAddTwoIntsRequestPayload(int64_t a, int64_t b)
{
  std::vector<uint8_t> payload;
  AppendI64(&payload, a);
  AppendI64(&payload, b);
  return payload;
}

std::vector<uint8_t> MakeServiceWirePayload(
  const uint8_t writer_guid[RMW_GID_STORAGE_SIZE],
  int64_t sequence_number,
  const std::vector<uint8_t> & body)
{
  std::vector<uint8_t> payload;
  AppendI64(&payload, sequence_number);
  payload.insert(payload.end(), writer_guid, writer_guid + RMW_GID_STORAGE_SIZE);
  AppendI64(&payload, 123456789);
  payload.insert(payload.end(), body.begin(), body.end());
  return payload;
}

void ServeRegistrationAckAndWaitForPrimaryClose(
  int listener_fd,
  std::atomic<bool> * registration_seen,
  std::atomic<bool> * primary_closed)
{
  std::string error;
  rmw_mdds_cpp::ipc::UniqueFd client =
    rmw_mdds_cpp::ipc::AcceptUnixSocket(listener_fd, &error);
  if (!client) {
    return;
  }

  rmw_mdds_cpp::ipc::Frame frame;
  if (rmw_mdds_cpp::ipc::ReadFrame(client.get(), &frame, &error) !=
    rmw_mdds_cpp::ipc::ReadFrameStatus::kOk)
  {
    return;
  }
  if (frame.kind != rmw_mdds_cpp::ipc::MessageKind::kRegisterClient) {
    return;
  }
  registration_seen->store(true);
  if (!rmw_mdds_cpp::ipc::WriteFrame(
      client.get(),
      rmw_mdds_cpp::ipc::Frame{
        rmw_mdds_cpp::ipc::MessageKind::kAck, frame.request_id, {}},
      &error))
  {
    return;
  }
  if (rmw_mdds_cpp::ipc::ReadFrame(client.get(), &frame, &error) !=
    rmw_mdds_cpp::ipc::ReadFrameStatus::kOk)
  {
    primary_closed->store(true);
  }
}

void ServeDeliveryBeforeRegistrationAck(
  int listener_fd,
  std::atomic<bool> * registration_seen,
  std::atomic<bool> * primary_closed)
{
  std::string error;
  rmw_mdds_cpp::ipc::UniqueFd client =
    rmw_mdds_cpp::ipc::AcceptUnixSocket(listener_fd, &error);
  if (!client) {
    return;
  }

  rmw_mdds_cpp::ipc::Frame frame;
  if (rmw_mdds_cpp::ipc::ReadFrame(client.get(), &frame, &error) !=
    rmw_mdds_cpp::ipc::ReadFrameStatus::kOk)
  {
    return;
  }
  if (frame.kind != rmw_mdds_cpp::ipc::MessageKind::kRegisterClient) {
    return;
  }
  registration_seen->store(true);

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 0u;
  sample.sequence_number = 1u;
  sample.mdds_payload = false;
  sample.payload = {0u, 1u, 2u, 3u};
  if (!rmw_mdds_cpp::ipc::WriteFrame(
      client.get(),
      rmw_mdds_cpp::ipc::Frame{
        rmw_mdds_cpp::ipc::MessageKind::kDeliverSample, 0u,
        rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)},
      &error))
  {
    return;
  }
  if (!rmw_mdds_cpp::ipc::WriteFrame(
      client.get(),
      rmw_mdds_cpp::ipc::Frame{
        rmw_mdds_cpp::ipc::MessageKind::kAck, frame.request_id, {}},
      &error))
  {
    return;
  }

  if (rmw_mdds_cpp::ipc::ReadFrame(client.get(), &frame, &error) !=
    rmw_mdds_cpp::ipc::ReadFrameStatus::kOk)
  {
    primary_closed->store(true);
  }
}

void ServeServiceRegistrationAndCapturePublishes(
  int listener_fd,
  std::vector<uint64_t> * sample_sequences)
{
  std::string error;
  rmw_mdds_cpp::ipc::UniqueFd service =
    rmw_mdds_cpp::ipc::AcceptUnixSocket(listener_fd, &error);
  if (!service) {
    return;
  }

  rmw_mdds_cpp::ipc::Frame frame;
  if (rmw_mdds_cpp::ipc::ReadFrame(service.get(), &frame, &error) !=
    rmw_mdds_cpp::ipc::ReadFrameStatus::kOk)
  {
    return;
  }
  if (frame.kind != rmw_mdds_cpp::ipc::MessageKind::kRegisterService) {
    return;
  }
  if (!rmw_mdds_cpp::ipc::WriteFrame(
      service.get(),
      rmw_mdds_cpp::ipc::Frame{
        rmw_mdds_cpp::ipc::MessageKind::kAck, frame.request_id, {}},
      &error))
  {
    return;
  }

  while (sample_sequences != nullptr && sample_sequences->size() < 2u) {
    if (rmw_mdds_cpp::ipc::ReadFrame(service.get(), &frame, &error) !=
      rmw_mdds_cpp::ipc::ReadFrameStatus::kOk)
    {
      return;
    }
    if (frame.kind != rmw_mdds_cpp::ipc::MessageKind::kPublishSample) {
      continue;
    }
    rmw_mdds_cpp::ipc::SampleMessage sample;
    if (!rmw_mdds_cpp::ipc::DecodeSampleMessage(
        frame.payload.data(), frame.payload.size(), &sample, &error))
    {
      return;
    }
    sample_sequences->push_back(sample.sequence_number);
  }
}

void ServeServiceRegistrationAndInjectDuplicateRequests(
  int listener_fd,
  const std::vector<uint8_t> & wire_payload,
  std::atomic<bool> * registration_seen)
{
  std::string error;
  rmw_mdds_cpp::ipc::UniqueFd service =
    rmw_mdds_cpp::ipc::AcceptUnixSocket(listener_fd, &error);
  if (!service) {
    return;
  }

  rmw_mdds_cpp::ipc::Frame frame;
  if (rmw_mdds_cpp::ipc::ReadFrame(service.get(), &frame, &error) !=
    rmw_mdds_cpp::ipc::ReadFrameStatus::kOk)
  {
    return;
  }
  if (frame.kind != rmw_mdds_cpp::ipc::MessageKind::kRegisterService) {
    return;
  }
  registration_seen->store(true);
  if (!rmw_mdds_cpp::ipc::WriteFrame(
      service.get(),
      rmw_mdds_cpp::ipc::Frame{
        rmw_mdds_cpp::ipc::MessageKind::kAck, frame.request_id, {}},
      &error))
  {
    return;
  }

  rmw_mdds_cpp::ipc::SampleMessage sample;
  sample.entity_id = 0u;
  sample.sequence_number = 1u;
  sample.mdds_payload = false;
  sample.payload = wire_payload;
  const auto delivery = rmw_mdds_cpp::ipc::Frame{
    rmw_mdds_cpp::ipc::MessageKind::kDeliverSample, 0u,
    rmw_mdds_cpp::ipc::EncodeSampleMessage(sample)};
  (void)rmw_mdds_cpp::ipc::WriteFrame(service.get(), delivery, &error);
  (void)rmw_mdds_cpp::ipc::WriteFrame(service.get(), delivery, &error);
  if (rmw_mdds_cpp::ipc::ReadFrame(service.get(), &frame, &error) !=
    rmw_mdds_cpp::ipc::ReadFrameStatus::kOk)
  {
    return;
  }
}
}  // namespace

TEST(RmwMddsService, DestroyClientDoesNotWaitForUnregisterAck)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::UniqueFd listener =
    rmw_mdds_cpp::ipc::ListenUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(listener) << error;

  std::atomic<bool> registration_seen{false};
  std::atomic<bool> primary_closed{false};
  std::thread fake_broker(
    [&listener, &registration_seen, &primary_closed]() {
      ServeRegistrationAckAndWaitForPrimaryClose(
        listener.get(), &registration_seen, &primary_closed);
    });

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(
    0,
    setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_destroy_client_cleanup_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_destroy_client_cleanup", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_client_t * client = rmw_create_client(
    node, type_support, "/mdds_destroy_client_cleanup",
    &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client) << rmw_get_error_string().str;
  ASSERT_TRUE(registration_seen.load());

  const auto started = std::chrono::steady_clock::now();
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - started);

  listener.reset();
  if (fake_broker.joinable()) {
    fake_broker.join();
  }
  EXPECT_TRUE(primary_closed.load());
  EXPECT_LT(elapsed, std::chrono::milliseconds(500));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, CreateClientToleratesDeliveryBeforeRegistrationAck)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::UniqueFd listener =
    rmw_mdds_cpp::ipc::ListenUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(listener) << error;

  std::atomic<bool> registration_seen{false};
  std::atomic<bool> primary_closed{false};
  std::thread fake_broker(
    [&listener, &registration_seen, &primary_closed]() {
      ServeDeliveryBeforeRegistrationAck(
        listener.get(), &registration_seen, &primary_closed);
    });

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(
    0,
    setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_create_client_interleaved_delivery_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_create_client_interleaved_delivery", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_client_t * client = rmw_create_client(
    node, type_support, "/mdds_create_client_interleaved_delivery",
    &rmw_qos_profile_services_default);
  const std::string create_error =
    client == nullptr ? rmw_get_error_string().str : "";
  rmw_reset_error();

  if (client != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  }
  listener.reset();
  if (fake_broker.joinable()) {
    fake_broker.join();
  }

  EXPECT_TRUE(registration_seen.load());
  EXPECT_TRUE(primary_closed.load());
  EXPECT_NE(nullptr, client) << create_error;

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, ServiceServerIsAvailableUsesUpstreamBadArgumentReturnCodes)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_service_available_arguments_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_service_available_argument_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_GET_SRV_TYPE_SUPPORT(std_srvs, srv, Trigger);
  rmw_client_t * client = rmw_create_client(
    node, type_support, "/mdds_service_available_argument", &rmw_qos_profile_default);
  ASSERT_NE(nullptr, client) << rmw_get_error_string().str;

  bool available = false;
  EXPECT_EQ(RMW_RET_ERROR, rmw_service_server_is_available(nullptr, client, &available))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_ERROR, rmw_service_server_is_available(node, nullptr, &available))
    << rmw_get_error_string().str;
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_ERROR, rmw_service_server_is_available(node, client, nullptr))
    << rmw_get_error_string().str;
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, CreateServiceAndClientRejectUnsupportedTypeSupport)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_invalid_service_type_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_invalid_service_type_node", "/mdds");
  ASSERT_NE(nullptr, node);

  rosidl_service_type_support_t unsupported_type_support{};
  unsupported_type_support.typesupport_identifier = "rmw_mdds_invalid_service_type_support";
  unsupported_type_support.func = UnsupportedServiceTypeSupportHandle;

  rmw_service_t * service = rmw_create_service(
    node, &unsupported_type_support, "/mdds_invalid_service_type",
    &rmw_qos_profile_services_default);
  EXPECT_EQ(nullptr, service);
  if (service != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  }
  rmw_reset_error();

  rmw_client_t * client = rmw_create_client(
    node, &unsupported_type_support, "/mdds_invalid_service_type",
    &rmw_qos_profile_services_default);
  EXPECT_EQ(nullptr, client);
  if (client != nullptr) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  }
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, CreateServiceAndClientRejectInvalidServiceNames)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_invalid_service_name_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_invalid_service_name_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  const char * invalid_service_names[] = {"", "relative_service", "/foo bar"};

  for (const char * service_name : invalid_service_names) {
    rmw_service_t * service =
      rmw_create_service(node, type_support, service_name, &rmw_qos_profile_services_default);
    EXPECT_EQ(nullptr, service) << service_name;
    if (service != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
    }
    rmw_reset_error();

    rmw_client_t * client =
      rmw_create_client(node, type_support, service_name, &rmw_qos_profile_services_default);
    EXPECT_EQ(nullptr, client) << service_name;
    if (client != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
    }
    rmw_reset_error();
  }

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, CreateServiceAndClientRejectInvalidQosProfiles)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_invalid_service_qos_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_invalid_service_qos_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  const rmw_qos_profile_t * invalid_qos_profiles[] = {nullptr, &rmw_qos_profile_unknown};

  for (const rmw_qos_profile_t * qos_profile : invalid_qos_profiles) {
    const char * qos_label = qos_profile == nullptr ? "null" : "unknown";

    rmw_service_t * service =
      rmw_create_service(node, type_support, "/mdds_invalid_service_qos", qos_profile);
    EXPECT_EQ(nullptr, service) << qos_label;
    if (service != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
    }
    rmw_reset_error();

    rmw_client_t * client =
      rmw_create_client(node, type_support, "/mdds_invalid_service_qos", qos_profile);
    EXPECT_EQ(nullptr, client) << qos_label;
    if (client != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
    }
    rmw_reset_error();
  }

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, ClientGidDoesNotExposeProcessLocalPointer)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_client_gid_identity_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_client_gid_identity_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
    rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_client_t * client = rmw_create_client(
    node, type_support, "/mdds_client_gid_identity", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client) << rmw_get_error_string().str;

  rmw_gid_t gid{};
  ASSERT_EQ(RMW_RET_OK, rmw_get_gid_for_client(client, &gid));
  uint64_t gid_entity = 0u;
  std::memcpy(&gid_entity, gid.data, std::min(sizeof(gid_entity), sizeof(gid.data)));
  const uint64_t raw_client_pointer =
    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(client->data));
  EXPECT_NE(raw_client_pointer, gid_entity)
    << "service client writer_guid must be process-unique, not only a "
       "process-local ClientData pointer";

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, BrokerServiceResponsesUseUniquePublicationSequences)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::UniqueFd listener =
    rmw_mdds_cpp::ipc::ListenUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(listener) << error;

  std::vector<uint64_t> sample_sequences;
  std::thread fake_broker(
    [&listener, &sample_sequences]() {
      ServeServiceRegistrationAndCapturePublishes(
        listener.get(), &sample_sequences);
    });

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(
    0,
    setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_service_response_sequence_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_service_response_sequence_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
    rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_service_t * service = rmw_create_service(
    node, type_support, "/mdds_service_response_sequence",
    &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service) << rmw_get_error_string().str;

  rmw_request_id_t first_request{};
  first_request.sequence_number = 1;
  first_request.writer_guid[0] = 1u;
  AddTwoIntsResponse first_response{42};
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &first_request, &first_response));

  rmw_request_id_t second_request{};
  second_request.sequence_number = 1;
  second_request.writer_guid[0] = 2u;
  AddTwoIntsResponse second_response{43};
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &second_request, &second_response));

  listener.reset();
  if (fake_broker.joinable()) {
    fake_broker.join();
  }
  ASSERT_EQ(2u, sample_sequences.size());
  EXPECT_NE(sample_sequences[0], sample_sequences[1])
    << "broker sample sequence must be unique for each response published by "
       "one service endpoint, even when different clients reuse request "
       "sequence number 1";

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, BrokerServiceDropsDuplicateRequestDeliveries)
{
  EnvVarGuard broker_guard("RMW_MDDS_BROKER");
  EnvVarGuard socket_guard("RMW_MDDS_BROKER_SOCKET");
  EnvVarGuard bridge_guard("RMW_MDDS_BRIDGE_LIBRARY");

  TempSocketPath socket_path;
  ASSERT_FALSE(socket_path.path().empty());

  std::string error;
  rmw_mdds_cpp::ipc::UniqueFd listener =
    rmw_mdds_cpp::ipc::ListenUnixSocket(socket_path.path(), &error);
  ASSERT_TRUE(listener) << error;

  uint8_t writer_guid[RMW_GID_STORAGE_SIZE] = {};
  writer_guid[0] = 42u;
  const std::vector<uint8_t> wire_payload =
    MakeServiceWirePayload(writer_guid, 7, MakeAddTwoIntsRequestPayload(4, 5));
  std::atomic<bool> registration_seen{false};
  std::thread fake_broker(
    [&listener, &wire_payload, &registration_seen]() {
      ServeServiceRegistrationAndInjectDuplicateRequests(
        listener.get(), wire_payload, &registration_seen);
    });

  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER", "1", 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_SOCKET", socket_path.path().c_str(), 1));
  ASSERT_EQ(
    0,
    setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_service_request_dedupe_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node =
    rmw_create_node(&context, "mdds_service_request_dedupe_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
    rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_service_t * service = rmw_create_service(
    node, type_support, "/mdds_service_request_dedupe",
    &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service) << rmw_get_error_string().str;
  ASSERT_TRUE(registration_seen.load());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  AddTwoIntsRequest first_request{0, 0};
  rmw_service_info_t first_header{};
  bool first_taken = false;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_request(service, &first_header, &first_request, &first_taken));
  ASSERT_TRUE(first_taken);
  EXPECT_EQ(7, first_header.request_id.sequence_number);
  EXPECT_EQ(4, first_request.a);
  EXPECT_EQ(5, first_request.b);

  AddTwoIntsRequest duplicate_request{0, 0};
  rmw_service_info_t duplicate_header{};
  bool duplicate_taken = true;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_request(
      service, &duplicate_header, &duplicate_request, &duplicate_taken));
  EXPECT_FALSE(duplicate_taken)
    << "duplicate broker deliveries with the same request writer_guid and "
       "sequence must not reach the service callback twice";

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  listener.reset();
  if (fake_broker.joinable()) {
    fake_broker.join();
  }
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, InProcessAddTwoIntsRoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_service_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_service_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_service_t * service =
    rmw_create_service(node, type_support, "/mdds_add_two_ints", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t * client =
    rmw_create_client(node, type_support, "/mdds_add_two_ints", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  rmw_gid_t client_gid{};
  ASSERT_EQ(RMW_RET_OK, rmw_get_gid_for_client(client, &client_gid));

  AddTwoIntsRequest request{7, 35};
  int64_t sequence_id = -1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &request, &sequence_id));
  EXPECT_GT(sequence_id, 0);

  void * service_handle = service->data;
  rmw_services_t services;
  services.service_count = 1;
  services.services = &service_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(nullptr, nullptr, &services, nullptr, nullptr, wait_set, &timeout));
  EXPECT_NE(nullptr, services.services[0]);

  AddTwoIntsRequest received_request{0, 0};
  rmw_service_info_t request_header{};
  bool request_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_request(service, &request_header, &received_request, &request_taken));
  ASSERT_TRUE(request_taken);
  EXPECT_EQ(sequence_id, request_header.request_id.sequence_number);
  EXPECT_GT(request_header.source_timestamp, 0);
  EXPECT_GE(request_header.received_timestamp, request_header.source_timestamp);
  EXPECT_EQ(
    0, std::memcmp(client_gid.data, request_header.request_id.writer_guid, RMW_GID_STORAGE_SIZE));
  EXPECT_EQ(7, received_request.a);
  EXPECT_EQ(35, received_request.b);

  AddTwoIntsResponse response{received_request.a + received_request.b};
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &request_header.request_id, &response));

  void * client_handle = client->data;
  rmw_clients_t clients;
  clients.client_count = 1;
  clients.clients = &client_handle;
  ASSERT_EQ(RMW_RET_OK, rmw_wait(nullptr, nullptr, nullptr, &clients, nullptr, wait_set, &timeout));
  EXPECT_NE(nullptr, clients.clients[0]);

  AddTwoIntsResponse received_response{0};
  rmw_service_info_t response_header{};
  bool response_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_response(client, &response_header, &received_response, &response_taken));
  ASSERT_TRUE(response_taken);
  EXPECT_EQ(sequence_id, response_header.request_id.sequence_number);
  EXPECT_GT(response_header.source_timestamp, 0);
  EXPECT_GE(response_header.received_timestamp, response_header.source_timestamp);
  EXPECT_EQ(42, received_response.sum);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, ServiceAvailabilityMatchesCClientToCppService)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_mixed_service_type_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_mixed_service_type_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * cpp_type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, std_srvs, srv, Trigger)();
  const rosidl_service_type_support_t * c_type_support =
    ROSIDL_GET_SRV_TYPE_SUPPORT(std_srvs, srv, Trigger);

  rmw_service_t * service = rmw_create_service(
    node, cpp_type_support, "/mdds_mixed_trigger", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service) << rmw_get_error_string().str;
  rmw_client_t * client = rmw_create_client(
    node, c_type_support, "/mdds_mixed_trigger", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client) << rmw_get_error_string().str;

  bool is_available = false;
  ASSERT_EQ(RMW_RET_OK, rmw_service_server_is_available(node, client, &is_available))
    << rmw_get_error_string().str;
  EXPECT_TRUE(is_available);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, WaitClearsUnreadySubscriptionsWhenServiceIsReady)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_wait_mixed_service_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_wait_mixed_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * service_type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_service_t * service = rmw_create_service(
    node, service_type_support, "/mdds_wait_service", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t * client = rmw_create_client(
    node, service_type_support, "/mdds_wait_service", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  const rosidl_message_type_support_t * message_type_support =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options = rmw_get_default_subscription_options();
  rmw_subscription_t * subscription = rmw_create_subscription(
    node, message_type_support, "/mdds_wait_empty_topic", &rmw_qos_profile_default,
    &subscription_options);
  ASSERT_NE(nullptr, subscription);

  AddTwoIntsRequest request{1, 2};
  int64_t sequence_id = -1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &request, &sequence_id));

  void * subscription_handle = subscription->data;
  rmw_subscriptions_t subscriptions;
  subscriptions.subscriber_count = 1;
  subscriptions.subscribers = &subscription_handle;

  void * service_handle = service->data;
  rmw_services_t services;
  services.service_count = 1;
  services.services = &service_handle;

  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 2);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(&subscriptions, nullptr, &services, nullptr, nullptr, wait_set, &timeout));
  EXPECT_EQ(nullptr, subscriptions.subscribers[0]);
  EXPECT_NE(nullptr, services.services[0]);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, subscription));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, InProcessArraysServiceRoundTrip)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_arrays_service_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_arrays_service_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, test_msgs, srv, Arrays)();
  rmw_service_t * service =
    rmw_create_service(node, type_support, "/mdds_arrays", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t * client =
    rmw_create_client(node, type_support, "/mdds_arrays", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  test_msgs::srv::Arrays::Request request;
  request.bool_values = {true, false, true};
  request.int32_values = {7, -35, 3588};
  request.string_values = {"request", "array", "payload"};
  request.basic_types_values[0].int32_value = 42;

  int64_t sequence_id = -1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &request, &sequence_id));

  void * service_handle = service->data;
  rmw_services_t services;
  services.service_count = 1;
  services.services = &service_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(nullptr, nullptr, &services, nullptr, nullptr, wait_set, &timeout));

  test_msgs::srv::Arrays::Request received_request;
  rmw_service_info_t request_header{};
  bool request_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_request(service, &request_header, &received_request, &request_taken));
  ASSERT_TRUE(request_taken);
  EXPECT_EQ(sequence_id, request_header.request_id.sequence_number);
  EXPECT_EQ(request.bool_values, received_request.bool_values);
  EXPECT_EQ(request.int32_values, received_request.int32_values);
  EXPECT_EQ(request.string_values, received_request.string_values);
  EXPECT_EQ(42, received_request.basic_types_values[0].int32_value);

  test_msgs::srv::Arrays::Response response;
  response.bool_values = {false, true, false};
  response.int32_values = {-1, 0, 1};
  response.string_values = {"response", "array", "payload"};
  response.basic_types_values[1].int32_value = -3588;
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &request_header.request_id, &response));

  void * client_handle = client->data;
  rmw_clients_t clients;
  clients.client_count = 1;
  clients.clients = &client_handle;
  ASSERT_EQ(RMW_RET_OK, rmw_wait(nullptr, nullptr, nullptr, &clients, nullptr, wait_set, &timeout));

  test_msgs::srv::Arrays::Response received_response;
  rmw_service_info_t response_header{};
  bool response_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_response(client, &response_header, &received_response, &response_taken));
  ASSERT_TRUE(response_taken);
  EXPECT_EQ(sequence_id, response_header.request_id.sequence_number);
  EXPECT_EQ(response.bool_values, received_response.bool_values);
  EXPECT_EQ(response.int32_values, received_response.int32_values);
  EXPECT_EQ(response.string_values, received_response.string_values);
  EXPECT_EQ(-3588, received_response.basic_types_values[1].int32_value);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, InProcessTriggerResponseCopiesCStringPayload)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_trigger_service_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_trigger_service_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_c, std_srvs, srv, Trigger)();
  rmw_service_t * service =
    rmw_create_service(node, type_support, "/mdds_trigger", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t * client =
    rmw_create_client(node, type_support, "/mdds_trigger", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  std_srvs__srv__Trigger_Request request;
  ASSERT_TRUE(std_srvs__srv__Trigger_Request__init(&request));
  int64_t sequence_id = -1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &request, &sequence_id));

  void * service_handle = service->data;
  rmw_services_t services;
  services.service_count = 1;
  services.services = &service_handle;
  rmw_time_t timeout;
  timeout.sec = 0;
  timeout.nsec = 100000000;
  rmw_wait_set_t * wait_set = rmw_create_wait_set(&context, 1);
  ASSERT_NE(nullptr, wait_set);
  ASSERT_EQ(
    RMW_RET_OK, rmw_wait(nullptr, nullptr, &services, nullptr, nullptr, wait_set, &timeout));

  std_srvs__srv__Trigger_Request received_request;
  ASSERT_TRUE(std_srvs__srv__Trigger_Request__init(&received_request));
  rmw_service_info_t request_header{};
  bool request_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_request(service, &request_header, &received_request, &request_taken));
  ASSERT_TRUE(request_taken);

  std_srvs__srv__Trigger_Response response;
  ASSERT_TRUE(std_srvs__srv__Trigger_Response__init(&response));
  response.success = true;
  const char * original_message = "trigger response with owned string payload";
  ASSERT_TRUE(rosidl_runtime_c__String__assign(&response.message, original_message));
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &request_header.request_id, &response));
  response.message.data[0] = 'X';

  void * client_handle = client->data;
  rmw_clients_t clients;
  clients.client_count = 1;
  clients.clients = &client_handle;
  ASSERT_EQ(RMW_RET_OK, rmw_wait(nullptr, nullptr, nullptr, &clients, nullptr, wait_set, &timeout));

  std_srvs__srv__Trigger_Response received_response;
  ASSERT_TRUE(std_srvs__srv__Trigger_Response__init(&received_response));
  rmw_service_info_t response_header{};
  bool response_taken = false;
  ASSERT_EQ(
    RMW_RET_OK, rmw_take_response(client, &response_header, &received_response, &response_taken));
  ASSERT_TRUE(response_taken);
  EXPECT_EQ(sequence_id, response_header.request_id.sequence_number);
  EXPECT_TRUE(received_response.success);
  EXPECT_FALSE(received_response.message.data == response.message.data);
  EXPECT_STREQ(original_message, received_response.message.data);

  if (received_response.message.data == response.message.data) {
    received_response.message.data = nullptr;
    received_response.message.size = 0;
    received_response.message.capacity = 0;
  }
  std_srvs__srv__Trigger_Response__fini(&received_response);
  std_srvs__srv__Trigger_Response__fini(&response);
  std_srvs__srv__Trigger_Request__fini(&received_request);
  std_srvs__srv__Trigger_Request__fini(&request);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(wait_set));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}

TEST(RmwMddsService, RequestAndResponseCallbacksReportUnreadSamples)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, allocator));
  SetEnclave(&options, "/rmw_mdds_service_callback_test");

  rmw_context_t context = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
  rmw_node_t * node = rmw_create_node(&context, "mdds_service_callback_node", "/mdds");
  ASSERT_NE(nullptr, node);

  const rosidl_service_type_support_t * type_support =
    ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
      rosidl_typesupport_cpp, example_interfaces, srv, AddTwoInts)();
  rmw_service_t * service =
    rmw_create_service(node, type_support, "/mdds_callback_add_two_ints", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, service);
  rmw_client_t * client =
    rmw_create_client(node, type_support, "/mdds_callback_add_two_ints", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, client);

  AddTwoIntsRequest first_request{1, 2};
  AddTwoIntsRequest second_request{3, 4};
  AddTwoIntsRequest third_request{5, 6};
  int64_t first_sequence_id = -1;
  int64_t second_sequence_id = -1;
  int64_t third_sequence_id = -1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &first_request, &first_sequence_id));
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &second_request, &second_sequence_id));

  size_t request_callback_count = 0;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_service_set_on_new_request_callback(service, CountEventCallback, &request_callback_count));
  EXPECT_EQ(2u, request_callback_count);

  ASSERT_EQ(RMW_RET_OK, rmw_send_request(client, &third_request, &third_sequence_id));
  EXPECT_EQ(3u, request_callback_count);

  AddTwoIntsRequest received_first_request{0, 0};
  AddTwoIntsRequest received_second_request{0, 0};
  AddTwoIntsRequest received_third_request{0, 0};
  rmw_service_info_t first_request_header{};
  rmw_service_info_t second_request_header{};
  rmw_service_info_t third_request_header{};
  bool request_taken = false;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_request(
      service, &first_request_header, &received_first_request, &request_taken));
  ASSERT_TRUE(request_taken);
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_request(
      service, &second_request_header, &received_second_request, &request_taken));
  ASSERT_TRUE(request_taken);
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_take_request(
      service, &third_request_header, &received_third_request, &request_taken));
  ASSERT_TRUE(request_taken);
  EXPECT_EQ(first_sequence_id, first_request_header.request_id.sequence_number);
  EXPECT_EQ(second_sequence_id, second_request_header.request_id.sequence_number);
  EXPECT_EQ(third_sequence_id, third_request_header.request_id.sequence_number);

  AddTwoIntsResponse first_response{received_first_request.a + received_first_request.b};
  AddTwoIntsResponse second_response{received_second_request.a + received_second_request.b};
  AddTwoIntsResponse third_response{received_third_request.a + received_third_request.b};
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &first_request_header.request_id, &first_response));
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &second_request_header.request_id, &second_response));

  size_t response_callback_count = 0;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_client_set_on_new_response_callback(client, CountEventCallback, &response_callback_count));
  EXPECT_EQ(2u, response_callback_count);

  ASSERT_EQ(RMW_RET_OK, rmw_send_response(service, &third_request_header.request_id, &third_response));
  EXPECT_EQ(3u, response_callback_count);

  ASSERT_EQ(RMW_RET_OK, rmw_service_set_on_new_request_callback(service, nullptr, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_client_set_on_new_response_callback(client, nullptr, nullptr));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, client));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, service));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options));
}
