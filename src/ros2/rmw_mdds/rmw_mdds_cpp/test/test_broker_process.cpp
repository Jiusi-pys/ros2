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

#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/detail/trigger__functions.h>
#include <std_srvs/srv/detail/trigger__type_support.h>

#include "ipc_protocol.hpp"
#include "ipc_transport.hpp"
#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rcutils/types/string_array.h"
#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/events_statuses/matched.h"
#include "rmw/get_node_info_and_types.h"
#include "rmw/get_service_names_and_types.h"
#include "rmw/get_topic_endpoint_info.h"
#include "rmw/get_topic_names_and_types.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/names_and_types.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw/subscription_options.h"
#include "rmw/topic_endpoint_info_array.h"
#include "rosidl_runtime_c/string_functions.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_interface/macros.h"

namespace {
using namespace std::chrono_literals;

class TempDirectory {
public:
  TempDirectory() {
    char templ[] = "/tmp/rmw_mdds_broker_process_XXXXXX";
    char *dir = mkdtemp(templ);
    if (dir != nullptr) {
      path_ = dir;
    }
  }

  ~TempDirectory() {
    if (path_.empty()) {
      return;
    }
    unlink(SocketPath().c_str());
    unlink(ReadyPath().c_str());
    unlink(ResultPath().c_str());
    unlink(ServiceReadyPath().c_str());
    unlink(ClientReadyPath().c_str());
    unlink(ClientResultPath().c_str());
    unlink(BrokerPidPath().c_str());
    rmdir(path_.c_str());
  }

  const std::string &path() const { return path_; }

  std::string SocketPath() const { return path_ + "/broker.sock"; }

  std::string ReadyPath() const { return path_ + "/subscriber.ready"; }

  std::string ResultPath() const { return path_ + "/subscriber.result"; }

  std::string ServiceReadyPath() const { return path_ + "/service.ready"; }

  std::string ClientReadyPath() const { return path_ + "/client.ready"; }

  std::string ClientResultPath() const { return path_ + "/client.result"; }

  std::string BrokerPidPath() const { return path_ + "/broker.pid"; }

private:
  std::string path_;
};

class EnvVarGuard {
public:
  explicit EnvVarGuard(const char *name) : name_(name) {
    const char *value = std::getenv(name);
    if (value != nullptr) {
      had_value_ = true;
      value_ = value;
    }
  }

  ~EnvVarGuard() {
    if (had_value_) {
      setenv(name_.c_str(), value_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

private:
  std::string name_;
  bool had_value_ = false;
  std::string value_;
};

void SetBrokerEnvironment(const std::string &socket_path) {
  setenv("RMW_MDDS_BROKER", "1", 1);
  setenv("RMW_MDDS_BROKER_SOCKET", socket_path.c_str(), 1);
  setenv("RMW_MDDS_BRIDGE_LIBRARY", "/no/such/libmdds_bridge_shared.z.so", 1);
}

void SetDefaultBrokerEnvironment(const std::string &socket_path) {
  unsetenv("RMW_MDDS_BROKER");
  setenv("RMW_MDDS_BROKER_SOCKET", socket_path.c_str(), 1);
  unsetenv("RMW_MDDS_BRIDGE_LIBRARY");
}

void SetEnclave(rmw_init_options_t *options, const char *enclave) {
  options->allocator.deallocate(options->enclave, options->allocator.state);
  options->enclave = rcutils_strdup(enclave, options->allocator);
}

void IgnoreRmwRet(rmw_ret_t ret) { (void)ret; }

void IgnoreRcutilsRet(rcutils_ret_t ret) { (void)ret; }

bool FileExists(const std::string &path) {
  return access(path.c_str(), F_OK) == 0;
}

std::string CurrentExecutablePath(const char *fallback) {
  std::vector<char> path(4096);
  const ssize_t len = readlink("/proc/self/exe", path.data(), path.size() - 1u);
  if (len > 0) {
    path[static_cast<size_t>(len)] = '\0';
    return path.data();
  }
  return fallback == nullptr ? std::string() : fallback;
}

std::string DirectoryName(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return ".";
  }
  if (slash == 0u) {
    return "/";
  }
  return path.substr(0, slash);
}

pid_t SpawnProcess(const std::string &executable,
                   const std::vector<std::string> &args) {
  const pid_t pid = fork();
  if (pid != 0) {
    return pid;
  }

  std::vector<char *> argv;
  argv.reserve(args.size() + 2u);
  argv.push_back(const_cast<char *>(executable.c_str()));
  for (const auto &arg : args) {
    argv.push_back(const_cast<char *>(arg.c_str()));
  }
  argv.push_back(nullptr);
  execv(executable.c_str(), argv.data());
  _exit(127);
}

int WaitForExit(pid_t pid, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      return status;
    }
    if (result < 0) {
      return -1;
    }
    std::this_thread::sleep_for(10ms);
  }
  kill(pid, SIGTERM);
  waitpid(pid, &status, 0);
  return -1;
}

bool TerminateNonChildProcess(pid_t pid, std::chrono::milliseconds timeout) {
  if (pid <= 0) {
    return false;
  }
  kill(pid, SIGTERM);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (kill(pid, 0) != 0 && errno == ESRCH) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  kill(pid, SIGKILL);
  return kill(pid, 0) != 0 && errno == ESRCH;
}

bool ExitedWithZero(int status) {
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool WaitForBrokerSocket(const std::string &socket_path,
                         std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::string error;
    rmw_mdds_cpp::ipc::UniqueFd fd =
        rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path, &error);
    if (fd) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

bool RawBrokerGraphContainsPublisher(const std::string &socket_path,
                                     const std::string &topic,
                                     const std::string &type) {
  std::string error;
  rmw_mdds_cpp::ipc::UniqueFd observer =
      rmw_mdds_cpp::ipc::ConnectUnixSocket(socket_path, &error);
  if (!observer) {
    return false;
  }
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (std::chrono::steady_clock::now() < deadline) {
    rmw_mdds_cpp::ipc::Frame frame;
    if (rmw_mdds_cpp::ipc::ReadFrame(observer.get(), &frame, &error) !=
        rmw_mdds_cpp::ipc::ReadFrameStatus::kOk) {
      return false;
    }
    if (frame.kind != rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate) {
      continue;
    }
    rmw_mdds_cpp::ipc::GraphUpdateMessage update;
    if (!rmw_mdds_cpp::ipc::DecodeGraphUpdate(
            frame.payload.data(), frame.payload.size(), &update, &error)) {
      return false;
    }
    for (const auto &endpoint : update.endpoints) {
      if (endpoint.kind == rmw_mdds_cpp::ipc::EndpointKind::kPublisher &&
          endpoint.topic_name == topic && endpoint.type_name == type) {
        return true;
      }
    }
  }
  return false;
}

rmw_mdds_cpp::ipc::EndpointDescriptor
MakeGraphEndpoint(uint64_t entity_id, rmw_mdds_cpp::ipc::EndpointKind kind,
                  const std::string &node_name,
                  const std::string &topic_name,
                  const std::string &type_name) {
  rmw_mdds_cpp::ipc::EndpointDescriptor endpoint;
  endpoint.entity_id = entity_id;
  endpoint.kind = kind;
  endpoint.node_name = node_name;
  endpoint.node_namespace = "/mdds";
  endpoint.node_enclave = "/mdds_test";
  endpoint.topic_name = topic_name;
  endpoint.type_name = type_name;
  endpoint.mdds_type_name = type_name;
  endpoint.qos = rmw_qos_profile_default;
  return endpoint;
}

bool WriteGraphUpdateFrame(
    int fd,
    const std::vector<rmw_mdds_cpp::ipc::EndpointDescriptor> &endpoints,
    uint64_t epoch = 1u) {
  std::string error;
  return rmw_mdds_cpp::ipc::WriteFrame(
      fd,
      rmw_mdds_cpp::ipc::Frame{
          rmw_mdds_cpp::ipc::MessageKind::kGraphUpdate, 0u,
          rmw_mdds_cpp::ipc::EncodeGraphUpdate(1u, epoch, endpoints)},
      &error);
}

bool WaitForFile(const std::string &path, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (FileExists(path)) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

std::string ReadFile(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

bool FileContains(const std::string &path, const std::string &needle) {
  return ReadFile(path).find(needle) != std::string::npos;
}

bool IsDecimalString(const char *value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  for (const char *cursor = value; *cursor != '\0'; ++cursor) {
    if (!std::isdigit(static_cast<unsigned char>(*cursor))) {
      return false;
    }
  }
  return true;
}

std::vector<pid_t> BrokerPidsForSocket(const std::string &socket_path) {
  std::vector<pid_t> pids;
  DIR *proc = opendir("/proc");
  if (proc == nullptr) {
    return pids;
  }
  while (dirent *entry = readdir(proc)) {
    if (!IsDecimalString(entry->d_name)) {
      continue;
    }
    const pid_t pid = static_cast<pid_t>(std::stol(entry->d_name));
    const std::string cmdline_path =
        std::string("/proc/") + entry->d_name + "/cmdline";
    std::string cmdline = ReadFile(cmdline_path);
    std::replace(cmdline.begin(), cmdline.end(), '\0', ' ');
    if (cmdline.find("rmw_mdds_broker") != std::string::npos &&
        cmdline.find(socket_path) != std::string::npos) {
      pids.push_back(pid);
    }
  }
  closedir(proc);
  return pids;
}

void TerminateBrokerProcessesForSocket(const std::string &socket_path) {
  for (pid_t pid : BrokerPidsForSocket(socket_path)) {
    TerminateNonChildProcess(pid, 2s);
  }
}

pid_t ReadPidFile(const std::string &path) {
  std::ifstream input(path);
  long long value = -1;
  input >> value;
  return static_cast<pid_t>(value);
}

bool NamesAndTypesContains(const rmw_names_and_types_t &names_and_types,
                           const std::string &name, const std::string &type) {
  for (size_t i = 0; i < names_and_types.names.size; ++i) {
    if (names_and_types.names.data[i] == nullptr ||
        name != names_and_types.names.data[i]) {
      continue;
    }
    for (size_t j = 0; j < names_and_types.types[i].size; ++j) {
      if (names_and_types.types[i].data[j] != nullptr &&
          type == names_and_types.types[i].data[j]) {
        return true;
      }
    }
  }
  return false;
}

bool NodeNamesContain(const rcutils_string_array_t &names,
                      const rcutils_string_array_t &namespaces,
                      const std::string &expected_name,
                      const std::string &expected_namespace) {
  if (names.size != namespaces.size) {
    return false;
  }
  for (size_t i = 0; i < names.size; ++i) {
    if (names.data[i] != nullptr && namespaces.data[i] != nullptr &&
        expected_name == names.data[i] &&
        expected_namespace == namespaces.data[i]) {
      return true;
    }
  }
  return false;
}

bool NodeNamesWithEnclavesContain(const rcutils_string_array_t &names,
                                  const rcutils_string_array_t &namespaces,
                                  const rcutils_string_array_t &enclaves,
                                  const std::string &expected_name,
                                  const std::string &expected_namespace,
                                  const std::string &expected_enclave) {
  if (names.size != namespaces.size || names.size != enclaves.size) {
    return false;
  }
  for (size_t i = 0; i < names.size; ++i) {
    if (names.data[i] != nullptr && namespaces.data[i] != nullptr &&
        enclaves.data[i] != nullptr && expected_name == names.data[i] &&
        expected_namespace == namespaces.data[i] &&
        expected_enclave == enclaves.data[i]) {
      return true;
    }
  }
  return false;
}

bool AddressUsesBrokerLoanMapping(const void *address) {
  if (address == nullptr) {
    return false;
  }
  FILE *maps = std::fopen("/proc/self/maps", "r");
  if (maps == nullptr) {
    return false;
  }
  const uintptr_t target = reinterpret_cast<uintptr_t>(address);
  char line[1024];
  bool found = false;
  while (std::fgets(line, sizeof(line), maps) != nullptr) {
    unsigned long long begin = 0u;
    unsigned long long end = 0u;
    if (std::sscanf(line, "%llx-%llx", &begin, &end) != 2 ||
        target < begin || target >= end) {
      continue;
    }
    found = std::strstr(line, "rmw_mdds_loan_") != nullptr;
    break;
  }
  std::fclose(maps);
  return found;
}

bool PublisherEndpointInfosContain(const rmw_topic_endpoint_info_array_t &infos,
                                   const std::string &expected_node_name,
                                   const std::string &expected_namespace,
                                   const std::string &expected_type) {
  for (size_t i = 0; i < infos.size; ++i) {
    const rmw_topic_endpoint_info_t &info = infos.info_array[i];
    if (info.node_name != nullptr && info.node_namespace != nullptr &&
        info.topic_type != nullptr && expected_node_name == info.node_name &&
        expected_namespace == info.node_namespace &&
        expected_type == info.topic_type &&
        info.endpoint_type == RMW_ENDPOINT_PUBLISHER) {
      return true;
    }
  }
  return false;
}

int RunPublisher(const std::string &socket_path, const std::string &topic,
                 const std::string &payload) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_process_pub");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_process_pub", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, topic.c_str(),
                           &rmw_qos_profile_default, &publisher_options);
  if (publisher == nullptr) {
    return 5;
  }

  std_msgs::msg::String msg;
  msg.data = payload;
  int ret = 0;
  for (int i = 0; i < 3; ++i) {
    if (rmw_publish(publisher, &msg, nullptr) != RMW_RET_OK) {
      ret = 6;
      break;
    }
    std::this_thread::sleep_for(20ms);
  }

  IgnoreRmwRet(rmw_destroy_publisher(node, publisher));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunLoanedInt32Publisher(const std::string &socket_path,
                            const std::string &topic, int32_t payload) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_loaned_pub");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_loaned_pub", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::Int32>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, topic.c_str(),
                           &rmw_qos_profile_default, &publisher_options);
  if (publisher == nullptr) {
    return 5;
  }
  if (!publisher->can_loan_messages) {
    IgnoreRmwRet(rmw_destroy_publisher(node, publisher));
    IgnoreRmwRet(rmw_destroy_node(node));
    IgnoreRmwRet(rmw_shutdown(&context));
    IgnoreRmwRet(rmw_context_fini(&context));
    IgnoreRmwRet(rmw_init_options_fini(&options));
    return 6;
  }

  std::this_thread::sleep_for(50ms);
  void *loaned_message = nullptr;
  int ret = 0;
  if (rmw_borrow_loaned_message(publisher, type_support, &loaned_message) !=
      RMW_RET_OK) {
    ret = 7;
  } else {
    auto *message = static_cast<std_msgs::msg::Int32 *>(loaned_message);
    message->data = payload;
    if (rmw_publish_loaned_message(publisher, loaned_message, nullptr) !=
        RMW_RET_OK) {
      ret = 8;
    }
  }

  IgnoreRmwRet(rmw_destroy_publisher(node, publisher));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunLoanedStringPublisher(const std::string &socket_path,
                             const std::string &topic,
                             const std::string &payload) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_loaned_string_pub");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_loaned_string_pub", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, topic.c_str(),
                           &rmw_qos_profile_default, &publisher_options);
  if (publisher == nullptr) {
    return 5;
  }
  if (!publisher->can_loan_messages) {
    IgnoreRmwRet(rmw_destroy_publisher(node, publisher));
    IgnoreRmwRet(rmw_destroy_node(node));
    IgnoreRmwRet(rmw_shutdown(&context));
    IgnoreRmwRet(rmw_context_fini(&context));
    IgnoreRmwRet(rmw_init_options_fini(&options));
    return 6;
  }

