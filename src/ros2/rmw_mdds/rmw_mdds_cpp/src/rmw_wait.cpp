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

#include <chrono>
#include <thread>
#include <vector>

#include "broker.hpp"
#include "context.hpp"
#include "ipc_client.hpp"
#include "rmw/error_handling.h"
#include "rmw/rmw.h"

namespace
{
bool IsZeroTimeout(const rmw_time_t * timeout)
{
  return timeout != nullptr && timeout->sec == 0 && timeout->nsec == 0;
}

std::chrono::nanoseconds ToDuration(const rmw_time_t * timeout)
{
  if (timeout == nullptr) {
    return std::chrono::nanoseconds::max();
  }
  return std::chrono::seconds(timeout->sec) + std::chrono::nanoseconds(timeout->nsec);
}

bool MarkReadySubscriptions(rmw_subscriptions_t * subscriptions)
{
  if (subscriptions == nullptr) {
    return false;
  }
  std::vector<bool> ready(subscriptions->subscriber_count, false);
  bool any_ready = false;
  for (size_t i = 0; i < subscriptions->subscriber_count; ++i) {
    auto * data = static_cast<rmw_mdds_cpp::SubscriptionData *>(subscriptions->subscribers[i]);
    ready[i] = rmw_mdds_cpp::HasQueuedSample(data);
    any_ready = any_ready || ready[i];
  }
  if (any_ready) {
    for (size_t i = 0; i < subscriptions->subscriber_count; ++i) {
      if (!ready[i]) {
        subscriptions->subscribers[i] = nullptr;
      }
    }
  }
  return any_ready;
}

bool MarkReadyGuards(rmw_guard_conditions_t * guard_conditions)
{
  if (guard_conditions == nullptr) {
    return false;
  }
  std::vector<bool> ready(guard_conditions->guard_condition_count, false);
  bool any_ready = false;
  for (size_t i = 0; i < guard_conditions->guard_condition_count; ++i) {
    auto * data =
      static_cast<rmw_mdds_cpp::GuardConditionData *>(guard_conditions->guard_conditions[i]);
    ready[i] = data != nullptr &&
      data->triggered.exchange(false, std::memory_order_acq_rel);
    any_ready = any_ready || ready[i];
  }
  if (any_ready) {
    for (size_t i = 0; i < guard_conditions->guard_condition_count; ++i) {
      if (!ready[i]) {
        guard_conditions->guard_conditions[i] = nullptr;
      }
    }
  }
  return any_ready;
}

bool MarkReadyServices(rmw_services_t * services)
{
  if (services == nullptr) {
    return false;
  }
  std::vector<bool> ready(services->service_count, false);
  bool any_ready = false;
  for (size_t i = 0; i < services->service_count; ++i) {
    auto * data = static_cast<rmw_mdds_cpp::ServiceData *>(services->services[i]);
    ready[i] = rmw_mdds_cpp::HasQueuedServiceRequest(data);
    any_ready = any_ready || ready[i];
  }
  if (any_ready) {
    for (size_t i = 0; i < services->service_count; ++i) {
      if (!ready[i]) {
        services->services[i] = nullptr;
      }
    }
  }
  return any_ready;
}

bool MarkReadyClients(rmw_clients_t * clients)
{
  if (clients == nullptr) {
    return false;
  }
  std::vector<bool> ready(clients->client_count, false);
  bool any_ready = false;
  for (size_t i = 0; i < clients->client_count; ++i) {
    auto * data = static_cast<rmw_mdds_cpp::ClientData *>(clients->clients[i]);
    ready[i] = rmw_mdds_cpp::HasQueuedClientResponse(data);
    any_ready = any_ready || ready[i];
  }
  if (any_ready) {
    for (size_t i = 0; i < clients->client_count; ++i) {
      if (!ready[i]) {
        clients->clients[i] = nullptr;
      }
    }
  }
  return any_ready;
}

size_t CurrentMatchedPublishers(rmw_mdds_cpp::SubscriptionData * subscription)
{
  if (subscription == nullptr) {
    return 0;
  }
  if (rmw_mdds_cpp::BrokerModeEnabled()) {
    return rmw_mdds_cpp::CountBrokerGraphPublishersForSubscription(subscription);
  }
  return rmw_mdds_cpp::CountPublishersForSubscription(*subscription);
}

bool IsReadyEvent(rmw_event_t * event)
{
  if (event == nullptr || !rmw_mdds_cpp::IsMddsIdentifier(event->implementation_identifier)) {
    return false;
  }
  switch (event->event_type) {
    case RMW_EVENT_PUBLICATION_MATCHED:
      if (rmw_mdds_cpp::BrokerModeEnabled()) {
        return rmw_mdds_cpp::HasUnreadBrokerGraphPublisherMatchedStatus(
          static_cast<rmw_mdds_cpp::PublisherData *>(event->data));
      }
      return rmw_mdds_cpp::HasUnreadPublisherMatchedStatus(
        static_cast<rmw_mdds_cpp::PublisherData *>(event->data));
    case RMW_EVENT_SUBSCRIPTION_MATCHED:
      if (rmw_mdds_cpp::BrokerModeEnabled()) {
        return rmw_mdds_cpp::HasUnreadBrokerGraphSubscriptionMatchedStatus(
          static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data));
      }
      return rmw_mdds_cpp::HasUnreadSubscriptionMatchedStatus(
        static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data));
    case RMW_EVENT_LIVELINESS_CHANGED: {
      auto * subscription = static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data);
      return rmw_mdds_cpp::HasUnreadSubscriptionLivelinessStatus(
        subscription, CurrentMatchedPublishers(subscription));
    }
    case RMW_EVENT_REQUESTED_DEADLINE_MISSED: {
      auto * subscription = static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data);
      return rmw_mdds_cpp::HasUnreadSubscriptionDeadlineStatus(
        subscription, rmw_mdds_cpp::MddsNowNanoseconds(),
        CurrentMatchedPublishers(subscription) > 0);
    }
    case RMW_EVENT_OFFERED_DEADLINE_MISSED:
      return rmw_mdds_cpp::HasUnreadPublisherDeadlineStatus(
        static_cast<rmw_mdds_cpp::PublisherData *>(event->data),
        rmw_mdds_cpp::MddsNowNanoseconds());
    case RMW_EVENT_LIVELINESS_LOST:
      return rmw_mdds_cpp::HasUnreadPublisherLivelinessLostStatus(
        static_cast<rmw_mdds_cpp::PublisherData *>(event->data),
        rmw_mdds_cpp::MddsNowNanoseconds());
    case RMW_EVENT_OFFERED_QOS_INCOMPATIBLE:
      return rmw_mdds_cpp::HasUnreadPublisherQosIncompatibleStatus(
        static_cast<rmw_mdds_cpp::PublisherData *>(event->data));
    case RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE:
      return rmw_mdds_cpp::HasUnreadSubscriptionQosIncompatibleStatus(
        static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data));
    case RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE:
      return rmw_mdds_cpp::HasUnreadPublisherIncompatibleTypeStatus(
        static_cast<rmw_mdds_cpp::PublisherData *>(event->data));
    case RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE:
      return rmw_mdds_cpp::HasUnreadSubscriptionIncompatibleTypeStatus(
        static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data));
    case RMW_EVENT_MESSAGE_LOST:
      return rmw_mdds_cpp::HasUnreadSubscriptionMessageLostStatus(
        static_cast<rmw_mdds_cpp::SubscriptionData *>(event->data));
    default:
      return false;
  }
}

