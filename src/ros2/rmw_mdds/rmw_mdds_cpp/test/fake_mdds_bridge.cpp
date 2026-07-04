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
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <utility>
#include <vector>

extern "C" {
typedef void (*MddsBridgeOnMatchedCallback)(uint32_t matchedCount,
                                            void *userData);

struct MddsBridgePublisher {
  std::string topic;
  std::string type;
  std::vector<uint8_t> last_payload;
  int publish_count = 0;
  uint32_t unacked_count = 0;
  MddsBridgeOnMatchedCallback matched_callback = nullptr;
  void *matched_user_data = nullptr;
};

struct MddsBridgeLoanedSample {
  std::vector<uint8_t> data;
};

struct MddsBridgeLoanedPayload {
  ~MddsBridgeLoanedPayload() { std::free(typed_storage); }

  std::vector<uint8_t> data;
  uint64_t timestamp = 0;
  uint64_t sequenceNumber = 0;
  uint8_t senderGuid[16] = {};
  uint8_t loanKind = 0;
  void *typed_storage = nullptr;
  uint32_t typed_storage_capacity = 0;
};

typedef struct {
  const void *data;
  uint32_t len;
  uint64_t timestamp;
  uint64_t sequenceNumber;
  uint8_t senderGuid[16];
  void *loanHandle;
  uint8_t loanKind;
} MddsBridgeLoanedMessage;

typedef struct {
  const void *data;
  uint32_t len;
  uint64_t sequenceNumber;
  uint8_t senderGuid[16];
} MddsBridgeSample;

typedef struct {
  int reliability;
  int durability;
  int historyKind;
  uint32_t historyDepth;
  uint32_t deadlineMs;
  uint32_t lifespanMs;
} MddsBridgeQos;

typedef void (*MddsBridgeDataCallback)(const MddsBridgeSample *sample,
                                       void *userData);

struct MddsBridgeSubscriber {
  std::string topic;
  std::string type;
  MddsBridgeDataCallback callback = nullptr;
  void *user_data = nullptr;
  MddsBridgeOnMatchedCallback matched_callback = nullptr;
  void *matched_user_data = nullptr;
  std::deque<MddsBridgeLoanedPayload> loaned_messages;
};

static std::vector<MddsBridgePublisher *> g_publishers;
static std::vector<MddsBridgeSubscriber *> g_subscribers;
static std::string g_publisher_topic;
static std::string g_publisher_type;
static std::string g_subscriber_topic;
static std::string g_subscriber_type;
static std::vector<uint8_t> g_last_payload;
static int g_publish_count = 0;
static int g_init_count = 0;
static int g_borrow_loaned_count = 0;
static int g_publish_loaned_count = 0;
static int g_return_loaned_count = 0;
static int g_subscriber_take_loaned_count = 0;
static int g_subscriber_take_loaned_with_storage_count = 0;
static int g_subscriber_return_loaned_count = 0;
static int g_protected_transport_activate_count = 0;
static int g_protected_transport_authenticated = 0;
static int g_protected_transport_encrypted = 0;
static void *g_last_borrowed_data = nullptr;
static uint32_t g_last_borrowed_size = 0;
static void *g_last_subscriber_typed_storage = nullptr;
static uint32_t g_last_subscriber_typed_storage_size = 0;

static bool SameTopicAndType(const std::string &topic, const std::string &type,
                             const std::string &other_topic,
                             const std::string &other_type) {
  return topic == other_topic && type == other_type;
}

static uint32_t CountSubscribersForPublisher(MddsBridgePublisher *publisher) {
  if (publisher == nullptr) {
    return 0u;
  }
  return static_cast<uint32_t>(std::count_if(
      g_subscribers.begin(), g_subscribers.end(),
      [publisher](const MddsBridgeSubscriber *subscriber) {
        return subscriber != nullptr &&
               SameTopicAndType(publisher->topic, publisher->type,
                                subscriber->topic, subscriber->type);
      }));
}

static uint32_t CountPublishersForSubscriber(MddsBridgeSubscriber *subscriber) {
  if (subscriber == nullptr) {
    return 0u;
  }
  return static_cast<uint32_t>(std::count_if(
      g_publishers.begin(), g_publishers.end(),
      [subscriber](const MddsBridgePublisher *publisher) {
        return publisher != nullptr &&
               SameTopicAndType(subscriber->topic, subscriber->type,
                                publisher->topic, publisher->type);
      }));
}

static void NotifyPublisherMatched(MddsBridgePublisher *publisher) {
  if (publisher != nullptr && publisher->matched_callback != nullptr) {
    publisher->matched_callback(CountSubscribersForPublisher(publisher),
                                publisher->matched_user_data);
  }
}

static void NotifySubscriberMatched(MddsBridgeSubscriber *subscriber) {
  if (subscriber != nullptr && subscriber->matched_callback != nullptr) {
    subscriber->matched_callback(CountPublishersForSubscriber(subscriber),
                                 subscriber->matched_user_data);
  }
}

void FakeMddsBridgeReset(void) {
  for (auto *publisher : g_publishers) {
    delete publisher;
  }
  g_publishers.clear();
  for (auto *subscriber : g_subscribers) {
    delete subscriber;
  }
  g_subscribers.clear();
  g_publisher_topic.clear();
  g_publisher_type.clear();
  g_subscriber_topic.clear();
  g_subscriber_type.clear();
  g_last_payload.clear();
  g_publish_count = 0;
  g_init_count = 0;
  g_borrow_loaned_count = 0;
  g_publish_loaned_count = 0;
  g_return_loaned_count = 0;
  g_subscriber_take_loaned_count = 0;
  g_subscriber_take_loaned_with_storage_count = 0;
  g_subscriber_return_loaned_count = 0;
  g_protected_transport_activate_count = 0;
  g_protected_transport_authenticated = 0;
  g_protected_transport_encrypted = 0;
  g_last_borrowed_data = nullptr;
  g_last_borrowed_size = 0;
  g_last_subscriber_typed_storage = nullptr;
  g_last_subscriber_typed_storage_size = 0;
}

int FakeMddsBridgePublishCount(void) { return g_publish_count; }

int FakeMddsBridgeInitCount(void) { return g_init_count; }

int FakeMddsBridgeBorrowLoanedCount(void) { return g_borrow_loaned_count; }

int FakeMddsBridgePublishLoanedCount(void) { return g_publish_loaned_count; }

int FakeMddsBridgeReturnLoanedCount(void) { return g_return_loaned_count; }

void *FakeMddsBridgeLastBorrowedData(void) { return g_last_borrowed_data; }

uint32_t FakeMddsBridgeLastBorrowedSize(void) { return g_last_borrowed_size; }

int FakeMddsBridgeSubscriberTakeLoanedCount(void) {
  return g_subscriber_take_loaned_count;
}

int FakeMddsBridgeSubscriberTakeLoanedWithStorageCount(void) {
  return g_subscriber_take_loaned_with_storage_count;
}

int FakeMddsBridgeSubscriberReturnLoanedCount(void) {
  return g_subscriber_return_loaned_count;
}

int FakeMddsBridgeProtectedTransportActivateCount(void) {
  return g_protected_transport_activate_count;
}

int FakeMddsBridgeProtectedTransportAuthenticated(void) {
  return g_protected_transport_authenticated;
}

int FakeMddsBridgeProtectedTransportEncrypted(void) {
  return g_protected_transport_encrypted;
}

void *FakeMddsBridgeLastSubscriberTypedStorage(void) {
  return g_last_subscriber_typed_storage;
}

uint32_t FakeMddsBridgeLastSubscriberTypedStorageSize(void) {
  return g_last_subscriber_typed_storage_size;
}

int FakeMddsBridgePublisherCount(void) {
  return static_cast<int>(g_publishers.size());
}

int FakeMddsBridgeSubscriberCount(void) {
  return static_cast<int>(g_subscribers.size());
}

const char *FakeMddsBridgeLastPublisherTopic(void) {
  return g_publisher_topic.c_str();
}

const char *FakeMddsBridgeLastSubscriberTopic(void) {
  return g_subscriber_topic.c_str();
}

const char *FakeMddsBridgeLastPublisherType(void) {
  return g_publisher_type.c_str();
}

const char *FakeMddsBridgeLastSubscriberType(void) {
  return g_subscriber_type.c_str();
}

const uint8_t *FakeMddsBridgeLastPayloadData(void) {
  return g_last_payload.data();
}

uint32_t FakeMddsBridgeLastPayloadLen(void) {
  return static_cast<uint32_t>(g_last_payload.size());
}

int FakeMddsBridgeHasPublisher(const char *topicName, const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  return std::any_of(g_publishers.begin(), g_publishers.end(),
                     [&topic, &type](const MddsBridgePublisher *publisher) {
                       return publisher != nullptr &&
                              publisher->topic == topic &&
                              publisher->type == type;
                     })
             ? 1
             : 0;
}

MddsBridgePublisher *FakeMddsBridgeFindPublisher(const char *topicName,
                                                 const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  auto it = std::find_if(g_publishers.begin(), g_publishers.end(),
                         [&topic, &type](const MddsBridgePublisher *publisher) {
                           return publisher != nullptr &&
                                  publisher->topic == topic &&
                                  publisher->type == type;
                         });
  return it == g_publishers.end() ? nullptr : *it;
}

int FakeMddsBridgePublisherPublishCount(const char *topicName,
                                        const char *typeName) {
  const auto *publisher = FakeMddsBridgeFindPublisher(topicName, typeName);
  return publisher == nullptr ? 0 : publisher->publish_count;
}

void FakeMddsBridgeSetPublisherUnackedCount(const char *topicName,
                                            const char *typeName,
                                            uint32_t count) {
  auto *publisher = FakeMddsBridgeFindPublisher(topicName, typeName);
  if (publisher != nullptr) {
    publisher->unacked_count = count;
  }
}

const uint8_t *FakeMddsBridgePublisherLastPayloadData(const char *topicName,
                                                      const char *typeName) {
  const auto *publisher = FakeMddsBridgeFindPublisher(topicName, typeName);
  return publisher == nullptr || publisher->last_payload.empty()
           ? nullptr
           : publisher->last_payload.data();
}

uint32_t FakeMddsBridgePublisherLastPayloadLen(const char *topicName,
                                               const char *typeName) {
  const auto *publisher = FakeMddsBridgeFindPublisher(topicName, typeName);
  return publisher == nullptr
           ? 0u
           : static_cast<uint32_t>(publisher->last_payload.size());
}

int FakeMddsBridgeHasSubscriber(const char *topicName, const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  return std::any_of(g_subscribers.begin(), g_subscribers.end(),
                     [&topic, &type](const MddsBridgeSubscriber *subscriber) {
                       return subscriber != nullptr &&
                              subscriber->topic == topic &&
                              subscriber->type == type;
                     })
             ? 1
             : 0;
}

void FakeMddsBridgeInject(const void *data, uint32_t len) {
  if (g_subscribers.empty() || g_subscribers.back()->callback == nullptr) {
    return;
  }
  MddsBridgeSample sample = {};
  sample.data = data;
  sample.len = len;
  sample.sequenceNumber = 1;
  auto *subscriber = g_subscribers.back();
  subscriber->callback(&sample, subscriber->user_data);
}

void FakeMddsBridgeInjectWithMetadata(const void *data, uint32_t len,
                                      uint64_t sequenceNumber,
                                      const uint8_t *senderGuid) {
  if (g_subscribers.empty() || g_subscribers.back()->callback == nullptr) {
    return;
  }
  MddsBridgeSample sample = {};
  sample.data = data;
  sample.len = len;
  sample.sequenceNumber = sequenceNumber;
  if (senderGuid != nullptr) {
    std::memcpy(sample.senderGuid, senderGuid, sizeof(sample.senderGuid));
  }
  auto *subscriber = g_subscribers.back();
  subscriber->callback(&sample, subscriber->user_data);
}

int FakeMddsBridgeInjectFor(const char *topicName, const char *typeName,
                            const void *data, uint32_t len,
                            uint64_t sequenceNumber) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  int delivered = 0;
  for (auto *subscriber : g_subscribers) {
    if (subscriber == nullptr || subscriber->callback == nullptr ||
        subscriber->topic != topic || subscriber->type != type) {
      continue;
    }
    MddsBridgeSample sample = {};
    sample.data = data;
    sample.len = len;
    sample.sequenceNumber = sequenceNumber;
    subscriber->callback(&sample, subscriber->user_data);
    ++delivered;
  }
  return delivered;
}

