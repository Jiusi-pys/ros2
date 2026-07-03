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

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <unistd.h>

#include "bridge_backend.hpp"
#include "context.hpp"
#include "ipc_client.hpp"
#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/discovery_options.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/security_options.h"
#include "rmw_mdds_cpp/identifier.hpp"

namespace
{
constexpr const char * kRtpsBindAddressEnv = "RMW_MDDS_RTPS_BIND_ADDRESS";
constexpr const char * kRtpsAdvertisedAddressEnv = "RMW_MDDS_RTPS_ADVERTISED_ADDRESS";
constexpr const char * kRtpsPortBaseEnv = "RMW_MDDS_RTPS_PORT_BASE";
constexpr const char * kRtpsSpdpPeersEnv = "RMW_MDDS_RTPS_SPDP_PEERS";
constexpr const char * kRtpsSpdpPeriodMsEnv = "RMW_MDDS_RTPS_SPDP_PERIOD_MS";
constexpr const char * kRtpsSpdpReceiveTimeoutMsEnv =
  "RMW_MDDS_RTPS_SPDP_RECEIVE_TIMEOUT_MS";

void SetError(std::string * error, const std::string & message)
{
  if (error != nullptr) {
    *error = message;
  }
}

bool ReadUint16Env(const char * name, uint16_t * value, std::string * error)
{
  if (name == nullptr || value == nullptr) {
    SetError(error, "RTPS uint16 environment output is null");
    return false;
  }
  const char * text = std::getenv(name);
  if (text == nullptr || text[0] == '\0') {
    return true;
  }

  char * end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(text, &end, 10);
  if (
    errno != 0 || end == text || *end != '\0' ||
    parsed > std::numeric_limits<uint16_t>::max()) {
    SetError(error, std::string(name) + " must be an integer in the UDP port range");
    return false;
  }
  *value = static_cast<uint16_t>(parsed);
  return true;
}

bool ReadUint32Env(const char * name, uint32_t * value, std::string * error)
{
  if (name == nullptr || value == nullptr) {
    SetError(error, "RTPS uint32 environment output is null");
    return false;
  }
  const char * text = std::getenv(name);
  if (text == nullptr || text[0] == '\0') {
    return true;
  }

  char * end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(text, &end, 10);
  if (
    errno != 0 || end == text || *end != '\0' ||
    parsed > std::numeric_limits<uint32_t>::max()) {
    SetError(error, std::string(name) + " must be an integer in the uint32 range");
    return false;
  }
  *value = static_cast<uint32_t>(parsed);
  return true;
}

bool ParsePort(const std::string & text, uint16_t * port, std::string * error)
{
  if (port == nullptr) {
    SetError(error, "RTPS peer port output is null");
    return false;
  }
  if (text.empty()) {
    SetError(error, "RTPS peer port is empty");
    return false;
  }

  char * end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
  if (
    errno != 0 || end == text.c_str() || *end != '\0' ||
    parsed > std::numeric_limits<uint16_t>::max()) {
    SetError(error, "RTPS peer port must be an integer in the UDP port range");
    return false;
  }
  *port = static_cast<uint16_t>(parsed);
  return true;
}

bool ParseSpdpPeers(std::vector<rmw_mdds_cpp::rtps::UdpEndpoint> * peers, std::string * error)
{
  if (peers == nullptr) {
    SetError(error, "RTPS SPDP peer output is null");
    return false;
  }
  const char * text = std::getenv(kRtpsSpdpPeersEnv);
  if (text == nullptr || text[0] == '\0') {
    return true;
  }

  std::string value(text);
  size_t start = 0u;
  while (start <= value.size()) {
    const size_t comma = value.find(',', start);
    const std::string item = value.substr(
      start, comma == std::string::npos ? std::string::npos : comma - start);
    if (item.empty()) {
      SetError(error, std::string(kRtpsSpdpPeersEnv) + " contains an empty endpoint");
      return false;
    }

    const size_t colon = item.rfind(':');
    if (colon == std::string::npos || colon == 0u || colon + 1u >= item.size()) {
      SetError(error, std::string(kRtpsSpdpPeersEnv) + " entries must use host:port");
      return false;
    }

    uint16_t port = 0u;
    if (!ParsePort(item.substr(colon + 1u), &port, error)) {
      return false;
    }
    peers->push_back(rmw_mdds_cpp::rtps::UdpEndpoint{item.substr(0u, colon), port});

    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1u;
  }
  return true;
}

rmw_mdds_cpp::rtps::GuidPrefix MakeGuidPrefix(uint32_t domain_id, uint32_t participant_id)
{
  const uint32_t pid = static_cast<uint32_t>(getpid());
  rmw_mdds_cpp::rtps::GuidPrefix prefix{};
  prefix[0] = static_cast<uint8_t>('M');
  prefix[1] = static_cast<uint8_t>('D');
  prefix[2] = static_cast<uint8_t>('D');
  prefix[3] = static_cast<uint8_t>('S');
  prefix[4] = static_cast<uint8_t>((domain_id >> 8u) & 0xffu);
  prefix[5] = static_cast<uint8_t>(domain_id & 0xffu);
  prefix[6] = static_cast<uint8_t>((pid >> 24u) & 0xffu);
  prefix[7] = static_cast<uint8_t>((pid >> 16u) & 0xffu);
  prefix[8] = static_cast<uint8_t>((pid >> 8u) & 0xffu);
  prefix[9] = static_cast<uint8_t>(pid & 0xffu);
  prefix[10] = static_cast<uint8_t>((participant_id >> 8u) & 0xffu);
  prefix[11] = static_cast<uint8_t>(participant_id & 0xffu);
  return prefix;
}

bool BuildRtpsParticipantConfig(
  const rmw_init_options_t & options, uint32_t domain_id,
  rmw_mdds_cpp::rtps::ParticipantConfig * config, std::string * error)
{
  if (config == nullptr) {
    SetError(error, "RTPS participant config output is null");
    return false;
  }
  if (options.instance_id > std::numeric_limits<uint32_t>::max()) {
    SetError(error, "RMW instance id exceeds RTPS participant id range");
    return false;
  }

  rmw_mdds_cpp::rtps::ParticipantConfig result;
  result.domain_id = domain_id;
  result.participant_id = static_cast<uint32_t>(options.instance_id);
  result.guid_prefix = MakeGuidPrefix(result.domain_id, result.participant_id);

  const char * bind_address = std::getenv(kRtpsBindAddressEnv);
  if (bind_address != nullptr && bind_address[0] != '\0') {
    result.bind_address = bind_address;
  }
  const char * advertised_address = std::getenv(kRtpsAdvertisedAddressEnv);
  if (advertised_address != nullptr && advertised_address[0] != '\0') {
    result.advertised_address = advertised_address;
  } else {
    result.advertised_address = result.bind_address;
  }
  if (!ReadUint16Env(kRtpsPortBaseEnv, &result.port_mapping.port_base, error)) {
    return false;
  }
  if (!ParseSpdpPeers(&result.spdp_peer_endpoints, error)) {
    return false;
  }
  if (!ReadUint32Env(
      kRtpsSpdpPeriodMsEnv, &result.spdp_announcement_period_ms, error)) {
    return false;
  }
  if (!ReadUint32Env(
      kRtpsSpdpReceiveTimeoutMsEnv, &result.spdp_receive_timeout_ms, error)) {
    return false;
  }
  if (
    !result.spdp_peer_endpoints.empty() &&
    result.spdp_announcement_period_ms == 0u) {
    SetError(error, std::string(kRtpsSpdpPeriodMsEnv) + " must be greater than zero");
    return false;
  }
  if (result.spdp_receive_timeout_ms == 0u) {
    SetError(error, std::string(kRtpsSpdpReceiveTimeoutMsEnv) + " must be greater than zero");
    return false;
  }
  if (
    result.spdp_receive_timeout_ms >
    static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    SetError(error, std::string(kRtpsSpdpReceiveTimeoutMsEnv) + " exceeds poll timeout range");
    return false;
  }

  *config = result;
  return true;
}
}  // namespace