bool MarkReadyEvents(rmw_events_t * events)
{
  if (events == nullptr) {
    return false;
  }
  std::vector<bool> ready(events->event_count, false);
  bool any_ready = false;
  for (size_t i = 0; i < events->event_count; ++i) {
    auto * event = static_cast<rmw_event_t *>(events->events[i]);
    ready[i] = IsReadyEvent(event);
    any_ready = any_ready || ready[i];
  }
  if (any_ready) {
    for (size_t i = 0; i < events->event_count; ++i) {
      if (!ready[i]) {
        events->events[i] = nullptr;
      }
    }
  }
  return any_ready;
}

void ClearSubscriptions(rmw_subscriptions_t * subscriptions)
{
  if (subscriptions == nullptr) {
    return;
  }
  for (size_t i = 0; i < subscriptions->subscriber_count; ++i) {
    subscriptions->subscribers[i] = nullptr;
  }
}

void ClearGuards(rmw_guard_conditions_t * guard_conditions)
{
  if (guard_conditions == nullptr) {
    return;
  }
  for (size_t i = 0; i < guard_conditions->guard_condition_count; ++i) {
    guard_conditions->guard_conditions[i] = nullptr;
  }
}

void ClearServices(rmw_services_t * services)
{
  if (services == nullptr) {
    return;
  }
  for (size_t i = 0; i < services->service_count; ++i) {
    services->services[i] = nullptr;
  }
}