  std::this_thread::sleep_for(50ms);
  void *loaned_message = nullptr;
  int ret = 0;
  if (rmw_borrow_loaned_message(publisher, type_support, &loaned_message) !=
      RMW_RET_OK) {
    ret = 7;
  } else {
    auto *message = static_cast<std_msgs::msg::String *>(loaned_message);
    message->data = payload;
    if (rmw_publish_loaned_message(publisher, loaned_message, nullptr) !=
        RMW_RET_OK) {
      ret = 8;
    }
  }

  IgnoreRmwRet(rmw_destroy_publisher(node, publisher));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunDefaultPublisher(const std::string &socket_path,
                        const std::string &topic, const std::string &payload) {
  SetDefaultBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_default_process_pub");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_default_process_pub", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, topic.c_str(),
                           &rmw_qos_profile_default, &publisher_options);
  if (publisher == nullptr) {
    return 5;
  }

  std_msgs::msg::String msg;
  msg.data = payload;
  int ret = 0;
  if (rmw_publish(publisher, &msg, nullptr) != RMW_RET_OK) {
    std::cerr << "default publisher rmw_publish failed: "
              << rmw_get_error_string().str << "\n";
    rmw_reset_error();
    ret = 6;
  }

  IgnoreRmwRet(rmw_destroy_publisher(node, publisher));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunDefaultAutostartOwner(const std::string &socket_path,
                             const std::string &ready_path,
                             const std::string &topic) {
  SetDefaultBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_default_autostart_owner");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    IgnoreRmwRet(rmw_init_options_fini(&options));
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_default_autostart_owner", "/mdds");
  if (node == nullptr) {
    IgnoreRmwRet(rmw_shutdown(&context));
    IgnoreRmwRet(rmw_context_fini(&context));
    IgnoreRmwRet(rmw_init_options_fini(&options));
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, topic.c_str(),
                           &rmw_qos_profile_default, &publisher_options);
  if (publisher == nullptr) {
    IgnoreRmwRet(rmw_destroy_node(node));
    IgnoreRmwRet(rmw_shutdown(&context));
    IgnoreRmwRet(rmw_context_fini(&context));
    IgnoreRmwRet(rmw_init_options_fini(&options));
    return 5;
  }

  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  IgnoreRmwRet(rmw_destroy_publisher(node, publisher));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return 0;
}

int RunDefaultAutostartOwnerAfterStart(const std::string &socket_path,
                                       const std::string &ready_path,
                                       const std::string &topic,
                                       const std::string &start_path) {
  if (!WaitForFile(start_path, 5s)) {
    return 70;
  }
  return RunDefaultAutostartOwner(socket_path, ready_path, topic);
}

int RunSubscriber(const std::string &socket_path, const std::string &ready_path,
                  const std::string &result_path, const std::string &topic,
                  const std::string &expected,
                  bool reliable_sensor_qos = false) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_process_sub");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_process_sub", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_qos_profile_t qos_profile = reliable_sensor_qos
                                      ? rmw_qos_profile_sensor_data
                                      : rmw_qos_profile_default;
  if (reliable_sensor_qos) {
    qos_profile.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  }
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, topic.c_str(), &qos_profile, &subscription_options);
  if (subscription == nullptr) {
    return 5;
  }

  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  rmw_wait_set_t *wait_set = rmw_create_wait_set(&context, 1);
  if (wait_set == nullptr) {
    return 6;
  }

  int ret = 7;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    void *subscription_handle = subscription->data;
    rmw_subscriptions_t subscriptions;
    subscriptions.subscriber_count = 1;
    subscriptions.subscribers = &subscription_handle;
    rmw_time_t timeout;
    timeout.sec = 0;
    timeout.nsec = 100000000;
    const rmw_ret_t wait_ret = rmw_wait(&subscriptions, nullptr, nullptr,
                                        nullptr, nullptr, wait_set, &timeout);
    if (wait_ret == RMW_RET_OK && subscriptions.subscribers[0] != nullptr) {
      std_msgs::msg::String received;
      bool taken = false;
      if (rmw_take(subscription, &received, &taken, nullptr) == RMW_RET_OK &&
          taken) {
        std::ofstream result(result_path);
        result << received.data << "\n";
        ret = received.data == expected ? 0 : 8;
        break;
      }
    }
  }

  IgnoreRmwRet(rmw_destroy_wait_set(wait_set));
  IgnoreRmwRet(rmw_destroy_subscription(node, subscription));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunLoanedStringSubscriber(const std::string &socket_path,
                              const std::string &ready_path,
                              const std::string &result_path,
                              const std::string &topic,
                              const std::string &expected) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_process_loaned_string_sub");
  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node = rmw_create_node(
      &context, "mdds_broker_process_loaned_string_sub", "/mdds");
  if (node == nullptr) {
    return 4;
  }
  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, topic.c_str(), &rmw_qos_profile_default,
      &subscription_options);
  if (subscription == nullptr || !subscription->can_loan_messages) {
    return 5;
  }
  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  int ret = 6;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    void *loaned_message = nullptr;
    bool taken = false;
    const rmw_ret_t take_ret = rmw_take_loaned_message(
        subscription, &loaned_message, &taken, nullptr);
    if (take_ret != RMW_RET_OK) {
      ret = 7;
      break;
    }
    if (!taken) {
      std::this_thread::sleep_for(10ms);
      continue;
    }
    auto *message = static_cast<std_msgs::msg::String *>(loaned_message);
    const std::string received(message->data.data(), message->data.size());
    const bool mapped = AddressUsesBrokerLoanMapping(message) &&
                        AddressUsesBrokerLoanMapping(message->data.data());
    const bool valid = mapped && received == expected;
    const rmw_ret_t return_ret =
        rmw_return_loaned_message_from_subscription(subscription, loaned_message);
    if (return_ret != RMW_RET_OK) {
      ret = 8;
      break;
    }
    std::ofstream result(result_path);
    result << received << "\n";
    ret = valid ? 0 : 9;
    break;
  }

  IgnoreRmwRet(rmw_destroy_subscription(node, subscription));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunInt32Subscriber(const std::string &socket_path,
                       const std::string &ready_path,
                       const std::string &result_path, const std::string &topic,
                       int32_t expected) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_int32_sub");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_int32_sub", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::Int32>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription =
      rmw_create_subscription(node, type_support, topic.c_str(),
                              &rmw_qos_profile_default, &subscription_options);
  if (subscription == nullptr) {
    return 5;
  }

  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  rmw_wait_set_t *wait_set = rmw_create_wait_set(&context, 1);
  if (wait_set == nullptr) {
    return 6;
  }

  int ret = 7;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    void *subscription_handle = subscription->data;
    rmw_subscriptions_t subscriptions;
    subscriptions.subscriber_count = 1;
    subscriptions.subscribers = &subscription_handle;
    rmw_time_t timeout;
    timeout.sec = 0;
    timeout.nsec = 100000000;
    const rmw_ret_t wait_ret = rmw_wait(&subscriptions, nullptr, nullptr,
                                        nullptr, nullptr, wait_set, &timeout);
    if (wait_ret == RMW_RET_OK && subscriptions.subscribers[0] != nullptr) {
      std_msgs::msg::Int32 received;
      bool taken = false;
      if (rmw_take(subscription, &received, &taken, nullptr) == RMW_RET_OK &&
          taken) {
        std::ofstream result(result_path);
        result << received.data << "\n";
        ret = received.data == expected ? 0 : 8;
        break;
      }
    }
  }

  IgnoreRmwRet(rmw_destroy_wait_set(wait_set));
  IgnoreRmwRet(rmw_destroy_subscription(node, subscription));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunDefaultSubscriber(const std::string &socket_path,
                         const std::string &ready_path,
                         const std::string &result_path,
                         const std::string &topic,
                         const std::string &expected) {
  SetDefaultBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_default_process_sub");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_default_process_sub", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription =
      rmw_create_subscription(node, type_support, topic.c_str(),
                              &rmw_qos_profile_default, &subscription_options);
  if (subscription == nullptr) {
    return 5;
  }

  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  rmw_wait_set_t *wait_set = rmw_create_wait_set(&context, 1);
  if (wait_set == nullptr) {
    return 6;
  }

  int ret = 7;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    void *subscription_handle = subscription->data;
    rmw_subscriptions_t subscriptions;
    subscriptions.subscriber_count = 1;
    subscriptions.subscribers = &subscription_handle;
    rmw_time_t timeout;
    timeout.sec = 0;
    timeout.nsec = 100000000;
    const rmw_ret_t wait_ret = rmw_wait(&subscriptions, nullptr, nullptr,
                                        nullptr, nullptr, wait_set, &timeout);
    if (wait_ret == RMW_RET_OK && subscriptions.subscribers[0] != nullptr) {
      std_msgs::msg::String received;
      bool taken = false;
      if (rmw_take(subscription, &received, &taken, nullptr) == RMW_RET_OK &&
          taken) {
        std::ofstream result(result_path);
        result << received.data << "\n";
        ret = received.data == expected ? 0 : 8;
        break;
      }
    }
  }

  IgnoreRmwRet(rmw_destroy_wait_set(wait_set));
  IgnoreRmwRet(rmw_destroy_subscription(node, subscription));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunGraphPublisher(const std::string &socket_path,
                      const std::string &ready_path, const std::string &topic) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_graph_pub");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_graph_pub", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, topic.c_str(),
                           &rmw_qos_profile_default, &publisher_options);
  if (publisher == nullptr) {
    return 5;
  }

  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  std::this_thread::sleep_for(5s);

  IgnoreRmwRet(rmw_destroy_publisher(node, publisher));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return 0;
}