int FakeMddsBridgeQueueLoanedFor(const char *topicName, const char *typeName,
                                 const void *data, uint32_t len,
                                 uint64_t sequenceNumber,
                                 const uint8_t *senderGuid) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  int queued = 0;
  for (auto *subscriber : g_subscribers) {
    if (subscriber == nullptr || subscriber->topic != topic ||
        subscriber->type != type) {
      continue;
    }
    MddsBridgeLoanedPayload payload;
    const auto *begin = static_cast<const uint8_t *>(data);
    if (begin != nullptr && len != 0) {
      payload.data.assign(begin, begin + len);
    }
    payload.timestamp = 123456789u;
    payload.sequenceNumber = sequenceNumber;
    if (senderGuid != nullptr) {
      std::memcpy(payload.senderGuid, senderGuid, sizeof(payload.senderGuid));
    }
    payload.loanKind = 7u;
    subscriber->loaned_messages.push_back(std::move(payload));
    ++queued;
  }
  return queued;
}

int32_t MddsBridgeInit(void) {
  ++g_init_count;
  return 0;
}

void MddsBridgeShutdown(void) {}
void MddsBridgeStopSpin(void) {}

int32_t MddsBridgeActivateProtectedTransport(uint32_t flags) {
  ++g_protected_transport_activate_count;
  g_protected_transport_authenticated = (flags & 0x1u) != 0u ? 1 : 0;
  g_protected_transport_encrypted = (flags & 0x2u) != 0u ? 1 : 0;
  return g_protected_transport_authenticated != 0 &&
         g_protected_transport_encrypted != 0
           ? 0
           : -1;
}

