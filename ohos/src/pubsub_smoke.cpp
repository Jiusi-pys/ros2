/* codex-file-meta: begin
relative_path: "ohos/src/pubsub_smoke.cpp"
language: "cpp"
summary: "Cpp source defining `SmokeOptions`, and `class`."
symbols: ["SmokeOptions", "class"]
generated_by: "codebase-frontmatter-summary"
codex-file-meta: end */

/*
 * Copyright (c) 2026
 * Licensed under the Apache License, Version 2.0
 */

#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

extern "C" {
#include "rcl/discovery_options.h"
#include "rcl/rcl.h"
#include "rcl/error_handling.h"
#include "rcl/wait.h"
#include "rmw/qos_profiles.h"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_runtime_c/string_functions.h"
#include "std_msgs/msg/string.h"
}

namespace OHOS {
namespace Ros2Port {

namespace {

constexpr char kNodeName[] = "ros2_ohos_pubsub_smoke";
constexpr char kTopicName[] = "/ros2_ohos_pubsub_smoke";
constexpr char kExpectedMessage[] = "kaihongos pubsub smoke";

rmw_qos_profile_t MakeCrossDeviceSmokeQos()
{
    rmw_qos_profile_t qos = rmw_qos_profile_default;
    qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
    qos.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
    return qos;
}

struct SmokeOptions
{
    enum class Mode
    {
        Loopback,
        Publisher,
        Subscriber,
    };

    Mode mode {Mode::Loopback};
    std::string topicName {kTopicName};
    std::string payload {kExpectedMessage};
};

void ExpectOk(rcl_ret_t ret, const char * step)
{
    if (ret == RCL_RET_OK) {
        return;
    }

    std::string message(step);
    message += ": ";
    message += rcl_get_error_string().str;
    rcl_reset_error();
    throw std::runtime_error(message);
}

void ConfigureDiscoveryOptions(rcl_init_options_t & initOptions)
{
    rmw_init_options_t * rmwInitOptions = rcl_init_options_get_rmw_init_options(&initOptions);
    if (rmwInitOptions == nullptr) {
        throw std::runtime_error("failed to get rmw init options");
    }

    ExpectOk(
        rcl_get_automatic_discovery_range(&rmwInitOptions->discovery_options),
        "rcl_get_automatic_discovery_range");

    if (rmwInitOptions->discovery_options.automatic_discovery_range != RMW_AUTOMATIC_DISCOVERY_RANGE_OFF) {
        rcutils_allocator_t allocator = rcl_get_default_allocator();
        ExpectOk(
            rcl_get_discovery_static_peers(&rmwInitOptions->discovery_options, &allocator),
            "rcl_get_discovery_static_peers");
    }
}

void PrintDiscoveryOptions(const rcl_init_options_t & initOptions)
{
    auto * mutableOptions = const_cast<rcl_init_options_t *>(&initOptions);
    rmw_init_options_t * rmwInitOptions = rcl_init_options_get_rmw_init_options(mutableOptions);
    if (rmwInitOptions == nullptr) {
        throw std::runtime_error("failed to get rmw init options");
    }

    std::cout << "discovery_range="
              << rcl_automatic_discovery_range_to_string(
                     rmwInitOptions->discovery_options.automatic_discovery_range)
              << std::endl;
    std::cout << "static_peers_count=" << rmwInitOptions->discovery_options.static_peers_count << std::endl;
    for (size_t i = 0; i < rmwInitOptions->discovery_options.static_peers_count; ++i) {
        std::cout << "static_peer[" << i << "]="
                  << rmwInitOptions->discovery_options.static_peers[i].peer_address << std::endl;
    }
}

void PrintNodeDomainId(const rcl_node_t & node)
{
    size_t domainId = RCL_DEFAULT_DOMAIN_ID;
    ExpectOk(rcl_node_get_domain_id(&node, &domainId), "rcl_node_get_domain_id");
    std::cout << "node_domain_id=" << domainId << std::endl;
}

void WaitForMatches(
    rcl_publisher_t & publisher,
    rcl_subscription_t & subscription,
    size_t expectedCount,
    int maxAttempts,
    std::chrono::milliseconds sleepDuration)
{
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        size_t publisherCount = 0;
        size_t subscriptionCount = 0;

        ExpectOk(
            rcl_publisher_get_subscription_count(&publisher, &publisherCount),
            "rcl_publisher_get_subscription_count");
        ExpectOk(
            rcl_subscription_get_publisher_count(&subscription, &subscriptionCount),
            "rcl_subscription_get_publisher_count");

        if (publisherCount >= expectedCount && subscriptionCount >= expectedCount) {
            return;
        }

        std::this_thread::sleep_for(sleepDuration);
    }

