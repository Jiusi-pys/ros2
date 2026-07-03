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

#ifndef RMW_MDDS_CPP_SRC__BROKER_HPP_
#define RMW_MDDS_CPP_SRC__BROKER_HPP_

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "rmw/init.h"
#include "rmw/event_callback_type.h"
#include "rmw/events_statuses/matched.h"
#include "rmw/events_statuses/liveliness_changed.h"
#include "rmw/events_statuses/liveliness_lost.h"
#include "rmw/events_statuses/requested_deadline_missed.h"
#include "rmw/events_statuses/offered_deadline_missed.h"
#include "rmw/events_statuses/incompatible_qos.h"
#include "rmw/events_statuses/incompatible_type.h"
#include "rmw/events_statuses/message_lost.h"
#include "rmw/qos_profiles.h"
#include "rmw/types.h"
#include "rosidl_runtime_c/type_hash.h"
#include "message_adapter.hpp"
#include "rtps_protocol.hpp"

namespace rmw_mdds_cpp
{

struct BridgePublisherLoanRecord
{
  void * loan = nullptr;
  void * data = nullptr;
  uint32_t capacity = 0;
  bool message_in_loan = false;
  bool raw_message_in_loan = false;
};

struct QueuedSample
{
  std::vector<uint8_t> payload;
  rmw_message_info_t info;
  bool from_bridge = false;
};

struct PublisherData
{
  rmw_context_t * context;
  std::string topic_name;
  std::string mdds_topic_name;
  std::string node_name;
  std::string node_namespace;
  std::string node_enclave;
  rmw_qos_profile_t actual_qos;
  MessageAdapter adapter;
  rtps::EntityId rtps_entity_id{};
  size_t matched_total_count = 0;
  size_t matched_last_total_count = 0;
  size_t matched_last_current_count = 0;
  size_t broker_matched_total_count = 0;
  size_t broker_matched_last_total_count = 0;
  size_t broker_matched_last_current_count = 0;
  rmw_event_callback_t matched_callback = nullptr;
  const void * matched_callback_user_data = nullptr;
  // Offered-deadline QoS enforcement (lazy poll accounting; see broker.cpp deadline helpers).
  int64_t offered_deadline_last_active_ns = 0;
  size_t offered_deadline_missed_total = 0;
  size_t offered_deadline_missed_last = 0;
  rmw_event_callback_t offered_deadline_callback = nullptr;
  const void * offered_deadline_callback_user_data = nullptr;
  // Offered-liveliness-lost QoS enforcement for MANUAL_BY_TOPIC publishers.
  int64_t offered_liveliness_last_assert_ns = 0;
  bool offered_liveliness_alive = true;
  size_t offered_liveliness_lost_total = 0;
  size_t offered_liveliness_lost_last = 0;
  rmw_event_callback_t offered_liveliness_lost_callback = nullptr;
  const void * offered_liveliness_lost_callback_user_data = nullptr;
  // Offered-QoS-incompatible: raised when a matched subscription requests a QoS this publisher's offer
  // cannot satisfy (detected at match time via rmw_dds_common compatibility check).
  size_t offered_qos_incompatible_total = 0;
  size_t offered_qos_incompatible_last = 0;
  rmw_qos_policy_kind_t offered_qos_last_policy_kind = RMW_QOS_POLICY_INVALID;
  rmw_event_callback_t offered_qos_incompatible_callback = nullptr;
  const void * offered_qos_incompatible_callback_user_data = nullptr;
  // Incompatible-type: same ROS topic name but different ROS type name.
  size_t offered_incompatible_type_total = 0;
  size_t offered_incompatible_type_last = 0;
  rmw_event_callback_t offered_incompatible_type_callback = nullptr;
  const void * offered_incompatible_type_callback_user_data = nullptr;
  std::mutex mutex;
  uint64_t next_publication_sequence_number = 1;
  std::deque<QueuedSample> transient_local_history;
  // Tracks every active publisher loaned message. loan is non-null only when
  // the typed ROS message is also backed by a bridge transport loan.
  std::map<void *, BridgePublisherLoanRecord> bridge_publisher_loans;
  bool bridge_reliable_publication_unacknowledged = false;
  void * bridge_publisher = nullptr;
  void * broker_client = nullptr;
};

struct BridgeLoanedMessageRecord
{
  const void * data = nullptr;
  uint32_t len = 0;
  uint64_t timestamp = 0;
  uint64_t sequenceNumber = 0;
  std::array<uint8_t, 16> senderGuid{};
  void * loanHandle = nullptr;
  uint8_t loanKind = 0;
  bool messageInBridgeStorage = false;
  bool rawMessageInBridgeLoan = false;
};

struct StringContentFilterClause
{
  std::string field = "data";
  int op = 1;  // 1:==  6:!=  7:LIKE  8:NOT LIKE
  std::string value;
  char escape_char = '\\';
};

struct NumericContentFilterClause
{
  std::string field;
  int op = 0;  // 1:<  2:<=  3:>  4:>=  5:==  6:!=
  double value = 0.0;
};

struct SubscriptionData
{
  rmw_context_t * context;
  std::string topic_name;
  std::string mdds_topic_name;
  std::string node_name;
  std::string node_namespace;
  std::string node_enclave;
  rmw_qos_profile_t actual_qos;
  bool ignore_local_publications = false;
  MessageAdapter adapter;
  rtps::EntityId rtps_entity_id{};
  size_t matched_total_count = 0;
  size_t matched_last_total_count = 0;
  size_t matched_last_current_count = 0;
  size_t broker_matched_total_count = 0;
  size_t broker_matched_last_total_count = 0;
  size_t broker_matched_last_current_count = 0;
  rmw_event_callback_t matched_callback = nullptr;
  const void * matched_callback_user_data = nullptr;
  rmw_event_callback_t new_message_callback = nullptr;
  const void * new_message_callback_user_data = nullptr;
  // Requested-deadline QoS enforcement (lazy poll accounting; reset on sample arrival).
  int64_t requested_deadline_last_active_ns = 0;
  size_t requested_deadline_missed_total = 0;
  size_t requested_deadline_missed_last = 0;
  rmw_event_callback_t requested_deadline_callback = nullptr;
  const void * requested_deadline_callback_user_data = nullptr;
  // Liveliness-changed QoS (AUTOMATIC): alive_count == currently-matched publisher count.
  size_t liveliness_last_alive_count = 0;
  bool liveliness_initialized = false;
  rmw_event_callback_t liveliness_callback = nullptr;
  const void * liveliness_callback_user_data = nullptr;
  // Requested-QoS-incompatible: raised when matched to a publisher offering an incompatible QoS.
  size_t requested_qos_incompatible_total = 0;
  size_t requested_qos_incompatible_last = 0;
  rmw_qos_policy_kind_t requested_qos_last_policy_kind = RMW_QOS_POLICY_INVALID;
  rmw_event_callback_t requested_qos_incompatible_callback = nullptr;
  const void * requested_qos_incompatible_callback_user_data = nullptr;
  // Incompatible-type: same ROS topic name but different ROS type name.
  size_t requested_incompatible_type_total = 0;
  size_t requested_incompatible_type_last = 0;
  rmw_event_callback_t requested_incompatible_type_callback = nullptr;
  const void * requested_incompatible_type_callback_user_data = nullptr;
  // Message-lost: detected from gaps in each writer's publication sequence numbers. In-process
  // delivery is lossless (count stays 0); gaps occur on the lossy RTPS / bridge ingress path.
  size_t message_lost_total = 0;
  size_t message_lost_last = 0;
  rmw_event_callback_t message_lost_callback = nullptr;
  const void * message_lost_callback_user_data = nullptr;
  std::map<std::array<uint8_t, RMW_GID_STORAGE_SIZE>, uint64_t> last_publication_seq_by_writer;
  std::mutex mutex;
  std::deque<QueuedSample> queue;
  std::map<void *, BridgeLoanedMessageRecord> bridge_loaned_messages;
  uint64_t next_reception_sequence_number = 1;
  bool content_filter_enabled = false;
  std::string content_filter_expression;
  std::vector<std::string> content_filter_parameters;
  std::string string_filter_value;
  int string_filter_op = 1;  // 1:==  6:!=  7:LIKE  8:NOT LIKE
  std::vector<StringContentFilterClause> string_filter_clauses;
  std::vector<std::vector<StringContentFilterClause>> string_filter_disjunctions;
  // Numeric content filter (`field OP number`, DDS-SQL subset) for non-String message types, evaluated
  // against the introspected field. Mutually exclusive with the std_msgs/String string filter above:
  // at most one of content_filter_enabled / numeric_filter_enabled is set.
  bool numeric_filter_enabled = false;
  std::string numeric_filter_field;
  int numeric_filter_op = 0;  // 1:<  2:<=  3:>  4:>=  5:==  6:!=
  double numeric_filter_value = 0.0;
  std::vector<std::vector<NumericContentFilterClause>> numeric_filter_disjunctions;
  void * bridge_subscription = nullptr;
  void * broker_client = nullptr;
};

struct NameAndTypes
{
  std::string name;
  std::vector<std::string> types;
};

struct TopicEndpointInfo
{
  std::string node_name;
  std::string node_namespace;
  std::string topic_type;
  rosidl_type_hash_t topic_type_hash = rosidl_get_zero_initialized_type_hash();
  rmw_endpoint_type_t endpoint_type = RMW_ENDPOINT_INVALID;
  rmw_gid_t gid{};
  rmw_qos_profile_t qos_profile = rmw_qos_profile_default;
};

void RegisterPublisher(PublisherData * publisher);
void UnregisterPublisher(PublisherData * publisher);
void RegisterSubscription(SubscriptionData * subscription);
void UnregisterSubscription(SubscriptionData * subscription);
uint64_t ReservePublicationSequenceNumber(PublisherData * publisher);
void PublishToSubscriptions(PublisherData * publisher, const std::vector<uint8_t> & payload);
void PublishToSubscriptions(
  PublisherData * publisher, const std::vector<uint8_t> & payload,
  uint64_t publication_sequence_number);
void EnqueueSample(SubscriptionData * subscription, const QueuedSample & sample);
bool PayloadMatchesContentFilter(
  const SubscriptionData & subscription, const std::vector<uint8_t> & payload,
  bool payload_is_mdds = false);
size_t EnqueueRtpsUserDataForReader(
  const rtps::EntityId & reader_id, const rtps::GuidPrefix & writer_guid_prefix,
  const rtps::EntityId & writer_id, int64_t writer_sequence_number,
  const std::vector<uint8_t> & payload);
size_t EnqueueRtpsUserDataForTopic(
  const std::string & dds_topic_name, const rtps::GuidPrefix & writer_guid_prefix,
  const rtps::EntityId & writer_id, int64_t writer_sequence_number,
  const std::vector<uint8_t> & payload);
bool HasQueuedSample(SubscriptionData * subscription);
bool TakeQueuedSample(SubscriptionData * subscription, QueuedSample * sample);
void FillPublisherGid(const PublisherData * publisher, rmw_gid_t * gid);
rmw_ret_t SetSubscriptionContentFilter(
  SubscriptionData * subscription, const rmw_subscription_content_filter_options_t * options);
rmw_ret_t GetSubscriptionContentFilter(
  SubscriptionData * subscription, const rcutils_allocator_t * allocator,
  rmw_subscription_content_filter_options_t * options);
size_t CountSubscriptionsForPublisher(const PublisherData & publisher);
size_t CountPublishersForSubscription(const SubscriptionData & subscription);
bool TakePublisherMatchedStatus(PublisherData * publisher, rmw_matched_status_t * status);
bool TakeSubscriptionMatchedStatus(SubscriptionData * subscription, rmw_matched_status_t * status);
bool HasUnreadPublisherMatchedStatus(PublisherData * publisher);
bool HasUnreadSubscriptionMatchedStatus(SubscriptionData * subscription);
size_t SetPublisherMatchedCallback(
  PublisherData * publisher, rmw_event_callback_t callback, const void * user_data);
size_t SetSubscriptionMatchedCallback(
  SubscriptionData * subscription, rmw_event_callback_t callback, const void * user_data);
size_t SetSubscriptionNewMessageCallback(
  SubscriptionData * subscription, rmw_event_callback_t callback, const void * user_data);

// Offered / requested QoS-incompatible events. Counts and last-incompatible policy kind are accrued at
// match time (RegisterPublisher / RegisterSubscription) by an rmw_dds_common compatibility check.
bool TakePublisherQosIncompatibleStatus(
  PublisherData * publisher, rmw_qos_incompatible_event_status_t * status);
bool TakeSubscriptionQosIncompatibleStatus(
  SubscriptionData * subscription, rmw_qos_incompatible_event_status_t * status);
bool HasUnreadPublisherQosIncompatibleStatus(PublisherData * publisher);
bool HasUnreadSubscriptionQosIncompatibleStatus(SubscriptionData * subscription);
size_t SetPublisherQosIncompatibleCallback(
  PublisherData * publisher, rmw_event_callback_t callback, const void * user_data);
size_t SetSubscriptionQosIncompatibleCallback(
  SubscriptionData * subscription, rmw_event_callback_t callback, const void * user_data);

// Offered / requested incompatible-type events. Counts are accrued when endpoints share a topic name
// but advertise different ROS type names, so they are visible even though the endpoints never match.
bool TakePublisherIncompatibleTypeStatus(
  PublisherData * publisher, rmw_incompatible_type_status_t * status);
bool TakeSubscriptionIncompatibleTypeStatus(
  SubscriptionData * subscription, rmw_incompatible_type_status_t * status);
bool HasUnreadPublisherIncompatibleTypeStatus(PublisherData * publisher);
bool HasUnreadSubscriptionIncompatibleTypeStatus(SubscriptionData * subscription);
size_t SetPublisherIncompatibleTypeCallback(
  PublisherData * publisher, rmw_event_callback_t callback, const void * user_data);
size_t SetSubscriptionIncompatibleTypeCallback(
  SubscriptionData * subscription, rmw_event_callback_t callback, const void * user_data);

// Message-lost event: detected from gaps in each writer's publication sequence numbers on ingress.
bool TakeSubscriptionMessageLostStatus(
  SubscriptionData * subscription, rmw_message_lost_status_t * status);
bool HasUnreadSubscriptionMessageLostStatus(SubscriptionData * subscription);
size_t SetSubscriptionMessageLostCallback(
  SubscriptionData * subscription, rmw_event_callback_t callback, const void * user_data);

// Shared wall clock (nanoseconds) used for deadline / lifespan accounting.
int64_t MddsNowNanoseconds();

// Liveliness-changed QoS (AUTOMATIC). The current alive (matched-publisher) count is
// supplied by the caller so it can use the broker-graph or local registry as appropriate.
bool HasUnreadSubscriptionLivelinessStatus(SubscriptionData * subscription, size_t alive_count);
bool TakeSubscriptionLivelinessStatus(
  SubscriptionData * subscription, size_t alive_count, rmw_liveliness_changed_status_t * status);
size_t SetSubscriptionLivelinessCallback(
  SubscriptionData * subscription, size_t alive_count, rmw_event_callback_t callback,
  const void * user_data);

// Deadline QoS enforcement (lazy poll accounting up to now_ns; no monitor thread).
// NoteSubscriptionSampleArrival / NotePublisherPublication reset the active reference so
// on-time traffic never registers a miss. `matched` gates requested-deadline misses.
void NoteSubscriptionSampleArrival(SubscriptionData * subscription, int64_t now_ns);
void NotePublisherPublication(PublisherData * publisher, int64_t now_ns);
void NotePublisherLivelinessAsserted(PublisherData * publisher, int64_t now_ns);
void NoteNodePublishersLivelinessAsserted(
  const char * node_name, const char * node_namespace, int64_t now_ns);
bool HasUnreadPublisherLivelinessLostStatus(PublisherData * publisher, int64_t now_ns);
bool TakePublisherLivelinessLostStatus(
  PublisherData * publisher, int64_t now_ns, rmw_liveliness_lost_status_t * status);
size_t SetPublisherLivelinessLostCallback(
  PublisherData * publisher, int64_t now_ns, rmw_event_callback_t callback,
  const void * user_data);
bool HasUnreadSubscriptionDeadlineStatus(
  SubscriptionData * subscription, int64_t now_ns, bool matched);
bool TakeSubscriptionDeadlineStatus(
  SubscriptionData * subscription, int64_t now_ns, bool matched,
  rmw_requested_deadline_missed_status_t * status);
size_t SetSubscriptionDeadlineCallback(
  SubscriptionData * subscription, int64_t now_ns, bool matched, rmw_event_callback_t callback,
  const void * user_data);
bool HasUnreadPublisherDeadlineStatus(PublisherData * publisher, int64_t now_ns);
bool TakePublisherDeadlineStatus(
  PublisherData * publisher, int64_t now_ns, rmw_offered_deadline_missed_status_t * status);
size_t SetPublisherDeadlineCallback(
  PublisherData * publisher, int64_t now_ns, rmw_event_callback_t callback,
  const void * user_data);

size_t CountPublishersByTopic(const char * topic_name);
size_t CountSubscriptionsByTopic(const char * topic_name);
std::vector<NameAndTypes> GetTopicNamesAndTypes();
std::vector<NameAndTypes> GetPublisherNamesAndTypesByNode(
  const char * node_name, const char * node_namespace);
std::vector<NameAndTypes> GetSubscriptionNamesAndTypesByNode(
  const char * node_name, const char * node_namespace);
std::vector<TopicEndpointInfo> GetPublisherEndpointInfosByTopic(const char * topic_name);
std::vector<TopicEndpointInfo> GetSubscriptionEndpointInfosByTopic(const char * topic_name);

}  // namespace rmw_mdds_cpp

#endif  // RMW_MDDS_CPP_SRC__BROKER_HPP_