MddsBridgePublisher *MddsBridgeCreatePublisherQos(const char *topicName,
                                                  const char *typeName,
                                                  const MddsBridgeQos *qos) {
  (void)qos;
  auto *publisher = new MddsBridgePublisher;
  publisher->topic = topicName;
  g_publisher_topic = topicName;
  publisher->type = typeName;
  g_publisher_type = typeName;
  g_publishers.push_back(publisher);
  return publisher;
}

int32_t MddsBridgePublish(MddsBridgePublisher *pub, const void *data,
                          uint32_t len) {
  ++g_publish_count;
  if (pub != nullptr) {
    g_publisher_topic = pub->topic;
    g_publisher_type = pub->type;
    ++pub->publish_count;
    pub->unacked_count = 1u;
  }
  const auto *begin = static_cast<const uint8_t *>(data);
  if (begin != nullptr && len != 0) {
    g_last_payload.assign(begin, begin + len);
    if (pub != nullptr) {
      pub->last_payload.assign(begin, begin + len);
    }
  } else {
    g_last_payload.clear();
    if (pub != nullptr) {
      pub->last_payload.clear();
    }
  }
  if (pub != nullptr) {
    MddsBridgeSample sample = {};
    sample.data = data;
    sample.len = len;
    sample.sequenceNumber = static_cast<uint64_t>(g_publish_count);
    for (auto *subscriber : g_subscribers) {
      if (subscriber != nullptr && subscriber->callback != nullptr &&
          pub->topic == subscriber->topic && pub->type == subscriber->type) {
        subscriber->callback(&sample, subscriber->user_data);
      }
    }
  }
  return 0;
}

