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
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

extern "C" {
typedef void (*MddsBridgeOnMatchedCallback)(uint32_t matchedCount,
                                            void *userData);

static constexpr uint32_t kPublisherFlagRemoteOnly = 0x1u;

typedef struct {
  int reliability;
  int durability;
  int historyKind;
  uint32_t historyDepth;
  uint32_t deadlineMs;
  uint32_t lifespanMs;
} MddsBridgeQos;

struct MddsBridgePublisher {
  std::string topic;
  std::string type;
  MddsBridgeQos qos = {};
  uint32_t publisher_flags = 0u;
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

typedef void (*MddsBridgeDataCallback)(const MddsBridgeSample *sample,
                                       void *userData);

struct MddsBridgeSubscriber {
  std::string topic;
  std::string type;
  MddsBridgeQos qos = {};
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
static int g_shutdown_count = 0;
static int g_borrow_loaned_count = 0;
static int g_publish_loaned_count = 0;
static int g_return_loaned_count = 0;
static int g_subscriber_take_loaned_count = 0;
static int g_subscriber_take_loaned_with_storage_count = 0;
static int g_subscriber_return_loaned_count = 0;
static int g_protected_transport_activate_count = 0;
static int g_protected_transport_authenticated = 0;
static int g_protected_transport_encrypted = 0;
static int g_fail_next_publisher_create = 0;
static int g_fail_next_subscriber_create = 0;
static void *g_last_borrowed_data = nullptr;
static uint32_t g_last_borrowed_size = 0;
static void *g_last_subscriber_typed_storage = nullptr;
static uint32_t g_last_subscriber_typed_storage_size = 0;
static std::mutex g_mutex;

struct DataCallbackTarget {
  MddsBridgeDataCallback callback;
  void *user_data;
};

static bool SameTopicAndType(const std::string &topic, const std::string &type,
                             const std::string &other_topic,
                             const std::string &other_type) {
  return topic == other_topic && type == other_type;
}

static MddsBridgePublisher *FindPublisherLocked(const std::string &topic,
                                                const std::string &type) {
  auto it = std::find_if(
      g_publishers.begin(), g_publishers.end(),
      [&topic, &type](const MddsBridgePublisher *publisher) {
        return publisher != nullptr && publisher->topic == topic &&
               publisher->type == type;
      });
  return it == g_publishers.end() ? nullptr : *it;
}

static MddsBridgeSubscriber *FindSubscriberLocked(const std::string &topic,
                                                  const std::string &type) {
  auto it = std::find_if(
      g_subscribers.begin(), g_subscribers.end(),
      [&topic, &type](const MddsBridgeSubscriber *subscriber) {
        return subscriber != nullptr && subscriber->topic == topic &&
               subscriber->type == type;
      });
  return it == g_subscribers.end() ? nullptr : *it;
}

static uint32_t
CountSubscribersForPublisherLocked(MddsBridgePublisher *publisher) {
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

static uint32_t
CountPublishersForSubscriberLocked(MddsBridgeSubscriber *subscriber) {
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

void FakeMddsBridgeReset(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
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
  g_shutdown_count = 0;
  g_borrow_loaned_count = 0;
  g_publish_loaned_count = 0;
  g_return_loaned_count = 0;
  g_subscriber_take_loaned_count = 0;
  g_subscriber_take_loaned_with_storage_count = 0;
  g_subscriber_return_loaned_count = 0;
  g_protected_transport_activate_count = 0;
  g_protected_transport_authenticated = 0;
  g_protected_transport_encrypted = 0;
  g_fail_next_publisher_create = 0;
  g_fail_next_subscriber_create = 0;
  g_last_borrowed_data = nullptr;
  g_last_borrowed_size = 0;
  g_last_subscriber_typed_storage = nullptr;
  g_last_subscriber_typed_storage_size = 0;
}

void FakeMddsBridgeFailNextPublisherCreate(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_fail_next_publisher_create = 1;
}

void FakeMddsBridgeFailNextSubscriberCreate(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_fail_next_subscriber_create = 1;
}

int FakeMddsBridgePublishCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_publish_count;
}

int FakeMddsBridgeInitCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_init_count;
}

int FakeMddsBridgeShutdownCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_shutdown_count;
}

int FakeMddsBridgeBorrowLoanedCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_borrow_loaned_count;
}

int FakeMddsBridgePublishLoanedCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_publish_loaned_count;
}

int FakeMddsBridgeReturnLoanedCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_return_loaned_count;
}

void *FakeMddsBridgeLastBorrowedData(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_last_borrowed_data;
}

uint32_t FakeMddsBridgeLastBorrowedSize(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_last_borrowed_size;
}

int FakeMddsBridgeSubscriberTakeLoanedCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_subscriber_take_loaned_count;
}

int FakeMddsBridgeSubscriberTakeLoanedWithStorageCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_subscriber_take_loaned_with_storage_count;
}

int FakeMddsBridgeSubscriberReturnLoanedCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_subscriber_return_loaned_count;
}

int FakeMddsBridgeProtectedTransportActivateCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_protected_transport_activate_count;
}

int FakeMddsBridgeProtectedTransportAuthenticated(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_protected_transport_authenticated;
}

int FakeMddsBridgeProtectedTransportEncrypted(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_protected_transport_encrypted;
}

void *FakeMddsBridgeLastSubscriberTypedStorage(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_last_subscriber_typed_storage;
}

uint32_t FakeMddsBridgeLastSubscriberTypedStorageSize(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_last_subscriber_typed_storage_size;
}

int FakeMddsBridgePublisherCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return static_cast<int>(g_publishers.size());
}

int FakeMddsBridgeSubscriberCount(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return static_cast<int>(g_subscribers.size());
}

const char *FakeMddsBridgeLastPublisherTopic(void) {
  thread_local std::string snapshot;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (snapshot != g_publisher_topic) {
    snapshot = g_publisher_topic;
  }
  return snapshot.c_str();
}

const char *FakeMddsBridgeLastSubscriberTopic(void) {
  thread_local std::string snapshot;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (snapshot != g_subscriber_topic) {
    snapshot = g_subscriber_topic;
  }
  return snapshot.c_str();
}

const char *FakeMddsBridgeLastPublisherType(void) {
  thread_local std::string snapshot;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (snapshot != g_publisher_type) {
    snapshot = g_publisher_type;
  }
  return snapshot.c_str();
}

const char *FakeMddsBridgeLastSubscriberType(void) {
  thread_local std::string snapshot;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (snapshot != g_subscriber_type) {
    snapshot = g_subscriber_type;
  }
  return snapshot.c_str();
}

const uint8_t *FakeMddsBridgeLastPayloadData(void) {
  thread_local std::vector<uint8_t> snapshot;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (snapshot != g_last_payload) {
    snapshot = g_last_payload;
  }
  return snapshot.data();
}

uint32_t FakeMddsBridgeLastPayloadLen(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return static_cast<uint32_t>(g_last_payload.size());
}

int FakeMddsBridgeHasPublisher(const char *topicName, const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  return FindPublisherLocked(topic, type) == nullptr ? 0 : 1;
}

int FakeMddsBridgePublisherPublishCount(const char *topicName,
                                        const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto *publisher = FindPublisherLocked(topic, type);
  return publisher == nullptr ? 0 : publisher->publish_count;
}

uint32_t FakeMddsBridgePublisherHistoryDepth(const char *topicName,
                                             const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto *publisher = FindPublisherLocked(topic, type);
  return publisher == nullptr ? 0u : publisher->qos.historyDepth;
}

int FakeMddsBridgePublisherHistoryKind(const char *topicName,
                                       const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto *publisher = FindPublisherLocked(topic, type);
  return publisher == nullptr ? -1 : publisher->qos.historyKind;
}

int FakeMddsBridgePublisherReliability(const char *topicName,
                                       const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto *publisher = FindPublisherLocked(topic, type);
  return publisher == nullptr ? -1 : publisher->qos.reliability;
}

void FakeMddsBridgeSetPublisherUnackedCount(const char *topicName,
                                            const char *typeName,
                                            uint32_t count) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto *publisher = FindPublisherLocked(topic, type);
  if (publisher != nullptr) {
    publisher->unacked_count = count;
  }
}

