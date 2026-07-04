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

#ifndef RMW_MDDS_CPP_SRC__BRIDGE_BACKEND_HPP_
#define RMW_MDDS_CPP_SRC__BRIDGE_BACKEND_HPP_

#include <cstdint>
#include <mutex>
#include <string>

#include "rmw/types.h"

namespace rmw_mdds_cpp {

struct BridgeQos {
  int reliability;
  int durability;
  int historyKind;
  uint32_t historyDepth;
  uint32_t deadlineMs;
  uint32_t lifespanMs;
};

struct BridgeSample {
  const void *data;
  uint32_t len;
  uint64_t sequenceNumber;
  uint8_t senderGuid[16];
};

struct BridgeLoanedMessage {
  const void *data;
  uint32_t len;
  uint64_t timestamp;
  uint64_t sequenceNumber;
  uint8_t senderGuid[16];
  void *loanHandle;
  uint8_t loanKind;
};

using BridgeDataCallback = void (*)(const BridgeSample *sample, void *userData);

class BridgeBackend {
public:
  static BridgeBackend &Instance();

  bool Available();
  bool Required() const;
  void *CreatePublisher(const char *topic_name, const char *type_name,
                        const rmw_qos_profile_t *qos);
  int32_t Publish(void *publisher, const void *data, uint32_t len);
  bool BorrowLoanedSample(void *publisher, uint32_t size, void **loan,
                          void **data);
  bool PublishLoaned(void *publisher, void *loan, uint32_t len);
  bool ReturnLoanedSample(void *publisher, void *loan);
  bool SupportsPublisherLoanedMessages(void *publisher);
  void DestroyPublisher(void *publisher);
  void *Subscribe(const char *topic_name, const char *type_name,
                  const rmw_qos_profile_t *qos, BridgeDataCallback callback,
                  void *user_data);
  bool SubscriberTakeLoaned(void *subscription, BridgeLoanedMessage *message);
  bool SubscriberTakeLoanedWithStorage(
      void *subscription, BridgeLoanedMessage *message, uint32_t storage_size,
      void **storage, uint32_t *storage_capacity);
  bool SubscriberReturnLoaned(void *subscription, BridgeLoanedMessage *message);
  bool SupportsSubscriberLoanedMessages(void *subscription);
  void Unsubscribe(void *subscription);
  /* Matched remote subscriber count for a bridge publisher (0 if the loaded
   * library predates this symbol). Lets the broker detect a remote service
   * provider for a local client's rq/ publisher across boards. */
  uint32_t PublisherSubCount(void *publisher);
  bool PublisherUnackedCount(void *publisher, uint32_t *count);
  /* Register a matched-count change listener on a bridge publisher; false if
   * unsupported by the loaded library. */
  bool PublisherSetOnMatched(void *publisher,
                             void (*callback)(uint32_t, void *),
                             void *user_data);
  /* Matched remote publisher count for a bridge subscriber (0 if unsupported);
   * detects a remote publisher for a local subscription across boards. */
  uint32_t SubscriberPubCount(void *subscription);
  bool SubscriberSetOnMatched(void *subscription,
                              void (*callback)(uint32_t, void *),
                              void *user_data);
  bool ActivateProtectedTransport(bool require_authenticated,
                                  bool require_encrypted, std::string *error);
  /* Stop the MDDS spin + lane-worker threads and tear down the runtime while
   * the process (hence openssl, used by the DSoftBus encrypt path) is still
   * alive. Must run before process exit: otherwise a lane worker can call into
   * openssl after its atexit teardown -> SIGSEGV on clean shutdown. Unlike
   * ResetForTesting() this keeps the library mapped (no dlclose), so it is safe
   * to call from rmw_context_fini. Idempotent. */
  void Shutdown();
  void ResetForTesting();

private:
  bool Load();
  void *Symbol(const char *name);

  // Serializes the lazy Load() against Shutdown() so the available_ /
  // load_attempted_ latch is not read+written by two threads at once (e.g. a
  // concurrent rmw_init Available() racing the last-context teardown's
  // Shutdown()).
  std::mutex state_mutex_;
  void *library_ = nullptr;
  bool load_attempted_ = false;
  bool available_ = false;

  int32_t (*init_)() = nullptr;
  void (*shutdown_)() = nullptr;
  void (*stop_spin_)() = nullptr;
  void *(*create_publisher_qos_)(const char *, const char *,
                                 const BridgeQos *) = nullptr;
  int32_t (*publish_)(void *, const void *, uint32_t) = nullptr;
  int32_t (*borrow_loaned_sample_)(void *, uint32_t, void **,
                                   void **) = nullptr;
  int32_t (*publish_loaned_)(void *, void *, uint32_t) = nullptr;
  int32_t (*return_loaned_sample_)(void *, void *) = nullptr;
  void (*destroy_publisher_)(void *) = nullptr;
  void *(*subscribe_qos_)(const char *, const char *, const BridgeQos *,
                          BridgeDataCallback, void *) = nullptr;
  int32_t (*subscriber_take_loaned_)(void *, BridgeLoanedMessage *) = nullptr;
  int32_t (*subscriber_take_loaned_with_storage_)(
      void *, BridgeLoanedMessage *, uint32_t, void **, uint32_t *) = nullptr;
  int32_t (*subscriber_return_loaned_)(void *, BridgeLoanedMessage *) = nullptr;
  void (*unsubscribe_)(void *) = nullptr;
  /* Optional (newer bridge libs): matched-count query + listener. */
  uint32_t (*publisher_sub_count_)(void *) = nullptr;
  uint32_t (*publisher_unacked_count_)(void *) = nullptr;
  int32_t (*publisher_set_on_matched_)(void *, void (*)(uint32_t, void *),
                                       void *) = nullptr;
  uint32_t (*subscriber_pub_count_)(void *) = nullptr;
  int32_t (*subscriber_set_on_matched_)(void *, void (*)(uint32_t, void *),
                                        void *) = nullptr;
  int32_t (*activate_protected_transport_)(uint32_t) = nullptr;
};

BridgeQos ToBridgeQos(const rmw_qos_profile_t *qos);
std::string ToMddsTopicName(const char *topic_name);

} // namespace rmw_mdds_cpp

#endif // RMW_MDDS_CPP_SRC__BRIDGE_BACKEND_HPP_
