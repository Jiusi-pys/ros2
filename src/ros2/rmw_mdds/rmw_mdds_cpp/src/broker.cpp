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

#include "broker.hpp"
#include "context.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

#include "rcutils/time.h"
#include "rmw/error_handling.h"
#include "rmw_dds_common/qos.hpp"
#include "rmw_mdds_cpp/identifier.hpp"

namespace rmw_mdds_cpp
{

namespace
{
std::mutex g_broker_mutex;
std::vector<PublisherData *> g_publishers;
std::vector<SubscriptionData *> g_subscriptions;

struct CallbackInvocation
{
  rmw_event_callback_t callback = nullptr;
  const void * user_data = nullptr;
  size_t event_count = 0;
};

rmw_time_point_value_t NowNanoseconds()
{
  rcutils_time_point_value_t now = 0;
  return rcutils_system_time_now(&now) == RCUTILS_RET_OK ? now : 0;
}

rmw_time_point_value_t ReceivedTimestampFor(rmw_time_point_value_t source_timestamp)
{
  const rmw_time_point_value_t received_timestamp = NowNanoseconds();
  return received_timestamp < source_timestamp ? source_timestamp : received_timestamp;
}

bool SameTopicAndType(const PublisherData * publisher, const SubscriptionData * subscription)
{
  return publisher != nullptr && subscription != nullptr &&
         publisher->topic_name == subscription->topic_name &&
         publisher->adapter.TypeName() == subscription->adapter.TypeName();
}

// Identify the QoS policy on which a subscription's request is incompatible with a publisher's offer,
// for the incompatible-QoS event's last_policy_kind. The rmw_dds_common check is authoritative on
// WHETHER the pair is incompatible (a hard ERROR, not just a warning); the policy mapping below is a
// best-effort attribution of which policy caused it. Returns RMW_QOS_POLICY_INVALID when compatible.
rmw_qos_policy_kind_t IncompatibleQosPolicyKind(
  const rmw_qos_profile_t & offered, const rmw_qos_profile_t & requested)
{
  rmw_qos_compatibility_type_t compatibility = RMW_QOS_COMPATIBILITY_OK;
  char reason[2048];
  reason[0] = '\0';
  if (rmw_dds_common::qos_profile_check_compatible(
      offered, requested, &compatibility, reason, sizeof(reason)) != RMW_RET_OK)
  {
    return RMW_QOS_POLICY_INVALID;
  }
  if (compatibility != RMW_QOS_COMPATIBILITY_ERROR) {
    return RMW_QOS_POLICY_INVALID;  // compatible, or only a soft warning
  }
  // A BEST_EFFORT offer cannot satisfy a RELIABLE request.
  if (offered.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT &&
      requested.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE)
  {
    return RMW_QOS_POLICY_RELIABILITY;
  }
  // A VOLATILE offer cannot satisfy a TRANSIENT_LOCAL request.
  if (offered.durability == RMW_QOS_POLICY_DURABILITY_VOLATILE &&
      requested.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL)
  {
    return RMW_QOS_POLICY_DURABILITY;
  }
  // A weaker offered liveliness kind cannot satisfy a stronger request.
  if (offered.liveliness == RMW_QOS_POLICY_LIVELINESS_AUTOMATIC &&
      requested.liveliness == RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC)
  {
    return RMW_QOS_POLICY_LIVELINESS;
  }
  return RMW_QOS_POLICY_DEADLINE;  // remaining hard-incompatibility cause is the deadline period
}

size_t CountMatchingSubscriptionsLocked(const PublisherData * publisher)
{
  return static_cast<size_t>(std::count_if(
    g_subscriptions.begin(), g_subscriptions.end(),
    [publisher](const SubscriptionData * subscription) {
      return SameTopicAndType(publisher, subscription);
    }));
}

size_t CountMatchingPublishersLocked(const SubscriptionData * subscription)
{
  return static_cast<size_t>(std::count_if(
    g_publishers.begin(), g_publishers.end(), [subscription](const PublisherData * publisher) {
      return SameTopicAndType(publisher, subscription);
    }));
}

int32_t CountChange(size_t current_count, size_t previous_count)
{
  const int64_t delta =
    static_cast<int64_t>(current_count) - static_cast<int64_t>(previous_count);
  if (delta > std::numeric_limits<int32_t>::max()) {
    return std::numeric_limits<int32_t>::max();
  }
  if (delta < std::numeric_limits<int32_t>::min()) {
    return std::numeric_limits<int32_t>::min();
  }
  return static_cast<int32_t>(delta);
}

size_t CountDifference(size_t current_count, size_t previous_count)
{
  return current_count >= previous_count ? current_count - previous_count
                                        : previous_count - current_count;
}

size_t PendingMatchedEventCountLocked(
  size_t current_count, size_t total_count, size_t last_total_count, size_t last_current_count)
{
  return std::max(total_count - last_total_count, CountDifference(current_count, last_current_count));
}

void AddCallbackInvocation(
  std::vector<CallbackInvocation> * callbacks, rmw_event_callback_t callback,
  const void * user_data, size_t event_count)
{
  if (callbacks == nullptr || callback == nullptr || event_count == 0) {
    return;
  }
  callbacks->push_back(CallbackInvocation{callback, user_data, event_count});
}

void AddPublisherCallbackInvocation(
  std::vector<CallbackInvocation> * callbacks, const PublisherData * publisher,
  size_t event_count)
{
  if (publisher == nullptr) {
    return;
  }
  AddCallbackInvocation(
    callbacks, publisher->matched_callback, publisher->matched_callback_user_data, event_count);
}

void AddSubscriptionCallbackInvocation(
  std::vector<CallbackInvocation> * callbacks, const SubscriptionData * subscription,
  size_t event_count)
{
  if (subscription == nullptr) {
    return;
  }
  AddCallbackInvocation(
    callbacks, subscription->matched_callback, subscription->matched_callback_user_data,
    event_count);
}

void InvokeCallbacks(const std::vector<CallbackInvocation> & callbacks)
{
  for (const auto & callback : callbacks) {
    callback.callback(callback.user_data, callback.event_count);
  }
}

// On a publisher<->subscription match, if their QoS is incompatible, bump both sides' incompatible-QoS
// counters, record the offending policy, and queue their incompatible callbacks. Caller holds
// g_broker_mutex (incompatible counters are accrued under it, like the matched counters).
void AccrueIncompatibleQosLocked(
  PublisherData * publisher, SubscriptionData * subscription,
  std::vector<CallbackInvocation> * callbacks)
{
  const rmw_qos_policy_kind_t bad =
    IncompatibleQosPolicyKind(publisher->actual_qos, subscription->actual_qos);
  if (bad == RMW_QOS_POLICY_INVALID) {
    return;
  }
  ++publisher->offered_qos_incompatible_total;
  publisher->offered_qos_last_policy_kind = bad;
  ++subscription->requested_qos_incompatible_total;
  subscription->requested_qos_last_policy_kind = bad;
  AddCallbackInvocation(
    callbacks, publisher->offered_qos_incompatible_callback,
    publisher->offered_qos_incompatible_callback_user_data, 1);
  AddCallbackInvocation(
    callbacks, subscription->requested_qos_incompatible_callback,
    subscription->requested_qos_incompatible_callback_user_data, 1);
}

void FillMatchedStatus(
  size_t current_count, size_t total_count, size_t * last_total_count,
  size_t * last_current_count, rmw_matched_status_t * status)
{
  if (last_total_count == nullptr || last_current_count == nullptr || status == nullptr) {
    return;
  }
  status->total_count = total_count;
  status->total_count_change = total_count - *last_total_count;
  status->current_count = current_count;
  status->current_count_change = CountChange(current_count, *last_current_count);
  *last_total_count = total_count;
  *last_current_count = current_count;
}

void AddNameAndType(
  std::vector<NameAndTypes> * names_and_types, const std::string & name, const std::string & type)
{
  if (names_and_types == nullptr || name.empty() || type.empty()) {
    return;
  }
  auto it = std::find_if(
    names_and_types->begin(), names_and_types->end(),
    [&name](const NameAndTypes & entry) { return entry.name == name; });
  if (it == names_and_types->end()) {
    names_and_types->push_back(NameAndTypes{name, {type}});
    return;
  }
  if (std::find(it->types.begin(), it->types.end(), type) == it->types.end()) {
    it->types.push_back(type);
  }
}

bool BelongsToNode(
  const std::string & entity_node_name, const std::string & entity_node_namespace,
  const char * node_name, const char * node_namespace)
{
  return node_name != nullptr && node_namespace != nullptr && entity_node_name == node_name &&
         entity_node_namespace == node_namespace;
}

void FillEndpointGid(
  const void * entity, const std::string & topic_name, uint8_t discriminator, rmw_gid_t * gid)
{
  if (gid == nullptr) {
    return;
  }
  *gid = {};
  gid->implementation_identifier = rmw_mdds_cpp_identifier;
  const uintptr_t address = reinterpret_cast<uintptr_t>(entity);
  std::memcpy(gid->data, &address, std::min(sizeof(address), sizeof(gid->data)));
  if (entity == nullptr) {
    return;
  }
  const size_t topic_size = std::min(topic_name.size(), sizeof(gid->data));
  for (size_t i = 0; i < topic_size; ++i) {
    gid->data[i] ^= static_cast<uint8_t>(topic_name[i]);
  }
  gid->data[sizeof(gid->data) - 1] ^= discriminator;
}

void FillRtpsWriterGid(
  const rtps::GuidPrefix & writer_guid_prefix, const rtps::EntityId & writer_id,
  rmw_gid_t * gid)
{
  if (gid == nullptr) {
    return;
  }
  *gid = {};
  gid->implementation_identifier = rmw_mdds_cpp_identifier;
  std::copy(writer_guid_prefix.begin(), writer_guid_prefix.end(), gid->data);
  std::copy(writer_id.begin(), writer_id.end(), gid->data + writer_guid_prefix.size());
}

TopicEndpointInfo MakePublisherEndpointInfo(const PublisherData * publisher)
{
  TopicEndpointInfo info;
  if (publisher == nullptr) {
    return info;
  }
  info.node_name = publisher->node_name;
  info.node_namespace = publisher->node_namespace;
  info.topic_type = publisher->adapter.TypeName();
  info.endpoint_type = RMW_ENDPOINT_PUBLISHER;
  FillEndpointGid(publisher, publisher->topic_name, 0, &info.gid);
  info.qos_profile = publisher->actual_qos;
  return info;
}

TopicEndpointInfo MakeSubscriptionEndpointInfo(const SubscriptionData * subscription)
{
  TopicEndpointInfo info;
  if (subscription == nullptr) {
    return info;
  }
  info.node_name = subscription->node_name;
  info.node_namespace = subscription->node_namespace;
  info.topic_type = subscription->adapter.TypeName();
  info.endpoint_type = RMW_ENDPOINT_SUBSCRIPTION;
  FillEndpointGid(subscription, subscription->topic_name, 0x5a, &info.gid);
  info.qos_profile = subscription->actual_qos;
  return info;
}

std::string RemoveAsciiWhitespace(const char * expression)
{
  if (expression == nullptr) {
    return {};
  }
  std::string compact;
  for (const unsigned char ch : std::string(expression)) {
    if (!std::isspace(ch)) {
      compact.push_back(static_cast<char>(ch));
    }
  }
  return compact;
}

bool IsSupportedStringContentFilter(
  const SubscriptionData & subscription, const char * filter_expression,
  const rcutils_string_array_t & expression_parameters)
{
  if (subscription.adapter.TypeName() != "std_msgs/msg/String") {
    RMW_SET_ERROR_MSG("content filters are currently supported only for std_msgs/msg/String");
    return false;
  }
  if (RemoveAsciiWhitespace(filter_expression) != "data=%0") {
    RMW_SET_ERROR_MSG("content filter expression is not supported");
    return false;
  }
  if (
    expression_parameters.size != 1 || expression_parameters.data == nullptr ||
    expression_parameters.data[0] == nullptr) {
    RMW_SET_ERROR_MSG("content filter expression requires one parameter");
    return false;
  }
  return true;
}

uint32_t ReadLeUint32(const uint8_t * data)
{
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8u) |
         (static_cast<uint32_t>(data[2]) << 16u) |
         (static_cast<uint32_t>(data[3]) << 24u);
}

bool DecodeCdrStringPayload(const std::vector<uint8_t> & payload, std::string * value)
{
  if (value == nullptr || payload.size() < 8u) {
    return false;
  }
  const bool little_endian_plain_cdr =
    payload[0] == 0u && payload[1] == 1u && payload[2] == 0u && payload[3] == 0u;
  if (!little_endian_plain_cdr) {
    return false;
  }
  const uint32_t encoded_size = ReadLeUint32(payload.data() + 4u);
  if (encoded_size == 0u || payload.size() < 8u + encoded_size ||
    payload[8u + encoded_size - 1u] != 0u) {
    return false;
  }
  value->assign(
    reinterpret_cast<const char *>(payload.data() + 8u),
    static_cast<size_t>(encoded_size - 1u));
  return true;
}

std::string TrimAsciiWhitespace(const std::string & value)
{
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(begin, end - begin);
}

// Parse a numeric content-filter expression "<field> <op> <value>" (a DDS-SQL subset). `value` is a
// numeric literal or a %N placeholder substituted from `parameters`. Recognized operators: < <= > >=
// = == != <>. Returns false (with no side effects) for anything else.
bool TryParseNumericFilter(
  const std::string & expression, const std::vector<std::string> & parameters, std::string * field,
  int * op, double * value)
{
  size_t i = 0;
  while (i < expression.size() && expression[i] != '<' && expression[i] != '>' &&
    expression[i] != '=' && expression[i] != '!')
  {
    ++i;
  }
  if (i == expression.size()) {
    return false;
  }
  const std::string two = expression.substr(i, 2);
  int code = 0;
  size_t op_len = 1;
  if (two == "<=") { code = 2; op_len = 2; }
  else if (two == ">=") { code = 4; op_len = 2; }
  else if (two == "==") { code = 5; op_len = 2; }
  else if (two == "!=") { code = 6; op_len = 2; }
  else if (two == "<>") { code = 6; op_len = 2; }
  else if (expression[i] == '<') { code = 1; }
  else if (expression[i] == '>') { code = 3; }
  else if (expression[i] == '=') { code = 5; }
  else { return false; }  // a lone '!' is not a valid operator

  std::string lhs = TrimAsciiWhitespace(expression.substr(0, i));
  std::string rhs = TrimAsciiWhitespace(expression.substr(i + op_len));
  if (lhs.empty() || rhs.empty()) {
    return false;
  }
  if (rhs[0] == '%') {
    const std::string index_text = rhs.substr(1);
    size_t consumed = 0;
    long index = -1;
    try {
      index = std::stol(index_text, &consumed);
    } catch (...) {
      return false;
    }
    if (consumed != index_text.size() || index < 0 ||
      static_cast<size_t>(index) >= parameters.size())
    {
      return false;
    }
    rhs = TrimAsciiWhitespace(parameters[static_cast<size_t>(index)]);
  }
  try {
    size_t consumed = 0;
    const double parsed = std::stod(rhs, &consumed);
    if (consumed != rhs.size()) {
      return false;
    }
    *value = parsed;
  } catch (...) {
    return false;
  }
  *field = lhs;
  *op = code;
  return true;
}

bool CompareNumeric(double lhs, int op, double rhs)
{
  switch (op) {
    case 1: return lhs < rhs;
    case 2: return lhs <= rhs;
    case 3: return lhs > rhs;
    case 4: return lhs >= rhs;
    case 5: return lhs == rhs;
    case 6: return lhs != rhs;
    default: return true;
  }
}

bool PayloadMatchesContentFilter(const SubscriptionData & subscription, const std::vector<uint8_t> & payload)
{
  if (subscription.numeric_filter_enabled) {
    // Decode the sample and evaluate `field OP value` against the introspected field. If the sample
    // cannot be decoded / the field read fails, keep it (a filter must not silently drop valid data).
    void * message = subscription.adapter.AllocateMessage();
    if (message == nullptr) {
      return true;
    }
    bool keep = true;
    double field_value = 0.0;
    if (subscription.adapter.Decode(payload.data(), payload.size(), message) &&
      subscription.adapter.ReadNumericField(message, subscription.numeric_filter_field, &field_value))
    {
      keep = CompareNumeric(
        field_value, subscription.numeric_filter_op, subscription.numeric_filter_value);
    }
    subscription.adapter.DestroyMessage(message);
    return keep;
  }
  if (!subscription.content_filter_enabled) {
    return true;
  }
  if (subscription.content_filter_parameters.empty()) {
    return false;
  }
  const std::string & expected = subscription.content_filter_parameters[0];
  std::string decoded;
  if (DecodeCdrStringPayload(payload, &decoded)) {
    return decoded == expected;
  }
  return payload.size() == expected.size() &&
         std::equal(payload.begin(), payload.end(), expected.begin());
}
}  // namespace