void ClearClients(rmw_clients_t * clients)
{
  if (clients == nullptr) {
    return;
  }
  for (size_t i = 0; i < clients->client_count; ++i) {
    clients->clients[i] = nullptr;
  }
}

void ClearEvents(rmw_events_t * events)
{
  if (events == nullptr) {
    return;
  }
  for (size_t i = 0; i < events->event_count; ++i) {
    events->events[i] = nullptr;
  }
}

void ClearAll(
  rmw_subscriptions_t * subscriptions, rmw_guard_conditions_t * guard_conditions,
  rmw_services_t * services, rmw_clients_t * clients, rmw_events_t * events)
{
  ClearSubscriptions(subscriptions);
  ClearGuards(guard_conditions);
  ClearServices(services);
  ClearClients(clients);
  ClearEvents(events);
}
}  // namespace

extern "C" {
rmw_ret_t rmw_wait(
  rmw_subscriptions_t * subscriptions, rmw_guard_conditions_t * guard_conditions,
  rmw_services_t * services, rmw_clients_t * clients, rmw_events_t * events,
  rmw_wait_set_t * wait_set, const rmw_time_t * wait_timeout)
{
  if (wait_set == nullptr) {
    RMW_SET_ERROR_MSG("wait set is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rmw_mdds_cpp::IsMddsIdentifier(wait_set->implementation_identifier)) {
    RMW_SET_ERROR_MSG("wait set implementation identifier does not match rmw_mdds_cpp");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }

  const auto start = std::chrono::steady_clock::now();
  const auto timeout = ToDuration(wait_timeout);
  while (true) {
    bool subscription_ready = MarkReadySubscriptions(subscriptions);
    bool guard_ready = MarkReadyGuards(guard_conditions);
    bool service_ready = MarkReadyServices(services);
    bool client_ready = MarkReadyClients(clients);
    bool event_ready = MarkReadyEvents(events);
    if (subscription_ready || guard_ready || service_ready || client_ready || event_ready) {
      if (!subscription_ready) {
        ClearSubscriptions(subscriptions);
      }
      if (!guard_ready) {
        ClearGuards(guard_conditions);
      }
      if (!service_ready) {
        ClearServices(services);
      }
      if (!client_ready) {
        ClearClients(clients);
      }
      if (!event_ready) {
        ClearEvents(events);
      }
      return RMW_RET_OK;
    }
    if (IsZeroTimeout(wait_timeout) || std::chrono::steady_clock::now() - start >= timeout) {
      ClearAll(subscriptions, guard_conditions, services, clients, events);
      return RMW_RET_TIMEOUT;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
}  // extern "C"