int32_t MddsBridgeBorrowLoanedSample(MddsBridgePublisher *pub, uint32_t size,
                                     MddsBridgeLoanedSample **outLoan,
                                     void **outBuf) {
  if (pub == nullptr || outLoan == nullptr || outBuf == nullptr) {
    return -1;
  }
  auto *loan = new MddsBridgeLoanedSample;
  loan->data.resize(size);
  *outLoan = loan;
  *outBuf = loan->data.data();
  g_last_borrowed_data = *outBuf;
  g_last_borrowed_size = size;
  ++g_borrow_loaned_count;
  return 0;
}

int32_t MddsBridgePublishLoaned(MddsBridgePublisher *pub,
                                MddsBridgeLoanedSample *loan, uint32_t len) {
  if (pub == nullptr || loan == nullptr || len > loan->data.size()) {
    return -1;
  }
  ++g_publish_loaned_count;
  int32_t ret = MddsBridgePublish(pub, loan->data.data(), len);
  delete loan;
  return ret;
}

int32_t MddsBridgeReturnLoanedSample(MddsBridgePublisher *pub,
                                     MddsBridgeLoanedSample *loan) {
  if (pub == nullptr || loan == nullptr) {
    return -1;
  }
  ++g_return_loaned_count;
  delete loan;
  return 0;
}

uint32_t MddsBridgePublisherGetUnackedCount(MddsBridgePublisher *pub) {
  return pub == nullptr ? 0u : pub->unacked_count;
}

void MddsBridgeDestroyPublisher(MddsBridgePublisher *pub) {
  auto it = std::find(g_publishers.begin(), g_publishers.end(), pub);
  if (it != g_publishers.end()) {
    g_publishers.erase(it);
  }
  delete pub;
}