int RunMatchedPublisher(const std::string &socket_path,
                        const std::string &ready_path,
                        const std::string &topic) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_matched_pub");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_matched_pub", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, topic.c_str(),
                           &rmw_qos_profile_default, &publisher_options);
  if (publisher == nullptr) {
    return 5;
  }

  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  std::this_thread::sleep_for(5s);

  IgnoreRmwRet(rmw_destroy_publisher(node, publisher));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return 0;
}

int RunMatchedSubscriber(const std::string &socket_path,
                         const std::string &ready_path,
                         const std::string &topic) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_matched_sub");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_matched_sub", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription =
      rmw_create_subscription(node, type_support, topic.c_str(),
                              &rmw_qos_profile_default, &subscription_options);
  if (subscription == nullptr) {
    return 5;
  }

  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  std::this_thread::sleep_for(5s);

  IgnoreRmwRet(rmw_destroy_subscription(node, subscription));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return 0;
}

int RunMatchedPublisherObserver(const std::string &socket_path,
                                const std::string &result_path,
                                const std::string &topic) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_matched_pub_observer");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_matched_pub_observer", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, topic.c_str(),
                           &rmw_qos_profile_default, &publisher_options);
  if (publisher == nullptr) {
    return 5;
  }

  int ret = 6;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    size_t subscription_count = 0;
    if (rmw_publisher_count_matched_subscriptions(
            publisher, &subscription_count) == RMW_RET_OK &&
        subscription_count == 1u) {
      std::ofstream result(result_path);
      result << "publisher-matched-ok\n";
      ret = 0;
      break;
    }
    std::this_thread::sleep_for(10ms);
  }

  IgnoreRmwRet(rmw_destroy_publisher(node, publisher));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunMatchedSubscriberObserver(const std::string &socket_path,
                                 const std::string &result_path,
                                 const std::string &topic) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_matched_sub_observer");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_matched_sub_observer", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription =
      rmw_create_subscription(node, type_support, topic.c_str(),
                              &rmw_qos_profile_default, &subscription_options);
  if (subscription == nullptr) {
    return 5;
  }

  int ret = 6;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    size_t publisher_count = 0;
    if (rmw_subscription_count_matched_publishers(
            subscription, &publisher_count) == RMW_RET_OK &&
        publisher_count == 1u) {
      std::ofstream result(result_path);
      result << "subscription-matched-ok\n";
      ret = 0;
      break;
    }
    std::this_thread::sleep_for(10ms);
  }

  IgnoreRmwRet(rmw_destroy_subscription(node, subscription));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunMatchedPublisherEventObserver(const std::string &socket_path,
                                     const std::string &result_path,
                                     const std::string &topic) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_pub_event_observer");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_pub_event_observer", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *publisher =
      rmw_create_publisher(node, type_support, topic.c_str(),
                           &rmw_qos_profile_default, &publisher_options);
  if (publisher == nullptr) {
    return 5;
  }

  rmw_event_t event = rmw_get_zero_initialized_event();
  if (rmw_publisher_event_init(&event, publisher,
                               RMW_EVENT_PUBLICATION_MATCHED) != RMW_RET_OK) {
    return 6;
  }
  rmw_wait_set_t *wait_set = rmw_create_wait_set(&context, 1);
  if (wait_set == nullptr) {
    return 7;
  }

  int ret = 8;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    void *event_handle = &event;
    rmw_events_t events;
    events.event_count = 1;
    events.events = &event_handle;
    rmw_time_t timeout;
    timeout.sec = 0;
    timeout.nsec = 100000000;
    const rmw_ret_t wait_ret = rmw_wait(nullptr, nullptr, nullptr, nullptr,
                                        &events, wait_set, &timeout);
    if (wait_ret != RMW_RET_OK || events.events[0] == nullptr) {
      continue;
    }
    rmw_matched_status_t status{};
    bool taken = false;
    if (rmw_take_event(&event, &status, &taken) == RMW_RET_OK && taken &&
        status.total_count == 1u && status.total_count_change == 1u &&
        status.current_count == 1u && status.current_count_change == 1) {
      std::ofstream result(result_path);
      result << "publisher-event-ok\n";
      ret = 0;
      break;
    }
  }

  IgnoreRmwRet(rmw_destroy_wait_set(wait_set));
  IgnoreRmwRet(rmw_event_fini(&event));
  IgnoreRmwRet(rmw_destroy_publisher(node, publisher));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunMatchedSubscriberEventObserver(const std::string &socket_path,
                                      const std::string &result_path,
                                      const std::string &topic) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_sub_event_observer");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_sub_event_observer", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription =
      rmw_create_subscription(node, type_support, topic.c_str(),
                              &rmw_qos_profile_default, &subscription_options);
  if (subscription == nullptr) {
    return 5;
  }

  rmw_event_t event = rmw_get_zero_initialized_event();
  if (rmw_subscription_event_init(
          &event, subscription, RMW_EVENT_SUBSCRIPTION_MATCHED) != RMW_RET_OK) {
    return 6;
  }
  rmw_wait_set_t *wait_set = rmw_create_wait_set(&context, 1);
  if (wait_set == nullptr) {
    return 7;
  }

  int ret = 8;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    void *event_handle = &event;
    rmw_events_t events;
    events.event_count = 1;
    events.events = &event_handle;
    rmw_time_t timeout;
    timeout.sec = 0;
    timeout.nsec = 100000000;
    const rmw_ret_t wait_ret = rmw_wait(nullptr, nullptr, nullptr, nullptr,
                                        &events, wait_set, &timeout);
    if (wait_ret != RMW_RET_OK || events.events[0] == nullptr) {
      continue;
    }
    rmw_matched_status_t status{};
    bool taken = false;
    if (rmw_take_event(&event, &status, &taken) == RMW_RET_OK && taken &&
        status.total_count == 1u && status.total_count_change == 1u &&
        status.current_count == 1u && status.current_count_change == 1) {
      std::ofstream result(result_path);
      result << "subscription-event-ok\n";
      ret = 0;
      break;
    }
  }

  IgnoreRmwRet(rmw_destroy_wait_set(wait_set));
  IgnoreRmwRet(rmw_event_fini(&event));
  IgnoreRmwRet(rmw_destroy_subscription(node, subscription));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

