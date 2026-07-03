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
#include <cmath>
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

bool SameTopicDifferentType(const PublisherData * publisher, const SubscriptionData * subscription)
{
  return publisher != nullptr && subscription != nullptr &&
         publisher->topic_name == subscription->topic_name &&
         publisher->adapter.TypeName() != subscription->adapter.TypeName();
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

bool SameTopicTypeAndCompatibleQos(
  const PublisherData * publisher, const SubscriptionData * subscription)
{
  return SameTopicAndType(publisher, subscription) &&
         IncompatibleQosPolicyKind(publisher->actual_qos, subscription->actual_qos) ==
           RMW_QOS_POLICY_INVALID;
}

size_t CountMatchingSubscriptionsLocked(const PublisherData * publisher)
{
  return static_cast<size_t>(std::count_if(
    g_subscriptions.begin(), g_subscriptions.end(),
    [publisher](const SubscriptionData * subscription) {
      return SameTopicTypeAndCompatibleQos(publisher, subscription);
    }));
}

size_t CountMatchingPublishersLocked(const SubscriptionData * subscription)
{
  return static_cast<size_t>(std::count_if(
    g_publishers.begin(), g_publishers.end(), [subscription](const PublisherData * publisher) {
      return SameTopicTypeAndCompatibleQos(publisher, subscription);
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

void AccrueIncompatibleTypeLocked(
  PublisherData * publisher, SubscriptionData * subscription,
  std::vector<CallbackInvocation> * callbacks)
{
  if (!SameTopicDifferentType(publisher, subscription)) {
    return;
  }
  ++publisher->offered_incompatible_type_total;
  ++subscription->requested_incompatible_type_total;
  AddCallbackInvocation(
    callbacks, publisher->offered_incompatible_type_callback,
    publisher->offered_incompatible_type_callback_user_data, 1);
  AddCallbackInvocation(
    callbacks, subscription->requested_incompatible_type_callback,
    subscription->requested_incompatible_type_callback_user_data, 1);
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
  info.topic_type_hash = publisher->adapter.TypeHash();
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
  info.topic_type_hash = subscription->adapter.TypeHash();
  info.endpoint_type = RMW_ENDPOINT_SUBSCRIPTION;
  FillEndpointGid(subscription, subscription->topic_name, 0x5a, &info.gid);
  info.qos_profile = subscription->actual_qos;
  return info;
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

bool HasBalancedEnclosingParentheses(const std::string & expression)
{
  if (expression.size() < 2u || expression.front() != '(' || expression.back() != ')') {
    return false;
  }
  int depth = 0;
  bool inside_string_literal = false;
  for (size_t i = 0; i < expression.size(); ++i) {
    const char ch = expression[i];
    if (ch == '\'') {
      if (inside_string_literal && i + 1 < expression.size() && expression[i + 1] == '\'') {
        ++i;
        continue;
      }
      inside_string_literal = !inside_string_literal;
      continue;
    }
    if (inside_string_literal) {
      continue;
    }
    if (ch == '(') {
      ++depth;
      continue;
    }
    if (ch != ')') {
      continue;
    }
    --depth;
    if (depth < 0 || (depth == 0 && i + 1 < expression.size())) {
      return false;
    }
  }
  return depth == 0 && !inside_string_literal;
}

std::string TrimEnclosingParentheses(std::string expression)
{
  expression = TrimAsciiWhitespace(expression);
  while (HasBalancedEnclosingParentheses(expression)) {
    expression = TrimAsciiWhitespace(expression.substr(1u, expression.size() - 2u));
  }
  return expression;
}

bool ParseSingleQuotedStringLiteral(const std::string & value, std::string * parsed)
{
  if (parsed == nullptr || value.size() < 2u || value.front() != '\'' || value.back() != '\'') {
    return false;
  }
  parsed->clear();
  for (size_t i = 1; i + 1 < value.size(); ++i) {
    if (value[i] != '\'') {
      parsed->push_back(value[i]);
      continue;
    }
    if (i + 2 < value.size() && value[i + 1] == '\'') {
      parsed->push_back('\'');
      ++i;
      continue;
    }
    return false;
  }
  return true;
}

bool ParseStringFilterValue(
  const std::string & value, const rcutils_string_array_t & expression_parameters,
  std::string * parsed_value)
{
  if (parsed_value == nullptr || value.empty()) {
    return false;
  }
  if (value[0] == '%') {
    size_t consumed = 0;
    long parameter_index = -1;
    try {
      parameter_index = std::stol(value.substr(1), &consumed);
    } catch (...) {
      return false;
    }
    if (
      consumed != value.size() - 1u || parameter_index < 0 ||
      static_cast<size_t>(parameter_index) >= expression_parameters.size ||
      expression_parameters.data == nullptr ||
      expression_parameters.data[parameter_index] == nullptr) {
      RMW_SET_ERROR_MSG("content filter expression parameter is invalid");
      return false;
    }
    *parsed_value = expression_parameters.data[parameter_index];
    return true;
  }
  if (ParseSingleQuotedStringLiteral(value, parsed_value)) {
    return true;
  }
  return false;
}

bool FindTopLevelAsciiWordOperator(
  const std::string & expression, const char * word, size_t * word_pos);

bool ParseLikeEscapeClause(
  const std::string & rhs, const rcutils_string_array_t & expression_parameters,
  std::string * pattern_expression, char * escape_char)
{
  if (pattern_expression == nullptr || escape_char == nullptr) {
    return false;
  }
  *pattern_expression = rhs;
  *escape_char = '\\';

  size_t escape_pos = std::string::npos;
  if (!FindTopLevelAsciiWordOperator(rhs, "ESCAPE", &escape_pos)) {
    return true;
  }

  const std::string pattern = TrimAsciiWhitespace(rhs.substr(0, escape_pos));
  const std::string escape_expression =
    TrimAsciiWhitespace(rhs.substr(escape_pos + std::strlen("ESCAPE")));
  if (pattern.empty() || escape_expression.empty()) {
    return false;
  }

  std::string parsed_escape;
  if (!ParseStringFilterValue(escape_expression, expression_parameters, &parsed_escape)) {
    return false;
  }
  if (parsed_escape.size() != 1u) {
    RMW_SET_ERROR_MSG("LIKE ESCAPE clause must resolve to one character");
    return false;
  }

  *pattern_expression = pattern;
  *escape_char = parsed_escape[0];
  return true;
}

bool IsAsciiWordOperatorAt(const std::string & expression, size_t pos, const char * word)
{
  const size_t len = std::strlen(word);
  if (pos + len > expression.size()) {
    return false;
  }
  if (pos > 0 && !std::isspace(static_cast<unsigned char>(expression[pos - 1]))) {
    return false;
  }
  if (pos + len < expression.size() &&
    !std::isspace(static_cast<unsigned char>(expression[pos + len])))
  {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    const auto actual = static_cast<unsigned char>(expression[pos + i]);
    const auto expected = static_cast<unsigned char>(word[i]);
    if (std::tolower(actual) != std::tolower(expected)) {
      return false;
    }
  }
  return true;
}

bool TryStripUnaryNot(const std::string & expression, std::string * operand)
{
  if (operand == nullptr || !IsAsciiWordOperatorAt(expression, 0u, "NOT")) {
    return false;
  }
  *operand = TrimEnclosingParentheses(expression.substr(std::strlen("NOT")));
  return !operand->empty();
}

bool FindTopLevelAsciiWordOperator(
  const std::string & expression, const char * word, size_t * word_pos)
{
  if (word == nullptr || word[0] == '\0' || word_pos == nullptr) {
    return false;
  }
  bool inside_string_literal = false;
  int parentheses_depth = 0;
  for (size_t i = 0; i < expression.size(); ++i) {
    if (expression[i] == '\'') {
      if (inside_string_literal && i + 1 < expression.size() && expression[i + 1] == '\'') {
        ++i;
        continue;
      }
      inside_string_literal = !inside_string_literal;
      continue;
    }
    if (inside_string_literal) {
      continue;
    }
    if (expression[i] == '(') {
      ++parentheses_depth;
      continue;
    }
    if (expression[i] == ')') {
      --parentheses_depth;
      if (parentheses_depth < 0) {
        return false;
      }
      continue;
    }
    if (parentheses_depth == 0 && IsAsciiWordOperatorAt(expression, i, word)) {
      *word_pos = i;
      return true;
    }
  }
  return false;
}

bool EndsWithAsciiWord(const std::string & expression, const char * word, size_t * word_pos)
{
  const size_t len = std::strlen(word);
  if (expression.size() < len) {
    return false;
  }
  const size_t pos = expression.size() - len;
  if (pos > 0 && !std::isspace(static_cast<unsigned char>(expression[pos - 1]))) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    const auto actual = static_cast<unsigned char>(expression[pos + i]);
    const auto expected = static_cast<unsigned char>(word[i]);
    if (std::tolower(actual) != std::tolower(expected)) {
      return false;
    }
  }
  if (word_pos != nullptr) {
    *word_pos = pos;
  }
  return true;
}

bool SplitStringFilterList(const std::string & expression, std::vector<std::string> * terms)
{
  if (terms == nullptr) {
    return false;
  }
  terms->clear();
  size_t term_begin = 0;
  bool inside_string_literal = false;
  int parentheses_depth = 0;
  for (size_t i = 0; i < expression.size(); ++i) {
    if (expression[i] == '\'') {
      if (inside_string_literal && i + 1 < expression.size() && expression[i + 1] == '\'') {
        ++i;
        continue;
      }
      inside_string_literal = !inside_string_literal;
      continue;
    }
    if (inside_string_literal) {
      continue;
    }
    if (expression[i] == '(') {
      ++parentheses_depth;
      continue;
    }
    if (expression[i] == ')') {
      --parentheses_depth;
      if (parentheses_depth < 0) {
        return false;
      }
      continue;
    }
    if (expression[i] != ',' || parentheses_depth != 0) {
      continue;
    }
    const std::string term = TrimAsciiWhitespace(expression.substr(term_begin, i - term_begin));
    if (term.empty()) {
      return false;
    }
    terms->push_back(term);
    term_begin = i + 1u;
  }
  if (parentheses_depth != 0 || inside_string_literal) {
    return false;
  }
  const std::string last_term = TrimAsciiWhitespace(expression.substr(term_begin));
  if (last_term.empty()) {
    return false;
  }
  terms->push_back(last_term);
  return true;
}

bool SplitStringFilterOperator(
  const std::string & expression, const char * operator_word, std::vector<std::string> * terms)
{
  if (operator_word == nullptr || operator_word[0] == '\0' || terms == nullptr) {
    return false;
  }
  terms->clear();
  size_t term_begin = 0;
  bool inside_string_literal = false;
  int parentheses_depth = 0;
  for (size_t i = 0; i < expression.size(); ++i) {
    if (expression[i] == '\'') {
      if (inside_string_literal && i + 1 < expression.size() && expression[i + 1] == '\'') {
        ++i;
        continue;
      }
      inside_string_literal = !inside_string_literal;
      continue;
    }
    if (!inside_string_literal && IsAsciiWordOperatorAt(expression, i, operator_word)) {
      if (parentheses_depth != 0) {
        continue;
      }
      const std::string term = TrimAsciiWhitespace(expression.substr(term_begin, i - term_begin));
      if (term.empty()) {
        return false;
      }
      terms->push_back(term);
      i += std::strlen(operator_word) - 1u;
      term_begin = i + 1u;
      continue;
    }
    if (inside_string_literal) {
      continue;
    }
    if (expression[i] == '(') {
      ++parentheses_depth;
      continue;
    }
    if (expression[i] == ')') {
      --parentheses_depth;
      if (parentheses_depth < 0) {
        return false;
      }
    }
  }
  if (parentheses_depth != 0 || inside_string_literal) {
    return false;
  }
  const std::string last_term = TrimAsciiWhitespace(expression.substr(term_begin));
  if (last_term.empty()) {
    return false;
  }
  terms->push_back(last_term);
  return true;
}

bool TryParseStringContentFilterInDnf(
  const std::string & expression, const rcutils_string_array_t & expression_parameters,
  std::vector<std::vector<StringContentFilterClause>> * disjunctions)
{
  if (disjunctions == nullptr) {
    return false;
  }
  const std::string normalized_expression = TrimEnclosingParentheses(expression);
  size_t in_pos = std::string::npos;
  if (!FindTopLevelAsciiWordOperator(normalized_expression, "IN", &in_pos)) {
    return false;
  }
  std::string lhs = TrimAsciiWhitespace(normalized_expression.substr(0, in_pos));
  size_t not_pos = std::string::npos;
  const bool negated = EndsWithAsciiWord(lhs, "NOT", &not_pos);
  if (negated) {
    lhs = TrimAsciiWhitespace(lhs.substr(0, not_pos));
  }
  const std::string rhs = TrimAsciiWhitespace(normalized_expression.substr(in_pos + 2u));
  if (lhs.empty() || !HasBalancedEnclosingParentheses(rhs)) {
    return false;
  }
  const std::string list_expression = rhs.substr(1u, rhs.size() - 2u);
  std::vector<std::string> terms;
  if (!SplitStringFilterList(list_expression, &terms)) {
    return false;
  }

  std::vector<std::vector<StringContentFilterClause>> parsed_disjunctions;
  std::vector<StringContentFilterClause> not_in_conjunction;
  for (const auto & term : terms) {
    std::string parsed_value;
    if (!ParseStringFilterValue(
        TrimEnclosingParentheses(term), expression_parameters, &parsed_value)) {
      return false;
    }
    StringContentFilterClause clause;
    clause.field = lhs;
    clause.op = negated ? 6 : 1;
    clause.value = std::move(parsed_value);
    if (negated) {
      not_in_conjunction.push_back(std::move(clause));
    } else {
      parsed_disjunctions.push_back({std::move(clause)});
    }
  }
  if (negated) {
    parsed_disjunctions.push_back(std::move(not_in_conjunction));
  }
  *disjunctions = std::move(parsed_disjunctions);
  return !disjunctions->empty();
}

bool SplitStringFilterConjunction(const std::string & expression, std::vector<std::string> * terms)
{
  return SplitStringFilterOperator(expression, "AND", terms);
}

bool SplitStringFilterDisjunction(const std::string & expression, std::vector<std::string> * terms)
{
  return SplitStringFilterOperator(expression, "OR", terms);
}

bool TryParseStringContentFilterClause(
  const std::string & expression, const rcutils_string_array_t & expression_parameters,
  StringContentFilterClause * clause)
{
  if (clause == nullptr) {
    return false;
  }
  const std::string normalized_expression = TrimEnclosingParentheses(expression);
  size_t op_pos = std::string::npos;
  int parsed_op = 0;
  size_t op_len = 0;
  for (size_t i = 0; i < normalized_expression.size(); ++i) {
    const std::string two = normalized_expression.substr(i, 2);
    if (two == "==") {
      parsed_op = 1;
      op_len = 2;
    } else if (two == "!=" || two == "<>") {
      parsed_op = 6;
      op_len = 2;
    } else if (IsAsciiWordOperatorAt(normalized_expression, i, "LIKE")) {
      parsed_op = 7;
      op_len = 4;
    } else if (normalized_expression[i] == '=') {
      parsed_op = 1;
      op_len = 1;
    } else {
      continue;
    }
    op_pos = i;
    break;
  }
  if (op_pos == std::string::npos) {
    return false;
  }
  std::string lhs = TrimAsciiWhitespace(normalized_expression.substr(0, op_pos));
  std::string rhs = TrimEnclosingParentheses(
    normalized_expression.substr(op_pos + op_len));
  char escape_char = '\\';
  if (parsed_op == 7) {
    size_t not_pos = std::string::npos;
    if (EndsWithAsciiWord(lhs, "NOT", &not_pos)) {
      lhs = TrimAsciiWhitespace(lhs.substr(0, not_pos));
      parsed_op = 8;
    }
    std::string pattern_expression;
    if (!ParseLikeEscapeClause(rhs, expression_parameters, &pattern_expression, &escape_char)) {
      return false;
    }
    rhs = std::move(pattern_expression);
  }
  if (lhs.empty() || rhs.empty()) {
    return false;
  }

  std::string parsed_value;
  if (!ParseStringFilterValue(rhs, expression_parameters, &parsed_value)) {
    return false;
  }

  clause->field = lhs;
  clause->op = parsed_op;
  clause->value = std::move(parsed_value);
  clause->escape_char = escape_char;
  return true;
}

bool InvertStringContentFilterClause(StringContentFilterClause * clause)
{
  if (clause == nullptr) {
    return false;
  }
  switch (clause->op) {
    case 1: clause->op = 6; return true;
    case 6: clause->op = 1; return true;
    case 7: clause->op = 8; return true;
    case 8: clause->op = 7; return true;
    default: return false;
  }
}

bool NegateStringContentFilterDnf(
  const std::vector<std::vector<StringContentFilterClause>> & source_disjunctions,
  std::vector<std::vector<StringContentFilterClause>> * negated_disjunctions)
{
  if (negated_disjunctions == nullptr || source_disjunctions.empty()) {
    return false;
  }
  std::vector<std::vector<StringContentFilterClause>> combinations(1u);
  for (const auto & disjunct : source_disjunctions) {
    if (disjunct.empty()) {
      return false;
    }
    std::vector<std::vector<StringContentFilterClause>> next_combinations;
    for (const auto & clause : disjunct) {
      StringContentFilterClause inverted_clause = clause;
      if (!InvertStringContentFilterClause(&inverted_clause)) {
        return false;
      }
      for (const auto & combination : combinations) {
        auto merged = combination;
        merged.push_back(inverted_clause);
        next_combinations.push_back(std::move(merged));
      }
    }
    combinations = std::move(next_combinations);
  }
  *negated_disjunctions = std::move(combinations);
  return !negated_disjunctions->empty();
}

bool TryParseStringContentFilterDnf(
  const std::string & expression, const rcutils_string_array_t & expression_parameters,
  std::vector<std::vector<StringContentFilterClause>> * disjunctions)
{
  if (disjunctions == nullptr) {
    return false;
  }
  const std::string normalized_expression = TrimEnclosingParentheses(expression);
  if (normalized_expression.empty()) {
    return false;
  }

  std::string negated_expression;
  if (TryStripUnaryNot(normalized_expression, &negated_expression)) {
    std::vector<std::vector<StringContentFilterClause>> nested_disjunctions;
    if (!TryParseStringContentFilterDnf(
        negated_expression, expression_parameters, &nested_disjunctions))
    {
      return false;
    }
    return NegateStringContentFilterDnf(nested_disjunctions, disjunctions);
  }

  std::vector<std::string> disjuncts;
  if (!SplitStringFilterDisjunction(normalized_expression, &disjuncts)) {
    RMW_SET_ERROR_MSG("unsupported string content filter disjunction");
    return false;
  }
  if (disjuncts.size() > 1u) {
    std::vector<std::vector<StringContentFilterClause>> parsed_disjunctions;
    for (const auto & disjunct : disjuncts) {
      std::vector<std::vector<StringContentFilterClause>> nested_disjunctions;
      if (!TryParseStringContentFilterDnf(disjunct, expression_parameters, &nested_disjunctions)) {
        return false;
      }
      parsed_disjunctions.insert(
        parsed_disjunctions.end(), nested_disjunctions.begin(), nested_disjunctions.end());
    }
    *disjunctions = std::move(parsed_disjunctions);
    return !disjunctions->empty();
  }

  std::vector<std::string> terms;
  if (!SplitStringFilterConjunction(normalized_expression, &terms)) {
    RMW_SET_ERROR_MSG("unsupported string content filter conjunction");
    return false;
  }
  if (terms.size() > 1u) {
    std::vector<std::vector<StringContentFilterClause>> combinations(1u);
    for (const auto & term : terms) {
      std::vector<std::vector<StringContentFilterClause>> term_disjunctions;
      if (!TryParseStringContentFilterDnf(term, expression_parameters, &term_disjunctions)) {
        return false;
      }
      std::vector<std::vector<StringContentFilterClause>> next_combinations;
      for (const auto & combination : combinations) {
        for (const auto & term_group : term_disjunctions) {
          auto merged = combination;
          merged.insert(merged.end(), term_group.begin(), term_group.end());
          next_combinations.push_back(std::move(merged));
        }
      }
      combinations = std::move(next_combinations);
    }
    *disjunctions = std::move(combinations);
    return !disjunctions->empty();
  }

  std::vector<std::vector<StringContentFilterClause>> in_disjunctions;
  if (TryParseStringContentFilterInDnf(
      normalized_expression, expression_parameters, &in_disjunctions)) {
    *disjunctions = std::move(in_disjunctions);
    return !disjunctions->empty();
  }

  StringContentFilterClause clause;
  if (!TryParseStringContentFilterClause(normalized_expression, expression_parameters, &clause)) {
    if (rmw_get_error_string().str == nullptr || rmw_get_error_string().str[0] == '\0') {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "unsupported string content filter clause: %s", normalized_expression.c_str());
    }
    return false;
  }
  *disjunctions = {{std::move(clause)}};
  return true;
}

bool TryParseStringContentFilter(
  const char * filter_expression, const rcutils_string_array_t & expression_parameters,
  std::vector<StringContentFilterClause> * string_filter_clauses,
  std::vector<std::vector<StringContentFilterClause>> * string_filter_disjunctions)
{
  if (
    filter_expression == nullptr || string_filter_clauses == nullptr ||
    string_filter_disjunctions == nullptr) {
    return false;
  }
  std::vector<std::vector<StringContentFilterClause>> parsed_disjunctions;
  if (!TryParseStringContentFilterDnf(
      filter_expression, expression_parameters, &parsed_disjunctions) ||
    parsed_disjunctions.empty())
  {
    return false;
  }
  *string_filter_clauses = parsed_disjunctions.front();
  *string_filter_disjunctions = std::move(parsed_disjunctions);
  return true;
}

bool StringContentFilterFieldsAreSupported(
  const MessageAdapter & adapter,
  const std::vector<std::vector<StringContentFilterClause>> & string_filter_disjunctions)
{
  if (string_filter_disjunctions.empty()) {
    return false;
  }
  for (const auto & disjunct : string_filter_disjunctions) {
    if (disjunct.empty()) {
      return false;
    }
    for (const auto & clause : disjunct) {
      if (!adapter.HasStringField(clause.field)) {
        return false;
      }
    }
  }
  return true;
}

bool IsSupportedStringContentFilter(
  const SubscriptionData & subscription, const char * filter_expression,
  const rcutils_string_array_t & expression_parameters,
  std::vector<StringContentFilterClause> * string_filter_clauses,
  std::vector<std::vector<StringContentFilterClause>> * string_filter_disjunctions)
{
  if (!TryParseStringContentFilter(
      filter_expression, expression_parameters, string_filter_clauses, string_filter_disjunctions)) {
    if (rmw_get_error_string().str == nullptr || rmw_get_error_string().str[0] == '\0') {
      RMW_SET_ERROR_MSG("content filter expression is not supported");
    }
    return false;
  }
  if (!StringContentFilterFieldsAreSupported(subscription.adapter, *string_filter_disjunctions)) {
    RMW_SET_ERROR_MSG("content filter string field is not supported");
    return false;
  }
  return true;
}

bool ParseNumericFilterValue(
  std::string value, const std::vector<std::string> & parameters, double * parsed_value)
{
  if (parsed_value == nullptr || value.empty()) {
    return false;
  }
  value = TrimEnclosingParentheses(value);
  if (value.empty()) {
    return false;
  }
  if (value[0] == '%') {
    const std::string index_text = value.substr(1);
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
    value = TrimAsciiWhitespace(parameters[static_cast<size_t>(index)]);
  }
  if (IsAsciiWordOperatorAt(value, 0u, "TRUE")) {
    *parsed_value = 1.0;
    return true;
  }
  if (IsAsciiWordOperatorAt(value, 0u, "FALSE")) {
    *parsed_value = 0.0;
    return true;
  }
  try {
    size_t consumed = 0;
    const double parsed = std::stod(value, &consumed);
    if (consumed != value.size()) {
      return false;
    }
    *parsed_value = parsed;
  } catch (...) {
    return false;
  }
  return true;
}

// Parse a numeric content-filter clause "<field> <op> <value>" (a DDS-SQL subset). `value` is a
// numeric literal or a %N placeholder substituted from `parameters`. Recognized operators: < <= > >=
// = == != <>. Returns false (with no side effects) for anything else.
bool TryParseNumericFilterClause(
  const std::string & expression, const std::vector<std::string> & parameters,
  NumericContentFilterClause * clause)
{
  if (clause == nullptr) {
    return false;
  }
  const std::string normalized_expression = TrimEnclosingParentheses(expression);
  size_t i = 0;
  while (i < normalized_expression.size() && normalized_expression[i] != '<' &&
    normalized_expression[i] != '>' && normalized_expression[i] != '=' &&
    normalized_expression[i] != '!')
  {
    ++i;
  }
  if (i == normalized_expression.size()) {
    return false;
  }
  const std::string two = normalized_expression.substr(i, 2);
  int code = 0;
  size_t op_len = 1;
  if (two == "<=") { code = 2; op_len = 2; }
  else if (two == ">=") { code = 4; op_len = 2; }
  else if (two == "==") { code = 5; op_len = 2; }
  else if (two == "!=") { code = 6; op_len = 2; }
  else if (two == "<>") { code = 6; op_len = 2; }
  else if (normalized_expression[i] == '<') { code = 1; }
  else if (normalized_expression[i] == '>') { code = 3; }
  else if (normalized_expression[i] == '=') { code = 5; }
  else { return false; }  // a lone '!' is not a valid operator

  std::string lhs = TrimAsciiWhitespace(normalized_expression.substr(0, i));
  std::string rhs = TrimEnclosingParentheses(normalized_expression.substr(i + op_len));
  if (lhs.empty() || rhs.empty()) {
    return false;
  }
  double parsed_value = 0.0;
  if (!ParseNumericFilterValue(rhs, parameters, &parsed_value)) {
    return false;
  }
  clause->field = lhs;
  clause->op = code;
  clause->value = parsed_value;
  return true;
}

bool TryParseNumericFilterInDnf(
  const std::string & expression, const std::vector<std::string> & parameters,
  std::vector<std::vector<NumericContentFilterClause>> * disjunctions)
{
  if (disjunctions == nullptr) {
    return false;
  }
  const std::string normalized_expression = TrimEnclosingParentheses(expression);
  size_t in_pos = std::string::npos;
  if (!FindTopLevelAsciiWordOperator(normalized_expression, "IN", &in_pos)) {
    return false;
  }
  std::string lhs = TrimAsciiWhitespace(normalized_expression.substr(0, in_pos));
  size_t not_pos = std::string::npos;
  const bool negated = EndsWithAsciiWord(lhs, "NOT", &not_pos);
  if (negated) {
    lhs = TrimAsciiWhitespace(lhs.substr(0, not_pos));
  }
  const std::string rhs = TrimAsciiWhitespace(normalized_expression.substr(in_pos + 2u));
  if (lhs.empty() || !HasBalancedEnclosingParentheses(rhs)) {
    return false;
  }
  const std::string list_expression = rhs.substr(1u, rhs.size() - 2u);
  std::vector<std::string> terms;
  if (!SplitStringFilterList(list_expression, &terms)) {
    return false;
  }

  std::vector<std::vector<NumericContentFilterClause>> parsed_disjunctions;
  std::vector<NumericContentFilterClause> not_in_conjunction;
  for (const auto & term : terms) {
    double parsed_value = 0.0;
    if (!ParseNumericFilterValue(term, parameters, &parsed_value)) {
      return false;
    }
    NumericContentFilterClause clause;
    clause.field = lhs;
    clause.op = negated ? 6 : 5;
    clause.value = parsed_value;
    if (negated) {
      not_in_conjunction.push_back(std::move(clause));
    } else {
      parsed_disjunctions.push_back({std::move(clause)});
    }
  }
  if (negated) {
    parsed_disjunctions.push_back(std::move(not_in_conjunction));
  }
  *disjunctions = std::move(parsed_disjunctions);
  return !disjunctions->empty();
}

bool TryParseNumericFilterBetweenDnf(
  const std::string & expression, const std::vector<std::string> & parameters,
  std::vector<std::vector<NumericContentFilterClause>> * disjunctions)
{
  if (disjunctions == nullptr) {
    return false;
  }
  const std::string normalized_expression = TrimEnclosingParentheses(expression);
  size_t between_pos = std::string::npos;
  if (!FindTopLevelAsciiWordOperator(normalized_expression, "BETWEEN", &between_pos)) {
    return false;
  }
  std::string lhs = TrimAsciiWhitespace(normalized_expression.substr(0, between_pos));
  size_t not_pos = std::string::npos;
  const bool negated = EndsWithAsciiWord(lhs, "NOT", &not_pos);
  if (negated) {
    lhs = TrimAsciiWhitespace(lhs.substr(0, not_pos));
  }
  const std::string rhs = TrimAsciiWhitespace(
    normalized_expression.substr(between_pos + std::strlen("BETWEEN")));
  std::vector<std::string> bounds;
  if (lhs.empty() || !SplitStringFilterOperator(rhs, "AND", &bounds) || bounds.size() != 2u) {
    return false;
  }
  double lower = 0.0;
  double upper = 0.0;
  if (!ParseNumericFilterValue(bounds[0], parameters, &lower) ||
    !ParseNumericFilterValue(bounds[1], parameters, &upper))
  {
    return false;
  }

  NumericContentFilterClause lower_clause;
  lower_clause.field = lhs;
  lower_clause.op = negated ? 1 : 4;
  lower_clause.value = lower;

  NumericContentFilterClause upper_clause;
  upper_clause.field = lhs;
  upper_clause.op = negated ? 3 : 2;
  upper_clause.value = upper;

  if (negated) {
    *disjunctions = {{std::move(lower_clause)}, {std::move(upper_clause)}};
  } else {
    *disjunctions = {{std::move(lower_clause), std::move(upper_clause)}};
  }
  return true;
}

bool InvertNumericContentFilterClause(NumericContentFilterClause * clause)
{
  if (clause == nullptr) {
    return false;
  }
  switch (clause->op) {
    case 1: clause->op = 4; return true;
    case 2: clause->op = 3; return true;
    case 3: clause->op = 2; return true;
    case 4: clause->op = 1; return true;
    case 5: clause->op = 6; return true;
    case 6: clause->op = 5; return true;
    default: return false;
  }
}

bool NegateNumericContentFilterDnf(
  const std::vector<std::vector<NumericContentFilterClause>> & source_disjunctions,
  std::vector<std::vector<NumericContentFilterClause>> * negated_disjunctions)
{
  if (negated_disjunctions == nullptr || source_disjunctions.empty()) {
    return false;
  }
  std::vector<std::vector<NumericContentFilterClause>> combinations(1u);
  for (const auto & disjunct : source_disjunctions) {
    if (disjunct.empty()) {
      return false;
    }
    std::vector<std::vector<NumericContentFilterClause>> next_combinations;
    for (const auto & clause : disjunct) {
      NumericContentFilterClause inverted_clause = clause;
      if (!InvertNumericContentFilterClause(&inverted_clause)) {
        return false;
      }
      for (const auto & combination : combinations) {
        auto merged = combination;
        merged.push_back(inverted_clause);
        next_combinations.push_back(std::move(merged));
      }
    }
    combinations = std::move(next_combinations);
  }
  *negated_disjunctions = std::move(combinations);
  return !negated_disjunctions->empty();
}

bool TryParseNumericFilterDnf(
  const std::string & expression, const std::vector<std::string> & parameters,
  std::vector<std::vector<NumericContentFilterClause>> * disjunctions)
{
  if (disjunctions == nullptr) {
    return false;
  }
  const std::string normalized_expression = TrimEnclosingParentheses(expression);
  if (normalized_expression.empty()) {
    return false;
  }

  std::string negated_expression;
  if (TryStripUnaryNot(normalized_expression, &negated_expression)) {
    std::vector<std::vector<NumericContentFilterClause>> nested_disjunctions;
    if (!TryParseNumericFilterDnf(negated_expression, parameters, &nested_disjunctions)) {
      return false;
    }
    return NegateNumericContentFilterDnf(nested_disjunctions, disjunctions);
  }

  std::vector<std::string> disjuncts;
  if (!SplitStringFilterDisjunction(normalized_expression, &disjuncts)) {
    return false;
  }
  if (disjuncts.size() > 1u) {
    std::vector<std::vector<NumericContentFilterClause>> parsed_disjunctions;
    for (const auto & disjunct : disjuncts) {
      std::vector<std::vector<NumericContentFilterClause>> nested_disjunctions;
      if (!TryParseNumericFilterDnf(disjunct, parameters, &nested_disjunctions)) {
        return false;
      }
      parsed_disjunctions.insert(
        parsed_disjunctions.end(), nested_disjunctions.begin(), nested_disjunctions.end());
    }
    *disjunctions = std::move(parsed_disjunctions);
    return !disjunctions->empty();
  }

  std::vector<std::vector<NumericContentFilterClause>> between_disjunctions;
  if (TryParseNumericFilterBetweenDnf(
      normalized_expression, parameters, &between_disjunctions)) {
    *disjunctions = std::move(between_disjunctions);
    return !disjunctions->empty();
  }

  std::vector<std::string> terms;
  if (!SplitStringFilterConjunction(normalized_expression, &terms)) {
    return false;
  }
  if (terms.size() > 1u) {
    std::vector<std::vector<NumericContentFilterClause>> combinations(1u);
    for (const auto & term : terms) {
      std::vector<std::vector<NumericContentFilterClause>> term_disjunctions;
      if (!TryParseNumericFilterDnf(term, parameters, &term_disjunctions)) {
        return false;
      }
      std::vector<std::vector<NumericContentFilterClause>> next_combinations;
      for (const auto & combination : combinations) {
        for (const auto & term_group : term_disjunctions) {
          auto merged = combination;
          merged.insert(merged.end(), term_group.begin(), term_group.end());
          next_combinations.push_back(std::move(merged));
        }
      }
      combinations = std::move(next_combinations);
    }
    *disjunctions = std::move(combinations);
    return !disjunctions->empty();
  }

  std::vector<std::vector<NumericContentFilterClause>> in_disjunctions;
  if (TryParseNumericFilterInDnf(normalized_expression, parameters, &in_disjunctions)) {
    *disjunctions = std::move(in_disjunctions);
    return !disjunctions->empty();
  }

  NumericContentFilterClause clause;
  if (!TryParseNumericFilterClause(normalized_expression, parameters, &clause)) {
    return false;
  }
  *disjunctions = {{std::move(clause)}};
  return true;
}

bool TryParseNumericFilter(
  const std::string & expression, const std::vector<std::string> & parameters, std::string * field,
  int * op, double * value,
  std::vector<std::vector<NumericContentFilterClause>> * numeric_filter_disjunctions)
{
  if (field == nullptr || op == nullptr || value == nullptr || numeric_filter_disjunctions == nullptr) {
    return false;
  }
  std::vector<std::vector<NumericContentFilterClause>> parsed_disjunctions;
  if (!TryParseNumericFilterDnf(expression, parameters, &parsed_disjunctions)) {
    return false;
  }
  if (parsed_disjunctions.empty() || parsed_disjunctions.front().empty()) {
    return false;
  }

  const auto & first_clause = parsed_disjunctions.front().front();
  *field = first_clause.field;
  *op = first_clause.op;
  *value = first_clause.value;
  *numeric_filter_disjunctions = std::move(parsed_disjunctions);
  return true;
}

bool NumericContentFilterFieldsAreSupported(
  const MessageAdapter & adapter,
  const std::vector<std::vector<NumericContentFilterClause>> & numeric_filter_disjunctions)
{
  if (numeric_filter_disjunctions.empty()) {
    return false;
  }
  for (const auto & disjunct : numeric_filter_disjunctions) {
    if (disjunct.empty()) {
      return false;
    }
    for (const auto & clause : disjunct) {
      if (!adapter.HasNumericField(clause.field)) {
        return false;
      }
    }
  }
  return true;
}

bool CompareNumeric(double lhs, int op, double rhs)
{
  constexpr double kNumericEqualityEpsilon = 1e-6;
  const bool equal = std::fabs(lhs - rhs) <= kNumericEqualityEpsilon;
  switch (op) {
    case 1: return lhs < rhs;
    case 2: return lhs < rhs || equal;
    case 3: return lhs > rhs;
    case 4: return lhs > rhs || equal;
    case 5: return equal;
    case 6: return !equal;
    default: return true;
  }
}

bool StringLikeMatches(const std::string & value, const std::string & pattern, char escape_char)
{
  constexpr size_t npos = std::string::npos;
  size_t value_pos = 0;
  size_t pattern_pos = 0;
  size_t wildcard_pos = npos;
  size_t wildcard_value_pos = 0;

  auto read_escaped_literal = [escape_char](const std::string & text, size_t pos,
                                            char * literal, size_t * width) {
    if (pos + 1u >= text.size() || text[pos] != escape_char) {
      return false;
    }
    const char next = text[pos + 1u];
    if (next != '%' && next != '_' && next != escape_char) {
      return false;
    }
    *literal = next;
    *width = 2u;
    return true;
  };

  while (value_pos < value.size()) {
    if (pattern_pos < pattern.size() && pattern[pattern_pos] == '%') {
      wildcard_pos = pattern_pos++;
      wildcard_value_pos = value_pos;
      continue;
    }
    char literal = '\0';
    size_t literal_width = 1u;
    if (pattern_pos < pattern.size() &&
      read_escaped_literal(pattern, pattern_pos, &literal, &literal_width) &&
      literal == value[value_pos])
    {
      pattern_pos += literal_width;
      ++value_pos;
      continue;
    }
    if (pattern_pos < pattern.size() &&
      (pattern[pattern_pos] == '_' || pattern[pattern_pos] == value[value_pos]))
    {
      ++pattern_pos;
      ++value_pos;
      continue;
    }
    if (wildcard_pos != npos) {
      pattern_pos = wildcard_pos + 1u;
      value_pos = ++wildcard_value_pos;
      continue;
    }
    return false;
  }
  while (pattern_pos < pattern.size() && pattern[pattern_pos] == '%') {
    ++pattern_pos;
  }
  return pattern_pos == pattern.size();
}

bool StringContentFilterClauseMatches(
  const std::string & decoded, const StringContentFilterClause & clause)
{
  if (clause.op == 7 || clause.op == 8) {
    const bool like = StringLikeMatches(decoded, clause.value, clause.escape_char);
    return clause.op == 8 ? !like : like;
  }
  const bool equals = decoded == clause.value;
  return clause.op == 6 ? !equals : equals;
}

}  // namespace

bool PayloadMatchesContentFilter(
  const SubscriptionData & subscription, const std::vector<uint8_t> & payload, bool payload_is_mdds)
{
  if (subscription.numeric_filter_enabled) {
    // Decode the sample and evaluate grouped numeric clauses against introspected fields. If the
    // sample cannot be decoded / a field read fails, keep it (a filter must not silently drop data).
    void * message = subscription.adapter.AllocateMessage();
    if (message == nullptr) {
      return true;
    }
    bool keep = true;
    const bool decoded = payload_is_mdds ?
      subscription.adapter.DecodeMdds(payload.data(), payload.size(), message) :
      subscription.adapter.Decode(payload.data(), payload.size(), message);
    if (decoded) {
      if (!subscription.numeric_filter_disjunctions.empty()) {
        keep = false;
        for (const auto & disjunct : subscription.numeric_filter_disjunctions) {
          bool disjunct_matches = true;
          for (const auto & clause : disjunct) {
            double field_value = 0.0;
            if (!subscription.adapter.ReadNumericField(message, clause.field, &field_value)) {
              subscription.adapter.DestroyMessage(message);
              return true;
            }
            if (!CompareNumeric(field_value, clause.op, clause.value)) {
              disjunct_matches = false;
              break;
            }
          }
          if (disjunct_matches) {
            keep = true;
            break;
          }
        }
      } else {
        double field_value = 0.0;
        if (subscription.adapter.ReadNumericField(message, subscription.numeric_filter_field, &field_value)) {
          keep = CompareNumeric(
            field_value, subscription.numeric_filter_op, subscription.numeric_filter_value);
        }
      }
    }
    subscription.adapter.DestroyMessage(message);
    return keep;
  }
  if (!subscription.content_filter_enabled) {
    return true;
  }
  void * message = subscription.adapter.AllocateMessage();
  if (message == nullptr) {
    return true;
  }
  const bool decoded = payload_is_mdds ?
    subscription.adapter.DecodeMdds(payload.data(), payload.size(), message) :
    subscription.adapter.Decode(payload.data(), payload.size(), message);
  if (!decoded) {
    subscription.adapter.DestroyMessage(message);
    return true;
  }

  if (!subscription.string_filter_disjunctions.empty()) {
    for (const auto & disjunct : subscription.string_filter_disjunctions) {
      bool disjunct_matches = true;
      for (const auto & clause : disjunct) {
        std::string field_value;
        if (!subscription.adapter.ReadStringField(message, clause.field, &field_value)) {
          subscription.adapter.DestroyMessage(message);
          return true;
        }
        if (!StringContentFilterClauseMatches(field_value, clause)) {
          disjunct_matches = false;
          break;
        }
      }
      if (disjunct_matches) {
        subscription.adapter.DestroyMessage(message);
        return true;
      }
    }
    subscription.adapter.DestroyMessage(message);
    return false;
  }

  if (!subscription.string_filter_clauses.empty()) {
    for (const auto & clause : subscription.string_filter_clauses) {
      std::string field_value;
      if (!subscription.adapter.ReadStringField(message, clause.field, &field_value)) {
        subscription.adapter.DestroyMessage(message);
        return true;
      }
      if (!StringContentFilterClauseMatches(field_value, clause)) {
        subscription.adapter.DestroyMessage(message);
        return false;
      }
    }
    subscription.adapter.DestroyMessage(message);
    return true;
  }

  StringContentFilterClause clause;
  clause.field = "data";
  clause.op = subscription.string_filter_op;
  clause.value = subscription.string_filter_value;
  std::string field_value;
  if (!subscription.adapter.ReadStringField(message, clause.field, &field_value)) {
    subscription.adapter.DestroyMessage(message);
    return true;
  }
  const bool keep = StringContentFilterClauseMatches(field_value, clause);
  subscription.adapter.DestroyMessage(message);
  return keep;
}

bool OffersTransientLocalDurability(const PublisherData * publisher)
{
  return publisher != nullptr &&
         publisher->actual_qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
}

bool RequestsTransientLocalDurability(const SubscriptionData * subscription)
{
  return subscription != nullptr &&
         subscription->actual_qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
}

size_t TransientLocalHistoryDepth(const rmw_qos_profile_t & qos)
{
  if (qos.history == RMW_QOS_POLICY_HISTORY_KEEP_ALL) {
    return qos.depth == 0u ? std::numeric_limits<size_t>::max() : qos.depth;
  }
  return qos.depth == 0u ? 1u : qos.depth;
}

void StoreTransientLocalSample(PublisherData * publisher, const QueuedSample & sample)
{
  if (!OffersTransientLocalDurability(publisher)) {
    return;
  }
  std::lock_guard<std::mutex> lock(publisher->mutex);
  publisher->transient_local_history.push_back(sample);
  const size_t depth = TransientLocalHistoryDepth(publisher->actual_qos);
  while (publisher->transient_local_history.size() > depth) {
    publisher->transient_local_history.pop_front();
  }
}

std::vector<QueuedSample> RetainedTransientLocalSamplesFor(
  PublisherData * publisher, const SubscriptionData * subscription)
{
  if (!OffersTransientLocalDurability(publisher) || !RequestsTransientLocalDurability(subscription)) {
    return {};
  }
  if (
    subscription->ignore_local_publications &&
    subscription->context == publisher->context) {
    return {};
  }
  std::lock_guard<std::mutex> lock(publisher->mutex);
  return std::vector<QueuedSample>(
    publisher->transient_local_history.begin(), publisher->transient_local_history.end());
}

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
        if (SameTopicTypeAndCompatibleQos(publisher, subscription)) {
          ++publisher->matched_total_count;
          ++subscription->matched_total_count;
          AddSubscriptionCallbackInvocation(&callbacks, subscription, 1);
        } else {
          AccrueIncompatibleQosLocked(publisher, subscription, &callbacks);
        }
      } else {
        AccrueIncompatibleTypeLocked(publisher, subscription, &callbacks);
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
      if (SameTopicTypeAndCompatibleQos(publisher, subscription)) {
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
  std::vector<QueuedSample> retained_samples;
  {
    std::lock_guard<std::mutex> lock(g_broker_mutex);
    for (auto * publisher : g_publishers) {
      if (SameTopicAndType(publisher, subscription)) {
        if (SameTopicTypeAndCompatibleQos(publisher, subscription)) {
          ++publisher->matched_total_count;
          ++subscription->matched_total_count;
          AddPublisherCallbackInvocation(&callbacks, publisher, 1);
          const auto publisher_samples =
            RetainedTransientLocalSamplesFor(publisher, subscription);
          retained_samples.insert(
            retained_samples.end(), publisher_samples.begin(), publisher_samples.end());
        } else {
          AccrueIncompatibleQosLocked(publisher, subscription, &callbacks);
        }
      } else {
        AccrueIncompatibleTypeLocked(publisher, subscription, &callbacks);
      }
    }
    g_subscriptions.push_back(subscription);
  }
  InvokeCallbacks(callbacks);
  for (const auto & sample : retained_samples) {
    EnqueueSample(subscription, sample);
  }
}

void UnregisterSubscription(SubscriptionData * subscription)
{
  std::vector<CallbackInvocation> callbacks;
  {
    std::lock_guard<std::mutex> lock(g_broker_mutex);
    for (auto * publisher : g_publishers) {
      if (SameTopicTypeAndCompatibleQos(publisher, subscription)) {
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
      if (
        subscription->ignore_local_publications &&
        subscription->context == publisher->context) {
        continue;
      }
      if (SameTopicTypeAndCompatibleQos(publisher, subscription)) {
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
  StoreTransientLocalSample(publisher, sample);
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
    if (!PayloadMatchesContentFilter(*subscription, sample.payload, sample.from_bridge)) {
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
  std::vector<StringContentFilterClause> string_filter_clauses;
  std::vector<std::vector<StringContentFilterClause>> string_filter_disjunctions;
  std::string numeric_field;
  int numeric_op = 0;
  double numeric_value = 0.0;
  std::vector<std::vector<NumericContentFilterClause>> numeric_filter_disjunctions;
  if (!clear_filter &&
      !IsSupportedStringContentFilter(
        *subscription, options->filter_expression, options->expression_parameters,
        &string_filter_clauses, &string_filter_disjunctions)) {
    if (subscription->adapter.TypeName() == "std_msgs/msg/String") {
      return RMW_RET_UNSUPPORTED;
    }
    // Not the supported std_msgs/String filter — try a numeric field filter for other message types.
    rmw_reset_error();  // discard the string-filter rejection; a numeric filter may still be accepted
    std::vector<std::string> params;
    for (size_t i = 0; i < options->expression_parameters.size; ++i) {
      params.emplace_back(
        options->expression_parameters.data[i] != nullptr ? options->expression_parameters.data[i] : "");
    }
    if (!TryParseNumericFilter(
          options->filter_expression, params, &numeric_field, &numeric_op, &numeric_value,
          &numeric_filter_disjunctions) ||
        !NumericContentFilterFieldsAreSupported(
          subscription->adapter, numeric_filter_disjunctions)) {
      RMW_SET_ERROR_MSG("content filter is not a supported string or numeric field expression");
      return RMW_RET_UNSUPPORTED;
    }
    numeric = true;
  }

  std::lock_guard<std::mutex> lock(subscription->mutex);
  if (clear_filter) {
    subscription->content_filter_enabled = false;
    subscription->numeric_filter_enabled = false;
    subscription->string_filter_op = 1;
    subscription->string_filter_value.clear();
    subscription->string_filter_clauses.clear();
    subscription->string_filter_disjunctions.clear();
    subscription->numeric_filter_disjunctions.clear();
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
    subscription->numeric_filter_disjunctions = std::move(numeric_filter_disjunctions);
    subscription->string_filter_value.clear();
    subscription->string_filter_clauses.clear();
    subscription->string_filter_disjunctions.clear();
    return RMW_RET_OK;
  }
  subscription->content_filter_enabled = true;
  subscription->numeric_filter_enabled = false;
  subscription->numeric_filter_disjunctions.clear();
  subscription->string_filter_op = string_filter_clauses.front().op;
  subscription->string_filter_value = string_filter_clauses.front().value;
  subscription->string_filter_clauses = std::move(string_filter_clauses);
  subscription->string_filter_disjunctions = std::move(string_filter_disjunctions);
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
    if (!subscription->content_filter_enabled && !subscription->numeric_filter_enabled) {
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

// --- Offered / requested incompatible-type events (accrued under g_broker_mutex at registration) ---

bool TakePublisherIncompatibleTypeStatus(
  PublisherData * publisher, rmw_incompatible_type_status_t * status)
{
  if (publisher == nullptr || status == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  status->total_count = static_cast<int32_t>(publisher->offered_incompatible_type_total);
  status->total_count_change = static_cast<int32_t>(
    publisher->offered_incompatible_type_total - publisher->offered_incompatible_type_last);
  publisher->offered_incompatible_type_last = publisher->offered_incompatible_type_total;
  return true;
}

bool TakeSubscriptionIncompatibleTypeStatus(
  SubscriptionData * subscription, rmw_incompatible_type_status_t * status)
{
  if (subscription == nullptr || status == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  status->total_count = static_cast<int32_t>(subscription->requested_incompatible_type_total);
  status->total_count_change = static_cast<int32_t>(
    subscription->requested_incompatible_type_total -
    subscription->requested_incompatible_type_last);
  subscription->requested_incompatible_type_last =
    subscription->requested_incompatible_type_total;
  return true;
}

bool HasUnreadPublisherIncompatibleTypeStatus(PublisherData * publisher)
{
  if (publisher == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  return publisher->offered_incompatible_type_total != publisher->offered_incompatible_type_last;
}

bool HasUnreadSubscriptionIncompatibleTypeStatus(SubscriptionData * subscription)
{
  if (subscription == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  return subscription->requested_incompatible_type_total !=
         subscription->requested_incompatible_type_last;
}

size_t SetPublisherIncompatibleTypeCallback(
  PublisherData * publisher, rmw_event_callback_t callback, const void * user_data)
{
  if (publisher == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  publisher->offered_incompatible_type_callback = callback;
  publisher->offered_incompatible_type_callback_user_data = user_data;
  if (callback == nullptr) {
    return 0;
  }
  return publisher->offered_incompatible_type_total - publisher->offered_incompatible_type_last;
}

size_t SetSubscriptionIncompatibleTypeCallback(
  SubscriptionData * subscription, rmw_event_callback_t callback, const void * user_data)
{
  if (subscription == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_broker_mutex);
  subscription->requested_incompatible_type_callback = callback;
  subscription->requested_incompatible_type_callback_user_data = user_data;
  if (callback == nullptr) {
    return 0;
  }
  return subscription->requested_incompatible_type_total -
         subscription->requested_incompatible_type_last;
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

int64_t LivelinessLeaseNanos(const rmw_qos_profile_t & qos)
{
  if (qos.liveliness != RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC) {
    return 0;
  }
  const rmw_time_t lease = qos.liveliness_lease_duration;
  if ((lease.sec == 0 && lease.nsec == 0) || lease.sec >= kDeadlineInfiniteSec) {
    return 0;
  }
  return static_cast<int64_t>(lease.sec) * 1000000000LL + static_cast<int64_t>(lease.nsec);
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

size_t AccrueLivelinessLostLocked(PublisherData * publisher, int64_t now_ns)
{
  const int64_t lease_ns = LivelinessLeaseNanos(publisher->actual_qos);
  if (lease_ns == 0) {
    return 0;
  }
  if (publisher->offered_liveliness_last_assert_ns == 0) {
    publisher->offered_liveliness_last_assert_ns = now_ns;
    publisher->offered_liveliness_alive = true;
    return 0;
  }
  if (!publisher->offered_liveliness_alive ||
    now_ns - publisher->offered_liveliness_last_assert_ns <= lease_ns) {
    return 0;
  }
  publisher->offered_liveliness_alive = false;
  ++publisher->offered_liveliness_lost_total;
  return 1;
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

void NotePublisherLivelinessAsserted(PublisherData * publisher, int64_t now_ns)
{
  if (publisher == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(publisher->mutex);
  publisher->offered_liveliness_last_assert_ns = now_ns;
  publisher->offered_liveliness_alive = true;
}

void NoteNodePublishersLivelinessAsserted(
  const char * node_name, const char * node_namespace, int64_t now_ns)
{
  if (node_name == nullptr || node_namespace == nullptr) {
    return;
  }
  std::vector<PublisherData *> publishers;
  {
    std::lock_guard<std::mutex> lock(g_broker_mutex);
    for (auto * publisher : g_publishers) {
      if (
        publisher != nullptr &&
        BelongsToNode(publisher->node_name, publisher->node_namespace, node_name, node_namespace))
      {
        publishers.push_back(publisher);
      }
    }
  }
  for (auto * publisher : publishers) {
    NotePublisherLivelinessAsserted(publisher, now_ns);
  }
}

bool HasUnreadPublisherLivelinessLostStatus(PublisherData * publisher, int64_t now_ns)
{
  if (publisher == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(publisher->mutex);
  (void)AccrueLivelinessLostLocked(publisher, now_ns);
  return publisher->offered_liveliness_lost_total != publisher->offered_liveliness_lost_last;
}

bool TakePublisherLivelinessLostStatus(
  PublisherData * publisher, int64_t now_ns, rmw_liveliness_lost_status_t * status)
{
  if (publisher == nullptr || status == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(publisher->mutex);
  (void)AccrueLivelinessLostLocked(publisher, now_ns);
  status->total_count = static_cast<int32_t>(publisher->offered_liveliness_lost_total);
  status->total_count_change = static_cast<int32_t>(
    publisher->offered_liveliness_lost_total - publisher->offered_liveliness_lost_last);
  publisher->offered_liveliness_lost_last = publisher->offered_liveliness_lost_total;
  return true;
}

size_t SetPublisherLivelinessLostCallback(
  PublisherData * publisher, int64_t now_ns, rmw_event_callback_t callback,
  const void * user_data)
{
  if (publisher == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(publisher->mutex);
  publisher->offered_liveliness_lost_callback = callback;
  publisher->offered_liveliness_lost_callback_user_data = user_data;
  if (callback == nullptr) {
    return 0;
  }
  (void)AccrueLivelinessLostLocked(publisher, now_ns);
  return publisher->offered_liveliness_lost_total - publisher->offered_liveliness_lost_last;
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
