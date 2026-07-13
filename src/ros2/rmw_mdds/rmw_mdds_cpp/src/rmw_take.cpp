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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "bridge_backend.hpp"
#include "broker.hpp"
#include "context.hpp"
#include "ipc_client.hpp"
#include "rmw_mdds_cpp/identifier.hpp"
#include "rcutils/time.h"
#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw/serialized_message.h"

extern "C" {
rmw_ret_t TryTakeBridgeLoanedMessageCopy(
  rmw_mdds_cpp::SubscriptionData * data, void * message, bool * taken,
  rmw_message_info_t * message_info, bool * attempted);
}

namespace
{
// DDS lifespan QoS: a sample expires `lifespan` after its source timestamp and must
// not be delivered once expired. {0,0} (unset default) and RMW_DURATION_INFINITE both
// mean "no expiry". Returns true only when a finite lifespan is set and exceeded.
bool IsSampleExpiredByLifespan(
  const rmw_qos_profile_t & qos, const rmw_message_info_t & info)
{
  const rmw_time_t lifespan = qos.lifespan;
  constexpr uint64_t kInfiniteSec = 9223372036ULL;  // RMW_DURATION_INFINITE.sec
  if ((lifespan.sec == 0 && lifespan.nsec == 0) || lifespan.sec >= kInfiniteSec) {
    return false;
  }
  if (info.source_timestamp <= 0) {
    return false;  // unknown publish time — cannot enforce, deliver the sample
  }
  rcutils_time_point_value_t now = 0;
  if (rcutils_system_time_now(&now) != RCUTILS_RET_OK) {
    return false;
  }
  const int64_t lifespan_ns =
    static_cast<int64_t>(lifespan.sec) * 1000000000LL + static_cast<int64_t>(lifespan.nsec);
  return (now - static_cast<int64_t>(info.source_timestamp)) > lifespan_ns;
}

// Take the next queued sample that has not expired by lifespan, discarding any expired
// ones along the way. Returns false when the queue drains without a deliverable sample.
bool TakeNextLiveSample(rmw_mdds_cpp::SubscriptionData * data, rmw_mdds_cpp::QueuedSample * sample)
{
  while (rmw_mdds_cpp::TakeQueuedSample(data, sample)) {
    if (!IsSampleExpiredByLifespan(data->actual_qos, sample->info)) {
      return true;
    }
  }
  return false;
}

bool ReturnBrokerLoan(
  rmw_mdds_cpp::SubscriptionData * data, uint64_t loan_id, const char * action)
{
  std::string error;
  if (
    data != nullptr && data->broker_client != nullptr &&
    rmw_mdds_cpp::BrokerClientReturnLoan(data->broker_client, loan_id, &error))
  {
    return true;
  }
  RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("%s: %s", action, error.c_str());
  return false;
}

bool TakeNextLiveBrokerLoan(
  rmw_mdds_cpp::SubscriptionData * data, rmw_mdds_cpp::BrokerLoanedSample * sample)
{
  while (rmw_mdds_cpp::TakeBrokerLoanedSample(data, sample)) {
    if (!IsSampleExpiredByLifespan(data->actual_qos, sample->info)) {
      return true;
    }
    rmw_mdds_cpp::DestroyBrokerLoanedSampleMessage(data, sample);
    std::string ignored_error;
    (void)rmw_mdds_cpp::BrokerClientReturnLoan(
      data->broker_client, sample->loan_id, &ignored_error);
  }
  return false;
}

rmw_time_point_value_t BridgeReceivedTimestamp(rmw_time_point_value_t source_timestamp)
{
  rcutils_time_point_value_t now = 0;
  if (rcutils_system_time_now(&now) == RCUTILS_RET_OK && now > 0) {
    return now;
  }
  return source_timestamp;
}

rmw_message_info_t MakeBridgeLoanedMessageInfo(
  rmw_mdds_cpp::SubscriptionData * data, const rmw_mdds_cpp::BridgeLoanedMessage & bridge_message)
{
  rmw_message_info_t info = rmw_get_zero_initialized_message_info();
  info.publication_sequence_number = bridge_message.sequenceNumber;
  info.publisher_gid.implementation_identifier = rmw_mdds_cpp_identifier;
  std::memcpy(
    info.publisher_gid.data, bridge_message.senderGuid,
    std::min(sizeof(bridge_message.senderGuid), sizeof(info.publisher_gid.data)));
  info.source_timestamp = static_cast<rmw_time_point_value_t>(bridge_message.timestamp);
  info.received_timestamp = BridgeReceivedTimestamp(info.source_timestamp);
  info.from_intra_process = false;

  std::lock_guard<std::mutex> lock(data->mutex);
  info.reception_sequence_number = data->next_reception_sequence_number++;
  data->requested_deadline_last_active_ns = static_cast<int64_t>(info.received_timestamp);
  return info;
}

rmw_ret_t StoreSerializedPayload(
  const std::vector<uint8_t> & payload, rmw_serialized_message_t * serialized_message)
{
  const size_t payload_size = payload.size();
  if (payload_size > serialized_message->buffer_capacity) {
    if (rmw_serialized_message_resize(serialized_message, payload_size) != RCUTILS_RET_OK) {
      RMW_SET_ERROR_MSG("failed to resize serialized message buffer");
      return RMW_RET_ERROR;
    }
  }
  if (payload_size != 0) {
    if (serialized_message->buffer == nullptr) {
      RMW_SET_ERROR_MSG("serialized message buffer is null");
      return RMW_RET_INVALID_ARGUMENT;
    }
    std::memcpy(serialized_message->buffer, payload.data(), payload_size);
  }
  serialized_message->buffer_length = payload_size;
  return RMW_RET_OK;
}

rmw_ret_t TryTakeBridgeLoanedSerializedMessage(
  rmw_mdds_cpp::SubscriptionData * data, rmw_serialized_message_t * serialized_message,
  bool * taken, rmw_message_info_t * message_info, bool * attempted)
{
  *attempted = false;
  if (
    data == nullptr || data->bridge_subscription == nullptr || !data->adapter.IsValid() ||
    rmw_mdds_cpp::HasQueuedSample(data)) {
    return RMW_RET_OK;
  }

  rmw_mdds_cpp::BridgeLoanedMessage bridge_message{};
  while (rmw_mdds_cpp::BridgeBackend::Instance().SubscriberTakeLoaned(
      data->bridge_subscription, &bridge_message)) {
    *attempted = true;
    if (bridge_message.data == nullptr && bridge_message.len != 0u) {
      (void)rmw_mdds_cpp::BridgeBackend::Instance().SubscriberReturnLoaned(
        data->bridge_subscription, &bridge_message);
      RMW_SET_ERROR_MSG("bridge loaned message payload is null");
      return RMW_RET_ERROR;
    }

    std::vector<uint8_t> mdds_payload;
    if (bridge_message.data != nullptr && bridge_message.len != 0u) {
      const auto * begin = static_cast<const uint8_t *>(bridge_message.data);
      mdds_payload.assign(begin, begin + bridge_message.len);
    }
    bool matches_filter = true;
    {
      std::lock_guard<std::mutex> lock(data->mutex);
      matches_filter = rmw_mdds_cpp::PayloadMatchesContentFilter(*data, mdds_payload, true);
    }
    if (!matches_filter) {
      (void)rmw_mdds_cpp::BridgeBackend::Instance().SubscriberReturnLoaned(
        data->bridge_subscription, &bridge_message);
      bridge_message = {};
      continue;
    }

    std::vector<uint8_t> payload;
    const bool converted = data->adapter.MddsPayloadToSerialized(
      static_cast<const uint8_t *>(bridge_message.data), bridge_message.len, &payload);
    const bool returned = rmw_mdds_cpp::BridgeBackend::Instance().SubscriberReturnLoaned(
      data->bridge_subscription, &bridge_message);
    if (!converted) {
      RMW_SET_ERROR_MSG("failed to convert bridge loaned payload to serialized message");
      return RMW_RET_ERROR;
    }
    if (!returned) {
      RMW_SET_ERROR_MSG("MddsBridgeSubscriberReturnLoaned failed");
      return RMW_RET_ERROR;
    }

    rmw_ret_t ret = StoreSerializedPayload(payload, serialized_message);
    if (ret != RMW_RET_OK) {
      return ret;
    }
    if (message_info != nullptr) {
      *message_info = MakeBridgeLoanedMessageInfo(data, bridge_message);
    }
    *taken = true;
    return RMW_RET_OK;
  }

  return RMW_RET_OK;
}

rmw_ret_t TakeSample(
  const rmw_subscription_t * subscription, void * ros_message, bool * taken,
  rmw_message_info_t * message_info)
{
  if (subscription == nullptr || ros_message == nullptr || taken == nullptr) {
    RMW_SET_ERROR_MSG("take argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(subscription->implementation_identifier)) {
    RMW_SET_ERROR_MSG("subscription implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  if (message_info != nullptr) {
    *message_info = rmw_get_zero_initialized_message_info();
  }
  auto * data = static_cast<rmw_mdds_cpp::SubscriptionData *>(subscription->data);
  rmw_mdds_cpp::BrokerLoanedSample broker_sample;
  if (TakeNextLiveBrokerLoan(data, &broker_sample)) {
    const bool decoded = broker_sample.from_bridge ?
      data->adapter.DecodeMdds(broker_sample.data, broker_sample.len, ros_message) :
      data->adapter.Decode(broker_sample.data, broker_sample.len, ros_message);
    const rmw_message_info_t info = broker_sample.info;
    rmw_mdds_cpp::DestroyBrokerLoanedSampleMessage(data, &broker_sample);
    const bool returned = ReturnBrokerLoan(
      data, broker_sample.loan_id, "failed to return broker loan after take");
    if (!decoded) {
      RMW_SET_ERROR_MSG("failed to decode broker loaned message");
      return RMW_RET_ERROR;
    }
    if (!returned) {
      return RMW_RET_ERROR;
    }
    if (message_info != nullptr) {
      *message_info = info;
    }
    *taken = true;
    return RMW_RET_OK;
  }
  rmw_mdds_cpp::QueuedSample sample;
  if (!TakeNextLiveSample(data, &sample)) {
    bool attempted_bridge_loaned = false;
    rmw_ret_t ret = TryTakeBridgeLoanedMessageCopy(
      data, ros_message, taken, message_info, &attempted_bridge_loaned);
    if (attempted_bridge_loaned) {
      return ret;
    }
    *taken = false;
    return RMW_RET_OK;
  }
  const bool decoded = sample.from_bridge ?
    data->adapter.DecodeMdds(sample.payload.data(), sample.payload.size(), ros_message) :
    data->adapter.Decode(sample.payload.data(), sample.payload.size(), ros_message);
  if (!decoded) {
    RMW_SET_ERROR_MSG("failed to decode message");
    return RMW_RET_ERROR;
  }
  if (message_info != nullptr) {
    *message_info = sample.info;
  }
  *taken = true;
  return RMW_RET_OK;
}

rmw_ret_t TakeSerializedSample(
  const rmw_subscription_t * subscription, rmw_serialized_message_t * serialized_message,
  bool * taken, rmw_message_info_t * message_info)
{
  if (subscription == nullptr || serialized_message == nullptr || taken == nullptr) {
    RMW_SET_ERROR_MSG("take serialized argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(subscription->implementation_identifier)) {
    RMW_SET_ERROR_MSG("subscription implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  if (message_info != nullptr) {
    *message_info = rmw_get_zero_initialized_message_info();
  }
  *taken = false;

  auto * data = static_cast<rmw_mdds_cpp::SubscriptionData *>(subscription->data);
  rmw_mdds_cpp::BrokerLoanedSample broker_sample;
  if (TakeNextLiveBrokerLoan(data, &broker_sample)) {
    std::vector<uint8_t> payload;
    bool converted = true;
    if (broker_sample.from_bridge && data != nullptr && data->adapter.IsValid()) {
      converted = data->adapter.MddsPayloadToSerialized(
        broker_sample.data, broker_sample.len, &payload);
    } else {
      payload.assign(broker_sample.data, broker_sample.data + broker_sample.len);
    }
    const rmw_message_info_t info = broker_sample.info;
    rmw_mdds_cpp::DestroyBrokerLoanedSampleMessage(data, &broker_sample);
    const bool returned = ReturnBrokerLoan(
      data, broker_sample.loan_id, "failed to return broker loan after serialized take");
    if (!converted) {
      RMW_SET_ERROR_MSG("failed to convert broker loaned payload to serialized message");
      return RMW_RET_ERROR;
    }
    if (!returned) {
      return RMW_RET_ERROR;
    }
    rmw_ret_t ret = StoreSerializedPayload(payload, serialized_message);
    if (ret != RMW_RET_OK) {
      return ret;
    }
    if (message_info != nullptr) {
      *message_info = info;
    }
    *taken = true;
    return RMW_RET_OK;
  }
  rmw_mdds_cpp::QueuedSample sample;
  if (!TakeNextLiveSample(data, &sample)) {
    bool attempted_bridge_loaned = false;
    rmw_ret_t ret = TryTakeBridgeLoanedSerializedMessage(
      data, serialized_message, taken, message_info, &attempted_bridge_loaned);
    if (attempted_bridge_loaned) {
      return ret;
    }
    serialized_message->buffer_length = 0;
    return RMW_RET_OK;
  }
  std::vector<uint8_t> payload = sample.payload;
  if (sample.from_bridge && data != nullptr && data->adapter.IsValid()) {
    if (!data->adapter.MddsPayloadToSerialized(
        sample.payload.data(), sample.payload.size(), &payload)) {
      RMW_SET_ERROR_MSG("failed to convert MDDS bridge payload to serialized message");
      return RMW_RET_ERROR;
    }
  }
  rmw_ret_t ret = StoreSerializedPayload(payload, serialized_message);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  if (message_info != nullptr) {
    *message_info = sample.info;
  }
  *taken = true;
  return RMW_RET_OK;
}
}  // namespace

extern "C" {
rmw_ret_t rmw_take(
  const rmw_subscription_t * subscription, void * ros_message, bool * taken,
  rmw_subscription_allocation_t * allocation)
{
  (void)allocation;
  return TakeSample(subscription, ros_message, taken, nullptr);
}

rmw_ret_t rmw_take_with_info(
  const rmw_subscription_t * subscription, void * ros_message, bool * taken,
  rmw_message_info_t * message_info, rmw_subscription_allocation_t * allocation)
{
  if (message_info == nullptr) {
    RMW_SET_ERROR_MSG("message info is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  (void)allocation;
  return TakeSample(subscription, ros_message, taken, message_info);
}

rmw_ret_t rmw_take_serialized_message(
  const rmw_subscription_t * subscription, rmw_serialized_message_t * serialized_message,
  bool * taken, rmw_subscription_allocation_t * allocation)
{
  (void)allocation;
  return TakeSerializedSample(subscription, serialized_message, taken, nullptr);
}

rmw_ret_t rmw_take_serialized_message_with_info(
  const rmw_subscription_t * subscription, rmw_serialized_message_t * serialized_message,
  bool * taken, rmw_message_info_t * message_info, rmw_subscription_allocation_t * allocation)
{
  if (message_info == nullptr) {
    RMW_SET_ERROR_MSG("message info is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  (void)allocation;
  return TakeSerializedSample(subscription, serialized_message, taken, message_info);
}
}  // extern "C"