bool GraphSnapshotContainsRemotePublisher(rmw_node_t *node,
                                          rcutils_allocator_t *allocator,
                                          const std::string &topic,
                                          const std::string &type) {
  rmw_names_and_types_t topic_names =
      rmw_get_zero_initialized_names_and_types();
  const bool has_topic =
      rmw_get_topic_names_and_types(node, allocator, false, &topic_names) ==
          RMW_RET_OK &&
      NamesAndTypesContains(topic_names, topic, type);
  IgnoreRmwRet(rmw_names_and_types_fini(&topic_names));

  rmw_names_and_types_t publisher_names =
      rmw_get_zero_initialized_names_and_types();
  const bool has_publisher =
      rmw_get_publisher_names_and_types_by_node(
          node, allocator, "mdds_broker_graph_pub", "/mdds", false,
          &publisher_names) == RMW_RET_OK &&
      NamesAndTypesContains(publisher_names, topic, type);
  IgnoreRmwRet(rmw_names_and_types_fini(&publisher_names));

  rcutils_string_array_t node_names =
      rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t node_namespaces =
      rcutils_get_zero_initialized_string_array();
  const bool has_node =
      rmw_get_node_names(node, &node_names, &node_namespaces) == RMW_RET_OK &&
      NodeNamesContain(node_names, node_namespaces, "mdds_broker_graph_pub",
                       "/mdds");
  IgnoreRcutilsRet(rcutils_string_array_fini(&node_names));
  IgnoreRcutilsRet(rcutils_string_array_fini(&node_namespaces));

  rcutils_string_array_t names_with_enclaves =
      rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t namespaces_with_enclaves =
      rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t enclaves = rcutils_get_zero_initialized_string_array();
  const bool has_enclave =
      rmw_get_node_names_with_enclaves(node, &names_with_enclaves,
                                       &namespaces_with_enclaves,
                                       &enclaves) == RMW_RET_OK &&
      NodeNamesWithEnclavesContain(
          names_with_enclaves, namespaces_with_enclaves, enclaves,
          "mdds_broker_graph_pub", "/mdds", "/rmw_mdds_broker_graph_pub");
  IgnoreRcutilsRet(rcutils_string_array_fini(&names_with_enclaves));
  IgnoreRcutilsRet(rcutils_string_array_fini(&namespaces_with_enclaves));
  IgnoreRcutilsRet(rcutils_string_array_fini(&enclaves));

  rmw_topic_endpoint_info_array_t publisher_infos =
      rmw_get_zero_initialized_topic_endpoint_info_array();
  const bool has_publisher_info =
      rmw_get_publishers_info_by_topic(node, allocator, topic.c_str(), false,
                                       &publisher_infos) == RMW_RET_OK &&
      PublisherEndpointInfosContain(publisher_infos, "mdds_broker_graph_pub",
                                    "/mdds", type);
  IgnoreRmwRet(rmw_topic_endpoint_info_array_fini(&publisher_infos, allocator));

  return has_topic && has_publisher && has_node && has_enclave &&
         has_publisher_info;
}

int RunGraphObserver(const std::string &socket_path,
                     const std::string &result_path, const std::string &topic,
                     const std::string &type) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_graph_observer");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_graph_observer", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription =
      rmw_create_subscription(node, type_support, "/mdds_broker_graph_anchor",
                              &rmw_qos_profile_default, &subscription_options);
  if (subscription == nullptr) {
    return 5;
  }

  int ret = 6;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (GraphSnapshotContainsRemotePublisher(node, &allocator, topic, type)) {
      std::ofstream result(result_path);
      result << "graph-ok\n";
      ret = 0;
      break;
    }
    std::this_thread::sleep_for(10ms);
  }

  IgnoreRmwRet(rmw_destroy_subscription(node, subscription));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunPrimedGraphObserver(const std::string &socket_path,
                           const std::string &ready_path,
                           const std::string &result_path,
                           const std::string &topic,
                           const std::string &type) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_primed_graph_observer");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_primed_graph_observer", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_broker_primed_graph_anchor",
      &rmw_qos_profile_default, &subscription_options);
  if (subscription == nullptr) {
    return 5;
  }

  // Prime the graph cache before the publisher exists. A local-only graph must
  // not suppress later refreshes while discovery is still changing.
  (void)GraphSnapshotContainsRemotePublisher(node, &allocator, topic, type);
  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  int ret = 6;
  const auto deadline = std::chrono::steady_clock::now() + 4s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (GraphSnapshotContainsRemotePublisher(node, &allocator, topic, type)) {
      std::ofstream result(result_path);
      result << "primed-graph-ok\n";
      ret = 0;
      break;
    }
    std::this_thread::sleep_for(10ms);
  }

  IgnoreRmwRet(rmw_destroy_subscription(node, subscription));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunGraphGuardObserver(const std::string &socket_path,
                          const std::string &ready_path,
                          const std::string &result_path,
                          const std::string &topic,
                          const std::string &type) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_graph_guard_observer");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_graph_guard_observer", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_broker_graph_guard_anchor",
      &rmw_qos_profile_default, &subscription_options);
  const rmw_guard_condition_t *guard =
      rmw_node_get_graph_guard_condition(node);
  rmw_wait_set_t *wait_set = rmw_create_wait_set(&context, 1u);
  if (subscription == nullptr || guard == nullptr || wait_set == nullptr) {
    return 5;
  }

  (void)GraphSnapshotContainsRemotePublisher(node, &allocator, topic, type);
  void *guard_handle = guard->data;
  rmw_guard_conditions_t guards;
  guards.guard_condition_count = 1u;
  guards.guard_conditions = &guard_handle;
  rmw_time_t zero_timeout;
  zero_timeout.sec = 0u;
  zero_timeout.nsec = 0u;
  const rmw_ret_t prime_wait_ret = rmw_wait(
      nullptr, &guards, nullptr, nullptr, nullptr, wait_set, &zero_timeout);
  (void)prime_wait_ret;

  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  int ret = 6;
  const auto deadline = std::chrono::steady_clock::now() + 4s;
  while (std::chrono::steady_clock::now() < deadline) {
    guard_handle = guard->data;
    guards.guard_conditions = &guard_handle;
    rmw_time_t timeout;
    timeout.sec = 0u;
    timeout.nsec = 500000000u;
    const rmw_ret_t wait_ret = rmw_wait(
        nullptr, &guards, nullptr, nullptr, nullptr, wait_set, &timeout);
    if (wait_ret == RMW_RET_TIMEOUT) {
      continue;
    }
    if (wait_ret != RMW_RET_OK || guards.guard_conditions[0] == nullptr) {
      ret = 7;
      break;
    }
    if (GraphSnapshotContainsRemotePublisher(node, &allocator, topic, type)) {
      std::ofstream result(result_path);
      result << "graph-guard-ok\n";
      ret = 0;
      break;
    }
  }

  IgnoreRmwRet(rmw_destroy_wait_set(wait_set));
  IgnoreRmwRet(rmw_destroy_subscription(node, subscription));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunPublisherPrimedGraphObserver(const std::string &socket_path,
                                    const std::string &ready_path,
                                    const std::string &result_path,
                                    const std::string &topic,
                                    const std::string &type) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_pub_primed_graph_observer");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node = rmw_create_node(
      &context, "mdds_broker_pub_primed_graph_observer", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t *anchor = rmw_create_publisher(
      node, type_support, "/mdds_broker_pub_primed_graph_anchor",
      &rmw_qos_profile_default, &publisher_options);
  if (anchor == nullptr) {
    return 5;
  }

  // Publishers do not have a broker reader thread. If this primes a local-only
  // cache, later graph queries must still refresh instead of trusting it.
  (void)GraphSnapshotContainsRemotePublisher(node, &allocator, topic, type);
  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  int ret = 6;
  const auto deadline = std::chrono::steady_clock::now() + 4s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (GraphSnapshotContainsRemotePublisher(node, &allocator, topic, type)) {
      std::ofstream result(result_path);
      result << "publisher-primed-graph-ok\n";
      ret = 0;
      break;
    }
    std::this_thread::sleep_for(10ms);
  }

  IgnoreRmwRet(rmw_destroy_publisher(node, anchor));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunColdGraphObserver(const std::string &socket_path,
                         const std::string &result_path,
                         const std::string &topic, const std::string &type) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_cold_graph_observer");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_cold_graph_observer", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  int ret = 5;
  bool saw_topic = false;
  bool saw_publisher = false;
  bool saw_node = false;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    rmw_names_and_types_t topic_names =
        rmw_get_zero_initialized_names_and_types();
    saw_topic = rmw_get_topic_names_and_types(node, &allocator, false,
                                              &topic_names) == RMW_RET_OK &&
                NamesAndTypesContains(topic_names, topic, type);
    IgnoreRmwRet(rmw_names_and_types_fini(&topic_names));

    rmw_names_and_types_t publisher_names =
        rmw_get_zero_initialized_names_and_types();
    saw_publisher = rmw_get_publisher_names_and_types_by_node(
                        node, &allocator, "mdds_broker_graph_pub", "/mdds",
                        false, &publisher_names) == RMW_RET_OK &&
                    NamesAndTypesContains(publisher_names, topic, type);
    IgnoreRmwRet(rmw_names_and_types_fini(&publisher_names));

    rcutils_string_array_t node_names =
        rcutils_get_zero_initialized_string_array();
    rcutils_string_array_t node_namespaces =
        rcutils_get_zero_initialized_string_array();
    saw_node =
        rmw_get_node_names(node, &node_names, &node_namespaces) == RMW_RET_OK &&
        NodeNamesContain(node_names, node_namespaces, "mdds_broker_graph_pub",
                         "/mdds");
    IgnoreRcutilsRet(rcutils_string_array_fini(&node_names));
    IgnoreRcutilsRet(rcutils_string_array_fini(&node_namespaces));

    if (GraphSnapshotContainsRemotePublisher(node, &allocator, topic, type)) {
      std::ofstream result(result_path);
      result << "cold-graph-ok\n";
      ret = 0;
      break;
    }
    std::this_thread::sleep_for(10ms);
  }
  if (ret != 0) {
    std::ofstream result(result_path);
    result << "cold-graph-fail topic=" << (saw_topic ? 1 : 0)
           << " publisher=" << (saw_publisher ? 1 : 0)
           << " node=" << (saw_node ? 1 : 0) << "\n";
  }

  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunOneShotTopicGraphObserver(const std::string &socket_path,
                                 const std::string &result_path,
                                 const std::string &topic,
                                 const std::string &type) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_one_shot_graph_observer");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_one_shot_graph_observer", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  rmw_names_and_types_t topic_names =
      rmw_get_zero_initialized_names_and_types();
  const bool has_topic =
      rmw_get_topic_names_and_types(node, &allocator, false, &topic_names) ==
          RMW_RET_OK &&
      NamesAndTypesContains(topic_names, topic, type);
  IgnoreRmwRet(rmw_names_and_types_fini(&topic_names));

  {
    std::ofstream result(result_path);
    result << (has_topic ? "one-shot-graph-ok" : "one-shot-graph-miss")
           << "\n";
  }

  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return has_topic ? 0 : 5;
}

