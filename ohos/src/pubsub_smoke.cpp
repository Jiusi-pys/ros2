/*
 * Copyright (c) 2026
 * Licensed under the Apache License, Version 2.0
 */

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

extern "C" {
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
    std_msgs__msg__String & message,
    int maxAttempts,
    std::chrono::milliseconds waitDuration)
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

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        ExpectOk(rcl_wait_set_clear(&waitSet), "rcl_wait_set_clear");
        ExpectOk(rcl_wait_set_add_subscription(&waitSet, &subscription, nullptr), "rcl_wait_set_add_subscription");

        rcl_ret_t waitRet = rcl_wait(&waitSet, RCL_MS_TO_NS(waitDuration.count()));
        if (waitRet == RCL_RET_TIMEOUT) {
            continue;
        }
        ExpectOk(waitRet, "rcl_wait");

        if (waitSet.subscriptions[0] == nullptr) {
            continue;
        }

        rmw_message_info_t messageInfo = rmw_get_zero_initialized_message_info();
        ExpectOk(rcl_take(&subscription, &message, &messageInfo, nullptr), "rcl_take");
        ExpectOk(rcl_wait_set_fini(&waitSet), "rcl_wait_set_fini");
        return true;
    }

    ExpectOk(rcl_wait_set_fini(&waitSet), "rcl_wait_set_fini");
    return false;
}

}  // namespace

int RunPubSubSmoke()
{
    rcl_allocator_t allocator = rcl_get_default_allocator();

    rcl_init_options_t initOptions = rcl_get_zero_initialized_init_options();
    ExpectOk(rcl_init_options_init(&initOptions, allocator), "rcl_init_options_init");

    rcl_context_t context = rcl_get_zero_initialized_context();
    ExpectOk(rcl_init(0, nullptr, &initOptions, &context), "rcl_init");

    rcl_node_options_t nodeOptions = rcl_node_get_default_options();
    rcl_node_t node = rcl_get_zero_initialized_node();
    ExpectOk(rcl_node_init(&node, kNodeName, "", &context, &nodeOptions), "rcl_node_init");

    const rosidl_message_type_support_t * typeSupport =
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String);
    if (typeSupport == nullptr) {
        throw std::runtime_error("failed to resolve std_msgs/msg/String type support");
    }

    rcl_publisher_options_t publisherOptions = rcl_publisher_get_default_options();
    publisherOptions.qos = rmw_qos_profile_default;
    rcl_publisher_t publisher = rcl_get_zero_initialized_publisher();
    ExpectOk(
        rcl_publisher_init(&publisher, &node, typeSupport, kTopicName, &publisherOptions),
        "rcl_publisher_init");

    rcl_subscription_options_t subscriptionOptions = rcl_subscription_get_default_options();
    subscriptionOptions.qos = rmw_qos_profile_default;
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

    std_msgs__msg__String incomingMessage;
    if (!std_msgs__msg__String__init(&incomingMessage)) {
        throw std::runtime_error("std_msgs__msg__String__init failed for incoming message");
    }

    const bool received =
        WaitForSubscriptionMessage(subscription, context, incomingMessage, 20, std::chrono::milliseconds(200));

    if (!received) {
        std_msgs__msg__String__fini(&incomingMessage);
        throw std::runtime_error("subscription did not receive the published message");
    }

    const std::string payload(incomingMessage.data.data, incomingMessage.data.size);
    std_msgs__msg__String__fini(&incomingMessage);

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

}  // namespace Ros2Port
}  // namespace OHOS

int main()
{
    try {
        return OHOS::Ros2Port::RunPubSubSmoke();
    } catch (const std::exception & error) {
        std::cerr << "pubsub smoke failed: " << error.what() << std::endl;
        return 70;
    }
}