const uint8_t *FakeMddsBridgePublisherLastPayloadData(const char *topicName,
                                                      const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  thread_local std::vector<uint8_t> snapshot;
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto *publisher = FindPublisherLocked(topic, type);
  if (publisher == nullptr) {
    snapshot.clear();
    return nullptr;
  }
  if (snapshot != publisher->last_payload) {
    snapshot = publisher->last_payload;
  }
  return snapshot.empty() ? nullptr : snapshot.data();
}

uint32_t FakeMddsBridgePublisherLastPayloadLen(const char *topicName,
                                               const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto *publisher = FindPublisherLocked(topic, type);
  return publisher == nullptr
             ? 0u
             : static_cast<uint32_t>(publisher->last_payload.size());
}

int FakeMddsBridgeHasSubscriber(const char *topicName, const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  return FindSubscriberLocked(topic, type) == nullptr ? 0 : 1;
}

int FakeMddsBridgeSubscriberCountFor(const char *topicName,
                                     const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  return static_cast<int>(std::count_if(
      g_subscribers.begin(), g_subscribers.end(),
      [&topic, &type](const MddsBridgeSubscriber *subscriber) {
        return subscriber != nullptr && subscriber->topic == topic &&
               subscriber->type == type;
      }));
}

uint32_t FakeMddsBridgeSubscriberHistoryDepth(const char *topicName,
                                              const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto *subscriber = FindSubscriberLocked(topic, type);
  return subscriber == nullptr ? 0u : subscriber->qos.historyDepth;
}

int FakeMddsBridgeSubscriberHistoryKind(const char *topicName,
                                        const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto *subscriber = FindSubscriberLocked(topic, type);
  return subscriber == nullptr ? -1 : subscriber->qos.historyKind;
}

int FakeMddsBridgeSubscriberReliability(const char *topicName,
                                        const char *typeName) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto *subscriber = FindSubscriberLocked(topic, type);
  return subscriber == nullptr ? -1 : subscriber->qos.reliability;
}

void FakeMddsBridgeInject(const void *data, uint32_t len) {
  DataCallbackTarget target = {};
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_subscribers.empty() || g_subscribers.back()->callback == nullptr) {
      return;
    }
    target.callback = g_subscribers.back()->callback;
    target.user_data = g_subscribers.back()->user_data;
  }
  MddsBridgeSample sample = {};
  sample.data = data;
  sample.len = len;
  sample.sequenceNumber = 1;
  target.callback(&sample, target.user_data);
}

void FakeMddsBridgeInjectWithMetadata(const void *data, uint32_t len,
                                      uint64_t sequenceNumber,
                                      const uint8_t *senderGuid) {
  DataCallbackTarget target = {};
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_subscribers.empty() || g_subscribers.back()->callback == nullptr) {
      return;
    }
    target.callback = g_subscribers.back()->callback;
    target.user_data = g_subscribers.back()->user_data;
  }
  MddsBridgeSample sample = {};
  sample.data = data;
  sample.len = len;
  sample.sequenceNumber = sequenceNumber;
  if (senderGuid != nullptr) {
    std::memcpy(sample.senderGuid, senderGuid, sizeof(sample.senderGuid));
  }
  target.callback(&sample, target.user_data);
}

int FakeMddsBridgeInjectFor(const char *topicName, const char *typeName,
                            const void *data, uint32_t len,
                            uint64_t sequenceNumber) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::vector<DataCallbackTarget> targets;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto *subscriber : g_subscribers) {
      if (subscriber == nullptr || subscriber->callback == nullptr ||
          subscriber->topic != topic || subscriber->type != type) {
        continue;
      }
      targets.push_back({subscriber->callback, subscriber->user_data});
    }
  }
  for (const auto &target : targets) {
    MddsBridgeSample sample = {};
    sample.data = data;
    sample.len = len;
    sample.sequenceNumber = sequenceNumber;
    target.callback(&sample, target.user_data);
  }
  return static_cast<int>(targets.size());
}