void RegisterPublisher(PublisherData * publisher)
{
  if (publisher == nullptr) {
    return;
  }
  std::vector<CallbackInvocation> callbacks;
  {
    std::lock_guard<std::mutex> lock(g_broker_mutex);
    for (auto * subscription : g_subscriptions) {
      if (SameTopicAndType(publisher, subscription)) {
        ++publisher->matched_total_count;
        ++subscription->matched_total_count;
        AddSubscriptionCallbackInvocation(&callbacks, subscription, 1);
        AccrueIncompatibleQosLocked(publisher, subscription, &callbacks);
      }
    }
    g_publishers.push_back(publisher);
  }
  InvokeCallbacks(callbacks);
}

void UnregisterPublisher(PublisherData * publisher)
{
  std::vector<CallbackInvocation> callbacks;
  {
    std::lock_guard<std::mutex> lock(g_broker_mutex);
    for (auto * subscription : g_subscriptions) {
      if (SameTopicAndType(publisher, subscription)) {
        AddSubscriptionCallbackInvocation(&callbacks, subscription, 1);
      }
    }
    g_publishers.erase(
      std::remove(g_publishers.begin(), g_publishers.end(), publisher), g_publishers.end());
  }
  InvokeCallbacks(callbacks);
}

void RegisterSubscription(SubscriptionData * subscription)
{
  if (subscription == nullptr) {
    return;
  }
  std::vector<CallbackInvocation> callbacks;
  {
    std::lock_guard<std::mutex> lock(g_broker_mutex);
    for (auto * publisher : g_publishers) {
      if (SameTopicAndType(publisher, subscription)) {
        ++publisher->matched_total_count;
        ++subscription->matched_total_count;
        AddPublisherCallbackInvocation(&callbacks, publisher, 1);
        AccrueIncompatibleQosLocked(publisher, subscription, &callbacks);
      }
    }
    g_subscriptions.push_back(subscription);
  }
  InvokeCallbacks(callbacks);
}