    throw std::runtime_error("publisher/subscription match did not establish");
}

bool WaitForSubscriptionMessage(
    rcl_subscription_t & subscription,
    rcl_context_t & context,
    int maxAttempts,
    std::chrono::milliseconds waitDuration,
    const std::string & expectedPayload,
    std::string & receivedPayload)
{
    rcl_wait_set_t waitSet = rcl_get_zero_initialized_wait_set();
    ExpectOk(
        rcl_wait_set_init(
            &waitSet,
            1,
            0,
            0,
            0,
            0,
            0,
            &context,
            rcl_get_default_allocator()),
        "rcl_wait_set_init");

    std_msgs__msg__String message;
    if (!std_msgs__msg__String__init(&message)) {
        ExpectOk(rcl_wait_set_fini(&waitSet), "rcl_wait_set_fini");
        throw std::runtime_error("std_msgs__msg__String__init failed for incoming message");
    }

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        size_t publisherCount = 0;
        ExpectOk(
            rcl_subscription_get_publisher_count(&subscription, &publisherCount),
            "rcl_subscription_get_publisher_count");
        std::cout << "subscriber_wait_publisher_count=" << publisherCount << std::endl;

        ExpectOk(rcl_wait_set_clear(&waitSet), "rcl_wait_set_clear");
        ExpectOk(rcl_wait_set_add_subscription(&waitSet, &subscription, nullptr), "rcl_wait_set_add_subscription");

        rcl_ret_t waitRet = rcl_wait(&waitSet, RCL_MS_TO_NS(waitDuration.count()));
        if (waitRet == RCL_RET_TIMEOUT) {
            std::cout << "subscriber_wait_timeout" << std::endl;
            continue;
        }
        ExpectOk(waitRet, "rcl_wait");

        if (waitSet.subscriptions[0] == nullptr) {
            continue;
        }

        rmw_message_info_t messageInfo = rmw_get_zero_initialized_message_info();
        ExpectOk(rcl_take(&subscription, &message, &messageInfo, nullptr), "rcl_take");
        const std::string payload(
            message.data.data != nullptr ? message.data.data : "",
            message.data.size);
        if (payload == expectedPayload) {
            receivedPayload = payload;
            std_msgs__msg__String__fini(&message);
            ExpectOk(rcl_wait_set_fini(&waitSet), "rcl_wait_set_fini");
            return true;
        }

        std::cout << "subscriber_ignored_payload=" << payload << std::endl;
    }

    std_msgs__msg__String__fini(&message);
    ExpectOk(rcl_wait_set_fini(&waitSet), "rcl_wait_set_fini");
    return false;
}

}  // namespace