MddsBridgeSubscriber *MddsBridgeSubscribeQos(const char *topicName,
                                             const char *typeName,
                                             const MddsBridgeQos *qos,
                                             MddsBridgeDataCallback cb,
                                             void *userData) {
  (void)qos;
  auto *subscriber = new MddsBridgeSubscriber;
  subscriber->topic = topicName;
  subscriber->type = typeName;
  subscriber->callback = cb;
  subscriber->user_data = userData;
  g_subscriber_topic = topicName;
  g_subscriber_type = typeName;
  g_subscribers.push_back(subscriber);
  return subscriber;
}

void MddsBridgeUnsubscribe(MddsBridgeSubscriber *sub) {
  auto it = std::find(g_subscribers.begin(), g_subscribers.end(), sub);
  if (it != g_subscribers.end()) {
    delete *it;
    g_subscribers.erase(it);
  }
}

uint32_t MddsBridgePublisherGetSubCount(MddsBridgePublisher *pub) {
  return CountSubscribersForPublisher(pub);
}

int32_t MddsBridgePublisherSetOnMatchedCallback(
    MddsBridgePublisher *pub, MddsBridgeOnMatchedCallback callback,
    void *userData) {
  if (pub == nullptr) {
    return -1;
  }
  pub->matched_callback = callback;
  pub->matched_user_data = userData;
  NotifyPublisherMatched(pub);
  return 0;
}

uint32_t MddsBridgeSubscriberGetPubCount(MddsBridgeSubscriber *sub) {
  return CountPublishersForSubscriber(sub);
}

int32_t MddsBridgeSubscriberSetOnMatchedCallback(
    MddsBridgeSubscriber *sub, MddsBridgeOnMatchedCallback callback,
    void *userData) {
  if (sub == nullptr) {
    return -1;
  }
  sub->matched_callback = callback;
  sub->matched_user_data = userData;
  NotifySubscriberMatched(sub);
  return 0;
}

int32_t MddsBridgeSubscriberTakeLoaned(MddsBridgeSubscriber *sub,
                                       MddsBridgeLoanedMessage *message) {
  if (sub == nullptr || message == nullptr || sub->loaned_messages.empty()) {
    return -1;
  }
  auto *loan = new MddsBridgeLoanedPayload(std::move(sub->loaned_messages.front()));
  sub->loaned_messages.pop_front();
  message->data = loan->data.data();
  message->len = static_cast<uint32_t>(loan->data.size());
  message->timestamp = loan->timestamp;
  message->sequenceNumber = loan->sequenceNumber;
  std::memcpy(message->senderGuid, loan->senderGuid, sizeof(message->senderGuid));
  message->loanHandle = loan;
  message->loanKind = loan->loanKind;
  ++g_subscriber_take_loaned_count;
  return 0;
}

int32_t MddsBridgeSubscriberTakeLoanedWithStorage(
    MddsBridgeSubscriber *sub, MddsBridgeLoanedMessage *message,
    uint32_t storageSize, void **outStorage, uint32_t *outStorageCapacity) {
  if (outStorage == nullptr || outStorageCapacity == nullptr) {
    return -1;
  }
  *outStorage = nullptr;
  *outStorageCapacity = 0;
  int32_t ret = MddsBridgeSubscriberTakeLoaned(sub, message);
  if (ret != 0) {
    return ret;
  }
  ++g_subscriber_take_loaned_with_storage_count;
  if (storageSize == 0u) {
    return 0;
  }
  auto *loan = static_cast<MddsBridgeLoanedPayload *>(message->loanHandle);
  loan->typed_storage = std::malloc(storageSize);
  if (loan->typed_storage == nullptr) {
    return -1;
  }
  std::memset(loan->typed_storage, 0, storageSize);
  loan->typed_storage_capacity = storageSize;
  *outStorage = loan->typed_storage;
  *outStorageCapacity = storageSize;
  g_last_subscriber_typed_storage = loan->typed_storage;
  g_last_subscriber_typed_storage_size = storageSize;
  return 0;
}

int32_t MddsBridgeSubscriberReturnLoaned(MddsBridgeSubscriber *sub,
                                         MddsBridgeLoanedMessage *message) {
  if (sub == nullptr || message == nullptr || message->loanHandle == nullptr) {
    return -1;
  }
  delete static_cast<MddsBridgeLoanedPayload *>(message->loanHandle);
  message->loanHandle = nullptr;
  ++g_subscriber_return_loaned_count;
  return 0;
}
} // extern "C"