int FakeMddsBridgeQueueLoanedFor(const char *topicName, const char *typeName,
                                 const void *data, uint32_t len,
                                 uint64_t sequenceNumber,
                                 const uint8_t *senderGuid) {
  const std::string topic = topicName == nullptr ? "" : topicName;
  const std::string type = typeName == nullptr ? "" : typeName;
  std::lock_guard<std::mutex> lock(g_mutex);
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
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_init_count;
  return 0;
}

void MddsBridgeShutdown(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_shutdown_count;
}
void MddsBridgeStopSpin(void) {}

int32_t MddsBridgeActivateProtectedTransport(uint32_t flags) {
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_protected_transport_activate_count;
  g_protected_transport_authenticated = (flags & 0x1u) != 0u ? 1 : 0;
  g_protected_transport_encrypted = (flags & 0x2u) != 0u ? 1 : 0;
  return g_protected_transport_authenticated != 0 &&
                 g_protected_transport_encrypted != 0
             ? 0
             : -1;
}

static MddsBridgePublisher *CreatePublisherQos(const char *topicName,
                                               const char *typeName,
                                               const MddsBridgeQos *qos,
                                               uint32_t publisherFlags) {
  if (topicName == nullptr || typeName == nullptr ||
      (publisherFlags & ~kPublisherFlagRemoteOnly) != 0u) {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_fail_next_publisher_create != 0) {
    g_fail_next_publisher_create = 0;
    return nullptr;
  }
  auto *publisher = new MddsBridgePublisher;
  publisher->topic = topicName;
  g_publisher_topic = topicName;
  publisher->type = typeName;
  g_publisher_type = typeName;
  if (qos != nullptr) {
    publisher->qos = *qos;
  }
  publisher->publisher_flags = publisherFlags;
  g_publishers.push_back(publisher);
  return publisher;
}

MddsBridgePublisher *MddsBridgeCreatePublisherQos(const char *topicName,
                                                  const char *typeName,
                                                  const MddsBridgeQos *qos) {
  return CreatePublisherQos(topicName, typeName, qos, 0u);
}

#ifndef FAKE_MDDS_BRIDGE_LEGACY_ABI
MddsBridgePublisher *MddsBridgeCreatePublisherQosEx(
    const char *topicName, const char *typeName, const MddsBridgeQos *qos,
    uint32_t publisherFlags) {
  return CreatePublisherQos(topicName, typeName, qos, publisherFlags);
}
#endif

int32_t MddsBridgePublish(MddsBridgePublisher *pub, const void *data,
                          uint32_t len) {
  std::vector<DataCallbackTarget> targets;
  uint64_t sequence_number = 0u;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_publish_count;
    sequence_number = static_cast<uint64_t>(g_publish_count);
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
    if (pub != nullptr &&
        (pub->publisher_flags & kPublisherFlagRemoteOnly) == 0u) {
      for (auto *subscriber : g_subscribers) {
        if (subscriber != nullptr && subscriber->callback != nullptr &&
            pub->topic == subscriber->topic && pub->type == subscriber->type) {
          targets.push_back({subscriber->callback, subscriber->user_data});
        }
      }
    }
  }
  MddsBridgeSample sample = {};
  sample.data = data;
  sample.len = len;
  sample.sequenceNumber = sequence_number;
  for (const auto &target : targets) {
    target.callback(&sample, target.user_data);
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
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last_borrowed_data = *outBuf;
    g_last_borrowed_size = size;
    ++g_borrow_loaned_count;
  }
  return 0;
}

int32_t MddsBridgePublishLoaned(MddsBridgePublisher *pub,
                                MddsBridgeLoanedSample *loan, uint32_t len) {
  if (pub == nullptr || loan == nullptr || len > loan->data.size()) {
    return -1;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_publish_loaned_count;
  }
  int32_t ret = MddsBridgePublish(pub, loan->data.data(), len);
  delete loan;
  return ret;
}

int32_t MddsBridgeReturnLoanedSample(MddsBridgePublisher *pub,
                                     MddsBridgeLoanedSample *loan) {
  if (pub == nullptr || loan == nullptr) {
    return -1;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_return_loaned_count;
  }
  delete loan;
  return 0;
}

uint32_t MddsBridgePublisherGetUnackedCount(MddsBridgePublisher *pub) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return pub == nullptr ? 0u : pub->unacked_count;
}