void UnregisterSubscription(SubscriptionData * subscription)
{
  std::vector<CallbackInvocation> callbacks;
  {
    std::lock_guard<std::mutex> lock(g_broker_mutex);
    for (auto * publisher : g_publishers) {
      if (SameTopicAndType(publisher, subscription)) {
        AddPublisherCallbackInvocation(&callbacks, publisher, 1);
      }
    }
    g_subscriptions.erase(
      std::remove(g_subscriptions.begin(), g_subscriptions.end(), subscription),
      g_subscriptions.end());
  }
  InvokeCallbacks(callbacks);
}

uint64_t ReservePublicationSequenceNumber(PublisherData * publisher)
{
  if (publisher == nullptr) {
    return 0u;
  }
  std::lock_guard<std::mutex> lock(publisher->mutex);
  return publisher->next_publication_sequence_number++;
}

void PublishToSubscriptions(PublisherData * publisher, const std::vector<uint8_t> & payload)
{
  PublishToSubscriptions(publisher, payload, ReservePublicationSequenceNumber(publisher));
}

void PublishToSubscriptions(
  PublisherData * publisher, const std::vector<uint8_t> & payload,
  uint64_t publication_sequence_number)
{
  if (publisher == nullptr) {
    return;
  }
  std::vector<SubscriptionData *> matches;
  {
    std::lock_guard<std::mutex> lock(g_broker_mutex);
    for (auto * subscription : g_subscriptions) {
      if (SameTopicAndType(publisher, subscription)) {
        matches.push_back(subscription);
      }
    }
  }

  QueuedSample sample;
  sample.payload = payload;
  sample.info = rmw_get_zero_initialized_message_info();
  FillPublisherGid(publisher, &sample.info.publisher_gid);
  sample.info.source_timestamp = NowNanoseconds();
  sample.info.publication_sequence_number = publication_sequence_number;
  sample.info.from_intra_process = false;
  for (auto * subscription : matches) {
    EnqueueSample(subscription, sample);
  }
}

