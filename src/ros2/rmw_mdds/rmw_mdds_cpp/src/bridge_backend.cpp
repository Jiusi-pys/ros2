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

#include <cstdlib>
#include <cstring>
#include <string>

#include <dlfcn.h>

namespace rmw_mdds_cpp {

namespace {
uint32_t TimeToMs(const rmw_time_t &time) {
  uint64_t ms = time.sec * 1000u + time.nsec / 1000000u;
  return ms > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(ms);
}

bool EnvValueDisabled(const char *value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
         std::strcmp(value, "FALSE") == 0 || std::strcmp(value, "off") == 0 ||
         std::strcmp(value, "OFF") == 0 || std::strcmp(value, "no") == 0 ||
         std::strcmp(value, "NO") == 0;
}
} // namespace

BridgeBackend &BridgeBackend::Instance() {
  static BridgeBackend backend;
  return backend;
}

bool BridgeBackend::Available() { return Load(); }

bool BridgeBackend::Required() const {
  const char *configured = std::getenv("RMW_MDDS_BRIDGE_LIBRARY");
  return configured != nullptr && configured[0] != '\0';
}

bool BridgeBackend::Load() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (load_attempted_) {
    return available_;
  }
  load_attempted_ = true;

  if (EnvValueDisabled(std::getenv("RMW_MDDS_BRIDGE"))) {
    return false;
  }

  const char *configured = std::getenv("RMW_MDDS_BRIDGE_LIBRARY");
  const char *library_name = (configured != nullptr && configured[0] != '\0')
                                 ? configured
                                 : "libmdds_bridge_shared.z.so";
  if (library_ == nullptr) {
    library_ = dlopen(library_name, RTLD_NOW | RTLD_LOCAL);
    if (library_ == nullptr && configured == nullptr) {
      library_ = dlopen("libmdds_bridge_shared.so", RTLD_NOW | RTLD_LOCAL);
    }
  }
  if (library_ == nullptr) {
    return false;
  }

  init_ = reinterpret_cast<int32_t (*)()>(Symbol("MddsBridgeInit"));
  shutdown_ = reinterpret_cast<void (*)()>(Symbol("MddsBridgeShutdown"));
  stop_spin_ = reinterpret_cast<void (*)()>(Symbol("MddsBridgeStopSpin"));
  create_publisher_qos_ = reinterpret_cast<void *(*)(const char *, const char *,
                                                     const BridgeQos *)>(
      Symbol("MddsBridgeCreatePublisherQos"));
  publish_ = reinterpret_cast<int32_t (*)(void *, const void *, uint32_t)>(
      Symbol("MddsBridgePublish"));
  borrow_loaned_sample_ =
      reinterpret_cast<int32_t (*)(void *, uint32_t, void **, void **)>(
          Symbol("MddsBridgeBorrowLoanedSample"));
  publish_loaned_ = reinterpret_cast<int32_t (*)(void *, void *, uint32_t)>(
      Symbol("MddsBridgePublishLoaned"));
  return_loaned_sample_ = reinterpret_cast<int32_t (*)(void *, void *)>(
      Symbol("MddsBridgeReturnLoanedSample"));
  destroy_publisher_ =
      reinterpret_cast<void (*)(void *)>(Symbol("MddsBridgeDestroyPublisher"));
  subscribe_qos_ =
      reinterpret_cast<void *(*)(const char *, const char *, const BridgeQos *,
                                 BridgeDataCallback, void *)>(
          Symbol("MddsBridgeSubscribeQos"));
  subscriber_take_loaned_ =
      reinterpret_cast<int32_t (*)(void *, BridgeLoanedMessage *)>(
          Symbol("MddsBridgeSubscriberTakeLoaned"));
  subscriber_take_loaned_with_storage_ =
      reinterpret_cast<int32_t (*)(
          void *, BridgeLoanedMessage *, uint32_t, void **, uint32_t *)>(
          Symbol("MddsBridgeSubscriberTakeLoanedWithStorage"));
  subscriber_return_loaned_ =
      reinterpret_cast<int32_t (*)(void *, BridgeLoanedMessage *)>(
          Symbol("MddsBridgeSubscriberReturnLoaned"));
  unsubscribe_ =
      reinterpret_cast<void (*)(void *)>(Symbol("MddsBridgeUnsubscribe"));
  // Optional: present only in bridge libs that expose matched-count
  // introspection.
  publisher_sub_count_ = reinterpret_cast<uint32_t (*)(void *)>(
      Symbol("MddsBridgePublisherGetSubCount"));
  publisher_unacked_count_ = reinterpret_cast<uint32_t (*)(void *)>(
      Symbol("MddsBridgePublisherGetUnackedCount"));
  publisher_set_on_matched_ =
      reinterpret_cast<int32_t (*)(void *, void (*)(uint32_t, void *), void *)>(
          Symbol("MddsBridgePublisherSetOnMatchedCallback"));
  subscriber_pub_count_ = reinterpret_cast<uint32_t (*)(void *)>(
      Symbol("MddsBridgeSubscriberGetPubCount"));
  subscriber_set_on_matched_ =
      reinterpret_cast<int32_t (*)(void *, void (*)(uint32_t, void *), void *)>(
          Symbol("MddsBridgeSubscriberSetOnMatchedCallback"));