int RunPubSubSmoke()
{
    rcl_allocator_t allocator = rcl_get_default_allocator();

    rcl_init_options_t initOptions = rcl_get_zero_initialized_init_options();
    ExpectOk(rcl_init_options_init(&initOptions, allocator), "rcl_init_options_init");
    ConfigureDiscoveryOptions(initOptions);
    PrintDiscoveryOptions(initOptions);

    rcl_context_t context = rcl_get_zero_initialized_context();
    ExpectOk(rcl_init(0, nullptr, &initOptions, &context), "rcl_init");
    std::cout << "rcl_init_ok" << std::endl;

    rcl_node_options_t nodeOptions = rcl_node_get_default_options();
    rcl_node_t node = rcl_get_zero_initialized_node();
    std::cout << "rcl_node_init_begin" << std::endl;
    ExpectOk(rcl_node_init(&node, kNodeName, "", &context, &nodeOptions), "rcl_node_init");
    std::cout << "rcl_node_init_ok" << std::endl;
    PrintNodeDomainId(node);

    const rosidl_message_type_support_t * typeSupport =
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String);
    if (typeSupport == nullptr) {
        throw std::runtime_error("failed to resolve std_msgs/msg/String type support");
    }

    rcl_publisher_options_t publisherOptions = rcl_publisher_get_default_options();
    publisherOptions.qos = MakeCrossDeviceSmokeQos();
    rcl_publisher_t publisher = rcl_get_zero_initialized_publisher();
    ExpectOk(
        rcl_publisher_init(&publisher, &node, typeSupport, kTopicName, &publisherOptions),
        "rcl_publisher_init");

    rcl_subscription_options_t subscriptionOptions = rcl_subscription_get_default_options();
    subscriptionOptions.qos = MakeCrossDeviceSmokeQos();
    rcl_subscription_t subscription = rcl_get_zero_initialized_subscription();
    ExpectOk(
        rcl_subscription_init(&subscription, &node, typeSupport, kTopicName, &subscriptionOptions),
        "rcl_subscription_init");

    WaitForMatches(publisher, subscription, 1, 20, std::chrono::milliseconds(100));

    std_msgs__msg__String outgoingMessage;
    if (!std_msgs__msg__String__init(&outgoingMessage)) {
        throw std::runtime_error("std_msgs__msg__String__init failed for outgoing message");
    }
    if (!rosidl_runtime_c__String__assign(&outgoingMessage.data, kExpectedMessage)) {
        std_msgs__msg__String__fini(&outgoingMessage);
        throw std::runtime_error("rosidl_runtime_c__String__assign failed");
    }

    ExpectOk(rcl_publish(&publisher, &outgoingMessage, nullptr), "rcl_publish");
    std_msgs__msg__String__fini(&outgoingMessage);

    std::string payload;
    const bool received = WaitForSubscriptionMessage(
        subscription,
        context,
        20,
        std::chrono::milliseconds(200),
        kExpectedMessage,
        payload);

    if (!received) {
        throw std::runtime_error("subscription did not receive the published message");
    }

    if (payload != kExpectedMessage) {
        throw std::runtime_error("received payload mismatch: " + payload);
    }

    std::cout << "pubsub_smoke_ok" << std::endl;
    std::cout << "topic=" << kTopicName << std::endl;
    std::cout << "payload=" << payload << std::endl;

    ExpectOk(rcl_subscription_fini(&subscription, &node), "rcl_subscription_fini");
    ExpectOk(rcl_publisher_fini(&publisher, &node), "rcl_publisher_fini");
    ExpectOk(rcl_node_fini(&node), "rcl_node_fini");
    ExpectOk(rcl_shutdown(&context), "rcl_shutdown");
    ExpectOk(rcl_context_fini(&context), "rcl_context_fini");
    ExpectOk(rcl_init_options_fini(&initOptions), "rcl_init_options_fini");
    return 0;
}