void EnqueueSample(SubscriptionData * subscription, const QueuedSample & sample)
{
  if (subscription == nullptr) {
    return;
  }
  CallbackInvocation callback;
  CallbackInvocation message_lost_callback;
  {
    std::lock_guard<std::mutex> lock(subscription->mutex);
    if (!PayloadMatchesContentFilter(*subscription, sample.payload)) {
      return;
    }
    // Message-lost detection: a forward gap in this writer's publication sequence numbers means samples
    // were lost before reaching us. In-process delivery is contiguous (count stays 0); gaps arise on the
    // lossy RTPS / bridge ingress path where writer sequence numbers can skip.
    const uint64_t pub_seq = sample.info.publication_sequence_number;
    if (pub_seq != 0u) {
      std::array<uint8_t, RMW_GID_STORAGE_SIZE> writer_key{};
      std::memcpy(writer_key.data(), sample.info.publisher_gid.data, RMW_GID_STORAGE_SIZE);
      auto it = subscription->last_publication_seq_by_writer.find(writer_key);
      if (it != subscription->last_publication_seq_by_writer.end() && pub_seq > it->second + 1u) {
        const uint64_t lost = pub_seq - it->second - 1u;
        subscription->message_lost_total += static_cast<size_t>(lost);
        message_lost_callback = CallbackInvocation{
          subscription->message_lost_callback, subscription->message_lost_callback_user_data,
          static_cast<size_t>(lost)};
      }
      if (it == subscription->last_publication_seq_by_writer.end() || pub_seq > it->second) {
        subscription->last_publication_seq_by_writer[writer_key] = pub_seq;
      }
    }
    QueuedSample queued_sample = sample;
    queued_sample.info.received_timestamp =
      ReceivedTimestampFor(queued_sample.info.source_timestamp);
    queued_sample.info.reception_sequence_number =
      subscription->next_reception_sequence_number++;
    const int64_t arrival_ns = static_cast<int64_t>(queued_sample.info.received_timestamp);
    subscription->queue.push_back(std::move(queued_sample));
    // A delivered sample satisfies the requested-deadline period (reset the reference).
    subscription->requested_deadline_last_active_ns = arrival_ns;
    callback = CallbackInvocation{
      subscription->new_message_callback, subscription->new_message_callback_user_data, 1};
  }
  if (message_lost_callback.callback != nullptr && message_lost_callback.event_count != 0) {
    message_lost_callback.callback(message_lost_callback.user_data, message_lost_callback.event_count);
  }
  if (callback.callback != nullptr) {
    callback.callback(callback.user_data, callback.event_count);
  }
}

size_t EnqueueRtpsUserDataForReader(
  const rtps::EntityId & reader_id, const rtps::GuidPrefix & writer_guid_prefix,
  const rtps::EntityId & writer_id, int64_t writer_sequence_number,
  const std::vector<uint8_t> & payload)
{
  std::vector<SubscriptionData *> matches;
  {
    std::lock_guard<std::mutex> lock(g_broker_mutex);
    for (auto * subscription : g_subscriptions) {
      if (subscription != nullptr && subscription->rtps_entity_id == reader_id) {
        matches.push_back(subscription);
      }
    }
  }

  QueuedSample sample;
  sample.payload = payload;
  sample.info = rmw_get_zero_initialized_message_info();
  FillRtpsWriterGid(writer_guid_prefix, writer_id, &sample.info.publisher_gid);
  sample.info.source_timestamp = NowNanoseconds();
  sample.info.publication_sequence_number =
    writer_sequence_number < 0 ? 0 : static_cast<uint64_t>(writer_sequence_number);
  sample.info.from_intra_process = false;
  for (auto * subscription : matches) {
    EnqueueSample(subscription, sample);
  }
  return matches.size();
}