void MddsBridgeDestroyPublisher(MddsBridgePublisher *pub) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = std::find(g_publishers.begin(), g_publishers.end(), pub);
    if (it != g_publishers.end()) {
      g_publishers.erase(it);
    }
  }
  delete pub;
}

MddsBridgeSubscriber *MddsBridgeSubscribeQos(const char *topicName,
                                             const char *typeName,
                                             const MddsBridgeQos *qos,
                                             MddsBridgeDataCallback cb,
                                             void *userData) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_fail_next_subscriber_create != 0) {
    g_fail_next_subscriber_create = 0;
    return nullptr;
  }
  auto *subscriber = new MddsBridgeSubscriber;
  subscriber->topic = topicName;
  subscriber->type = typeName;
  if (qos != nullptr) {
    subscriber->qos = *qos;
  }
  subscriber->callback = cb;
  subscriber->user_data = userData;
  g_subscriber_topic = topicName;
  g_subscriber_type = typeName;
  g_subscribers.push_back(subscriber);
  return subscriber;
}

void MddsBridgeUnsubscribe(MddsBridgeSubscriber *sub) {
  MddsBridgeSubscriber *removed = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = std::find(g_subscribers.begin(), g_subscribers.end(), sub);
    if (it != g_subscribers.end()) {
      removed = *it;
      g_subscribers.erase(it);
    }
  }
  delete removed;
}

uint32_t MddsBridgePublisherGetSubCount(MddsBridgePublisher *pub) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return CountSubscribersForPublisherLocked(pub);
}

int32_t
MddsBridgePublisherSetOnMatchedCallback(MddsBridgePublisher *pub,
                                        MddsBridgeOnMatchedCallback callback,
                                        void *userData) {
  if (pub == nullptr) {
    return -1;
  }
  uint32_t matched_count = 0u;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    pub->matched_callback = callback;
    pub->matched_user_data = userData;
    matched_count = CountSubscribersForPublisherLocked(pub);
  }
  if (callback != nullptr) {
    callback(matched_count, userData);
  }
  return 0;
}

uint32_t MddsBridgeSubscriberGetPubCount(MddsBridgeSubscriber *sub) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return CountPublishersForSubscriberLocked(sub);
}

int32_t
MddsBridgeSubscriberSetOnMatchedCallback(MddsBridgeSubscriber *sub,
                                         MddsBridgeOnMatchedCallback callback,
                                         void *userData) {
  if (sub == nullptr) {
    return -1;
  }
  uint32_t matched_count = 0u;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    sub->matched_callback = callback;
    sub->matched_user_data = userData;
    matched_count = CountPublishersForSubscriberLocked(sub);
  }
  if (callback != nullptr) {
    callback(matched_count, userData);
  }
  return 0;
}

int32_t MddsBridgeSubscriberTakeLoaned(MddsBridgeSubscriber *sub,
                                       MddsBridgeLoanedMessage *message) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (sub == nullptr || message == nullptr || sub->loaned_messages.empty()) {
    return -1;
  }
  auto *loan =
      new MddsBridgeLoanedPayload(std::move(sub->loaned_messages.front()));
  sub->loaned_messages.pop_front();
  message->data = loan->data.data();
  message->len = static_cast<uint32_t>(loan->data.size());
  message->timestamp = loan->timestamp;
  message->sequenceNumber = loan->sequenceNumber;
  std::memcpy(message->senderGuid, loan->senderGuid,
              sizeof(message->senderGuid));
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
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_subscriber_take_loaned_with_storage_count;
  }
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
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last_subscriber_typed_storage = loan->typed_storage;
    g_last_subscriber_typed_storage_size = storageSize;
  }
  return 0;
}

int32_t MddsBridgeSubscriberReturnLoaned(MddsBridgeSubscriber *sub,
                                         MddsBridgeLoanedMessage *message) {
  if (sub == nullptr || message == nullptr || message->loanHandle == nullptr) {
    return -1;
  }
  delete static_cast<MddsBridgeLoanedPayload *>(message->loanHandle);
  message->loanHandle = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_subscriber_return_loaned_count;
  }
  return 0;
}
} // extern "C"