  available_ = init_ != nullptr && shutdown_ != nullptr &&
               stop_spin_ != nullptr && create_publisher_qos_ != nullptr &&
               publish_ != nullptr && destroy_publisher_ != nullptr &&
               subscribe_qos_ != nullptr && unsubscribe_ != nullptr &&
               init_() == 0;
  return available_;
}

void *BridgeBackend::Symbol(const char *name) {
  return library_ == nullptr ? nullptr : dlsym(library_, name);
}

void *BridgeBackend::CreatePublisher(const char *topic_name,
                                     const char *type_name,
                                     const rmw_qos_profile_t *qos) {
  if (!Available()) {
    return nullptr;
  }
  BridgeQos bridge_qos = ToBridgeQos(qos);
  return create_publisher_qos_(topic_name, type_name, &bridge_qos);
}

int32_t BridgeBackend::Publish(void *publisher, const void *data,
                               uint32_t len) {
  if (!Available() || publisher == nullptr) {
    return -1;
  }
  return publish_(publisher, data, len);
}

bool BridgeBackend::BorrowLoanedSample(void *publisher, uint32_t size,
                                       void **loan, void **data) {
  if (!Available() || publisher == nullptr || loan == nullptr ||
      data == nullptr || borrow_loaned_sample_ == nullptr) {
    return false;
  }
  return borrow_loaned_sample_(publisher, size, loan, data) == 0;
}

bool BridgeBackend::PublishLoaned(void *publisher, void *loan, uint32_t len) {
  if (!Available() || publisher == nullptr || loan == nullptr ||
      publish_loaned_ == nullptr) {
    return false;
  }
  return publish_loaned_(publisher, loan, len) == 0;
}

bool BridgeBackend::ReturnLoanedSample(void *publisher, void *loan) {
  if (!Available() || publisher == nullptr || loan == nullptr ||
      return_loaned_sample_ == nullptr) {
    return false;
  }
  return return_loaned_sample_(publisher, loan) == 0;
}

bool BridgeBackend::SupportsPublisherLoanedMessages(void *publisher) {
  return Available() && publisher != nullptr && borrow_loaned_sample_ != nullptr &&
         publish_loaned_ != nullptr && return_loaned_sample_ != nullptr;
}

void BridgeBackend::DestroyPublisher(void *publisher) {
  if (publisher != nullptr && destroy_publisher_ != nullptr) {
    destroy_publisher_(publisher);
  }
}

void *BridgeBackend::Subscribe(const char *topic_name, const char *type_name,
                               const rmw_qos_profile_t *qos,
                               BridgeDataCallback callback, void *user_data) {
  if (!Available()) {
    return nullptr;
  }
  BridgeQos bridge_qos = ToBridgeQos(qos);
  return subscribe_qos_(topic_name, type_name, &bridge_qos, callback,
                        user_data);
}

bool BridgeBackend::SubscriberTakeLoaned(void *subscription,
                                         BridgeLoanedMessage *message) {
  if (!Available() || subscription == nullptr || message == nullptr ||
      subscriber_take_loaned_ == nullptr) {
    return false;
  }
  return subscriber_take_loaned_(subscription, message) == 0;
}

bool BridgeBackend::SubscriberTakeLoanedWithStorage(
    void *subscription, BridgeLoanedMessage *message, uint32_t storage_size,
    void **storage, uint32_t *storage_capacity) {
  if (storage != nullptr) {
    *storage = nullptr;
  }
  if (storage_capacity != nullptr) {
    *storage_capacity = 0;
  }
  if (!Available() || subscription == nullptr || message == nullptr ||
      storage == nullptr || storage_capacity == nullptr) {
    return false;
  }
  if (subscriber_take_loaned_with_storage_ != nullptr) {
    return subscriber_take_loaned_with_storage_(
        subscription, message, storage_size, storage, storage_capacity) == 0;
  }
  return SubscriberTakeLoaned(subscription, message);
}

bool BridgeBackend::SubscriberReturnLoaned(void *subscription,
                                           BridgeLoanedMessage *message) {
  if (!Available() || subscription == nullptr || message == nullptr ||
      subscriber_return_loaned_ == nullptr) {
    return false;
  }
  return subscriber_return_loaned_(subscription, message) == 0;
}

bool BridgeBackend::SupportsSubscriberLoanedMessages(void *subscription) {
  return Available() && subscription != nullptr && subscriber_take_loaned_ != nullptr &&
         subscriber_return_loaned_ != nullptr;
}

void BridgeBackend::Unsubscribe(void *subscription) {
  if (subscription != nullptr && unsubscribe_ != nullptr) {
    unsubscribe_(subscription);
  }
}