int RunGraphService(const std::string &socket_path,
                    const std::string &ready_path,
                    const std::string &service_name) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_graph_service");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_graph_service", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_service_type_support_t *type_support =
      ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
          rosidl_typesupport_c, std_srvs, srv, Trigger)();
  rmw_service_t *service =
      rmw_create_service(node, type_support, service_name.c_str(),
                         &rmw_qos_profile_services_default);
  if (service == nullptr) {
    return 5;
  }

  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  std::this_thread::sleep_for(5s);

  IgnoreRmwRet(rmw_destroy_service(node, service));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return 0;
}

int RunGraphClient(const std::string &socket_path,
                   const std::string &ready_path,
                   const std::string &service_name) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_graph_client");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_graph_client", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_service_type_support_t *type_support =
      ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
          rosidl_typesupport_c, std_srvs, srv, Trigger)();
  rmw_client_t *client =
      rmw_create_client(node, type_support, service_name.c_str(),
                        &rmw_qos_profile_services_default);
  if (client == nullptr) {
    return 5;
  }

  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  std::this_thread::sleep_for(5s);

  IgnoreRmwRet(rmw_destroy_client(node, client));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return 0;
}

bool ServiceClientGraphSnapshotContains(rmw_node_t *node,
                                        rcutils_allocator_t *allocator,
                                        const std::string &service_name,
                                        const std::string &type) {
  size_t service_count = 0;
  const bool has_service_count =
      rmw_count_services(node, service_name.c_str(), &service_count) ==
          RMW_RET_OK &&
      service_count == 1u;

  size_t client_count = 0;
  const bool has_client_count =
      rmw_count_clients(node, service_name.c_str(), &client_count) ==
          RMW_RET_OK &&
      client_count == 1u;

  rmw_names_and_types_t service_names =
      rmw_get_zero_initialized_names_and_types();
  const bool has_global_service =
      rmw_get_service_names_and_types(node, allocator, &service_names) ==
          RMW_RET_OK &&
      NamesAndTypesContains(service_names, service_name, type);
  IgnoreRmwRet(rmw_names_and_types_fini(&service_names));

  rmw_names_and_types_t service_names_by_node =
      rmw_get_zero_initialized_names_and_types();
  const bool has_service_by_node =
      rmw_get_service_names_and_types_by_node(
          node, allocator, "mdds_broker_graph_service", "/mdds",
          &service_names_by_node) == RMW_RET_OK &&
      NamesAndTypesContains(service_names_by_node, service_name, type);
  IgnoreRmwRet(rmw_names_and_types_fini(&service_names_by_node));

  rmw_names_and_types_t client_names_by_node =
      rmw_get_zero_initialized_names_and_types();
  const bool has_client_by_node =
      rmw_get_client_names_and_types_by_node(
          node, allocator, "mdds_broker_graph_client", "/mdds",
          &client_names_by_node) == RMW_RET_OK &&
      NamesAndTypesContains(client_names_by_node, service_name, type);
  IgnoreRmwRet(rmw_names_and_types_fini(&client_names_by_node));

  rcutils_string_array_t names_with_enclaves =
      rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t namespaces_with_enclaves =
      rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t enclaves = rcutils_get_zero_initialized_string_array();
  const bool has_service_node =
      rmw_get_node_names_with_enclaves(node, &names_with_enclaves,
                                       &namespaces_with_enclaves,
                                       &enclaves) == RMW_RET_OK &&
      NodeNamesWithEnclavesContain(names_with_enclaves,
                                   namespaces_with_enclaves, enclaves,
                                   "mdds_broker_graph_service", "/mdds",
                                   "/rmw_mdds_broker_graph_service");
  const bool has_client_node = NodeNamesWithEnclavesContain(
      names_with_enclaves, namespaces_with_enclaves, enclaves,
      "mdds_broker_graph_client", "/mdds", "/rmw_mdds_broker_graph_client");
  IgnoreRcutilsRet(rcutils_string_array_fini(&names_with_enclaves));
  IgnoreRcutilsRet(rcutils_string_array_fini(&namespaces_with_enclaves));
  IgnoreRcutilsRet(rcutils_string_array_fini(&enclaves));

  return has_service_count && has_client_count && has_global_service &&
         has_service_by_node && has_client_by_node && has_service_node &&
         has_client_node;
}

int RunServiceClientGraphObserver(const std::string &socket_path,
                                  const std::string &result_path,
                                  const std::string &service_name,
                                  const std::string &type) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_service_graph_observer");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_service_graph_observer", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_message_type_support_t *type_support =
      rosidl_typesupport_cpp::get_message_type_support_handle<
          std_msgs::msg::String>();
  rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_subscription_t *subscription = rmw_create_subscription(
      node, type_support, "/mdds_broker_service_graph_anchor",
      &rmw_qos_profile_default, &subscription_options);
  if (subscription == nullptr) {
    return 5;
  }

  int ret = 6;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (ServiceClientGraphSnapshotContains(node, &allocator, service_name,
                                           type)) {
      std::ofstream result(result_path);
      result << "service-client-graph-ok\n";
      ret = 0;
      break;
    }
    std::this_thread::sleep_for(10ms);
  }

  IgnoreRmwRet(rmw_destroy_subscription(node, subscription));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunService(const std::string &socket_path, const std::string &ready_path,
               const std::string &service_name,
               const std::string &response_message) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_process_service");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_process_service", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_service_type_support_t *type_support =
      ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
          rosidl_typesupport_c, std_srvs, srv, Trigger)();
  rmw_service_t *service =
      rmw_create_service(node, type_support, service_name.c_str(),
                         &rmw_qos_profile_services_default);
  if (service == nullptr) {
    return 5;
  }

  {
    std::ofstream ready(ready_path);
    ready << "ready\n";
  }

  rmw_wait_set_t *wait_set = rmw_create_wait_set(&context, 1);
  if (wait_set == nullptr) {
    return 6;
  }

  int ret = 7;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    void *service_handle = service->data;
    rmw_services_t services;
    services.service_count = 1;
    services.services = &service_handle;
    rmw_time_t timeout;
    timeout.sec = 0;
    timeout.nsec = 100000000;
    const rmw_ret_t wait_ret = rmw_wait(nullptr, nullptr, &services, nullptr,
                                        nullptr, wait_set, &timeout);
    if (wait_ret != RMW_RET_OK || services.services[0] == nullptr) {
      continue;
    }

    std_srvs__srv__Trigger_Request request;
    if (!std_srvs__srv__Trigger_Request__init(&request)) {
      ret = 8;
      break;
    }
    rmw_service_info_t request_header{};
    bool taken = false;
    const rmw_ret_t take_ret =
        rmw_take_request(service, &request_header, &request, &taken);
    std_srvs__srv__Trigger_Request__fini(&request);
    if (take_ret != RMW_RET_OK || !taken) {
      continue;
    }

    std_srvs__srv__Trigger_Response response;
    if (!std_srvs__srv__Trigger_Response__init(&response)) {
      ret = 9;
      break;
    }
    response.success = true;
    if (!rosidl_runtime_c__String__assign(&response.message,
                                          response_message.c_str())) {
      std_srvs__srv__Trigger_Response__fini(&response);
      ret = 10;
      break;
    }
    ret = rmw_send_response(service, &request_header.request_id, &response) ==
                  RMW_RET_OK
              ? 0
              : 11;
    std_srvs__srv__Trigger_Response__fini(&response);
    break;
  }

  IgnoreRmwRet(rmw_destroy_wait_set(wait_set));
  IgnoreRmwRet(rmw_destroy_service(node, service));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}

