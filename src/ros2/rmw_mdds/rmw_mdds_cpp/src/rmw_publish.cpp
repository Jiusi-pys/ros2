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

#include "bridge_backend.hpp"
#include "broker.hpp"
#include "context.hpp"
#include "ipc_client.hpp"
#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw/serialized_message.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace
{
int64_t ToRtpsSequenceNumber(uint64_t publication_sequence_number)
{
  constexpr uint64_t max_rtps_sequence_number =
    static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  if (publication_sequence_number > max_rtps_sequence_number) {
    return std::numeric_limits<int64_t>::max();
  }
  return static_cast<int64_t>(publication_sequence_number);
}

rmw_ret_t PublishRtpsUserData(
  rmw_mdds_cpp::PublisherData * data, const std::vector<uint8_t> & payload,
  uint64_t publication_sequence_number)
{
  if (
    data == nullptr || data->context == nullptr || data->context->impl == nullptr ||
    data->context->impl->rtps_participant == nullptr || !data->adapter.IsValid()) {
    return RMW_RET_OK;
  }

  const std::string rtps_topic_name = rmw_mdds_cpp::ToRtpsTopicName(data->topic_name.c_str());
  if (rtps_topic_name.empty() || data->adapter.WireTypeName().empty()) {
    return RMW_RET_OK;
  }

  auto * participant = data->context->impl->rtps_participant.get();
  const auto matches = participant->GetMatchedRemoteSedpEndpoints(
    rtps_topic_name, data->adapter.WireTypeName(),
    rmw_mdds_cpp::rtps::SedpEndpointKind::kSubscription);
  if (std::getenv("RMW_MDDS_RTPS_DEBUG_DISCOVERY") != nullptr) {
    std::fprintf(
      stderr,
      "[mdds-publish] topic=%s type=%s matches=%zu\n",
      rtps_topic_name.c_str(), data->adapter.WireTypeName().c_str(), matches.size());
  }
  std::string last_error;
  for (const auto & match : matches) {
    if (std::getenv("RMW_MDDS_RTPS_DEBUG_DISCOVERY") != nullptr) {
      std::fprintf(
        stderr,
        "[mdds-publish] send remote=%s:%u reader=%02x%02x%02x%02x\n",
        match.user_data_endpoint.address.c_str(), match.user_data_endpoint.port,
        match.endpoint_entity_id[0], match.endpoint_entity_id[1],
        match.endpoint_entity_id[2], match.endpoint_entity_id[3]);
    }
    std::string send_error;
    if (!participant->SendUserDataMessage(
        match.user_data_endpoint, match.endpoint_entity_id, data->rtps_entity_id, payload,
        ToRtpsSequenceNumber(publication_sequence_number), &send_error)) {
      last_error = send_error;
    }
  }
  if (!last_error.empty()) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("RTPS user DATA publish failed: %s", last_error.c_str());
    return RMW_RET_ERROR;
  }
  return RMW_RET_OK;
}