size_t EnqueueRtpsUserDataForTopic(
  const std::string & dds_topic_name, const rtps::GuidPrefix & writer_guid_prefix,
  const rtps::EntityId & writer_id, int64_t writer_sequence_number,
  const std::vector<uint8_t> & payload)
{
  // Best-effort RTPS DATA from third-party writers (e.g. Fast-DDS) carries
  // reader_id = ENTITYID_UNKNOWN and so cannot be matched by reader entity id.
  // Route it to every local subscription on the writer's DDS topic instead.
  std::vector<SubscriptionData *> matches;
  {
    std::lock_guard<std::mutex> lock(g_broker_mutex);
    for (auto * subscription : g_subscriptions) {
      // Match on the DDS/RTPS topic name (e.g. "rt/chatter"), which is what the
      // remote writer's SEDP carries — not the MDDS bridge topic name.
      if (subscription != nullptr &&
          ToRtpsTopicName(subscription->topic_name.c_str()) == dds_topic_name) {
        matches.push_back(subscription);
      }
    }
  }

  QueuedSample sample;
  sample.payload = payload;
  sample.info = rmw_get_zero_initialized_message_info();
  FillRtpsWriterGid(writer_guid_prefix, writer_id, &sample.info.publisher_gid);
  sample.info.source_timestamp = NowNanoseconds();
  sample.info.publication_sequence_number =
    writer_sequence_number < 0 ? 0 : static_cast<uint64_t>(writer_sequence_number);
  sample.info.from_intra_process = false;
  for (auto * subscription : matches) {
    EnqueueSample(subscription, sample);
  }
  return matches.size();
}