int RunClient(const std::string &socket_path, const std::string &result_path,
              const std::string &service_name,
              const std::string &expected_response) {
  SetBrokerEnvironment(socket_path);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 2;
  }
  SetEnclave(&options, "/rmw_mdds_broker_process_client");

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return 3;
  }
  rmw_node_t *node =
      rmw_create_node(&context, "mdds_broker_process_client", "/mdds");
  if (node == nullptr) {
    return 4;
  }

  const rosidl_service_type_support_t *type_support =
      ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(
          rosidl_typesupport_c, std_srvs, srv, Trigger)();
  rmw_client_t *client =
      rmw_create_client(node, type_support, service_name.c_str(),
                        &rmw_qos_profile_services_default);
  if (client == nullptr) {
    return 5;
  }

  bool service_available = false;
  const auto availability_deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < availability_deadline) {
    bool current_available = false;
    if (rmw_service_server_is_available(node, client, &current_available) ==
            RMW_RET_OK &&
        current_available) {
      service_available = true;
      break;
    }
    std::this_thread::sleep_for(10ms);
  }
  if (!service_available) {
    return 6;
  }

  std_srvs__srv__Trigger_Request request;
  if (!std_srvs__srv__Trigger_Request__init(&request)) {
    return 7;
  }
  int64_t sequence_id = -1;
  if (rmw_send_request(client, &request, &sequence_id) != RMW_RET_OK) {
    std_srvs__srv__Trigger_Request__fini(&request);
    return 8;
  }
  std_srvs__srv__Trigger_Request__fini(&request);

  rmw_wait_set_t *wait_set = rmw_create_wait_set(&context, 1);
  if (wait_set == nullptr) {
    return 9;
  }

  int ret = 10;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    void *client_handle = client->data;
    rmw_clients_t clients;
    clients.client_count = 1;
    clients.clients = &client_handle;
    rmw_time_t timeout;
    timeout.sec = 0;
    timeout.nsec = 100000000;
    const rmw_ret_t wait_ret = rmw_wait(nullptr, nullptr, nullptr, &clients,
                                        nullptr, wait_set, &timeout);
    if (wait_ret != RMW_RET_OK || clients.clients[0] == nullptr) {
      continue;
    }

    std_srvs__srv__Trigger_Response response;
    if (!std_srvs__srv__Trigger_Response__init(&response)) {
      ret = 11;
      break;
    }
    rmw_service_info_t response_header{};
    bool taken = false;
    const rmw_ret_t take_ret =
        rmw_take_response(client, &response_header, &response, &taken);
    if (take_ret == RMW_RET_OK && taken) {
      const std::string actual = response.message.data == nullptr
                                     ? std::string()
                                     : response.message.data;
      std::ofstream result(result_path);
      result << actual << "\n";
      ret = response.success &&
                    response_header.request_id.sequence_number == sequence_id &&
                    actual == expected_response
                ? 0
                : 12;
      std_srvs__srv__Trigger_Response__fini(&response);
      break;
    }
    std_srvs__srv__Trigger_Response__fini(&response);
  }

  IgnoreRmwRet(rmw_destroy_wait_set(wait_set));
  IgnoreRmwRet(rmw_destroy_client(node, client));
  IgnoreRmwRet(rmw_destroy_node(node));
  IgnoreRmwRet(rmw_shutdown(&context));
  IgnoreRmwRet(rmw_context_fini(&context));
  IgnoreRmwRet(rmw_init_options_fini(&options));
  return ret;
}
} // namespace

TEST(RmwMddsBrokerProcess, RoutesSamplesBetweenSeparateRmwProcesses) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_process_string";
  const std::string payload = "hello separate rmw processes";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t subscriber_pid = SpawnProcess(
      self, {"--loaned-string-subscriber", temp_dir.SocketPath(),
             temp_dir.ReadyPath(), temp_dir.ResultPath(), topic, payload});
  ASSERT_GT(subscriber_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));

  const pid_t publisher_pid = SpawnProcess(
      self, {"--publisher", temp_dir.SocketPath(), topic, payload});
  ASSERT_GT(publisher_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 3s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(subscriber_pid, 4s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ(payload, received);
}

TEST(RmwMddsBrokerProcess,
     RoutesFixedSizeLoanedSampleBetweenSeparateRmwProcesses) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_process_loaned_int32";
  constexpr int32_t payload = 3588;

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t subscriber_pid = SpawnProcess(
      self, {"--int32-subscriber", temp_dir.SocketPath(), temp_dir.ReadyPath(),
             temp_dir.ResultPath(), topic, std::to_string(payload)});
  ASSERT_GT(subscriber_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));

  const pid_t publisher_pid =
      SpawnProcess(self, {"--loaned-int32-publisher", temp_dir.SocketPath(),
                          topic, std::to_string(payload)});
  ASSERT_GT(publisher_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 3s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(subscriber_pid, 4s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ(std::to_string(payload), received);
}

TEST(RmwMddsBrokerProcess,
     RoutesLoanedStringSampleBetweenSeparateRmwProcesses) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_process_loaned_string";
  const std::string payload = "loaned string through broker";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t subscriber_pid = SpawnProcess(
      self, {"--subscriber", temp_dir.SocketPath(), temp_dir.ReadyPath(),
             temp_dir.ResultPath(), topic, payload});
  ASSERT_GT(subscriber_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));

  const pid_t publisher_pid =
      SpawnProcess(self, {"--loaned-string-publisher", temp_dir.SocketPath(),
                          topic, payload});
  ASSERT_GT(publisher_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 3s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(subscriber_pid, 4s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ(payload, received);
}

TEST(RmwMddsBrokerProcess,
     RoutesExplicitReliableCliQosBetweenSeparateRmwProcesses) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_cli_reliable_string";
  const std::string payload = "qos_reliable_ok";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t subscriber_pid = SpawnProcess(
      self, {"--reliable-sensor-subscriber", temp_dir.SocketPath(),
             temp_dir.ReadyPath(), temp_dir.ResultPath(), topic, payload});
  ASSERT_GT(subscriber_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));

  const pid_t publisher_pid = SpawnProcess(
      self, {"--publisher", temp_dir.SocketPath(), topic, payload});
  ASSERT_GT(publisher_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 3s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(subscriber_pid, 4s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ(payload, received);
}

TEST(RmwMddsBrokerProcess,
     DefaultTransportRoutesSamplesBetweenSeparateRmwProcesses) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_default_process_string";
  const std::string payload = "hello default rmw mdds transport";

  const pid_t subscriber_pid = SpawnProcess(
      self, {"--default-subscriber", temp_dir.SocketPath(),
             temp_dir.ReadyPath(), temp_dir.ResultPath(), topic, payload});
  ASSERT_GT(subscriber_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));
  EXPECT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t publisher_pid = SpawnProcess(
      self, {"--default-publisher", temp_dir.SocketPath(), topic, payload});
  ASSERT_GT(publisher_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 3s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(subscriber_pid, 4s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ(payload, received);
}

TEST(RmwMddsBrokerProcess, AutoStartedBrokerOutlivesStarterRmwProcess) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_default_autostart_owner";

  EnvVarGuard executable_guard("RMW_MDDS_BROKER_EXECUTABLE");
  EnvVarGuard pid_guard("RMW_MDDS_BROKER_PID_FILE");
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_EXECUTABLE", broker.c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_PID_FILE",
                      temp_dir.BrokerPidPath().c_str(), 1));

  const pid_t owner_pid =
      SpawnProcess(self, {"--default-autostart-owner", temp_dir.SocketPath(),
                          temp_dir.ReadyPath(), topic});
  ASSERT_GT(owner_pid, 0);
  ASSERT_TRUE(ExitedWithZero(WaitForExit(owner_pid, 4s)));
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));
  ASSERT_TRUE(WaitForFile(temp_dir.BrokerPidPath(), 2s))
      << "auto-start must use an external broker process, not an embedded "
         "broker tied to the starter RMW process";

  const pid_t broker_pid = ReadPidFile(temp_dir.BrokerPidPath());
  ASSERT_GT(broker_pid, 0);
  EXPECT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s))
      << "auto-started broker socket must remain after the starter RMW process "
         "has exited";

  EXPECT_TRUE(TerminateNonChildProcess(broker_pid, 2s));
}