uint32_t BridgeBackend::PublisherSubCount(void *publisher) {
  if (!Available() || publisher == nullptr || publisher_sub_count_ == nullptr) {
    return 0u;
  }
  return publisher_sub_count_(publisher);
}

bool BridgeBackend::PublisherUnackedCount(void *publisher, uint32_t *count) {
  if (count == nullptr) {
    return false;
  }
  if (!Available() || publisher == nullptr || publisher_unacked_count_ == nullptr) {
    return false;
  }
  *count = publisher_unacked_count_(publisher);
  return true;
}

bool BridgeBackend::PublisherSetOnMatched(void *publisher,
                                          void (*callback)(uint32_t, void *),
                                          void *user_data) {
  if (!Available() || publisher == nullptr ||
      publisher_set_on_matched_ == nullptr) {
    return false;
  }
  return publisher_set_on_matched_(publisher, callback, user_data) == 0;
}

uint32_t BridgeBackend::SubscriberPubCount(void *subscription) {
  if (!Available() || subscription == nullptr ||
      subscriber_pub_count_ == nullptr) {
    return 0u;
  }
  return subscriber_pub_count_(subscription);
}

bool BridgeBackend::SubscriberSetOnMatched(void *subscription,
                                           void (*callback)(uint32_t, void *),
                                           void *user_data) {
  if (!Available() || subscription == nullptr ||
      subscriber_set_on_matched_ == nullptr) {
    return false;
  }
  return subscriber_set_on_matched_(subscription, callback, user_data) == 0;
}

void BridgeBackend::Shutdown() {
  // Quiesce the MDDS threads (spin + lane workers) and tear down the runtime
  // while openssl is still alive. The DSoftBus encrypt path (SoftBusEncrypt ->
  // RAND_bytes_ex) runs on the lane-worker thread; if the process exits with a
  // lane worker still live, openssl's atexit teardown wins the race and the
  // worker dereferences a freed crypto context -> SIGSEGV. Calling this from
  // rmw_context_fini (before the process's atexit phase) closes that race.
  //
  // Held under state_mutex_ so a later Load()/Available() either sees the
  // active runtime or a fully shut down runtime that can be initialized again
  // by a subsequent ROS context.
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (!available_) {
    return;
  }
  if (stop_spin_ != nullptr) {
    stop_spin_();
  }
  if (shutdown_ != nullptr) {
    shutdown_();
  }
  // Keep the library mapped (no dlclose); just mark it unusable so a late
  // Available() can safely call MddsBridgeInit again for a later context.
  available_ = false;
  load_attempted_ = false;
}

void BridgeBackend::ResetForTesting() {
  if (available_ && shutdown_ != nullptr) {
    shutdown_();
  }
  if (library_ != nullptr) {
    dlclose(library_);
  }
  library_ = nullptr;
  load_attempted_ = false;
  available_ = false;
  init_ = nullptr;
  shutdown_ = nullptr;
  stop_spin_ = nullptr;
  create_publisher_qos_ = nullptr;
  publish_ = nullptr;
  borrow_loaned_sample_ = nullptr;
  publish_loaned_ = nullptr;
  return_loaned_sample_ = nullptr;
  destroy_publisher_ = nullptr;
  subscribe_qos_ = nullptr;
  subscriber_take_loaned_ = nullptr;
  subscriber_return_loaned_ = nullptr;
  unsubscribe_ = nullptr;
  publisher_sub_count_ = nullptr;
  publisher_unacked_count_ = nullptr;
  publisher_set_on_matched_ = nullptr;
  subscriber_pub_count_ = nullptr;
  subscriber_set_on_matched_ = nullptr;
}

BridgeQos ToBridgeQos(const rmw_qos_profile_t *qos) {
  BridgeQos bridge_qos = {};
  bridge_qos.reliability =
      (qos != nullptr &&
       qos->reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT)
          ? 1
          : 0;
  bridge_qos.durability =
      (qos != nullptr &&
       qos->durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL)
          ? 1
          : 0;
  bridge_qos.historyKind =
      (qos != nullptr && qos->history == RMW_QOS_POLICY_HISTORY_KEEP_ALL) ? 1
                                                                          : 0;
  bridge_qos.historyDepth = (qos != nullptr && qos->depth > 0)
                                ? static_cast<uint32_t>(qos->depth)
                                : 10u;
  if (qos != nullptr) {
    bridge_qos.deadlineMs = TimeToMs(qos->deadline);
    bridge_qos.lifespanMs = TimeToMs(qos->lifespan);
  }
  return bridge_qos;
}

std::string ToMddsTopicName(const char *topic_name) {
  if (topic_name == nullptr) {
    return {};
  }
  std::string normalized(topic_name);
  const auto first_non_slash = normalized.find_first_not_of('/');
  if (first_non_slash == std::string::npos) {
    return {};
  }
  normalized.erase(0, first_non_slash);
  return normalized;
}

} // namespace rmw_mdds_cpp