int RunPublisherOnly(const std::string & topicName, const std::string & payload)
{
    rcl_allocator_t allocator = rcl_get_default_allocator();

    rcl_init_options_t initOptions = rcl_get_zero_initialized_init_options();
    ExpectOk(rcl_init_options_init(&initOptions, allocator), "rcl_init_options_init");
    ConfigureDiscoveryOptions(initOptions);
    PrintDiscoveryOptions(initOptions);

    rcl_context_t context = rcl_get_zero_initialized_context();
    ExpectOk(rcl_init(0, nullptr, &initOptions, &context), "rcl_init");
    std::cout << "rcl_init_ok" << std::endl;

    rcl_node_options_t nodeOptions = rcl_node_get_default_options();
    rcl_node_t node = rcl_get_zero_initialized_node();
    std::cout << "rcl_node_init_begin" << std::endl;
    ExpectOk(rcl_node_init(&node, "ros2_ohos_pubsub_publisher", "", &context, &nodeOptions), "rcl_node_init");
    std::cout << "rcl_node_init_ok" << std::endl;
    PrintNodeDomainId(node);

    const rosidl_message_type_support_t * typeSupport =
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String);
    if (typeSupport == nullptr) {
        throw std::runtime_error("failed to resolve std_msgs/msg/String type support");
    }

    rcl_publisher_options_t publisherOptions = rcl_publisher_get_default_options();
    publisherOptions.qos = MakeCrossDeviceSmokeQos();
    rcl_publisher_t publisher = rcl_get_zero_initialized_publisher();
    ExpectOk(
        rcl_publisher_init(&publisher, &node, typeSupport, topicName.c_str(), &publisherOptions),
        "rcl_publisher_init");

    std_msgs__msg__String outgoingMessage;
    if (!std_msgs__msg__String__init(&outgoingMessage)) {
        throw std::runtime_error("std_msgs__msg__String__init failed for outgoing message");
    }
    if (!rosidl_runtime_c__String__assign(&outgoingMessage.data, payload.c_str())) {
        std_msgs__msg__String__fini(&outgoingMessage);
        throw std::runtime_error("rosidl_runtime_c__String__assign failed");
    }

    for (int attempt = 0; attempt < 150; ++attempt) {
        size_t subscriptionCount = 0;
        ExpectOk(
            rcl_publisher_get_subscription_count(&publisher, &subscriptionCount),
            "rcl_publisher_get_subscription_count");
        std::cout << "publisher_subscription_count=" << subscriptionCount << std::endl;
        ExpectOk(rcl_publish(&publisher, &outgoingMessage, nullptr), "rcl_publish");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std_msgs__msg__String__fini(&outgoingMessage);

    std::cout << "publisher_sent" << std::endl;
    std::cout << "topic=" << topicName << std::endl;
    std::cout << "payload=" << payload << std::endl;

    ExpectOk(rcl_publisher_fini(&publisher, &node), "rcl_publisher_fini");
    ExpectOk(rcl_node_fini(&node), "rcl_node_fini");
    ExpectOk(rcl_shutdown(&context), "rcl_shutdown");
    ExpectOk(rcl_context_fini(&context), "rcl_context_fini");
    ExpectOk(rcl_init_options_fini(&initOptions), "rcl_init_options_fini");
    return 0;
}