bool HasQueuedSample(SubscriptionData * subscription)
{
  if (subscription == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(subscription->mutex);
  return !subscription->queue.empty();
}

bool TakeQueuedSample(SubscriptionData * subscription, QueuedSample * sample)
{
  if (subscription == nullptr || sample == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(subscription->mutex);
  if (subscription->queue.empty()) {
    return false;
  }
  *sample = std::move(subscription->queue.front());
  subscription->queue.pop_front();
  return true;
}

void FillPublisherGid(const PublisherData * publisher, rmw_gid_t * gid)
{
  FillEndpointGid(publisher, publisher == nullptr ? std::string() : publisher->topic_name, 0, gid);
}

rmw_ret_t SetSubscriptionContentFilter(
  SubscriptionData * subscription, const rmw_subscription_content_filter_options_t * options)
{
  if (subscription == nullptr || options == nullptr || options->filter_expression == nullptr) {
    RMW_SET_ERROR_MSG("content filter argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }

  const bool clear_filter = options->filter_expression[0] == '\0';
  bool numeric = false;
  std::string numeric_field;
  int numeric_op = 0;
  double numeric_value = 0.0;
  if (!clear_filter &&
      !IsSupportedStringContentFilter(
        *subscription, options->filter_expression, options->expression_parameters)) {
    // Not the supported std_msgs/String filter — try a numeric field filter for other message types.
    rmw_reset_error();  // discard the string-filter rejection; a numeric filter may still be accepted
    std::vector<std::string> params;
    for (size_t i = 0; i < options->expression_parameters.size; ++i) {
      params.emplace_back(
        options->expression_parameters.data[i] != nullptr ? options->expression_parameters.data[i] : "");
    }
    if (!TryParseNumericFilter(
          options->filter_expression, params, &numeric_field, &numeric_op, &numeric_value) ||
        !subscription->adapter.HasNumericField(numeric_field)) {
      RMW_SET_ERROR_MSG("content filter is not a supported string or numeric field expression");
      return RMW_RET_UNSUPPORTED;
    }
    numeric = true;
  }

  std::lock_guard<std::mutex> lock(subscription->mutex);
  if (clear_filter) {
    subscription->content_filter_enabled = false;
    subscription->numeric_filter_enabled = false;
    subscription->content_filter_expression.clear();
    subscription->content_filter_parameters.clear();
    return RMW_RET_OK;
  }

  // Store the raw expression/parameters for get_content_filter round-trip in either mode.
  subscription->content_filter_expression = options->filter_expression;
  subscription->content_filter_parameters.clear();
  for (size_t i = 0; i < options->expression_parameters.size; ++i) {
    subscription->content_filter_parameters.emplace_back(
      options->expression_parameters.data[i] != nullptr ? options->expression_parameters.data[i] : "");
  }
  if (numeric) {
    subscription->numeric_filter_enabled = true;
    subscription->content_filter_enabled = false;
    subscription->numeric_filter_field = numeric_field;
    subscription->numeric_filter_op = numeric_op;
    subscription->numeric_filter_value = numeric_value;
    return RMW_RET_OK;
  }
  subscription->content_filter_enabled = true;
  subscription->numeric_filter_enabled = false;
  return RMW_RET_OK;
}

rmw_ret_t GetSubscriptionContentFilter(
  SubscriptionData * subscription, const rcutils_allocator_t * allocator,
  rmw_subscription_content_filter_options_t * options)
{
  if (subscription == nullptr || allocator == nullptr || options == nullptr) {
    RMW_SET_ERROR_MSG("content filter get argument is null");
    return RMW_RET_INVALID_ARGUMENT;
  }

  std::string expression;
  std::vector<std::string> parameters;
  {
    std::lock_guard<std::mutex> lock(subscription->mutex);
    if (!subscription->content_filter_enabled) {
      RMW_SET_ERROR_MSG("subscription does not have an active content filter");
      return RMW_RET_ERROR;
    }
    expression = subscription->content_filter_expression;
    parameters = subscription->content_filter_parameters;
  }

  std::vector<const char *> parameter_argv;
  parameter_argv.reserve(parameters.size());
  for (const auto & parameter : parameters) {
    parameter_argv.push_back(parameter.c_str());
  }
  return rmw_subscription_content_filter_options_init(
    expression.c_str(), parameter_argv.size(),
    parameter_argv.empty() ? nullptr : parameter_argv.data(), allocator, options);
}

size_t CountSubscriptionsForPublisher(const PublisherData & publisher)
{
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  return CountMatchingSubscriptionsLocked(&publisher);
}

size_t CountPublishersForSubscription(const SubscriptionData & subscription)
{
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  return CountMatchingPublishersLocked(&subscription);
}

bool TakePublisherMatchedStatus(PublisherData * publisher, rmw_matched_status_t * status)
{
  if (publisher == nullptr || status == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  FillMatchedStatus(
    CountMatchingSubscriptionsLocked(publisher), publisher->matched_total_count,
    &publisher->matched_last_total_count, &publisher->matched_last_current_count, status);
  return true;
}

bool TakeSubscriptionMatchedStatus(SubscriptionData * subscription, rmw_matched_status_t * status)
{
  if (subscription == nullptr || status == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  FillMatchedStatus(
    CountMatchingPublishersLocked(subscription), subscription->matched_total_count,
    &subscription->matched_last_total_count, &subscription->matched_last_current_count, status);
  return true;
}

bool HasUnreadPublisherMatchedStatus(PublisherData * publisher)
{
  if (publisher == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  return publisher->matched_total_count != publisher->matched_last_total_count ||
         CountMatchingSubscriptionsLocked(publisher) != publisher->matched_last_current_count;
}

bool HasUnreadSubscriptionMatchedStatus(SubscriptionData * subscription)
{
  if (subscription == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  return subscription->matched_total_count != subscription->matched_last_total_count ||
         CountMatchingPublishersLocked(subscription) != subscription->matched_last_current_count;
}

size_t SetPublisherMatchedCallback(
  PublisherData * publisher, rmw_event_callback_t callback, const void * user_data)
{
  if (publisher == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  publisher->matched_callback = callback;
  publisher->matched_callback_user_data = user_data;
  if (callback == nullptr) {
    return 0;
  }
  return PendingMatchedEventCountLocked(
    CountMatchingSubscriptionsLocked(publisher), publisher->matched_total_count,
    publisher->matched_last_total_count, publisher->matched_last_current_count);
}

size_t SetSubscriptionMatchedCallback(
  SubscriptionData * subscription, rmw_event_callback_t callback, const void * user_data)
{
  if (subscription == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  subscription->matched_callback = callback;
  subscription->matched_callback_user_data = user_data;
  if (callback == nullptr) {
    return 0;
  }
  return PendingMatchedEventCountLocked(
    CountMatchingPublishersLocked(subscription), subscription->matched_total_count,
    subscription->matched_last_total_count, subscription->matched_last_current_count);
}

size_t SetSubscriptionNewMessageCallback(
  SubscriptionData * subscription, rmw_event_callback_t callback, const void * user_data)
{
  if (subscription == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(subscription->mutex);
  subscription->new_message_callback = callback;
  subscription->new_message_callback_user_data = user_data;
  return callback == nullptr ? 0 : subscription->queue.size();
}

// --- Offered / requested QoS-incompatible events (accrued under g_broker_mutex at match time) ---

bool TakePublisherQosIncompatibleStatus(
  PublisherData * publisher, rmw_qos_incompatible_event_status_t * status)
{
  if (publisher == nullptr || status == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  status->total_count = static_cast<int32_t>(publisher->offered_qos_incompatible_total);
  status->total_count_change = static_cast<int32_t>(
    publisher->offered_qos_incompatible_total - publisher->offered_qos_incompatible_last);
  status->last_policy_kind = publisher->offered_qos_last_policy_kind;
  publisher->offered_qos_incompatible_last = publisher->offered_qos_incompatible_total;
  return true;
}

bool TakeSubscriptionQosIncompatibleStatus(
  SubscriptionData * subscription, rmw_qos_incompatible_event_status_t * status)
{
  if (subscription == nullptr || status == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  status->total_count = static_cast<int32_t>(subscription->requested_qos_incompatible_total);
  status->total_count_change = static_cast<int32_t>(
    subscription->requested_qos_incompatible_total - subscription->requested_qos_incompatible_last);
  status->last_policy_kind = subscription->requested_qos_last_policy_kind;
  subscription->requested_qos_incompatible_last = subscription->requested_qos_incompatible_total;
  return true;
}

bool HasUnreadPublisherQosIncompatibleStatus(PublisherData * publisher)
{
  if (publisher == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  return publisher->offered_qos_incompatible_total != publisher->offered_qos_incompatible_last;
}

bool HasUnreadSubscriptionQosIncompatibleStatus(SubscriptionData * subscription)
{
  if (subscription == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  return subscription->requested_qos_incompatible_total !=
         subscription->requested_qos_incompatible_last;
}

size_t SetPublisherQosIncompatibleCallback(
  PublisherData * publisher, rmw_event_callback_t callback, const void * user_data)
{
  if (publisher == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  publisher->offered_qos_incompatible_callback = callback;
  publisher->offered_qos_incompatible_callback_user_data = user_data;
  if (callback == nullptr) {
    return 0;
  }
  return publisher->offered_qos_incompatible_total - publisher->offered_qos_incompatible_last;
}

size_t SetSubscriptionQosIncompatibleCallback(
  SubscriptionData * subscription, rmw_event_callback_t callback, const void * user_data)
{
  if (subscription == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  subscription->requested_qos_incompatible_callback = callback;
  subscription->requested_qos_incompatible_callback_user_data = user_data;
  if (callback == nullptr) {
    return 0;
  }
  return subscription->requested_qos_incompatible_total -
         subscription->requested_qos_incompatible_last;
}

// --- Message-lost event (accrued under subscription->mutex on each enqueue) ---

bool TakeSubscriptionMessageLostStatus(
  SubscriptionData * subscription, rmw_message_lost_status_t * status)
{
  if (subscription == nullptr || status == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(subscription->mutex);
  status->total_count = subscription->message_lost_total;
  status->total_count_change = subscription->message_lost_total - subscription->message_lost_last;
  subscription->message_lost_last = subscription->message_lost_total;
  return true;
}

bool HasUnreadSubscriptionMessageLostStatus(SubscriptionData * subscription)
{
  if (subscription == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(subscription->mutex);
  return subscription->message_lost_total != subscription->message_lost_last;
}

size_t SetSubscriptionMessageLostCallback(
  SubscriptionData * subscription, rmw_event_callback_t callback, const void * user_data)
{
  if (subscription == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(subscription->mutex);
  subscription->message_lost_callback = callback;
  subscription->message_lost_callback_user_data = user_data;
  if (callback == nullptr) {
    return 0;
  }
  return subscription->message_lost_total - subscription->message_lost_last;
}

int64_t MddsNowNanoseconds()
{
  return static_cast<int64_t>(NowNanoseconds());
}

namespace
{
constexpr uint64_t kDeadlineInfiniteSec = 9223372036ULL;  // RMW_DURATION_INFINITE.sec

// Deadline period in ns, or 0 when unset / infinite (no enforcement).
int64_t DeadlineNanos(const rmw_qos_profile_t & qos)
{
  const rmw_time_t d = qos.deadline;
  if ((d.sec == 0 && d.nsec == 0) || d.sec >= kDeadlineInfiniteSec) {
    return 0;
  }
  return static_cast<int64_t>(d.sec) * 1000000000LL + static_cast<int64_t>(d.nsec);
}

// Advance *last_active over whole deadline periods elapsed before now_ns, counting one
// miss per elapsed period. Lazily initializes the reference on first observation.
size_t AccrueDeadlineMisses(int64_t * last_active, int64_t now_ns, int64_t deadline_ns)
{
  if (*last_active == 0 || now_ns <= *last_active) {
    if (*last_active == 0) {
      *last_active = now_ns;
    }
    return 0;
  }
  const int64_t elapsed = now_ns - *last_active;
  if (elapsed <= deadline_ns) {
    return 0;
  }
  const int64_t periods = elapsed / deadline_ns;
  *last_active += periods * deadline_ns;
  return static_cast<size_t>(periods);
}
}  // namespace

bool HasUnreadSubscriptionLivelinessStatus(SubscriptionData * subscription, size_t alive_count)
{
  if (subscription == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(subscription->mutex);
  if (!subscription->liveliness_initialized) {
    return alive_count != 0;
  }
  return alive_count != subscription->liveliness_last_alive_count;
}

bool TakeSubscriptionLivelinessStatus(
  SubscriptionData * subscription, size_t alive_count, rmw_liveliness_changed_status_t * status)
{
  if (subscription == nullptr || status == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(subscription->mutex);
  const size_t previous =
    subscription->liveliness_initialized ? subscription->liveliness_last_alive_count : 0;
  status->alive_count = static_cast<int32_t>(alive_count);
  status->not_alive_count = 0;
  status->alive_count_change = static_cast<int32_t>(alive_count) - static_cast<int32_t>(previous);
  status->not_alive_count_change = 0;
  subscription->liveliness_last_alive_count = alive_count;
  subscription->liveliness_initialized = true;
  return true;
}

size_t SetSubscriptionLivelinessCallback(
  SubscriptionData * subscription, size_t alive_count, rmw_event_callback_t callback,
  const void * user_data)
{
  if (subscription == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(subscription->mutex);
  subscription->liveliness_callback = callback;
  subscription->liveliness_callback_user_data = user_data;
  if (callback == nullptr) {
    return 0;
  }
  const size_t previous =
    subscription->liveliness_initialized ? subscription->liveliness_last_alive_count : 0;
  return alive_count != previous ? 1u : 0u;
}

void NoteSubscriptionSampleArrival(SubscriptionData * subscription, int64_t now_ns)
{
  if (subscription == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(subscription->mutex);
  subscription->requested_deadline_last_active_ns = now_ns;
}

void NotePublisherPublication(PublisherData * publisher, int64_t now_ns)
{
  if (publisher == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(publisher->mutex);
  publisher->offered_deadline_last_active_ns = now_ns;
}

bool HasUnreadSubscriptionDeadlineStatus(
  SubscriptionData * subscription, int64_t now_ns, bool matched)
{
  if (subscription == nullptr) {
    return false;
  }
  const int64_t deadline_ns = DeadlineNanos(subscription->actual_qos);
  if (deadline_ns == 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(subscription->mutex);
  if (!matched) {
    subscription->requested_deadline_last_active_ns = now_ns;  // clock holds while unmatched
  } else {
    subscription->requested_deadline_missed_total +=
      AccrueDeadlineMisses(&subscription->requested_deadline_last_active_ns, now_ns, deadline_ns);
  }
  return subscription->requested_deadline_missed_total !=
         subscription->requested_deadline_missed_last;
}

bool TakeSubscriptionDeadlineStatus(
  SubscriptionData * subscription, int64_t now_ns, bool matched,
  rmw_requested_deadline_missed_status_t * status)
{
  if (subscription == nullptr || status == nullptr) {
    return false;
  }
  const int64_t deadline_ns = DeadlineNanos(subscription->actual_qos);
  std::lock_guard<std::mutex> lock(subscription->mutex);
  if (deadline_ns != 0 && matched) {
    subscription->requested_deadline_missed_total +=
      AccrueDeadlineMisses(&subscription->requested_deadline_last_active_ns, now_ns, deadline_ns);
  } else if (deadline_ns != 0) {
    subscription->requested_deadline_last_active_ns = now_ns;
  }
  status->total_count = static_cast<int32_t>(subscription->requested_deadline_missed_total);
  status->total_count_change = static_cast<int32_t>(
    subscription->requested_deadline_missed_total - subscription->requested_deadline_missed_last);
  subscription->requested_deadline_missed_last = subscription->requested_deadline_missed_total;
  return true;
}

size_t SetSubscriptionDeadlineCallback(
  SubscriptionData * subscription, int64_t now_ns, bool matched, rmw_event_callback_t callback,
  const void * user_data)
{
  if (subscription == nullptr) {
    return 0;
  }
  const int64_t deadline_ns = DeadlineNanos(subscription->actual_qos);
  std::lock_guard<std::mutex> lock(subscription->mutex);
  subscription->requested_deadline_callback = callback;
  subscription->requested_deadline_callback_user_data = user_data;
  if (callback == nullptr) {
    return 0;
  }
  if (deadline_ns != 0 && matched) {
    subscription->requested_deadline_missed_total +=
      AccrueDeadlineMisses(&subscription->requested_deadline_last_active_ns, now_ns, deadline_ns);
  }
  return subscription->requested_deadline_missed_total -
         subscription->requested_deadline_missed_last;
}

bool HasUnreadPublisherDeadlineStatus(PublisherData * publisher, int64_t now_ns)
{
  if (publisher == nullptr) {
    return false;
  }
  const int64_t deadline_ns = DeadlineNanos(publisher->actual_qos);
  if (deadline_ns == 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(publisher->mutex);
  publisher->offered_deadline_missed_total +=
    AccrueDeadlineMisses(&publisher->offered_deadline_last_active_ns, now_ns, deadline_ns);
  return publisher->offered_deadline_missed_total != publisher->offered_deadline_missed_last;
}

bool TakePublisherDeadlineStatus(
  PublisherData * publisher, int64_t now_ns, rmw_offered_deadline_missed_status_t * status)
{
  if (publisher == nullptr || status == nullptr) {
    return false;
  }
  const int64_t deadline_ns = DeadlineNanos(publisher->actual_qos);
  std::lock_guard<std::mutex> lock(publisher->mutex);
  if (deadline_ns != 0) {
    publisher->offered_deadline_missed_total +=
      AccrueDeadlineMisses(&publisher->offered_deadline_last_active_ns, now_ns, deadline_ns);
  }
  status->total_count = static_cast<int32_t>(publisher->offered_deadline_missed_total);
  status->total_count_change = static_cast<int32_t>(
    publisher->offered_deadline_missed_total - publisher->offered_deadline_missed_last);
  publisher->offered_deadline_missed_last = publisher->offered_deadline_missed_total;
  return true;
}

size_t SetPublisherDeadlineCallback(
  PublisherData * publisher, int64_t now_ns, rmw_event_callback_t callback,
  const void * user_data)
{
  if (publisher == nullptr) {
    return 0;
  }
  const int64_t deadline_ns = DeadlineNanos(publisher->actual_qos);
  std::lock_guard<std::mutex> lock(publisher->mutex);
  publisher->offered_deadline_callback = callback;
  publisher->offered_deadline_callback_user_data = user_data;
  if (callback == nullptr) {
    return 0;
  }
  if (deadline_ns != 0) {
    publisher->offered_deadline_missed_total +=
      AccrueDeadlineMisses(&publisher->offered_deadline_last_active_ns, now_ns, deadline_ns);
  }
  return publisher->offered_deadline_missed_total - publisher->offered_deadline_missed_last;
}

size_t CountPublishersByTopic(const char * topic_name)
{
  if (topic_name == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  return static_cast<size_t>(std::count_if(
    g_publishers.begin(), g_publishers.end(), [topic_name](const PublisherData * publisher) {
      return publisher != nullptr && publisher->topic_name == topic_name;
    }));
}

size_t CountSubscriptionsByTopic(const char * topic_name)
{
  if (topic_name == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  return static_cast<size_t>(std::count_if(
    g_subscriptions.begin(), g_subscriptions.end(),
    [topic_name](const SubscriptionData * subscription) {
      return subscription != nullptr && subscription->topic_name == topic_name;
    }));
}

std::vector<NameAndTypes> GetTopicNamesAndTypes()
{
  std::vector<NameAndTypes> names_and_types;
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  for (const auto * publisher : g_publishers) {
    if (publisher != nullptr && publisher->adapter.IsValid()) {
      AddNameAndType(&names_and_types, publisher->topic_name, publisher->adapter.TypeName());
    }
  }
  for (const auto * subscription : g_subscriptions) {
    if (subscription != nullptr && subscription->adapter.IsValid()) {
      AddNameAndType(&names_and_types, subscription->topic_name, subscription->adapter.TypeName());
    }
  }
  return names_and_types;
}

std::vector<NameAndTypes> GetPublisherNamesAndTypesByNode(
  const char * node_name, const char * node_namespace)
{
  std::vector<NameAndTypes> names_and_types;
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  for (const auto * publisher : g_publishers) {
    if (
      publisher != nullptr && publisher->adapter.IsValid() &&
      BelongsToNode(publisher->node_name, publisher->node_namespace, node_name, node_namespace)) {
      AddNameAndType(&names_and_types, publisher->topic_name, publisher->adapter.TypeName());
    }
  }
  return names_and_types;
}

std::vector<NameAndTypes> GetSubscriptionNamesAndTypesByNode(
  const char * node_name, const char * node_namespace)
{
  std::vector<NameAndTypes> names_and_types;
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  for (const auto * subscription : g_subscriptions) {
    if (
      subscription != nullptr && subscription->adapter.IsValid() &&
      BelongsToNode(
        subscription->node_name, subscription->node_namespace, node_name, node_namespace)) {
      AddNameAndType(&names_and_types, subscription->topic_name, subscription->adapter.TypeName());
    }
  }
  return names_and_types;
}

std::vector<TopicEndpointInfo> GetPublisherEndpointInfosByTopic(const char * topic_name)
{
  std::vector<TopicEndpointInfo> infos;
  if (topic_name == nullptr) {
    return infos;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  for (const auto * publisher : g_publishers) {
    if (
      publisher != nullptr && publisher->adapter.IsValid() &&
      publisher->topic_name == topic_name) {
      infos.push_back(MakePublisherEndpointInfo(publisher));
    }
  }
  return infos;
}

std::vector<TopicEndpointInfo> GetSubscriptionEndpointInfosByTopic(const char * topic_name)
{
  std::vector<TopicEndpointInfo> infos;
  if (topic_name == nullptr) {
    return infos;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  for (const auto * subscription : g_subscriptions) {
    if (
      subscription != nullptr && subscription->adapter.IsValid() &&
      subscription->topic_name == topic_name) {
      infos.push_back(MakeSubscriptionEndpointInfo(subscription));
    }
  }
  return infos;
}

}  // namespace rmw_mdds_cpp
