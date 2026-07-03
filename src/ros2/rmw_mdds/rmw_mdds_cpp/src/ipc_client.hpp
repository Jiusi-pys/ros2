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

#ifndef RMW_MDDS_CPP_SRC__IPC_CLIENT_HPP_
#define RMW_MDDS_CPP_SRC__IPC_CLIENT_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "broker.hpp"
#include "context.hpp"

namespace rmw_mdds_cpp
{

struct PublisherData;
struct SubscriptionData;
struct ClientData;
struct ServiceData;

using BrokerDeliveryCallback = void (*)(const std::vector<uint8_t> & payload, void * user_data);

bool BrokerModeEnabled();
bool BrokerBridgePayloadEnabled();
std::string BrokerSocketPath();
/* Track live rmw contexts so the process-shared embedded broker + MDDS bridge
 * are torn down only once the LAST context is fini'd (they are process
 * singletons; tearing them down on an earlier context's fini would break the
 * others). Call NoteContextInitialized() on each successful rmw_init. */
void NoteContextInitialized();

/* Decrement the live-context count and, when it reaches zero, stop the embedded
 * auto-started broker (if this process hosts it) and quiesce the MDDS bridge
 * runtime. Call from rmw_context_fini so the lane-worker threads are joined
 * before the process's atexit phase tears down openssl — otherwise a clean exit
 * races into a SIGSEGV in the DSoftBus encrypt path. No-op while other contexts
 * remain, and in a process that only connects to a remote broker. */
void ShutdownEmbeddedBrokerIfLastContext();
bool BrokerGraphHasMatchingService(const ClientData * client);
size_t CountBrokerGraphPublishersByTopic(const char * topic_name);
size_t CountBrokerGraphSubscriptionsByTopic(const char * topic_name);
size_t CountBrokerGraphPublishersForSubscription(const SubscriptionData * subscription);
size_t CountBrokerGraphSubscriptionsForPublisher(const PublisherData * publisher);
bool HasUnreadBrokerGraphPublisherMatchedStatus(PublisherData * publisher);
bool HasUnreadBrokerGraphSubscriptionMatchedStatus(SubscriptionData * subscription);
bool TakeBrokerGraphPublisherMatchedStatus(
  PublisherData * publisher, rmw_matched_status_t * status);
bool TakeBrokerGraphSubscriptionMatchedStatus(
  SubscriptionData * subscription, rmw_matched_status_t * status);
size_t CountBrokerGraphClientsByName(const char * service_name);
size_t CountBrokerGraphServicesByName(const char * service_name);
std::vector<NameAndTypes> GetBrokerGraphTopicNamesAndTypes();
std::vector<NameAndTypes> GetBrokerGraphPublisherNamesAndTypesByNode(
  const char * node_name, const char * node_namespace);
std::vector<NameAndTypes> GetBrokerGraphSubscriptionNamesAndTypesByNode(
  const char * node_name, const char * node_namespace);
std::vector<NameAndTypes> GetBrokerGraphServiceNamesAndTypes();
std::vector<NameAndTypes> GetBrokerGraphServiceNamesAndTypesByNode(
  const char * node_name, const char * node_namespace);
std::vector<NameAndTypes> GetBrokerGraphClientNamesAndTypesByNode(
  const char * node_name, const char * node_namespace);
std::vector<NodeGraphInfo> GetBrokerGraphNodes();
std::vector<TopicEndpointInfo> GetBrokerGraphPublisherEndpointInfosByTopic(
  const char * topic_name);
std::vector<TopicEndpointInfo> GetBrokerGraphSubscriptionEndpointInfosByTopic(
  const char * topic_name);

void * CreatePublisherBrokerClient(PublisherData * publisher, std::string * error);
void * CreateSubscriptionBrokerClient(SubscriptionData * subscription, std::string * error);
void * CreateClientBrokerClient(
  ClientData * client, BrokerDeliveryCallback callback, void * user_data, std::string * error);
void * CreateServiceBrokerClient(
  ServiceData * service, BrokerDeliveryCallback callback, void * user_data, std::string * error);
void DestroyBrokerClient(void * client);
bool BrokerClientPublish(
  void * client, const std::vector<uint8_t> & payload, uint64_t sequence_number,
  std::string * error, bool mdds_payload = false);

}  // namespace rmw_mdds_cpp

#endif  // RMW_MDDS_CPP_SRC__IPC_CLIENT_HPP_