TEST(RmwMddsBrokerProcess, ConcurrentAutoStartSerializesExternalBrokerStart) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_default_autostart_concurrent";
  const std::string start_path = temp_dir.path() + "/start";
  const std::string log_path = temp_dir.path() + "/broker.log";

  EnvVarGuard executable_guard("RMW_MDDS_BROKER_EXECUTABLE");
  EnvVarGuard pid_guard("RMW_MDDS_BROKER_PID_FILE");
  EnvVarGuard log_guard("RMW_MDDS_BROKER_LOG");
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_EXECUTABLE", broker.c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_PID_FILE",
                      temp_dir.BrokerPidPath().c_str(), 1));
  ASSERT_EQ(0, setenv("RMW_MDDS_BROKER_LOG", log_path.c_str(), 1));

  constexpr int kOwnerCount = 32;
  std::vector<pid_t> owner_pids;
  std::vector<std::string> ready_paths;
  owner_pids.reserve(kOwnerCount);
  ready_paths.reserve(kOwnerCount);
  for (int i = 0; i < kOwnerCount; ++i) {
    ready_paths.push_back(temp_dir.path() + "/owner_" + std::to_string(i) +
                          ".ready");
    const pid_t owner_pid = SpawnProcess(
        self, {"--default-autostart-owner-after-start", temp_dir.SocketPath(),
               ready_paths.back(), topic + "_" + std::to_string(i),
               start_path});
    ASSERT_GT(owner_pid, 0);
    owner_pids.push_back(owner_pid);
  }

  {
    std::ofstream start(start_path);
    start << "start\n";
  }

  std::vector<int> owner_statuses;
  owner_statuses.reserve(owner_pids.size());
  for (pid_t owner_pid : owner_pids) {
    owner_statuses.push_back(WaitForExit(owner_pid, 8s));
  }
  const bool socket_ready = WaitForBrokerSocket(temp_dir.SocketPath(), 2s);
  const std::vector<pid_t> broker_pids =
      BrokerPidsForSocket(temp_dir.SocketPath());
  TerminateBrokerProcessesForSocket(temp_dir.SocketPath());

  for (int status : owner_statuses) {
    EXPECT_TRUE(ExitedWithZero(status)) << "owner exit status=" << status;
  }
  for (const std::string &ready_path : ready_paths) {
    EXPECT_TRUE(FileExists(ready_path)) << "missing ready file " << ready_path;
  }
  EXPECT_TRUE(socket_ready);
  EXPECT_LE(broker_pids.size(), 1u);
  EXPECT_FALSE(FileContains(log_path, "active listener")) << ReadFile(log_path);
  EXPECT_FALSE(FileContains(log_path, "Address in use")) << ReadFile(log_path);
}

TEST(RmwMddsBrokerProcess, ConcurrentDirectBrokerStartsLeaveSingleListener) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string start_path = temp_dir.path() + "/start";
  const std::string command =
      "while [ ! -f '" + start_path + "' ]; do sleep 0.01; done; exec '" +
      broker + "' --socket '" + temp_dir.SocketPath() + "'";

  constexpr int kBrokerCount = 32;
  std::vector<pid_t> pids;
  pids.reserve(kBrokerCount);
  for (int i = 0; i < kBrokerCount; ++i) {
    const pid_t pid = SpawnProcess("/bin/sh", {"-c", command});
    ASSERT_GT(pid, 0);
    pids.push_back(pid);
  }

  {
    std::ofstream start(start_path);
    start << "start\n";
  }

  EXPECT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));
  std::this_thread::sleep_for(300ms);
  const std::vector<pid_t> broker_pids =
      BrokerPidsForSocket(temp_dir.SocketPath());
  TerminateBrokerProcessesForSocket(temp_dir.SocketPath());
  for (pid_t pid : pids) {
    (void)WaitForExit(pid, 2s);
  }

  EXPECT_LE(broker_pids.size(), 1u);
}

TEST(RmwMddsBrokerProcess, RoutesServiceCallsBetweenSeparateRmwProcesses) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string service_name = "/mdds_broker_process_trigger";
  const std::string response =
      "trigger response from separate brokered service";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t service_pid =
      SpawnProcess(self, {"--service", temp_dir.SocketPath(),
                          temp_dir.ServiceReadyPath(), service_name, response});
  ASSERT_GT(service_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ServiceReadyPath(), 2s));

  const pid_t client_pid =
      SpawnProcess(self, {"--client", temp_dir.SocketPath(),
                          temp_dir.ClientResultPath(), service_name, response});
  ASSERT_GT(client_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(client_pid, 4s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(service_pid, 4s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream result(temp_dir.ClientResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ(response, received);
}

TEST(RmwMddsBrokerProcess, ReportsGraphAcrossSeparateRmwProcesses) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_graph_string";
  const std::string type = "std_msgs/msg/String";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t publisher_pid =
      SpawnProcess(self, {"--graph-publisher", temp_dir.SocketPath(),
                          temp_dir.ReadyPath(), topic});
  ASSERT_GT(publisher_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));
  ASSERT_TRUE(
      RawBrokerGraphContainsPublisher(temp_dir.SocketPath(), topic, type));

  const pid_t observer_pid =
      SpawnProcess(self, {"--graph-observer", temp_dir.SocketPath(),
                          temp_dir.ResultPath(), topic, type});
  ASSERT_GT(observer_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(observer_pid, 9s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 6s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ("graph-ok", received);
}

TEST(RmwMddsBrokerProcess, ReportsGraphFromColdGraphOnlyRmwProcess) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_cold_graph_string";
  const std::string type = "std_msgs/msg/String";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t publisher_pid =
      SpawnProcess(self, {"--graph-publisher", temp_dir.SocketPath(),
                          temp_dir.ReadyPath(), topic});
  ASSERT_GT(publisher_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));

  const pid_t observer_pid =
      SpawnProcess(self, {"--cold-graph-observer", temp_dir.SocketPath(),
                          temp_dir.ResultPath(), topic, type});
  ASSERT_GT(observer_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(observer_pid, 9s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 6s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ("cold-graph-ok", received);
}

TEST(RmwMddsBrokerProcess, RefreshesGraphAfterLocalOnlyCachePrime) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_primed_graph_string";
  const std::string type = "std_msgs/msg/String";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t observer_pid =
      SpawnProcess(self, {"--primed-graph-observer", temp_dir.SocketPath(),
                          temp_dir.ClientReadyPath(), temp_dir.ResultPath(),
                          topic, type});
  ASSERT_GT(observer_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ClientReadyPath(), 8s));

  const pid_t publisher_pid =
      SpawnProcess(self, {"--graph-publisher", temp_dir.SocketPath(),
                          temp_dir.ReadyPath(), topic});
  ASSERT_GT(publisher_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));

  EXPECT_TRUE(ExitedWithZero(WaitForExit(observer_pid, 9s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 6s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ("primed-graph-ok", received);
}

TEST(RmwMddsBrokerProcess, GraphUpdateTriggersNodeGraphGuardCondition) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_graph_guard_string";
  const std::string type = "std_msgs/msg/String";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t observer_pid = SpawnProcess(
      self, {"--graph-guard-observer", temp_dir.SocketPath(),
             temp_dir.ClientReadyPath(), temp_dir.ResultPath(), topic, type});
  ASSERT_GT(observer_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ClientReadyPath(), 8s));

  const pid_t publisher_pid =
      SpawnProcess(self, {"--graph-publisher", temp_dir.SocketPath(),
                          temp_dir.ReadyPath(), topic});
  ASSERT_GT(publisher_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));

  EXPECT_TRUE(ExitedWithZero(WaitForExit(observer_pid, 7s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 6s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ("graph-guard-ok", received);
}

TEST(RmwMddsBrokerProcess, RefreshesGraphAfterPublisherOnlyCachePrime) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_publisher_primed_graph_string";
  const std::string type = "std_msgs/msg/String";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t observer_pid = SpawnProcess(
      self, {"--publisher-primed-graph-observer", temp_dir.SocketPath(),
             temp_dir.ClientReadyPath(), temp_dir.ResultPath(), topic, type});
  ASSERT_GT(observer_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ClientReadyPath(), 8s));

  const pid_t publisher_pid =
      SpawnProcess(self, {"--graph-publisher", temp_dir.SocketPath(),
                          temp_dir.ReadyPath(), topic});
  ASSERT_GT(publisher_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));

  EXPECT_TRUE(ExitedWithZero(WaitForExit(observer_pid, 9s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 6s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ("publisher-primed-graph-ok", received);
}

TEST(RmwMddsBrokerProcess, OneShotGraphRefreshWaitsForSettledSnapshot) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_settled_graph_string";
  const std::string type = "std_msgs/msg/String";

  std::string error;
  rmw_mdds_cpp::ipc::UniqueFd listener =
      rmw_mdds_cpp::ipc::ListenUnixSocket(temp_dir.SocketPath(), &error);
  ASSERT_TRUE(listener) << error;

  const pid_t observer_pid =
      SpawnProcess(self, {"--one-shot-topic-graph-observer",
                          temp_dir.SocketPath(), temp_dir.ResultPath(), topic,
                          type});
  ASSERT_GT(observer_pid, 0);

  rmw_mdds_cpp::ipc::UniqueFd client =
      rmw_mdds_cpp::ipc::AcceptUnixSocket(listener.get(), &error);
  ASSERT_TRUE(client) << error;

  ASSERT_TRUE(WriteGraphUpdateFrame(
      client.get(),
      {MakeGraphEndpoint(1u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                         "mdds_local_only_graph_pub",
                         "/mdds_broker_local_only_graph_string", type)}));
  std::this_thread::sleep_for(10ms);
  ASSERT_TRUE(WriteGraphUpdateFrame(
      client.get(),
      {MakeGraphEndpoint(2u, rmw_mdds_cpp::ipc::EndpointKind::kPublisher,
                         "mdds_broker_settled_graph_pub", topic, type)},
      2u));

  EXPECT_TRUE(ExitedWithZero(WaitForExit(observer_pid, 4s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ("one-shot-graph-ok", received);
}

TEST(RmwMddsBrokerProcess,
     ReportsServiceClientGraphAcrossSeparateRmwProcesses) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string service_name = "/mdds_broker_graph_trigger";
  const std::string type = "std_srvs/srv/Trigger";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t service_pid =
      SpawnProcess(self, {"--graph-service", temp_dir.SocketPath(),
                          temp_dir.ServiceReadyPath(), service_name});
  ASSERT_GT(service_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ServiceReadyPath(), 2s));

  const pid_t client_pid =
      SpawnProcess(self, {"--graph-client", temp_dir.SocketPath(),
                          temp_dir.ClientReadyPath(), service_name});
  ASSERT_GT(client_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ClientReadyPath(), 2s));

  const pid_t observer_pid = SpawnProcess(
      self, {"--service-client-graph-observer", temp_dir.SocketPath(),
             temp_dir.ResultPath(), service_name, type});
  ASSERT_GT(observer_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(observer_pid, 4s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(client_pid, 6s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(service_pid, 6s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream result(temp_dir.ResultPath());
  std::string received;
  std::getline(result, received);
  EXPECT_EQ("service-client-graph-ok", received);
}

TEST(RmwMddsBrokerProcess, ReportsMatchedCountsAcrossSeparateRmwProcesses) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_matched_string";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t subscriber_pid =
      SpawnProcess(self, {"--matched-subscriber", temp_dir.SocketPath(),
                          temp_dir.ReadyPath(), topic});
  ASSERT_GT(subscriber_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));

  const pid_t publisher_observer_pid =
      SpawnProcess(self, {"--matched-publisher-observer", temp_dir.SocketPath(),
                          temp_dir.ResultPath(), topic});
  ASSERT_GT(publisher_observer_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_observer_pid, 4s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(subscriber_pid, 6s)));

  std::ifstream publisher_result(temp_dir.ResultPath());
  std::string publisher_received;
  std::getline(publisher_result, publisher_received);
  EXPECT_EQ("publisher-matched-ok", publisher_received);

  const pid_t publisher_pid =
      SpawnProcess(self, {"--matched-publisher", temp_dir.SocketPath(),
                          temp_dir.ServiceReadyPath(), topic});
  ASSERT_GT(publisher_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ServiceReadyPath(), 2s));

  const pid_t subscriber_observer_pid = SpawnProcess(
      self, {"--matched-subscriber-observer", temp_dir.SocketPath(),
             temp_dir.ClientResultPath(), topic});
  ASSERT_GT(subscriber_observer_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(subscriber_observer_pid, 4s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 6s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream subscriber_result(temp_dir.ClientResultPath());
  std::string subscriber_received;
  std::getline(subscriber_result, subscriber_received);
  EXPECT_EQ("subscription-matched-ok", subscriber_received);
}

TEST(RmwMddsBrokerProcess, ReportsMatchedEventsAcrossSeparateRmwProcesses) {
  const std::string self = CurrentExecutablePath(nullptr);
  ASSERT_FALSE(self.empty());
  const std::string broker = DirectoryName(self) + "/rmw_mdds_broker";
  ASSERT_EQ(0, access(broker.c_str(), X_OK))
      << "missing broker executable: " << broker;

  TempDirectory temp_dir;
  ASSERT_FALSE(temp_dir.path().empty());
  const std::string topic = "/mdds_broker_matched_event_string";

  const pid_t broker_pid =
      SpawnProcess(broker, {"--socket", temp_dir.SocketPath()});
  ASSERT_GT(broker_pid, 0);
  ASSERT_TRUE(WaitForBrokerSocket(temp_dir.SocketPath(), 2s));

  const pid_t subscriber_pid =
      SpawnProcess(self, {"--matched-subscriber", temp_dir.SocketPath(),
                          temp_dir.ReadyPath(), topic});
  ASSERT_GT(subscriber_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ReadyPath(), 2s));

  const pid_t publisher_observer_pid =
      SpawnProcess(self, {"--matched-publisher-event-observer",
                          temp_dir.SocketPath(), temp_dir.ResultPath(), topic});
  ASSERT_GT(publisher_observer_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_observer_pid, 4s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(subscriber_pid, 6s)));

  std::ifstream publisher_result(temp_dir.ResultPath());
  std::string publisher_received;
  std::getline(publisher_result, publisher_received);
  EXPECT_EQ("publisher-event-ok", publisher_received);

  const pid_t publisher_pid =
      SpawnProcess(self, {"--matched-publisher", temp_dir.SocketPath(),
                          temp_dir.ServiceReadyPath(), topic});
  ASSERT_GT(publisher_pid, 0);
  ASSERT_TRUE(WaitForFile(temp_dir.ServiceReadyPath(), 2s));

  const pid_t subscriber_observer_pid = SpawnProcess(
      self, {"--matched-subscriber-event-observer", temp_dir.SocketPath(),
             temp_dir.ClientResultPath(), topic});
  ASSERT_GT(subscriber_observer_pid, 0);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(subscriber_observer_pid, 4s)));
  EXPECT_TRUE(ExitedWithZero(WaitForExit(publisher_pid, 6s)));

  kill(broker_pid, SIGTERM);
  EXPECT_TRUE(ExitedWithZero(WaitForExit(broker_pid, 2s)));

  std::ifstream subscriber_result(temp_dir.ClientResultPath());
  std::string subscriber_received;
  std::getline(subscriber_result, subscriber_received);
  EXPECT_EQ("subscription-event-ok", subscriber_received);
}