extern "C" {
rmw_ret_t rmw_init_options_init(rmw_init_options_t * init_options, rcutils_allocator_t allocator)
{
  if (init_options == nullptr) {
    RMW_SET_ERROR_MSG("init options is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  RCUTILS_CHECK_ALLOCATOR(&allocator, return RMW_RET_INVALID_ARGUMENT);
  if (init_options->implementation_identifier != nullptr) {
    RMW_SET_ERROR_MSG("expected zero-initialized init options");
    return RMW_RET_INVALID_ARGUMENT;
  }

  rmw_init_options_t tmp = rmw_get_zero_initialized_init_options();
  tmp.instance_id = 0;
  tmp.implementation_identifier = rmw_mdds_cpp_identifier;
  tmp.domain_id = RMW_DEFAULT_DOMAIN_ID;
  tmp.security_options = rmw_get_default_security_options();
  tmp.localhost_only = RMW_LOCALHOST_ONLY_DEFAULT;
  tmp.discovery_options = rmw_get_zero_initialized_discovery_options();
  tmp.enclave = nullptr;
  tmp.allocator = allocator;
  tmp.impl = nullptr;

  rmw_ret_t ret = rmw_discovery_options_init(&tmp.discovery_options, 0, &tmp.allocator);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  *init_options = tmp;
  return RMW_RET_OK;
}

rmw_ret_t rmw_init_options_copy(const rmw_init_options_t * src, rmw_init_options_t * dst)
{
  if (src == nullptr || dst == nullptr) {
    RMW_SET_ERROR_MSG("init options copy argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (src->implementation_identifier == nullptr) {
    RMW_SET_ERROR_MSG("expected initialized source init options");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(src->implementation_identifier)) {
    RMW_SET_ERROR_MSG("source init options implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  if (dst->implementation_identifier != nullptr) {
    RMW_SET_ERROR_MSG("expected zero-initialized destination init options");
    return RMW_RET_INVALID_ARGUMENT;
  }

  rcutils_allocator_t allocator = src->allocator;
  RCUTILS_CHECK_ALLOCATOR(&allocator, return RMW_RET_INVALID_ARGUMENT);

  rmw_init_options_t tmp = *src;
  tmp.enclave = nullptr;
  if (src->enclave != nullptr) {
    tmp.enclave = rcutils_strdup(src->enclave, allocator);
    if (tmp.enclave == nullptr) {
      return RMW_RET_BAD_ALLOC;
    }
  }

  tmp.security_options = rmw_get_zero_initialized_security_options();
  rmw_ret_t ret =
    rmw_security_options_copy(&src->security_options, &allocator, &tmp.security_options);
  if (ret != RMW_RET_OK) {
    allocator.deallocate(tmp.enclave, allocator.state);
    return ret;
  }

  tmp.discovery_options = rmw_get_zero_initialized_discovery_options();
  ret = rmw_discovery_options_copy(&src->discovery_options, &allocator, &tmp.discovery_options);
  if (ret != RMW_RET_OK) {
    allocator.deallocate(tmp.enclave, allocator.state);
    (void)rmw_security_options_fini(&tmp.security_options, &allocator);
    return ret;
  }

  *dst = tmp;
  return RMW_RET_OK;
}

rmw_ret_t rmw_init_options_fini(rmw_init_options_t * init_options)
{
  if (init_options == nullptr) {
    RMW_SET_ERROR_MSG("init options is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (init_options->implementation_identifier == nullptr) {
    RMW_SET_ERROR_MSG("expected initialized init options");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(init_options->implementation_identifier)) {
    RMW_SET_ERROR_MSG("init options implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }

  rcutils_allocator_t allocator = init_options->allocator;
  RCUTILS_CHECK_ALLOCATOR(&allocator, return RMW_RET_INVALID_ARGUMENT);

  allocator.deallocate(init_options->enclave, allocator.state);
  rmw_ret_t security_ret = rmw_security_options_fini(&init_options->security_options, &allocator);
  rmw_ret_t discovery_ret = rmw_discovery_options_fini(&init_options->discovery_options);
  *init_options = rmw_get_zero_initialized_init_options();

  if (security_ret != RMW_RET_OK) {
    return security_ret;
  }
  return discovery_ret;
}

rmw_ret_t rmw_init(const rmw_init_options_t * options, rmw_context_t * context)
{
  if (options == nullptr || context == nullptr) {
    RMW_SET_ERROR_MSG("rmw init argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (options->implementation_identifier == nullptr) {
    RMW_SET_ERROR_MSG("expected initialized init options");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(options->implementation_identifier)) {
    RMW_SET_ERROR_MSG("init options implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  if (options->enclave == nullptr) {
    RMW_SET_ERROR_MSG("expected non-null enclave");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (context->implementation_identifier != nullptr || context->impl != nullptr) {
    RMW_SET_ERROR_MSG("expected zero-initialized context");
    return RMW_RET_INVALID_ARGUMENT;
  }

  rmw_context_t tmp = rmw_get_zero_initialized_context();
  tmp.instance_id = options->instance_id;
  tmp.implementation_identifier = rmw_mdds_cpp_identifier;
  tmp.actual_domain_id = (options->domain_id == RMW_DEFAULT_DOMAIN_ID) ? 0u : options->domain_id;
  tmp.impl = new (std::nothrow) rmw_context_impl_t();
  if (tmp.impl == nullptr) {
    return RMW_RET_BAD_ALLOC;
  }

  std::string security_error;
  if (!rmw_mdds_cpp::LoadSecurityPolicy(tmp.impl, options->security_options, &security_error)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "ROS security enforcement is not supported by rmw_mdds_cpp without a readable policy: %s",
      security_error.c_str());
    delete tmp.impl;
    return RMW_RET_UNSUPPORTED;
  }

  std::string rtps_error;
  if (
    !BuildRtpsParticipantConfig(
      *options, static_cast<uint32_t>(tmp.actual_domain_id),
      &tmp.impl->rtps_participant_config, &rtps_error)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "RTPS participant configuration failed: %s", rtps_error.c_str());
    delete tmp.impl;
    return RMW_RET_INVALID_ARGUMENT;
  }
  // In broker mode the RTPS participant is unused (publish/subscribe go through
  // the broker, availability uses the broker graph). Creating it + its SPDP
  // threads is pure overhead and its teardown crashed clean process exit, so
  // skip it; all RTPS paths null-check rtps_participant.
  const bool broker_mode = rmw_mdds_cpp::BrokerModeEnabled();
  if (!broker_mode) {
    tmp.impl->rtps_participant = rmw_mdds_cpp::rtps::RtpsParticipant::Create(
      tmp.impl->rtps_participant_config, &rtps_error);
    if (tmp.impl->rtps_participant == nullptr) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "RTPS participant creation failed: %s", rtps_error.c_str());
      delete tmp.impl;
      return RMW_RET_ERROR;
    }
    if (!tmp.impl->rtps_participant->StartSpdpReceiver(
        tmp.impl->rtps_participant_config.spdp_receive_timeout_ms, &rtps_error)) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "RTPS SPDP receiver start failed: %s", rtps_error.c_str());
      delete tmp.impl;
      return RMW_RET_ERROR;
    }
    if (!tmp.impl->rtps_participant_config.spdp_peer_endpoints.empty()) {
      if (!tmp.impl->rtps_participant->StartSpdpAnnouncer(
          tmp.impl->rtps_participant_config.spdp_peer_endpoints,
          tmp.impl->rtps_participant_config.spdp_announcement_period_ms, &rtps_error)) {
        RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
          "RTPS SPDP announcer start failed: %s", rtps_error.c_str());
        delete tmp.impl;
        return RMW_RET_ERROR;
      }
    }
  }

  tmp.options = rmw_get_zero_initialized_init_options();
  rmw_ret_t ret = rmw_init_options_copy(options, &tmp.options);
  if (ret != RMW_RET_OK) {
    delete tmp.impl;
    return ret;
  }
  if (!broker_mode &&
    !rmw_mdds_cpp::StartRtpsUserDataReceiver(
      &tmp, tmp.impl->rtps_participant_config.spdp_receive_timeout_ms, &rtps_error)) {
    const rmw_ret_t cleanup_ret = rmw_init_options_fini(&tmp.options);
    if (cleanup_ret != RMW_RET_OK) {
      rmw_reset_error();
    }
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "RTPS user DATA receiver start failed: %s", rtps_error.c_str());
    delete tmp.impl;
    return RMW_RET_ERROR;
  }

  // Count this context BEFORE touching the shared bridge, so a concurrent
  // last-context teardown observes a non-zero count and keeps the broker +
  // bridge alive instead of tearing them down under this initializing context.
  // This is the last fallible point of no return — nothing below can fail —
  // so the increment still pairs 1:1 with exactly one rmw_context_fini.
  rmw_mdds_cpp::NoteContextInitialized();

  (void)rmw_mdds_cpp::BridgeBackend::Instance().Available();

  *context = tmp;
  return RMW_RET_OK;
}

rmw_ret_t rmw_shutdown(rmw_context_t * context)
{
  rmw_ret_t ret = rmw_mdds_cpp::CheckContext(context);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  rmw_mdds_cpp::StopRtpsUserDataReceiver(context);
  context->impl->is_shutdown = true;
  return RMW_RET_OK;
}

rmw_ret_t rmw_context_fini(rmw_context_t * context)
{
  rmw_ret_t ret = rmw_mdds_cpp::CheckContext(context);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (!context->impl->is_shutdown) {
    RMW_SET_ERROR_MSG("context has not been shutdown");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (context->impl->node_count != 0) {
    RMW_SET_ERROR_MSG("context still has active nodes");
    return RMW_RET_ERROR;
  }

  rmw_context_impl_t * impl = context->impl;
  rmw_mdds_cpp::StopRtpsUserDataReceiver(context);
  // Once this is the last live context, quiesce the embedded broker + MDDS bridge
  // runtime while openssl is still alive, so the DSoftBus lane-worker thread
  // cannot race openssl's atexit teardown into a SIGSEGV on clean process exit.
  // No-op while other contexts remain, and outside broker mode.
  rmw_mdds_cpp::ShutdownEmbeddedBrokerIfLastContext();
  rmw_ret_t options_ret = rmw_init_options_fini(&context->options);
  delete impl;
  *context = rmw_get_zero_initialized_context();
  return options_ret;
}
}  // extern "C"
