// Copyright 2026 Yusheng Peng
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

// Board-side probe: run a matrix of message types through rmw_mdds's
// rmw_serialize() exactly as a publisher would (same top-level type support
// handles rclcpp/rcl pass), and report which message/member fails.
//
// Build with build_probe.sh, push to the board next to the ROS 2 install
// tree, and run with LD_LIBRARY_PATH pointing at the install lib dir.

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "rcutils/allocator.h"
#include "rcutils/error_handling.h"
#include "rcutils/types/uint8_array.h"

#include "rmw/types.h"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_runtime_c/primitives_sequence_functions.h"
#include "rosidl_runtime_c/string_functions.h"
#include "rosidl_typesupport_interface/macros.h"

// C++ message structs
#include "std_msgs/msg/string.hpp"
#include "builtin_interfaces/msg/time.hpp"
#include "rcl_interfaces/msg/log.hpp"
#include "rcl_interfaces/msg/parameter_event.hpp"
#include "rcl_interfaces/msg/parameter_type.hpp"

// C message structs + init/fini/sequence functions
#include "std_msgs/msg/string.h"
#include "builtin_interfaces/msg/time.h"
#include "rcl_interfaces/msg/log.h"
#include "rcl_interfaces/msg/parameter.h"
#include "rcl_interfaces/msg/parameter_event.h"

// rmw_serialize is implemented by librmw_mdds.so; the probe links against it
// directly (bypassing rmw_implementation) to exercise the exact same code.
extern "C" rmw_ret_t
rmw_serialize(
  const void * ros_message,
  const rosidl_message_type_support_t * type_support,
  rmw_serialized_message_t * serialized_message);

// Exported per-message top-level type support handle getters.
#define DECL_TS(ts, pkg, msg_name) \
  extern "C" const rosidl_message_type_support_t * \
  ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(ts, pkg, msg, msg_name)();

DECL_TS(rosidl_typesupport_cpp, std_msgs, String)
DECL_TS(rosidl_typesupport_cpp, builtin_interfaces, Time)
DECL_TS(rosidl_typesupport_cpp, rcl_interfaces, Log)
DECL_TS(rosidl_typesupport_cpp, rcl_interfaces, ParameterEvent)
DECL_TS(rosidl_typesupport_c, std_msgs, String)
DECL_TS(rosidl_typesupport_c, builtin_interfaces, Time)
DECL_TS(rosidl_typesupport_c, rcl_interfaces, Log)
DECL_TS(rosidl_typesupport_c, rcl_interfaces, ParameterEvent)

#define GET_TS(ts, pkg, msg_name) \
  ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(ts, pkg, msg, msg_name)()