int main(int argc, char **argv) {
  if (argc >= 2 && std::strcmp(argv[1], "--publisher") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunPublisher(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--loaned-int32-publisher") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunLoanedInt32Publisher(argv[2], argv[3],
                                   static_cast<int32_t>(std::stol(argv[4])));
  }
  if (argc >= 2 && std::strcmp(argv[1], "--loaned-string-publisher") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunLoanedStringPublisher(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--default-publisher") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunDefaultPublisher(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--default-autostart-owner") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunDefaultAutostartOwner(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 &&
      std::strcmp(argv[1], "--default-autostart-owner-after-start") == 0) {
    if (argc != 6) {
      return 64;
    }
    return RunDefaultAutostartOwnerAfterStart(argv[2], argv[3], argv[4],
                                             argv[5]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--subscriber") == 0) {
    if (argc != 7) {
      return 64;
    }
    return RunSubscriber(argv[2], argv[3], argv[4], argv[5], argv[6]);
  }
  if (argc >= 2 &&
      std::strcmp(argv[1], "--loaned-string-subscriber") == 0) {
    if (argc != 7) {
      return 64;
    }
    return RunLoanedStringSubscriber(
        argv[2], argv[3], argv[4], argv[5], argv[6]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--reliable-sensor-subscriber") == 0) {
    if (argc != 7) {
      return 64;
    }
    return RunSubscriber(argv[2], argv[3], argv[4], argv[5], argv[6], true);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--int32-subscriber") == 0) {
    if (argc != 7) {
      return 64;
    }
    return RunInt32Subscriber(argv[2], argv[3], argv[4], argv[5],
                              static_cast<int32_t>(std::stol(argv[6])));
  }
  if (argc >= 2 && std::strcmp(argv[1], "--default-subscriber") == 0) {
    if (argc != 7) {
      return 64;
    }
    return RunDefaultSubscriber(argv[2], argv[3], argv[4], argv[5], argv[6]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--graph-publisher") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunGraphPublisher(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--graph-observer") == 0) {
    if (argc != 6) {
      return 64;
    }
    return RunGraphObserver(argv[2], argv[3], argv[4], argv[5]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--cold-graph-observer") == 0) {
    if (argc != 6) {
      return 64;
    }
    return RunColdGraphObserver(argv[2], argv[3], argv[4], argv[5]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--primed-graph-observer") == 0) {
    if (argc != 7) {
      return 64;
    }
    return RunPrimedGraphObserver(argv[2], argv[3], argv[4], argv[5], argv[6]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--graph-guard-observer") == 0) {
    if (argc != 7) {
      return 64;
    }
    return RunGraphGuardObserver(argv[2], argv[3], argv[4], argv[5], argv[6]);
  }
  if (argc >= 2 &&
      std::strcmp(argv[1], "--publisher-primed-graph-observer") == 0) {
    if (argc != 7) {
      return 64;
    }
    return RunPublisherPrimedGraphObserver(argv[2], argv[3], argv[4], argv[5],
                                          argv[6]);
  }
  if (argc >= 2 &&
      std::strcmp(argv[1], "--one-shot-topic-graph-observer") == 0) {
    if (argc != 6) {
      return 64;
    }
    return RunOneShotTopicGraphObserver(argv[2], argv[3], argv[4], argv[5]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--matched-publisher") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunMatchedPublisher(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--matched-subscriber") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunMatchedSubscriber(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--matched-publisher-observer") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunMatchedPublisherObserver(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--matched-subscriber-observer") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunMatchedSubscriberObserver(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 &&
      std::strcmp(argv[1], "--matched-publisher-event-observer") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunMatchedPublisherEventObserver(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 &&
      std::strcmp(argv[1], "--matched-subscriber-event-observer") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunMatchedSubscriberEventObserver(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--graph-service") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunGraphService(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--graph-client") == 0) {
    if (argc != 5) {
      return 64;
    }
    return RunGraphClient(argv[2], argv[3], argv[4]);
  }
  if (argc >= 2 &&
      std::strcmp(argv[1], "--service-client-graph-observer") == 0) {
    if (argc != 6) {
      return 64;
    }
    return RunServiceClientGraphObserver(argv[2], argv[3], argv[4], argv[5]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--service") == 0) {
    if (argc != 6) {
      return 64;
    }
    return RunService(argv[2], argv[3], argv[4], argv[5]);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--client") == 0) {
    if (argc != 6) {
      return 64;
    }
    return RunClient(argv[2], argv[3], argv[4], argv[5]);
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