int RunSubscriberOnly(const std::string & topicName, const std::string & expectedPayload)
{
    rcl_allocator_t allocator = rcl_get_default_allocator();

    rcl_init_options_t initOptions = rcl_get_zero_initialized_init_options();
    ExpectOk(rcl_init_options_init(&initOptions, allocator), "rcl_init_options_init");
    ConfigureDiscoveryOptions(initOptions);
    PrintDiscoveryOptions(initOptions);

    rcl_context_t context = rcl_get_zero_initialized_context();
    ExpectOk(rcl_init(0, nullptr, &initOptions, &context), "rcl_init");
    std::cout << "rcl_init_ok" << std::endl;

    rcl_node_options_t nodeOptions = rcl_node_get_default_options();
    rcl_node_t node = rcl_get_zero_initialized_node();
    std::cout << "rcl_node_init_begin" << std::endl;
    ExpectOk(rcl_node_init(&node, "ros2_ohos_pubsub_subscriber", "", &context, &nodeOptions), "rcl_node_init");
    std::cout << "rcl_node_init_ok" << std::endl;
    PrintNodeDomainId(node);

    const rosidl_message_type_support_t * typeSupport =
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String);
    if (typeSupport == nullptr) {
        throw std::runtime_error("failed to resolve std_msgs/msg/String type support");
    }

    rcl_subscription_options_t subscriptionOptions = rcl_subscription_get_default_options();
    subscriptionOptions.qos = MakeCrossDeviceSmokeQos();
    rcl_subscription_t subscription = rcl_get_zero_initialized_subscription();
    ExpectOk(
        rcl_subscription_init(&subscription, &node, typeSupport, topicName.c_str(), &subscriptionOptions),
        "rcl_subscription_init");

    std::cout << "subscriber_ready" << std::endl;
    std::cout << "topic=" << topicName << std::endl;
    for (int attempt = 0; attempt < 15; ++attempt) {
        size_t publisherCount = 0;
        ExpectOk(
            rcl_subscription_get_publisher_count(&subscription, &publisherCount),
            "rcl_subscription_get_publisher_count");
        std::cout << "subscriber_publisher_count=" << publisherCount << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::string payload;
    const bool received = WaitForSubscriptionMessage(
        subscription,
        context,
        150,
        std::chrono::milliseconds(200),
        expectedPayload,
        payload);

    if (!received) {
        throw std::runtime_error("subscription did not receive the published message");
    }

    if (payload != expectedPayload) {
        throw std::runtime_error("received payload mismatch: " + payload);
    }

    std::cout << "subscriber_received" << std::endl;
    std::cout << "topic=" << topicName << std::endl;
    std::cout << "payload=" << payload << std::endl;

    ExpectOk(rcl_subscription_fini(&subscription, &node), "rcl_subscription_fini");
    ExpectOk(rcl_node_fini(&node), "rcl_node_fini");
    ExpectOk(rcl_shutdown(&context), "rcl_shutdown");
    ExpectOk(rcl_context_fini(&context), "rcl_context_fini");
    ExpectOk(rcl_init_options_fini(&initOptions), "rcl_init_options_fini");
    return 0;
}

SmokeOptions ParseOptions(int argc, char ** argv)
{
    SmokeOptions options;
    if (argc == 1) {
        return options;
    }

    if (argc == 4 && 0 == std::strcmp(argv[1], "publisher")) {
        options.mode = SmokeOptions::Mode::Publisher;
        options.topicName = argv[2];
        options.payload = argv[3];
        return options;
    }

    if (argc == 4 && 0 == std::strcmp(argv[1], "subscriber")) {
        options.mode = SmokeOptions::Mode::Subscriber;
        options.topicName = argv[2];
        options.payload = argv[3];
        return options;
    }

    throw std::runtime_error(
        "usage: ros2_ohos_pubsub_smoke [publisher <topic> <payload> | subscriber <topic> <payload>]");
}

}  // namespace Ros2Port
}  // namespace OHOS

int main(int argc, char ** argv)
{
    try {
        const auto options = OHOS::Ros2Port::ParseOptions(argc, argv);
        switch (options.mode) {
            case OHOS::Ros2Port::SmokeOptions::Mode::Loopback:
                return OHOS::Ros2Port::RunPubSubSmoke();
            case OHOS::Ros2Port::SmokeOptions::Mode::Publisher:
                return OHOS::Ros2Port::RunPublisherOnly(options.topicName, options.payload);
            case OHOS::Ros2Port::SmokeOptions::Mode::Subscriber:
                return OHOS::Ros2Port::RunSubscriberOnly(options.topicName, options.payload);
        }
        throw std::runtime_error("unsupported smoke mode");
    } catch (const std::exception & error) {
        std::cerr << "pubsub smoke failed: " << error.what() << std::endl;
        return 70;
    }
}