namespace
{

int g_failures = 0;

void
report_ok(const char * name, const rmw_serialized_message_t & sm)
{
  printf("[ OK ] %-32s %zu bytes:", name, sm.buffer_length);
  const size_t dump = sm.buffer_length < 32 ? sm.buffer_length : 32;
  for (size_t i = 0; i < dump; ++i) {
    printf(" %02x", sm.buffer[i]);
  }
  printf("%s\n", sm.buffer_length > dump ? " ..." : "");
}

void
report_fail(const char * name, rmw_ret_t ret)
{
  rcutils_error_string_t err = rcutils_get_error_string();
  printf("[FAIL] %-32s ret=%d: %s\n", name, ret, err.str);
  ++g_failures;
}

// Serialize one message; returns the serialized size or -1.
int
try_serialize(
  const char * name,
  const rosidl_message_type_support_t * ts,
  const void * msg)
{
  rcutils_reset_error();
  if (ts == nullptr) {
    printf("[FAIL] %-32s null type support handle\n", name);
    ++g_failures;
    return -1;
  }
  rcutils_allocator_t alloc = rcutils_get_default_allocator();
  rmw_serialized_message_t sm = rcutils_get_zero_initialized_uint8_array();
  if (rcutils_uint8_array_init(&sm, 64, &alloc) != RCUTILS_RET_OK) {
    printf("[FAIL] %-32s could not init serialized message\n", name);
    ++g_failures;
    return -1;
  }
  const rmw_ret_t ret = rmw_serialize(msg, ts, &sm);
  if (ret == RMW_RET_OK) {
    report_ok(name, sm);
  } else {
    report_fail(name, ret);
  }
  if (rcutils_uint8_array_fini(&sm) != RCUTILS_RET_OK) {
    printf("[WARN] %-32s serialized message fini failed\n", name);
  }
  return ret == RMW_RET_OK ? 0 : -1;
}

// ---------------------------------------------------------------------------
// C++ message constructors
// ---------------------------------------------------------------------------

void
test_cpp_string()
{
  std_msgs::msg::String msg;
  msg.data = "hello mdds";
  try_serialize("cpp std_msgs/String", GET_TS(rosidl_typesupport_cpp, std_msgs, String), &msg);
}

void
test_cpp_time()
{
  builtin_interfaces::msg::Time msg;
  msg.sec = 123;
  msg.nanosec = 456;
  try_serialize(
    "cpp builtin_interfaces/Time",
    GET_TS(rosidl_typesupport_cpp, builtin_interfaces, Time), &msg);
}

void
test_cpp_log()
{
  rcl_interfaces::msg::Log msg;
  msg.stamp.sec = 1700000000;
  msg.stamp.nanosec = 42;
  msg.level = rcl_interfaces::msg::Log::INFO;
  msg.name = "probe_node";
  msg.msg = "some log text";
  msg.file = "serdes_probe.cpp";
  msg.function = "test_cpp_log";
  msg.line = 99;
  try_serialize("cpp rcl_interfaces/Log", GET_TS(rosidl_typesupport_cpp, rcl_interfaces, Log), &msg);
}

void
test_cpp_parameter_event()
{
  rcl_interfaces::msg::ParameterEvent msg;
  msg.stamp.sec = 1;
  msg.node = "/probe";

  rcl_interfaces::msg::Parameter p_bool;
  p_bool.name = "use_sim_time";
  p_bool.value.type = rcl_interfaces::msg::ParameterType::PARAMETER_BOOL;
  p_bool.value.bool_value = true;
  msg.new_parameters.push_back(p_bool);

  rcl_interfaces::msg::Parameter p_arrays;
  p_arrays.name = "arrays";
  p_arrays.value.type = rcl_interfaces::msg::ParameterType::PARAMETER_STRING;
  p_arrays.value.string_value = "value";
  p_arrays.value.bool_array_value = {true, false, true};
  p_arrays.value.integer_array_value = {1, 2, 3};
  p_arrays.value.double_array_value = {1.5, 2.5};
  p_arrays.value.string_array_value = {"a", "bb"};
  msg.changed_parameters.push_back(p_arrays);

  try_serialize(
    "cpp rcl_interfaces/ParameterEvent",
    GET_TS(rosidl_typesupport_cpp, rcl_interfaces, ParameterEvent), &msg);
}

// ---------------------------------------------------------------------------
// C message constructors
// ---------------------------------------------------------------------------

void
test_c_string()
{
  std_msgs__msg__String msg;
  std_msgs__msg__String__init(&msg);
  rosidl_runtime_c__String__assign(&msg.data, "hello mdds");
  try_serialize("c std_msgs/String", GET_TS(rosidl_typesupport_c, std_msgs, String), &msg);
  std_msgs__msg__String__fini(&msg);
}

void
test_c_time()
{
  builtin_interfaces__msg__Time msg;
  builtin_interfaces__msg__Time__init(&msg);
  msg.sec = 123;
  msg.nanosec = 456;
  try_serialize(
    "c builtin_interfaces/Time",
    GET_TS(rosidl_typesupport_c, builtin_interfaces, Time), &msg);
  builtin_interfaces__msg__Time__fini(&msg);
}

void
test_c_log()
{
  rcl_interfaces__msg__Log msg;
  rcl_interfaces__msg__Log__init(&msg);
  msg.stamp.sec = 1700000000;
  msg.stamp.nanosec = 42;
  msg.level = 20;  // INFO
  rosidl_runtime_c__String__assign(&msg.name, "probe_node");
  rosidl_runtime_c__String__assign(&msg.msg, "some log text");
  rosidl_runtime_c__String__assign(&msg.file, "serdes_probe.cpp");
  rosidl_runtime_c__String__assign(&msg.function, "test_c_log");
  msg.line = 99;
  try_serialize("c rcl_interfaces/Log", GET_TS(rosidl_typesupport_c, rcl_interfaces, Log), &msg);
  rcl_interfaces__msg__Log__fini(&msg);
}

void
test_c_parameter_event()
{
  rcl_interfaces__msg__ParameterEvent msg;
  rcl_interfaces__msg__ParameterEvent__init(&msg);
  msg.stamp.sec = 1;
  rosidl_runtime_c__String__assign(&msg.node, "/probe");

  if (rcl_interfaces__msg__Parameter__Sequence__init(&msg.new_parameters, 1)) {
    rcl_interfaces__msg__Parameter * p = &msg.new_parameters.data[0];
    rosidl_runtime_c__String__assign(&p->name, "use_sim_time");
    p->value.type = 1;  // PARAMETER_BOOL
    p->value.bool_value = true;
    rosidl_runtime_c__String__assign(&p->value.string_value, "value");
    rosidl_runtime_c__int64__Sequence__init(&p->value.integer_array_value, 2);
    p->value.integer_array_value.data[0] = 7;
    p->value.integer_array_value.data[1] = 8;
  }
  try_serialize(
    "c rcl_interfaces/ParameterEvent",
    GET_TS(rosidl_typesupport_c, rcl_interfaces, ParameterEvent), &msg);
  rcl_interfaces__msg__ParameterEvent__fini(&msg);
}

}  // namespace

int
main()
{
  printf("rmw_mdds serialization probe\n");

  test_cpp_string();
  test_cpp_time();
  test_cpp_log();
  test_cpp_parameter_event();

  test_c_string();
  test_c_time();
  test_c_log();
  test_c_parameter_event();

  printf("%s (%d failure%s)\n",
    g_failures == 0 ? "ALL PASSED" : "FAILURES PRESENT",
    g_failures, g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