rmw_ret_t PublishPayload(rmw_mdds_cpp::PublisherData * data, const std::vector<uint8_t> & payload)
{
  if (data == nullptr) {
    RMW_SET_ERROR_MSG("publisher data is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  // A write satisfies the offered-deadline period for this publisher.
  rmw_mdds_cpp::NotePublisherPublication(data, rmw_mdds_cpp::MddsNowNanoseconds());
  if (data->broker_client != nullptr) {
    const uint64_t publication_sequence_number =
      rmw_mdds_cpp::ReservePublicationSequenceNumber(data);
    std::string error;
    if (!rmw_mdds_cpp::BrokerClientPublish(
        data->broker_client, payload, publication_sequence_number, &error,
        rmw_mdds_cpp::BrokerBridgePayloadEnabled())) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("MDDS broker publish failed: %s", error.c_str());
      return RMW_RET_ERROR;
    }
    return RMW_RET_OK;
  }
  if (data->bridge_publisher != nullptr) {
    int32_t ret = rmw_mdds_cpp::BridgeBackend::Instance().Publish(
      data->bridge_publisher, payload.data(), static_cast<uint32_t>(payload.size()));
    if (ret != 0) {
      RMW_SET_ERROR_MSG("MddsBridgePublish failed");
      return RMW_RET_ERROR;
    }
    if (data->actual_qos.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE) {
      std::lock_guard<std::mutex> lock(data->mutex);
      data->bridge_reliable_publication_unacknowledged = true;
    }
  } else {
    const uint64_t publication_sequence_number =
      rmw_mdds_cpp::ReservePublicationSequenceNumber(data);
    rmw_ret_t ret = PublishRtpsUserData(data, payload, publication_sequence_number);
    if (ret != RMW_RET_OK) {
      return ret;
    }
    rmw_mdds_cpp::PublishToSubscriptions(data, payload, publication_sequence_number);
  }
  return RMW_RET_OK;
}
}  // namespace

extern "C" {
rmw_ret_t rmw_publish(
  const rmw_publisher_t * publisher, const void * ros_message,
  rmw_publisher_allocation_t * allocation)
{
  (void)allocation;
  if (publisher == nullptr || ros_message == nullptr) {
    RMW_SET_ERROR_MSG("publish argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(publisher->implementation_identifier)) {
    RMW_SET_ERROR_MSG("publisher implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  auto * data = static_cast<rmw_mdds_cpp::PublisherData *>(publisher->data);
  if (data == nullptr) {
    RMW_SET_ERROR_MSG("publisher data is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!data->adapter.IsValid()) {
    return RMW_RET_OK;
  }

  std::vector<uint8_t> payload;
  const bool use_mdds_payload =
    data->bridge_publisher != nullptr ||
    (data->broker_client != nullptr && rmw_mdds_cpp::BrokerBridgePayloadEnabled());
  const bool encoded = use_mdds_payload ?
    data->adapter.EncodeMdds(ros_message, &payload) :
    data->adapter.Encode(ros_message, &payload);
  if (!encoded) {
    RMW_SET_ERROR_MSG("failed to encode message");
    return RMW_RET_ERROR;
  }
  return PublishPayload(data, payload);
}

rmw_ret_t rmw_publish_serialized_message(
  const rmw_publisher_t * publisher, const rmw_serialized_message_t * serialized_message,
  rmw_publisher_allocation_t * allocation)
{
  (void)allocation;
  if (publisher == nullptr || serialized_message == nullptr) {
    RMW_SET_ERROR_MSG("publish serialized argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(publisher->implementation_identifier)) {
    RMW_SET_ERROR_MSG("publisher implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  if (serialized_message->buffer == nullptr && serialized_message->buffer_length != 0) {
    RMW_SET_ERROR_MSG("serialized message buffer is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto * data = static_cast<rmw_mdds_cpp::PublisherData *>(publisher->data);
  if (data == nullptr) {
    RMW_SET_ERROR_MSG("publisher data is null");
    return RMW_RET_INVALID_ARGUMENT;
  }

  std::vector<uint8_t> payload;
  const bool use_mdds_payload =
    data->bridge_publisher != nullptr ||
    (data->broker_client != nullptr && rmw_mdds_cpp::BrokerBridgePayloadEnabled());
  if (use_mdds_payload && data->adapter.IsValid()) {
    if (!data->adapter.SerializedToMddsPayload(
        serialized_message->buffer, serialized_message->buffer_length, &payload)) {
      RMW_SET_ERROR_MSG("failed to convert serialized message to MDDS bridge payload");
      return RMW_RET_ERROR;
    }
  } else if (serialized_message->buffer_length != 0) {
    payload.assign(
      serialized_message->buffer,
      serialized_message->buffer + serialized_message->buffer_length);
  }
  return PublishPayload(data, payload);
}
}  // extern "C"
